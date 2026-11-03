#pragma once

// Minimal JSON support used for the versioned report/manifest, saved action
// plans and the append-only journal. It is deliberately dependency-free and
// limited to what those formats need (objects, arrays, strings, numbers,
// booleans, null). Object keys are kept in a std::map so serialized output is
// byte-for-byte deterministic across runs, which makes reports diffable and
// testable.
//
// Numbers are stored as double. This is exact for every value we emit except
// gigantic (>2^53) byte counts, which no supported file size reaches; sizes are
// additionally reported as decimal strings in human-facing summaries.
//
// Security note: parsing is only ever applied to files this program wrote
// (journal, saved plans). The parser bounds nesting depth and rejects trailing
// garbage, but callers must still validate every field before acting on it.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dupli::json {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    using Array = std::vector<Value>;
    using Object = std::map<std::string, Value>;

    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool value) : m_type(Type::Bool), m_bool(value) {}
    Value(int value) : m_type(Type::Number), m_number(value) {}
    Value(int64_t value) : m_type(Type::Number), m_number(static_cast<double>(value)) {}
    Value(uint64_t value) : m_type(Type::Number), m_number(static_cast<double>(value)) {}
    Value(double value) : m_type(Type::Number), m_number(value) {}
    Value(const char* value) : m_type(Type::String), m_string(value ? value : "") {}
    Value(std::string value) : m_type(Type::String), m_string(std::move(value)) {}
    Value(Array values) : m_type(Type::Array), m_array(std::move(values)) {}
    Value(Object values) : m_type(Type::Object), m_object(std::move(values)) {}

    Type type() const { return m_type; }
    bool is_null() const { return m_type == Type::Null; }
    bool is_bool() const { return m_type == Type::Bool; }
    bool is_number() const { return m_type == Type::Number; }
    bool is_string() const { return m_type == Type::String; }
    bool is_array() const { return m_type == Type::Array; }
    bool is_object() const { return m_type == Type::Object; }

    bool as_bool(bool fallback = false) const {
        return m_type == Type::Bool ? m_bool : fallback;
    }
    double as_double(double fallback = 0.0) const {
        return m_type == Type::Number ? m_number : fallback;
    }
    int64_t as_int(int64_t fallback = 0) const {
        return m_type == Type::Number ? static_cast<int64_t>(m_number) : fallback;
    }
    /// Returns an empty string for non-string values instead of throwing; call
    /// sites validate required fields explicitly and report a typed error.
    const std::string& as_string() const;
    const Array& as_array() const;
    const Object& as_object() const;

    /// Object member lookup; returns a shared null value when absent.
    const Value& operator[](const std::string& key) const;
    bool contains(const std::string& key) const;

    /// Insert/replace an object member (converts this value to an object).
    Value& set(const std::string& key, Value value);
    /// Append to an array (converts this value to an array).
    Value& push(Value value);

    static const Value& null();

private:
    Type m_type = Type::Null;
    bool m_bool = false;
    double m_number = 0.0;
    std::string m_string;
    Array m_array;
    Object m_object;
};

struct ParseError {
    size_t offset = 0;
    std::string message;
};

/// Serialize a value. `pretty` adds two-space indentation and newlines.
std::string dump(const Value& value, bool pretty = true);

/// Parse JSON text. Returns std::nullopt and fills `error` on malformed input.
std::optional<Value> parse(std::string_view text, ParseError* error = nullptr);

} // namespace dupli::json
