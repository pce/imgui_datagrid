#pragma once
#include <string>
#include <vector>

namespace TypeInfer {

enum class InferredType
{
    Boolean,  ///< {true,false,yes,no,t,f,y,n,1,0} — case-insensitive
    Integer,  ///< Whole numbers with optional leading sign
    Real,     ///< Floating-point (strict superset of Integer)
    DateTime, ///< ISO-8601 date+time: YYYY-MM-DD HH:MM[:SS] or YYYY-MM-DDTHH:MM
    Date,     ///< ISO-8601 date only: YYYY-MM-DD (also YYYY/MM/DD, DD-MM-YYYY, DD/MM/YYYY)
    Text,     ///< Catch-all; returned when no stricter type fits
};

/// Human-readable lowercase name for a type (e.g. "integer", "date").
[[nodiscard]] const char* TypeName(InferredType t) noexcept;

struct TypeResult
{
    InferredType type       = InferredType::Text;
    float        confidence = 0.0f; ///< Fraction of non-null samples that matched (0–1)
    int          nullCount  = 0;    ///< Empty / null values in the sample set
    int          total      = 0;    ///< Total samples examined (including nulls)
    std::string  minValue;          ///< Lexicographically smallest non-null value
    std::string  maxValue;          ///< Lexicographically largest non-null value
};

/// Infer the most specific type fitting at least 90 % of non-null values.
/// Up to 200 samples are examined; the remainder is ignored for performance.
[[nodiscard]] TypeResult InferColumnType(const std::vector<std::string>& samples);

struct DelimiterResult
{
    char  delimiter  = ',';  ///< Most likely field separator
    float confidence = 0.5f; ///< Confidence (0–1); < 0.4 → few or inconsistent delimiters
};

/// Detect the field delimiter by inspecting the first few raw lines of a file.
/// Pass 5–10 lines for best accuracy; zero lines returns {',', 0.0f}.
[[nodiscard]] DelimiterResult DetectDelimiter(const std::vector<std::string>& lines);

struct HeaderResult
{
    bool        hasHeader  = true; ///< True if row 0 is most likely a header
    float       confidence = 0.5f; ///< Confidence in the decision (0–1)
    std::string reason;            ///< Human-readable scoring explanation
};

/// Estimate whether the first row of a parsed file is a header row.
///
/// @param firstRow   Fields of row 0 (already split by the detected delimiter).
/// @param secondRow  Fields of row 1.  May be empty for single-row files —
///                   confidence will be low in that case.
[[nodiscard]] HeaderResult DetectHeader(const std::vector<std::string>& firstRow,
                                        const std::vector<std::string>& secondRow);

} // namespace TypeInfer
