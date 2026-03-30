#include "type_inferrer.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <unordered_set>

namespace TypeInfer {

// ============================================================
//  Internal helpers
// ============================================================

static std::string ToLower(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s)
        out += static_cast<char>(std::tolower(c));
    return out;
}

// Returns true for values that unambiguously represent a boolean WORD
// (i.e. not just "0" or "1" which could equally be integers).
static bool IsBoolWord(const std::string& s)
{
    const std::string lo = ToLower(s);
    static const std::unordered_set<std::string> words = {
        "true","false","yes","no","y","n","t","f"
    };
    return words.count(lo) > 0;
}

// Returns true for any value in the full boolean set {true,false,…,0,1}.
static bool IsBoolAny(const std::string& s)
{
    const std::string lo = ToLower(s);
    static const std::unordered_set<std::string> all = {
        "true","false","yes","no","y","n","t","f","1","0"
    };
    return all.count(lo) > 0;
}

static bool IsInteger(const std::string& s)
{
    if (s.empty()) return false;
    size_t start = 0;
    if (s[0] == '+' || s[0] == '-') start = 1;
    if (start >= s.size()) return false;
    for (size_t i = start; i < s.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    return true;
}

static bool IsReal(const std::string& s)
{
    if (s.empty()) return false;
    char* end = nullptr;
    std::strtod(s.c_str(), &end);
    return end != s.c_str() && *end == '\0';
}

// Matches YYYY-MM-DD  or  YYYY/MM/DD  or  DD-MM-YYYY  or  DD/MM/YYYY
static bool IsDate(const std::string& s)
{
    if (s.size() < 8) return false;
    // Try YYYY-MM-DD  or  YYYY/MM/DD
    if (std::isdigit((unsigned char)s[0]) &&
        std::isdigit((unsigned char)s[1]) &&
        std::isdigit((unsigned char)s[2]) &&
        std::isdigit((unsigned char)s[3]) &&
        (s[4] == '-' || s[4] == '/') &&
        std::isdigit((unsigned char)s[5]) &&
        std::isdigit((unsigned char)s[6]) &&
        (s[7] == '-' || s[7] == '/') &&
        s.size() >= 10 &&
        std::isdigit((unsigned char)s[8]) &&
        std::isdigit((unsigned char)s[9]))
        return true;
    // Try DD-MM-YYYY  or  DD/MM/YYYY
    if (s.size() >= 10 &&
        std::isdigit((unsigned char)s[0]) &&
        std::isdigit((unsigned char)s[1]) &&
        (s[2] == '-' || s[2] == '/') &&
        std::isdigit((unsigned char)s[3]) &&
        std::isdigit((unsigned char)s[4]) &&
        (s[5] == '-' || s[5] == '/') &&
        std::isdigit((unsigned char)s[6]) &&
        std::isdigit((unsigned char)s[7]) &&
        std::isdigit((unsigned char)s[8]) &&
        std::isdigit((unsigned char)s[9]))
        return true;
    return false;
}

// Matches YYYY-MM-DD[T ]HH:MM  (seconds optional)
static bool IsDateTime(const std::string& s)
{
    if (s.size() < 16) return false;
    // Must start with a valid date in YYYY-MM-DD form
    if (!std::isdigit((unsigned char)s[0]) ||
        !std::isdigit((unsigned char)s[1]) ||
        !std::isdigit((unsigned char)s[2]) ||
        !std::isdigit((unsigned char)s[3]) ||
        (s[4] != '-' && s[4] != '/'))
        return false;
    // Separator between date and time: 'T' or ' '
    if (s[10] != 'T' && s[10] != ' ') return false;
    // HH:MM
    if (!std::isdigit((unsigned char)s[11]) ||
        !std::isdigit((unsigned char)s[12]) ||
        s[13] != ':' ||
        !std::isdigit((unsigned char)s[14]) ||
        !std::isdigit((unsigned char)s[15]))
        return false;
    return true;
}

// ============================================================
//  TypeName
// ============================================================

const char* TypeName(InferredType t) noexcept
{
    switch (t) {
        case InferredType::Boolean:  return "boolean";
        case InferredType::Integer:  return "integer";
        case InferredType::Real:     return "real";
        case InferredType::DateTime: return "datetime";
        case InferredType::Date:     return "date";
        case InferredType::Text:     return "text";
    }
    return "text";
}

// ============================================================
//  InferColumnType
// ============================================================

TypeResult InferColumnType(const std::vector<std::string>& samples)
{
    TypeResult result;
    if (samples.empty()) return result;

    // Cap at 200 samples for performance
    const size_t limit = std::min(samples.size(), size_t(200));
    result.total = static_cast<int>(limit);

    // Counters
    int nullCount  = 0;
    int boolWord   = 0;  // non-numeric boolean words
    int boolAny    = 0;  // all boolean values (including 0/1)
    int intCount   = 0;
    int realCount  = 0;
    int dateCount  = 0;
    int dtCount    = 0;

    bool firstNonNull = true;

    for (size_t i = 0; i < limit; ++i) {
        const std::string& s = samples[i];
        if (s.empty()) { ++nullCount; continue; }

        // Track lexicographic min / max for the inspector
        if (firstNonNull) {
            result.minValue = s;
            result.maxValue = s;
            firstNonNull = false;
        } else {
            if (s < result.minValue) result.minValue = s;
            if (s > result.maxValue) result.maxValue = s;
        }

        // DateTime is a strict superset of Date — check it first
        if (IsDateTime(s))  { ++dtCount; ++dateCount; }
        else if (IsDate(s)) { ++dateCount; }

        if (IsInteger(s))        { ++intCount; ++realCount; }
        else if (IsReal(s))      { ++realCount; }

        if (IsBoolWord(s))   ++boolWord;
        if (IsBoolAny(s))    ++boolAny;
    }

    result.nullCount = nullCount;
    const int nonNull = result.total - nullCount;
    if (nonNull == 0) return result;  // all nulls → Text, confidence 0

    const float threshold = 0.90f;
    auto conf = [&](int count) { return static_cast<float>(count) / nonNull; };

    // Boolean: only promote when the column contains actual word booleans,
    // not just 0/1 (which is better described as Integer).
    if (boolWord > 0 && conf(boolAny) >= threshold) {
        result.type       = InferredType::Boolean;
        result.confidence = conf(boolAny);
        return result;
    }

    if (conf(dtCount) >= threshold) {
        result.type       = InferredType::DateTime;
        result.confidence = conf(dtCount);
        return result;
    }

    if (conf(dateCount) >= threshold) {
        result.type       = InferredType::Date;
        result.confidence = conf(dateCount);
        return result;
    }

    if (conf(intCount) >= threshold) {
        result.type       = InferredType::Integer;
        result.confidence = conf(intCount);
        return result;
    }

    if (conf(realCount) >= threshold) {
        result.type       = InferredType::Real;
        result.confidence = conf(realCount);
        return result;
    }

    // Text fallback: confidence = fraction of non-null values
    result.type       = InferredType::Text;
    result.confidence = static_cast<float>(nonNull) / result.total;
    return result;
}

// ============================================================
//  DetectDelimiter
// ============================================================

// Count occurrences of `delim` in `line` that are NOT inside RFC-4180
// double-quoted spans (handles "field with, comma" correctly).
static int CountOutsideQuotes(const std::string& line, char delim)
{
    int count = 0;
    bool inQuotes = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '"') {
            // RFC-4180: "" inside quotes is an escaped quote, not a close
            if (inQuotes && i + 1 < line.size() && line[i + 1] == '"')
                ++i;
            else
                inQuotes = !inQuotes;
        } else if (!inQuotes && c == delim) {
            ++count;
        }
    }
    return count;
}

DelimiterResult DetectDelimiter(const std::vector<std::string>& lines)
{
    if (lines.empty()) return {',', 0.0f};

    const char candidates[] = {',', '\t', ';', '|', ':'};
    const size_t checkLines = std::min(lines.size(), size_t(8));

    char  bestDelim = ',';
    float bestScore = -1.0f;

    for (char c : candidates) {
        std::vector<int> counts;
        counts.reserve(checkLines);
        for (size_t i = 0; i < checkLines; ++i)
            counts.push_back(CountOutsideQuotes(lines[i], c));

        const int maxCnt = *std::max_element(counts.begin(), counts.end());
        if (maxCnt == 0) continue;   // delimiter never appears

        // Mean
        float mean = 0.0f;
        for (int n : counts) mean += n;
        mean /= static_cast<float>(counts.size());

        // Sample standard deviation
        float variance = 0.0f;
        for (int n : counts) {
            const float diff = n - mean;
            variance += diff * diff;
        }
        variance /= static_cast<float>(counts.size());
        const float stddev = std::sqrt(variance);

        // Score: high mean (many fields) + low stddev (consistent across lines)
        // stddev = 0 when all lines have the same field count → perfect
        const float score = mean / (1.0f + stddev);

        if (score > bestScore) {
            bestScore = score;
            bestDelim = c;
        }
    }

    // Normalise to 0–1.
    // When stddev == 0 (all lines have identical field counts) the raw score
    // equals the mean count.  A 3-field CSV gives score = 2, which the old
    // formula mapped to only 0.33.  We therefore boost perfectly-consistent
    // files by halving the normalisation constant so that score = 2 → 0.5,
    // score = 3 → 0.6, score = 5 → 0.71, etc.
    const float normK = (bestScore > 0.0f && bestScore == std::floor(bestScore))
        ? 2.0f   // integer score → stddev was 0 → perfectly consistent
        : 4.0f;  // fractional score → some variance → be more conservative
    const float confidence = (bestScore > 0.0f)
        ? std::min(1.0f, bestScore / (bestScore + normK))
        : 0.0f;

    return {bestDelim, confidence};
}

// ============================================================
//  DetectHeader
// ============================================================

HeaderResult DetectHeader(
    const std::vector<std::string>& firstRow,
    const std::vector<std::string>& secondRow)
{
    HeaderResult result;
    result.confidence = 0.5f;   // start neutral

    if (firstRow.empty()) {
        result.hasHeader  = false;
        result.confidence = 0.0f;
        result.reason     = "empty first row";
        return result;
    }

    float score = 0.5f;
    std::vector<std::string> reasons;

    // ── Heuristic 1: column count agreement ───────────────────────────────
    if (!secondRow.empty() && firstRow.size() != secondRow.size()) {
        score -= 0.10f;
        reasons.push_back("column count mismatch");
    }

    // ── Heuristic 2: how numeric is each row? ─────────────────────────────
    int firstNumeric  = 0;
    int secondNumeric = 0;

    for (const auto& v : firstRow)
        if (!v.empty() && (IsInteger(v) || IsReal(v))) ++firstNumeric;

    for (const auto& v : secondRow)
        if (!v.empty() && (IsInteger(v) || IsReal(v))) ++secondNumeric;

    const float firstNumRatio =
        static_cast<float>(firstNumeric) / static_cast<float>(firstRow.size());

    if (firstNumRatio < 0.25f) {
        // First row mostly non-numeric: strong header signal
        score += 0.25f;
        reasons.push_back("non-numeric first row");
    } else if (firstNumRatio > 0.70f) {
        // First row mostly numeric: unlikely to be a header
        score -= 0.30f;
        reasons.push_back("mostly numeric first row");
    }

    if (!secondRow.empty()) {
        const float secondNumRatio =
            static_cast<float>(secondNumeric) / static_cast<float>(secondRow.size());
        if (secondNumRatio > firstNumRatio + 0.30f) {
            score += 0.15f;
            reasons.push_back("data row more numeric");
        }
    }

    // ── Heuristic 3: all first-row values unique ───────────────────────────
    {
        std::unordered_set<std::string> uniq(firstRow.begin(), firstRow.end());
        if (uniq.size() == firstRow.size() && firstRow.size() > 1) {
            score += 0.10f;
            reasons.push_back("all values unique");
        }
    }

    // ── Heuristic 4: values look like identifiers ──────────────────────────
    // Header names tend to be short, start with a letter/underscore, no leading digit
    {
        int idLike = 0;
        for (const auto& v : firstRow) {
            if (v.empty()) continue;
            const unsigned char first = static_cast<unsigned char>(v[0]);
            if (!std::isdigit(first) && v.size() <= 40)
                ++idLike;
        }
        if (static_cast<float>(idLike) / static_cast<float>(firstRow.size()) > 0.70f) {
            score += 0.10f;
            reasons.push_back("identifier-like names");
        }
    }

    // ── Heuristic 5: low confidence if only one row provided ──────────────
    if (secondRow.empty()) {
        score = 0.5f + (score - 0.5f) * 0.5f;   // pull towards neutral
        reasons.push_back("single row — low confidence");
    }

    score = std::max(0.0f, std::min(1.0f, score));

    // Build reason string
    std::string reasonStr;
    for (size_t i = 0; i < reasons.size(); ++i) {
        if (i) reasonStr += "; ";
        reasonStr += reasons[i];
    }
    if (reasonStr.empty()) reasonStr = "neutral evidence";

    result.hasHeader  = (score > 0.50f);
    result.confidence = score;
    result.reason     = std::move(reasonStr);
    return result;
}

} // namespace TypeInfer
