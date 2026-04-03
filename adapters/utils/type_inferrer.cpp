#include "type_inferrer.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <unordered_set>

namespace {

std::string ToLower(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s)
        out += static_cast<char>(std::tolower(c));
    return out;
}

bool IsBoolWord(std::string_view s)
{
    const std::string                            lo    = ToLower(s);
    static const std::unordered_set<std::string> words = {"true", "false", "yes", "no", "y", "n", "t", "f"};
    return words.count(lo) > 0;
}

bool IsBoolAny(std::string_view s)
{
    const std::string                            lo  = ToLower(s);
    static const std::unordered_set<std::string> all = {"true", "false", "yes", "no", "y", "n", "t", "f", "1", "0"};
    return all.count(lo) > 0;
}

bool IsInteger(std::string_view s)
{
    if (s.empty())
        return false;
    size_t start = 0;
    if (s[0] == '+' || s[0] == '-')
        start = 1;
    if (start >= s.size())
        return false;
    for (size_t i = start; i < s.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(s[i])))
            return false;
    return true;
}

bool IsReal(std::string_view s)
{
    if (s.empty())
        return false;
    char* end = nullptr;
    // std::string needed for strtod null-termination
    const std::string tmp{s};
    std::strtod(tmp.c_str(), &end);
    return end != tmp.c_str() && *end == '\0';
}

bool IsDate(std::string_view s)
{
    if (s.size() < 8)
        return false;
    if (std::isdigit(static_cast<unsigned char>(s[0])) && std::isdigit(static_cast<unsigned char>(s[1])) && std::isdigit(static_cast<unsigned char>(s[2])) &&
        std::isdigit(static_cast<unsigned char>(s[3])) && (s[4] == '-' || s[4] == '/') && std::isdigit(static_cast<unsigned char>(s[5])) &&
        std::isdigit(static_cast<unsigned char>(s[6])) && (s[7] == '-' || s[7] == '/') && s.size() >= 10 &&
        std::isdigit(static_cast<unsigned char>(s[8])) && std::isdigit(static_cast<unsigned char>(s[9])))
        return true;
    if (s.size() >= 10 && std::isdigit(static_cast<unsigned char>(s[0])) && std::isdigit(static_cast<unsigned char>(s[1])) &&
        (s[2] == '-' || s[2] == '/') && std::isdigit(static_cast<unsigned char>(s[3])) && std::isdigit(static_cast<unsigned char>(s[4])) &&
        (s[5] == '-' || s[5] == '/') && std::isdigit(static_cast<unsigned char>(s[6])) && std::isdigit(static_cast<unsigned char>(s[7])) &&
        std::isdigit(static_cast<unsigned char>(s[8])) && std::isdigit(static_cast<unsigned char>(s[9])))
        return true;
    return false;
}

bool IsDateTime(std::string_view s)
{
    if (s.size() < 16)
        return false;
    if (!std::isdigit(static_cast<unsigned char>(s[0])) || !std::isdigit(static_cast<unsigned char>(s[1])) ||
        !std::isdigit(static_cast<unsigned char>(s[2])) || !std::isdigit(static_cast<unsigned char>(s[3])) || (s[4] != '-' && s[4] != '/'))
        return false;
    if (s[10] != 'T' && s[10] != ' ')
        return false;
    if (!std::isdigit(static_cast<unsigned char>(s[11])) || !std::isdigit(static_cast<unsigned char>(s[12])) || s[13] != ':' ||
        !std::isdigit(static_cast<unsigned char>(s[14])) || !std::isdigit(static_cast<unsigned char>(s[15])))
        return false;
    return true;
}

int CountOutsideQuotes(std::string_view line, char delim)
{
    int  count    = 0;
    bool inQuotes = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '"') {
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

} // anonymous namespace


namespace datagrid::adapters::utils {

const char* TypeName(InferredType t) noexcept
{
    switch (t) {
        case InferredType::Boolean:
            return "boolean";
        case InferredType::Integer:
            return "integer";
        case InferredType::Real:
            return "real";
        case InferredType::DateTime:
            return "datetime";
        case InferredType::Date:
            return "date";
        case InferredType::Text:
            return "text";
    }
    return "text";
}

TypeResult InferColumnType(const std::vector<std::string>& samples)
{
    TypeResult result;
    if (samples.empty())
        return result;

    const size_t limit = std::min(samples.size(), size_t(200));
    result.total       = static_cast<int>(limit);

    int nullCount = 0;
    int boolWord  = 0;
    int boolAny   = 0;
    int intCount  = 0;
    int realCount = 0;
    int dateCount = 0;
    int dtCount   = 0;

    bool firstNonNull = true;

    for (size_t i = 0; i < limit; ++i) {
        const std::string& s = samples[i];
        if (s.empty()) {
            ++nullCount;
            continue;
        }

        if (firstNonNull) {
            result.minValue = s;
            result.maxValue = s;
            firstNonNull    = false;
        } else {
            if (s < result.minValue)
                result.minValue = s;
            if (s > result.maxValue)
                result.maxValue = s;
        }

        if (IsDateTime(s)) {
            ++dtCount;
            ++dateCount;
        } else if (IsDate(s)) {
            ++dateCount;
        }

        if (IsInteger(s)) {
            ++intCount;
            ++realCount;
        } else if (IsReal(s)) {
            ++realCount;
        }

        if (IsBoolWord(s))
            ++boolWord;
        if (IsBoolAny(s))
            ++boolAny;
    }

    result.nullCount  = nullCount;
    const int nonNull = result.total - nullCount;
    if (nonNull == 0)
        return result;

    const float threshold = 0.90f;
    auto        conf      = [&](int count) { return static_cast<float>(count) / nonNull; };

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

    result.type       = InferredType::Text;
    result.confidence = static_cast<float>(nonNull) / result.total;
    return result;
}

DelimiterResult DetectDelimiter(const std::vector<std::string>& lines)
{
    if (lines.empty())
        return {',', 0.0f};

    const char   candidates[] = {',', '\t', ';', '|', ':'};
    const size_t checkLines   = std::min(lines.size(), size_t(8));

    char  bestDelim = ',';
    float bestScore = -1.0f;

    for (char c : candidates) {
        std::vector<int> counts;
        counts.reserve(checkLines);
        for (size_t i = 0; i < checkLines; ++i)
            counts.push_back(CountOutsideQuotes(lines[i], c));

        const int maxCnt = *std::max_element(counts.begin(), counts.end());
        if (maxCnt == 0)
            continue;

        float mean = 0.0f;
        for (int n : counts)
            mean += n;
        mean /= static_cast<float>(counts.size());

        float variance = 0.0f;
        for (int n : counts) {
            const float diff = n - mean;
            variance += diff * diff;
        }
        variance /= static_cast<float>(counts.size());
        const float stddev = std::sqrt(variance);

        const float score = mean / (1.0f + stddev);

        if (score > bestScore) {
            bestScore = score;
            bestDelim = c;
        }
    }

    const float normK      = (bestScore > 0.0f && bestScore == std::floor(bestScore)) ? 2.0f : 4.0f;
    const float confidence = (bestScore > 0.0f) ? std::min(1.0f, bestScore / (bestScore + normK)) : 0.0f;

    return {bestDelim, confidence};
}

HeaderResult DetectHeader(const std::vector<std::string>& firstRow, const std::vector<std::string>& secondRow)
{
    HeaderResult result;
    result.confidence = 0.5f;

    if (firstRow.empty()) {
        result.hasHeader  = false;
        result.confidence = 0.0f;
        result.reason     = "empty first row";
        return result;
    }

    float                    score = 0.5f;
    std::vector<std::string> reasons;

    if (!secondRow.empty() && firstRow.size() != secondRow.size()) {
        score -= 0.10f;
        reasons.push_back("column count mismatch");
    }

    int firstNumeric  = 0;
    int secondNumeric = 0;

    for (const auto& v : firstRow)
        if (!v.empty() && (IsInteger(v) || IsReal(v)))
            ++firstNumeric;

    for (const auto& v : secondRow)
        if (!v.empty() && (IsInteger(v) || IsReal(v)))
            ++secondNumeric;

    const float firstNumRatio = static_cast<float>(firstNumeric) / static_cast<float>(firstRow.size());

    if (firstNumRatio < 0.25f) {
        score += 0.25f;
        reasons.push_back("non-numeric first row");
    } else if (firstNumRatio > 0.70f) {
        score -= 0.30f;
        reasons.push_back("mostly numeric first row");
    }

    if (!secondRow.empty()) {
        const float secondNumRatio = static_cast<float>(secondNumeric) / static_cast<float>(secondRow.size());
        if (secondNumRatio > firstNumRatio + 0.30f) {
            score += 0.15f;
            reasons.push_back("data row more numeric");
        }
    }

    {
        std::unordered_set<std::string> uniq(firstRow.begin(), firstRow.end());
        if (uniq.size() == firstRow.size() && firstRow.size() > 1) {
            score += 0.10f;
            reasons.push_back("all values unique");
        }
    }

    {
        int idLike = 0;
        for (const auto& v : firstRow) {
            if (v.empty())
                continue;
            if (const auto first = static_cast<unsigned char>(v[0]); !std::isdigit(first) && v.size() <= 40)
                ++idLike;
        }
        if (static_cast<float>(idLike) / static_cast<float>(firstRow.size()) > 0.70f) {
            score += 0.10f;
            reasons.emplace_back("identifier-like names");
        }
    }

    if (secondRow.empty()) {
        score = 0.5f + (score - 0.5f) * 0.5f;
        reasons.emplace_back("single row — low confidence");
    }

    score = std::max(0.0f, std::min(1.0f, score));

    std::string reasonStr;
    for (size_t i = 0; i < reasons.size(); ++i) {
        if (i)
            reasonStr += "; ";
        reasonStr += reasons[i];
    }
    if (reasonStr.empty())
        reasonStr = "neutral evidence";

    result.hasHeader  = (score > 0.50f);
    result.confidence = score;
    result.reason     = std::move(reasonStr);
    return result;
}

} // namespace datagrid::adapters::utils
