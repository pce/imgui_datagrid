/// SIMD strategy for numeric predicates
/// applyNumericOp() iterates over a contiguous int64 array and ANDs a uint8
/// bitmask.  The loop body is a single comparison + mask-AND with no branching
/// inside — a textbook auto-vectorisation candidate.  GCC/Clang emit AVX-512
/// vmovdqu64 + vpcmpq + kandw sequences at -O2 -march=native without any
/// intrinsics.  An explicit AVX2 path is provided for targets without AVX-512.

#include "tabular_query.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <format>
#include <ranges>
#include <string>
#include <string_view>

// Optional explicit SIMD for the numeric inner loop.
#if defined(__AVX2__) && defined(__GNUC__)
#include <immintrin.h>
#define TQ_HAVE_AVX2 1
#endif

namespace datagrid::adapters {

namespace {

std::string ToLower(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

/// SQL LIKE pattern matching (% = any sequence, _ = one char).
/// O(n+m) two-pointer backtracking — no heap allocation.
bool LikeMatch(std::string_view text, std::string_view pat, bool caseSensitive)
{
    auto eq = [caseSensitive](char a, char b) {
        if (caseSensitive)
            return a == b;
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    };
    size_t ti = 0, pi = 0;
    size_t starPi = SIZE_MAX, starTi = 0;
    while (ti < text.size()) {
        if (pi < pat.size() && (pat[pi] == '_' || eq(text[ti], pat[pi]))) {
            ++ti;
            ++pi;
        } else if (pi < pat.size() && pat[pi] == '%') {
            starPi = pi++;
            starTi = ti;
        } else if (starPi != SIZE_MAX) {
            pi = starPi + 1;
            ti = ++starTi;
        } else {
            return false;
        }
    }
    while (pi < pat.size() && pat[pi] == '%')
        ++pi;
    return pi == pat.size();
}

/// Glob pattern matching (* = any sequence, ? = one char), always case-insensitive.
bool GlobMatch(std::string_view text, std::string_view pat)
{
    auto eq = [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    };
    size_t ti = 0, pi = 0;
    size_t starPi = SIZE_MAX, starTi = 0;
    while (ti < text.size()) {
        if (pi < pat.size() && (pat[pi] == '?' || eq(text[ti], pat[pi]))) {
            ++ti;
            ++pi;
        } else if (pi < pat.size() && pat[pi] == '*') {
            starPi = pi++;
            starTi = ti;
        } else if (starPi != SIZE_MAX) {
            pi = starPi + 1;
            ti = ++starTi;
        } else {
            return false;
        }
    }
    while (pi < pat.size() && pat[pi] == '*')
        ++pi;
    return pi == pat.size();
}

enum class TokKind
{
    kSelect,
    kFrom,
    kWhere,
    kOrder,
    kBy,
    kLimit,
    kOffset,
    kRecursive,
    kAnd,
    kOr,
    kNot,
    kLike,
    kGlob,
    kIlike,
    kBetween,
    kIn,
    kAsc,
    kDesc,
    kIdent,
    kStrLit,
    kIntLit,
    kStar,
    kComma,
    kLParen,
    kRParen,
    kOp, ///< comparison operator stored in .value
    kEof
};

struct Token
{
    TokKind     kind = TokKind::kEof;
    std::string value;
};

class Lexer
{
  public:
    explicit Lexer(std::string_view input) : src_(input), pos_(0) {}

    Token next()
    {
        skipWs();
        if (pos_ >= src_.size())
            return {TokKind::kEof, {}};
        const char c = src_[pos_];

        if (c == '\'' || c == '"') {
            const char delim = c;
            ++pos_;
            std::string val;
            while (pos_ < src_.size() && src_[pos_] != delim) {
                if (src_[pos_] == '\\' && pos_ + 1 < src_.size()) {
                    ++pos_;
                    val += src_[pos_++];
                } else {
                    val += src_[pos_++];
                }
            }
            if (pos_ < src_.size())
                ++pos_;
            return {TokKind::kStrLit, val};
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            std::string val;
            while (pos_ < src_.size() && std::isdigit(static_cast<unsigned char>(src_[pos_])))
                val += src_[pos_++];
            return {TokKind::kIntLit, val};
        }
        if (c == '<' || c == '>' || c == '!' || c == '=') {
            std::string op{c};
            ++pos_;
            if (pos_ < src_.size()) {
                const char nc = src_[pos_];
                if ((c == '<' || c == '>' || c == '!') && nc == '=') {
                    op += nc;
                    ++pos_;
                } else if (c == '<' && nc == '>') {
                    op += nc;
                    ++pos_;
                }
            }
            return {TokKind::kOp, op};
        }
        if (c == '*') {
            ++pos_;
            return {TokKind::kStar, "*"};
        }
        if (c == ',') {
            ++pos_;
            return {TokKind::kComma, ","};
        }
        if (c == '(') {
            ++pos_;
            return {TokKind::kLParen, "("};
        }
        if (c == ')') {
            ++pos_;
            return {TokKind::kRParen, ")"};
        }

        // Identifier, keyword, or path token
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '/' || c == '\\' || c == '~' || c == '.' ||
            c == '-') {
            std::string val;
            while (pos_ < src_.size()) {
                const char nc = src_[pos_];
                if (std::isalnum(static_cast<unsigned char>(nc)) || nc == '_' || nc == '/' || nc == '\\' || nc == '~' ||
                    nc == '.' || nc == '-' || nc == ':') // Windows drive letter e.g. C:
                {
                    val += nc;
                    ++pos_;
                } else
                    break;
            }
            return {keyword(val), val};
        }
        ++pos_;
        return {TokKind::kEof, {}};
    }

    Token peek()
    {
        const size_t s = pos_;
        Token        t = next();
        pos_           = s;
        return t;
    }

  private:
    void skipWs()
    {
        while (pos_ < src_.size() && std::isspace(static_cast<unsigned char>(src_[pos_])))
            ++pos_;
    }

    static TokKind keyword(const std::string& s)
    {
        static const std::unordered_map<std::string, TokKind> kMap = {
            {"select", TokKind::kSelect},
            {"from", TokKind::kFrom},
            {"where", TokKind::kWhere},
            {"order", TokKind::kOrder},
            {"by", TokKind::kBy},
            {"limit", TokKind::kLimit},
            {"offset", TokKind::kOffset},
            {"recursive", TokKind::kRecursive},
            {"and", TokKind::kAnd},
            {"or", TokKind::kOr},
            {"not", TokKind::kNot},
            {"like", TokKind::kLike},
            {"glob", TokKind::kGlob},
            {"ilike", TokKind::kIlike},
            {"between", TokKind::kBetween},
            {"in", TokKind::kIn},
            {"asc", TokKind::kAsc},
            {"desc", TokKind::kDesc},
        };
        const std::string lo = ToLower(s);
        auto it = kMap.find(lo);
        return (it != kMap.end()) ? it->second : TokKind::kIdent;
    }

    std::string_view src_;
    size_t           pos_;
};

class Parser
{
  public:
    explicit Parser(std::string_view sql) : lex_(sql) { advance(); }

    std::expected<QueryPlan, std::string> parse()
    {
        QueryPlan plan;

        if (!expect(TokKind::kSelect))
            return fail("Expected SELECT");

        if (cur_.kind == TokKind::kStar) {
            advance();
        } else {
            do {
                if (cur_.kind != TokKind::kIdent)
                    return fail("Expected column name in SELECT list");
                plan.selectCols.push_back(cur_.value);
                advance();
            } while (cur_.kind == TokKind::kComma && (advance(), true));
        }

        if (!expect(TokKind::kFrom))
            return fail("Expected FROM");

        // FROM: quoted string or bare tokens up to the next clause keyword
        if (cur_.kind == TokKind::kStrLit) {
            plan.fromSource = cur_.value;
            advance();
        } else {
            while (cur_.kind != TokKind::kEof && cur_.kind != TokKind::kWhere && cur_.kind != TokKind::kOrder &&
                   cur_.kind != TokKind::kLimit && cur_.kind != TokKind::kRecursive) {
                if (!plan.fromSource.empty())
                    plan.fromSource += ' ';
                plan.fromSource += cur_.value;
                advance();
            }
        }
        if (plan.fromSource.empty())
            return fail("Expected source after FROM");

        // Optional RECURSIVE keyword (filesystem use-case, harmless for others)
        if (cur_.kind == TokKind::kRecursive) {
            plan.recursive = true;
            advance();
        }

        if (cur_.kind == TokKind::kWhere) {
            advance();
            auto nodeRes = parseOr();
            if (!nodeRes)
                return std::unexpected(nodeRes.error());
            plan.hasPred = true;
            plan.pred    = std::move(*nodeRes);
        }

        if (cur_.kind == TokKind::kOrder) {
            advance();
            if (!expect(TokKind::kBy))
                return fail("Expected BY after ORDER");
            if (cur_.kind != TokKind::kIdent)
                return fail("Expected column after ORDER BY");
            plan.orderByCol = cur_.value;
            advance();
            if (cur_.kind == TokKind::kAsc) {
                plan.orderAsc = true;
                advance();
            } else if (cur_.kind == TokKind::kDesc) {
                plan.orderAsc = false;
                advance();
            }
        }

        if (cur_.kind == TokKind::kLimit) {
            advance();
            if (cur_.kind != TokKind::kIntLit)
                return fail("Expected integer after LIMIT");
            plan.limitVal = std::stoi(cur_.value);
            advance();
        }
        if (cur_.kind == TokKind::kOffset) {
            advance();
            if (cur_.kind != TokKind::kIntLit)
                return fail("Expected integer after OFFSET");
            plan.offsetVal = std::stoi(cur_.value);
            advance();
        }

        return plan;
    }

  private:
    std::expected<PredNode, std::string> parseOr()
    {
        auto left = parseAnd();
        if (!left)
            return left;
        while (cur_.kind == TokKind::kOr) {
            advance();
            auto right = parseAnd();
            if (!right)
                return right;
            PredNode n;
            n.kind = PredNode::Kind::Or;
            n.children.push_back(std::move(*left));
            n.children.push_back(std::move(*right));
            left = std::move(n);
        }
        return left;
    }

    std::expected<PredNode, std::string> parseAnd()
    {
        auto left = parseNot();
        if (!left)
            return left;
        while (cur_.kind == TokKind::kAnd) {
            advance();
            auto right = parseNot();
            if (!right)
                return right;
            PredNode n;
            n.kind = PredNode::Kind::And;
            n.children.push_back(std::move(*left));
            n.children.push_back(std::move(*right));
            left = std::move(n);
        }
        return left;
    }

    std::expected<PredNode, std::string> parseNot()
    {
        if (cur_.kind == TokKind::kNot) {
            advance();
            auto inner = parseNot();
            if (!inner)
                return inner;
            PredNode n;
            n.kind = PredNode::Kind::Not;
            n.children.push_back(std::move(*inner));
            return n;
        }
        return parsePrimary();
    }

    std::expected<PredNode, std::string> parsePrimary()
    {
        if (cur_.kind == TokKind::kLParen) {
            advance();
            auto inner = parseOr();
            if (!inner)
                return inner;
            if (!expect(TokKind::kRParen))
                return fail("Expected ')'");
            return inner;
        }

        if (cur_.kind != TokKind::kIdent)
            return fail("Expected column name or '(' in WHERE clause");
        const std::string col = cur_.value;
        advance();

        if (cur_.kind == TokKind::kBetween) {
            advance();
            if (cur_.kind != TokKind::kIntLit && cur_.kind != TokKind::kStrLit)
                return fail("Expected value after BETWEEN");
            const std::string loStr = cur_.value;
            advance();
            if (!expect(TokKind::kAnd))
                return fail("Expected AND in BETWEEN");
            if (cur_.kind != TokKind::kIntLit && cur_.kind != TokKind::kStrLit)
                return fail("Expected value after BETWEEN…AND");
            const std::string hiStr = cur_.value;
            advance();

            PredNode n;
            n.atom.colName = col;
            n.atom.op      = PredOp::Between;
            n.atom.strVal  = loStr; // low bound (string form)
            try {
                n.atom.numLo = std::stoll(loStr);
                n.atom.numHi = std::stoll(hiStr);
            } catch (...) {
            }
            return n;
        }

        if (cur_.kind == TokKind::kIn) {
            advance();
            if (!expect(TokKind::kLParen))
                return fail("Expected '(' after IN");
            std::vector<std::string> vals;
            do {
                if (cur_.kind != TokKind::kStrLit && cur_.kind != TokKind::kIntLit)
                    return fail("Expected literal in IN list");
                vals.push_back(cur_.value);
                advance();
            } while (cur_.kind == TokKind::kComma && (advance(), true));
            if (!expect(TokKind::kRParen))
                return fail("Expected ')' after IN list");

            PredNode n;
            n.atom.colName = col;
            n.atom.op      = PredOp::In;
            n.atom.inVals  = std::move(vals);
            return n;
        }

        std::string op;
        if (cur_.kind == TokKind::kOp) {
            op = cur_.value;
            advance();
        } else if (cur_.kind == TokKind::kLike) {
            op = "like";
            advance();
        } else if (cur_.kind == TokKind::kGlob) {
            op = "glob";
            advance();
        } else if (cur_.kind == TokKind::kIlike) {
            op = "ilike";
            advance();
        } else
            return fail(std::format("Expected operator after column '{}'", col));

        if (cur_.kind != TokKind::kStrLit && cur_.kind != TokKind::kIntLit)
            return fail("Expected literal after operator");
        const std::string val = cur_.value;
        advance();

        PredNode n;
        n.atom.colName = col;
        n.atom.strVal  = val;
        try {
            n.atom.numLo = std::stoll(val);
        } catch (...) {
        }

        // Map operators to PredOp values
        static const std::unordered_map<std::string, PredOp> kOpMap = {
            {"=", PredOp::Eq},    {"==", PredOp::Eq},
            {"!=", PredOp::Ne},   {"<>", PredOp::Ne},
            {"<", PredOp::Lt},    {">", PredOp::Gt},
            {"<=", PredOp::Le},   {">=", PredOp::Ge},
            {"like", PredOp::Like},
            {"ilike", PredOp::ILike},
            {"glob", PredOp::Glob},
        };

        auto it = kOpMap.find(op);
        if (it != kOpMap.end()) {
            n.atom.op = it->second;
        } else {
            return fail(std::format("Unknown operator '{}'", op));
        }

        return n;
    }

    void advance() { cur_ = lex_.next(); }
    bool expect(TokKind k)
    {
        if (cur_.kind != k)
            return false;
        advance();
        return true;
    }

    static std::unexpected<std::string> fail(const std::string& msg) { return std::unexpected(msg); }

    Lexer lex_;
    Token cur_;
};

} // anonymous namespace

size_t TabularSoA::col_index(std::string_view name) const
{
    const std::string lo = ToLower(std::string{name});
    for (size_t i = 0; i < colNames.size(); ++i)
        if (ToLower(colNames[i]) == lo)
            return i;
    return SIZE_MAX;
}

TabularSoA TabularSoA::from_rows(const std::vector<std::string>&              names,
                                 const std::vector<ColType>&                  types,
                                 const std::vector<std::vector<std::string>>& rows)
{
    TabularSoA soa;
    soa.rowCount    = rows.size();
    soa.colNames    = names;
    soa.colTypes    = types;
    const size_t nc = names.size();
    soa.strCols.resize(nc);
    soa.numCols.resize(nc);

    for (size_t ci = 0; ci < nc; ++ci) {
        soa.strCols[ci].reserve(soa.rowCount);
        if (types[ci] == ColType::Numeric)
            soa.numCols[ci].reserve(soa.rowCount);
    }

    for (const auto& row : rows) {
        for (size_t ci = 0; ci < nc; ++ci) {
            const std::string& cell = ci < row.size() ? row[ci] : "";
            soa.strCols[ci].push_back(cell);
            if (types[ci] == ColType::Numeric) {
                int64_t v = 0;
                try {
                    v = std::stoll(cell);
                } catch (...) {
                }
                soa.numCols[ci].push_back(v);
            }
        }
    }
    return soa;
}

/// Inner numeric loop — a single comparison + mask-AND.
/// Kept in its own function so the compiler can freely vectorise it.
/// The switch is outside the per-row loop: the compiler emits a tight
/// stride-1 kernel for each case (no branch inside the hot loop).
void TabularQuery::applyNumericOp(
    PredOp op, int64_t lo, int64_t hi, const std::vector<int64_t>& col, std::vector<uint8_t>& mask)
{
    const size_t n = col.size();

#if defined(TQ_HAVE_AVX2)
    // Explicit AVX2 path for Gt/Lt — the most common filesystem predicates.
    // Each lane processes 4 × int64.  The scalar tail handles the remainder.
    if (op == PredOp::Gt || op == PredOp::Ge) {
        const int64_t threshold = (op == PredOp::Ge) ? lo - 1 : lo;
        const __m256i t4        = _mm256_set1_epi64x(threshold);
        size_t        i         = 0;
        for (; i + 4 <= n; i += 4) {
            __m256i v    = _mm256_loadu_si256((const __m256i*)&col[i]);
            __m256i cmp  = _mm256_cmpgt_epi64(v, t4); // -1 or 0 per lane
            int     bits = _mm256_movemask_epi8(cmp);
            mask[i + 0] &= (bits & 0x000000FF) ? 1u : 0u;
            mask[i + 1] &= (bits & 0x0000FF00) ? 1u : 0u;
            mask[i + 2] &= (bits & 0x00FF0000) ? 1u : 0u;
            mask[i + 3] &= static_cast<uint8_t>((static_cast<uint32_t>(bits) & 0xFF000000u) ? 1u : 0u);
        }
        for (; i < n; ++i)
            mask[i] &= (col[i] > threshold) ? 1u : 0u;
        return;
    }
    if (op == PredOp::Lt || op == PredOp::Le) {
        const int64_t threshold = (op == PredOp::Le) ? lo + 1 : lo;
        const __m256i t4        = _mm256_set1_epi64x(threshold);
        size_t        i         = 0;
        for (; i + 4 <= n; i += 4) {
            __m256i v    = _mm256_loadu_si256((const __m256i*)&col[i]);
            __m256i cmp  = _mm256_cmpgt_epi64(t4, v);
            int     bits = _mm256_movemask_epi8(cmp);
            mask[i + 0] &= (bits & 0x000000FF) ? 1u : 0u;
            mask[i + 1] &= (bits & 0x0000FF00) ? 1u : 0u;
            mask[i + 2] &= (bits & 0x00FF0000) ? 1u : 0u;
            mask[i + 3] &= static_cast<uint8_t>((static_cast<uint32_t>(bits) & 0xFF000000u) ? 1u : 0u);
        }
        for (; i < n; ++i)
            mask[i] &= (col[i] < threshold) ? 1u : 0u;
        return;
    }
#endif

    // Scalar path (also used as tail after AVX2, and for Eq/Ne/Between).
    // Simple stride-1 loops — GCC/Clang auto-vectorise at -O2 -march=native.
    switch (op) {
        case PredOp::Eq:
            for (size_t i = 0; i < n; ++i)
                mask[i] &= (col[i] == lo) ? 1u : 0u;
            break;
        case PredOp::Ne:
            for (size_t i = 0; i < n; ++i)
                mask[i] &= (col[i] != lo) ? 1u : 0u;
            break;
        case PredOp::Lt:
            for (size_t i = 0; i < n; ++i)
                mask[i] &= (col[i] < lo) ? 1u : 0u;
            break;
        case PredOp::Gt:
            for (size_t i = 0; i < n; ++i)
                mask[i] &= (col[i] > lo) ? 1u : 0u;
            break;
        case PredOp::Le:
            for (size_t i = 0; i < n; ++i)
                mask[i] &= (col[i] <= lo) ? 1u : 0u;
            break;
        case PredOp::Ge:
            for (size_t i = 0; i < n; ++i)
                mask[i] &= (col[i] >= lo) ? 1u : 0u;
            break;
        case PredOp::Between:
            for (size_t i = 0; i < n; ++i)
                mask[i] &= (col[i] >= lo && col[i] <= hi) ? 1u : 0u;
            break;
        default:
            break;
    }
}

void TabularQuery::applyTextOp(PredOp                          op,
                               const std::string&              val,
                               const std::vector<std::string>& col,
                               std::vector<uint8_t>&           mask)
{
    const std::string lo = ToLower(val);
    const size_t      n  = col.size();

    switch (op) {
        case PredOp::Eq:
            for (size_t i = 0; i < n; ++i)
                mask[i] &= (col[i] == val) ? 1u : 0u;
            break;
        case PredOp::Ne:
            for (size_t i = 0; i < n; ++i)
                mask[i] &= (col[i] != val) ? 1u : 0u;
            break;
        case PredOp::Lt:
            for (size_t i = 0; i < n; ++i)
                mask[i] &= (col[i] < val) ? 1u : 0u;
            break;
        case PredOp::Gt:
            for (size_t i = 0; i < n; ++i)
                mask[i] &= (col[i] > val) ? 1u : 0u;
            break;
        case PredOp::Le:
            for (size_t i = 0; i < n; ++i)
                mask[i] &= (col[i] <= val) ? 1u : 0u;
            break;
        case PredOp::Ge:
            for (size_t i = 0; i < n; ++i)
                mask[i] &= (col[i] >= val) ? 1u : 0u;
            break;
        case PredOp::Like:
            for (size_t i = 0; i < n; ++i)
                mask[i] &= LikeMatch(col[i], val, true) ? 1u : 0u;
            break;
        case PredOp::ILike:
            for (size_t i = 0; i < n; ++i)
                mask[i] &= LikeMatch(col[i], val, false) ? 1u : 0u;
            break;
        case PredOp::Glob:
            for (size_t i = 0; i < n; ++i)
                mask[i] &= GlobMatch(col[i], val) ? 1u : 0u;
            break;
        case PredOp::Between:
            for (size_t i = 0; i < n; ++i)
                mask[i] &= (col[i] >= lo && col[i] <= val) ? 1u : 0u;
            break;
        default:
            break;
    }
}

void TabularQuery::applyInOp(const std::vector<std::string>& vals,
                             const std::vector<std::string>& col,
                             std::vector<uint8_t>&           mask)
{
    const size_t n = col.size();
    for (size_t i = 0; i < n; ++i) {
        if (!mask[i])
            continue;
        bool found = false;
        for (const auto& v : vals)
            if (col[i] == v) {
                found = true;
                break;
            }
        if (!found)
            mask[i] = 0;
    }
}

void TabularQuery::applyAtom(const AtomicPred& a, const TabularSoA& soa, std::vector<uint8_t>& mask)
{
    // Normalize known column aliases
    std::string       colName = a.colName;
    const std::string lcName  = ToLower(colName);

    const size_t ci = soa.col_index(lcName);
    if (ci == SIZE_MAX)
        return; // unknown column — skip predicate (no rows killed)

    // IN is always text-based
    if (a.op == PredOp::In) {
        applyInOp(a.inVals, soa.strCols[ci], mask);
        return;
    }

    // Numeric column + numeric-compatible op → fast int64 path
    const bool isNumericOp = (a.op == PredOp::Lt || a.op == PredOp::Gt || a.op == PredOp::Le || a.op == PredOp::Ge ||
                              a.op == PredOp::Between);
    if (soa.colTypes[ci] == ColType::Numeric && !soa.numCols[ci].empty()) {
        applyNumericOp(a.op, a.numLo, a.numHi, soa.numCols[ci], mask);
        return;
    }

    // Text column with numeric op: try dynamic numeric coercion (SQLite affinity).
    // If strVal parses as a number, compare numerically; else fall through to string.
    if (isNumericOp) {
        try {
            const double dval = std::stod(a.strVal);
            const double dhi  = a.numHi ? static_cast<double>(a.numHi) : dval;
            const size_t n    = soa.strCols[ci].size();
            switch (a.op) {
                case PredOp::Lt:
                    for (size_t i = 0; i < n; ++i) {
                        try {
                            mask[i] &= std::stod(soa.strCols[ci][i]) < dval ? 1u : 0u;
                        } catch (...) {
                            mask[i] = 0;
                        }
                    }
                    return;
                case PredOp::Gt:
                    for (size_t i = 0; i < n; ++i) {
                        try {
                            mask[i] &= std::stod(soa.strCols[ci][i]) > dval ? 1u : 0u;
                        } catch (...) {
                            mask[i] = 0;
                        }
                    }
                    return;
                case PredOp::Le:
                    for (size_t i = 0; i < n; ++i) {
                        try {
                            mask[i] &= std::stod(soa.strCols[ci][i]) <= dval ? 1u : 0u;
                        } catch (...) {
                            mask[i] = 0;
                        }
                    }
                    return;
                case PredOp::Ge:
                    for (size_t i = 0; i < n; ++i) {
                        try {
                            mask[i] &= std::stod(soa.strCols[ci][i]) >= dval ? 1u : 0u;
                        } catch (...) {
                            mask[i] = 0;
                        }
                    }
                    return;
                case PredOp::Between:
                    for (size_t i = 0; i < n; ++i) {
                        try {
                            const double d = std::stod(soa.strCols[ci][i]);
                            mask[i] &= (d >= dval && d <= dhi) ? 1u : 0u;
                        } catch (...) {
                            mask[i] = 0;
                        }
                    }
                    return;
                default:
                    break;
            }
            (void)dval; // suppress unused warning for fallthrough
        } catch (...) {
        }
    }

    applyTextOp(a.op, a.strVal, soa.strCols[ci], mask);
}

void TabularQuery::applyNode(const PredNode& node, const TabularSoA& soa, std::vector<uint8_t>& mask)
{
    switch (node.kind) {
        case PredNode::Kind::Atom:
            applyAtom(node.atom, soa, mask);
            break;

        case PredNode::Kind::And:
            for (const auto& child : node.children)
                applyNode(child, soa, mask); // sequential AND: each pass narrows mask
            break;

        case PredNode::Kind::Or: {
            // Build a temporary mask for each branch, OR them together.
            const size_t         n = mask.size();
            std::vector<uint8_t> result(n, 0);
            for (const auto& child : node.children) {
                std::vector<uint8_t> branch(mask.begin(), mask.end());
                applyNode(child, soa, branch);
                for (size_t i = 0; i < n; ++i)
                    result[i] |= branch[i];
            }
            mask = std::move(result);
            break;
        }

        case PredNode::Kind::Not: {
            if (!node.children.empty()) {
                applyNode(node.children[0], soa, mask);
                for (auto& b : mask)
                    b = b ? 0u : 1u;
            }
            break;
        }
    }
}

std::expected<QueryPlan, std::string> TabularQuery::parse(std::string_view sql)
{
    return Parser{sql}.parse();
}

std::vector<uint8_t> TabularQuery::filter_mask(const QueryPlan& plan, const TabularSoA& soa)
{
    std::vector<uint8_t> mask(soa.rowCount, 1);
    if (plan.hasPred)
        applyNode(plan.pred, soa, mask);
    return mask;
}

QueryResult TabularQuery::execute(const QueryPlan& plan, const TabularSoA& soa, const std::vector<ColumnInfo>& schema)
{
    const auto  t0 = std::chrono::steady_clock::now();
    QueryResult result;

    if (soa.rowCount == 0 || soa.colNames.empty()) {
        result.columns = schema;
        return result;
    }

    std::vector<uint8_t> mask = filter_mask(plan, soa);

    std::vector<size_t> survivors;
    survivors.reserve(soa.rowCount);
    for (size_t i = 0; i < soa.rowCount; ++i)
        if (mask[i])
            survivors.push_back(i);

    if (!plan.orderByCol.empty()) {
        const size_t sortCI = soa.col_index(plan.orderByCol);
        if (sortCI != SIZE_MAX) {
            const bool asc   = plan.orderAsc;
            const bool isNum = soa.colTypes[sortCI] == ColType::Numeric && !soa.numCols[sortCI].empty();
            if (isNum) {
                const auto& nc = soa.numCols[sortCI];
                std::ranges::stable_sort(survivors, [&](size_t a, size_t b) {
                    return asc ? nc[a] < nc[b] : nc[a] > nc[b];
                });
            } else {
                const auto& sc = soa.strCols[sortCI];
                std::ranges::stable_sort(survivors, [&](size_t a, size_t b) {
                    return asc ? sc[a] < sc[b] : sc[a] > sc[b];
                });
            }
        }
    }

    if (plan.offsetVal > 0) {
        const size_t skip = static_cast<size_t>(plan.offsetVal);
        if (skip >= survivors.size())
            survivors.clear();
        else
            survivors.erase(survivors.begin(), survivors.begin() + static_cast<ptrdiff_t>(skip));
    }
    if (plan.limitVal >= 0) {
        const size_t cap = static_cast<size_t>(plan.limitVal);
        if (survivors.size() > cap)
            survivors.resize(cap);
    }

    // Build an ordered list of (SoA colIdx, ColumnInfo) for the SELECT list.
    struct OutCol
    {
        size_t     soaIdx;
        ColumnInfo info;
    };
    std::vector<OutCol> outCols;

    if (plan.selectCols.empty()) {
        // SELECT * — use schema order
        for (size_t ci = 0; ci < soa.colNames.size(); ++ci) {
            const ColumnInfo* ci_info = nullptr;
            for (const auto& sc : schema)
                if (ToLower(sc.name) == ToLower(soa.colNames[ci])) {
                    ci_info = &sc;
                    break;
                }
            if (ci_info)
                outCols.push_back({ci, *ci_info});
            else
                outCols.push_back({ci, ColumnInfo{soa.colNames[ci], "TEXT", true, false}});
        }
    } else {
        for (const auto& colName : plan.selectCols) {
            const size_t ci = soa.col_index(colName);
            if (ci == SIZE_MAX)
                continue;
            const ColumnInfo* ci_info = nullptr;
            for (const auto& sc : schema)
                if (ToLower(sc.name) == ToLower(colName)) {
                    ci_info = &sc;
                    break;
                }
            if (ci_info)
                outCols.push_back({ci, *ci_info});
            else
                outCols.push_back({ci, ColumnInfo{colName, "TEXT", true, false}});
        }
    }

    result.columns.reserve(outCols.size());
    for (const auto& oc : outCols)
        result.columns.push_back(oc.info);

    result.rows.reserve(survivors.size());
    for (const size_t ri : survivors) {
        std::vector<std::string> row;
        row.reserve(outCols.size());
        for (const auto& oc : outCols)
            row.push_back(oc.soaIdx < soa.strCols.size() ? soa.strCols[oc.soaIdx][ri] : "");
        result.rows.push_back(std::move(row));
    }

    const auto t1      = std::chrono::steady_clock::now();
    result.executionMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return result;
}

} // namespace datagrid::adapters
