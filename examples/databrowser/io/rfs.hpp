#ifndef DATAGRID_RFS_H
#define DATAGRID_RFS_H

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace datagrid::io {

    /// RWX permission bitmask
    enum struct Perms : unsigned {
        None  = 0,
        Read  = 1u << 0,   ///< |r
        Write = 1u << 1,   ///< |w
        Exec  = 1u << 2,   ///< |x
        RO    = Read,
        RW    = Read | Write,
        RX    = Read | Exec,
        All   = Read | Write | Exec,
    };

    constexpr Perms operator|(Perms a, Perms b) noexcept {
        return static_cast<Perms>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
    }
    constexpr Perms operator&(Perms a, Perms b) noexcept {
        return static_cast<Perms>(static_cast<unsigned>(a) & static_cast<unsigned>(b));
    }
    constexpr bool has(Perms set, Perms flag) noexcept {
        return (set & flag) != Perms::None;
    }

    // ── MountStatus ───────────────────────────────────────────────────────────
    //
    //  Returned by Rfs::mount() so callers know exactly what happened.
    //
    //  Mounted   — new, non-overlapping mount added.
    //  Updated   — same path remounted; perms changed in-place.
    //  Narrowed  — submount; child perms ⊆ parent — silently allowed.
    //  Escalated — submount grants bits the parent does NOT have; only possible
    //              when force = true (the caller punched a deliberate hole).
    //  Rejected  — escalating submount and force == false; no change made.
    //
    enum class MountStatus { Mounted, Updated, Narrowed, Escalated, Rejected };

    struct Mount {
        std::filesystem::path root;
        Perms                 perms{Perms::All};
    };

    /// Opaque resolved-path token — only Rfs::resolve() constructs one.
    class ResolvedPath {
        friend class Rfs;
        explicit ResolvedPath(std::filesystem::path p, Perms perms) noexcept
            : path_(std::move(p)), perms_(perms) {}
    public:
        [[nodiscard]] const std::filesystem::path& native()     const noexcept { return path_; }
        [[nodiscard]] bool readable()   const noexcept { return has(perms_, Perms::Read);  }
        [[nodiscard]] bool writable()   const noexcept { return has(perms_, Perms::Write); }
        [[nodiscard]] bool executable() const noexcept { return has(perms_, Perms::Exec);  }
    private:
        std::filesystem::path path_;
        Perms                 perms_;
    };

    // ── Rfs ───────────────────────────────────────────────────────────────────
    //
    //  Tiny virtual filesystem with named mount points and RWX permissions.
    //  Front-insert vector: O(n) scan beats any tree for the expected ≤10 mounts
    //  and stays hot in L1 cache.
    //
    //  Permission policy
    //  -----------------
    //  Re-mounting the same root:  always succeeds (Updated).
    //  Narrowing submount:         child perms ⊆ parent perms — allowed (Narrowed).
    //  Escalating submount:        child has bits the parent does NOT — Rejected
    //                              unless force = true.  Escalation is intentional
    //                              (e.g. resources/ RO but resources/themes/ RW) so
    //                              callers must opt-in explicitly.
    //
    class Rfs {
        std::vector<Mount> mounts_;
        mutable std::mutex mutex_;

        // Returns the perms of the longest-prefix ancestor of `root` (if any).
        // Must be called with mutex_ held.
        [[nodiscard]] const Mount* find_parent_locked(const std::filesystem::path& root) const noexcept {
            const Mount* best    = nullptr;
            std::size_t  bestLen = 0;
            for (const auto& m : mounts_) {
                if (m.root == root) continue; // skip exact match
                auto [mi, pi] = std::mismatch(m.root.begin(), m.root.end(),
                                              root.begin(),   root.end());
                if (mi == m.root.end()) { // m.root is a strict prefix of root
                    const auto len = static_cast<std::size_t>(
                        std::distance(m.root.begin(), m.root.end()));
                    if (len > bestLen) { bestLen = len; best = &m; }
                }
            }
            return best;
        }

    public:
        /// Mount root with perms.
        ///
        /// @param force  Allow escalating submounts (child gains bits the parent
        ///               does not have).  Default false — returns Rejected instead.
        ///
        /// Returns:
        ///   Updated   — path already mounted; perms updated in-place.
        ///   Narrowed  — submount; child perms are a subset of parent perms.
        ///   Mounted   — new independent mount (no ancestor found).
        ///   Rejected  — escalating submount and force == false; no change made.
        MountStatus mount(std::filesystem::path root, Perms perms = Perms::All, bool force = false) {
            std::unique_lock lock(mutex_);

            // Re-mount (same root): update perms in-place, no overlap check.
            for (auto& m : mounts_) {
                if (m.root == root) { m.perms = perms; return MountStatus::Updated; }
            }

            // Check for a parent mount.
            const Mount* parent    = find_parent_locked(root);
            bool         escalates = false;
            if (parent) {
                // Escalation: child wants bits the parent does not grant.
                escalates = (has(perms, Perms::Read)  && !has(parent->perms, Perms::Read))
                         || (has(perms, Perms::Write) && !has(parent->perms, Perms::Write))
                         || (has(perms, Perms::Exec)  && !has(parent->perms, Perms::Exec));
                if (escalates && !force)
                    return MountStatus::Rejected;
            }

            mounts_.insert(mounts_.begin(), Mount{std::move(root), perms});
            return parent
                ? (escalates ? MountStatus::Escalated : MountStatus::Narrowed)
                : MountStatus::Mounted;
        }

        void unmount(const std::filesystem::path& root) {
            std::unique_lock lock(mutex_);
            std::erase_if(mounts_, [&](const Mount& m) { return m.root == root; });
        }

        /// Longest-prefix resolve.
        /// Returns nullopt when no mount covers path.
        [[nodiscard]]
        std::optional<ResolvedPath> resolve(const std::filesystem::path& path) const {
            std::unique_lock lock(mutex_);
            const Mount* best    = nullptr;
            std::size_t  bestLen = 0;
            for (const auto& m : mounts_) {
                auto [mi, pi] = std::mismatch(m.root.begin(), m.root.end(),
                                              path.begin(),   path.end());
                if (mi == m.root.end()) {   // m.root is a prefix of path
                    const auto len = static_cast<std::size_t>(
                        std::distance(m.root.begin(), m.root.end()));
                    if (len > bestLen) { bestLen = len; best = &m; }
                }
            }
            if (!best) return std::nullopt;
            return ResolvedPath{path, best->perms};
        }
    };

} // namespace datagrid::io

#endif // DATAGRID_RFS_H

