#include "dupli/json.hpp"

#include <cmath>
#include <cstdio>
#include <limits>
#include <utility>

namespace dupli::json {

namespace {

const Value& null_value() {
    static const Value value;
    return value;
}

std::string escape_string(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 8);
    out.push_back('"');
    for (unsigned char c : input) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[7];
                std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                out += buf;
            } else {
                // UTF-8 bytes are passed through unchanged; callers supply
                // UTF-8 strings (std::filesystem::path conversions on POSIX).
                out.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    out.push_back('"');
    return out;
}

std::string format_number(double value) {
    if (!std::isfinite(value)) return "0";
    if (value == static_cast<double>(static_cast<int64_t>(value)) &&
        std::fabs(value) < 9.007199254740992e15) {
        return std::to_string(static_cast<int64_t>(value));
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.17g", value);
    return buf;
}

void dump_into(const Value& value, std::string& out, bool pretty, int indent) {
    const std::string pad(static_cast<size_t>(pretty ? indent : 0), ' ');
    const std::string pad_child(static_cast<size_t>(pretty ? indent + 2 : 0), ' ');
    switch (value.type()) {
    case Value::Type::Null:
        out += "null";
        break;
    case Value::Type::Bool:
        out += value.as_bool() ? "true" : "false";
        break;
    case Value::Type::Number:
        out += format_number(value.as_double());
        break;
    case Value::Type::String:
        out += escape_string(value.as_string());
        break;
    case Value::Type::Array: {
        const auto& items = value.as_array();
        if (items.empty()) {
            out += "[]";
            break;
        }
        out += "[";
        for (size_t i = 0; i < items.size(); ++i) {
            if (i) out += ",";
            if (pretty) { out += "\n"; out += pad_child; }
            dump_into(items[i], out, pretty, indent + 2);
        }
        if (pretty) { out += "\n"; out += pad; }
        out += "]";
        break;
    }
    case Value::Type::Object: {
        const auto& members = value.as_object();
        if (members.empty()) {
            out += "{}";
            break;
        }
        out += "{";
        bool first = true;
        for (const auto& [key, member] : members) {
            if (!first) out += ",";
            first = false;
            if (pretty) { out += "\n"; out += pad_child; }
            out += escape_string(key);
            out += pretty ? ": " : ":";
            dump_into(member, out, pretty, indent + 2);
        }
        if (pretty) { out += "\n"; out += pad; }
        out += "}";
        break;
    }
    }
}

class Parser {
public:
    Parser(std::string_view text, ParseError* error) : m_text(text), m_error(error) {}

    std::optional<Value> run() {
        skip_whitespace();
        auto value = parse_value(0);
        if (!value) return std::nullopt;
        skip_whitespace();
        if (m_pos != m_text.size()) {
            fail("unexpected trailing characters");
            return std::nullopt;
        }
        return value;
    }

private:
    static constexpr int kMaxDepth = 64;

    bool fail(const std::string& message) {
        if (m_error && m_error->message.empty()) {
            m_error->offset = m_pos;
            m_error->message = message;
        }
        return false;
    }

    void skip_whitespace() {
        while (m_pos < m_text.size()) {
            char c = m_text[m_pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++m_pos;
            } else {
                break;
            }
        }
    }

    bool consume(char expected) {
        if (m_pos < m_text.size() && m_text[m_pos] == expected) {
            ++m_pos;
            return true;
        }
        return false;
    }

    std::optional<Value> parse_value(int depth) {
        if (depth > kMaxDepth) {
            fail("nesting too deep");
            return std::nullopt;
        }
        if (m_pos >= m_text.size()) {
            fail("unexpected end of input");
            return std::nullopt;
        }
        char c = m_text[m_pos];
        switch (c) {
        case '{': return parse_object(depth);
        case '[': return parse_array(depth);
        case '"': {
            auto text = parse_string();
            if (!text) return std::nullopt;
            return Value(std::move(*text));
        }
        case 't':
            if (m_text.substr(m_pos, 4) == "true") { m_pos += 4; return Value(true); }
            fail("invalid literal");
            return std::nullopt;
        case 'f':
            if (m_text.substr(m_pos, 5) == "false") { m_pos += 5; return Value(false); }
            fail("invalid literal");
            return std::nullopt;
        case 'n':
            if (m_text.substr(m_pos, 4) == "null") { m_pos += 4; return Value(); }
            fail("invalid literal");
            return std::nullopt;
        default:
            return parse_number();
        }
    }

    std::optional<Value> parse_object(int depth) {
        if (!consume('{')) return std::nullopt;
        Value::Object members;
        skip_whitespace();
        if (consume('}')) return Value(std::move(members));
        while (true) {
            skip_whitespace();
            auto key = parse_string();
            if (!key) return std::nullopt;
            skip_whitespace();
            if (!consume(':')) {
                fail("expected ':' after object key");
                return std::nullopt;
            }
            skip_whitespace();
            auto value = parse_value(depth + 1);
            if (!value) return std::nullopt;
            members.emplace(std::move(*key), std::move(*value));
            skip_whitespace();
            if (consume(',')) continue;
            if (consume('}')) break;
            fail("expected ',' or '}' in object");
            return std::nullopt;
        }
        return Value(std::move(members));
    }

    std::optional<Value> parse_array(int depth) {
        if (!consume('[')) return std::nullopt;
        Value::Array items;
        skip_whitespace();
        if (consume(']')) return Value(std::move(items));
        while (true) {
            skip_whitespace();
            auto value = parse_value(depth + 1);
            if (!value) return std::nullopt;
            items.push_back(std::move(*value));
            skip_whitespace();
            if (consume(',')) continue;
            if (consume(']')) break;
            fail("expected ',' or ']' in array");
            return std::nullopt;
        }
        return Value(std::move(items));
    }

    std::optional<std::string> parse_string() {
        if (!consume('"')) {
            fail("expected string");
            return std::nullopt;
        }
        std::string out;
        while (m_pos < m_text.size()) {
            unsigned char c = static_cast<unsigned char>(m_text[m_pos++]);
            if (c == '"') return out;
            if (c != '\\') {
                out.push_back(static_cast<char>(c));
                continue;
            }
            if (m_pos >= m_text.size()) {
                fail("unterminated escape sequence");
                return std::nullopt;
            }
            char esc = m_text[m_pos++];
            switch (esc) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                // Only the Basic Latin range is decoded; \u sequences outside
                // that are replaced to keep the parser small. Our own writer
                // only emits \u for ASCII control characters.
                if (m_pos + 4 > m_text.size()) {
                    fail("truncated unicode escape");
                    return std::nullopt;
                }
                unsigned code = 0;
                for (int i = 0; i < 4; ++i) {
                    char h = m_text[m_pos + static_cast<size_t>(i)];
                    code <<= 4;
                    if (h >= '0' && h <= '9') code |= static_cast<unsigned>(h - '0');
                    else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
                    else { fail("invalid unicode escape"); return std::nullopt; }
                }
                m_pos += 4;
                if (code == 0) {
                    out.push_back('\0');
                } else if (code < 0x80) {
                    out.push_back(static_cast<char>(code));
                } else {
                    out.push_back('?');
                }
                break;
            }
            default:
                fail("invalid escape sequence");
                return std::nullopt;
            }
        }
        fail("unterminated string");
        return std::nullopt;
    }

    std::optional<Value> parse_number() {
        size_t start = m_pos;
        if (m_pos < m_text.size() && (m_text[m_pos] == '-' || m_text[m_pos] == '+')) ++m_pos;
        bool digits = false;
        while (m_pos < m_text.size() && m_text[m_pos] >= '0' && m_text[m_pos] <= '9') {
            ++m_pos;
            digits = true;
        }
        if (m_pos < m_text.size() && m_text[m_pos] == '.') {
            ++m_pos;
            while (m_pos < m_text.size() && m_text[m_pos] >= '0' && m_text[m_pos] <= '9') {
                ++m_pos;
                digits = true;
            }
        }
        if (!digits) {
            fail("expected value");
            return std::nullopt;
        }
        if (m_pos < m_text.size() && (m_text[m_pos] == 'e' || m_text[m_pos] == 'E')) {
            ++m_pos;
            if (m_pos < m_text.size() && (m_text[m_pos] == '-' || m_text[m_pos] == '+')) ++m_pos;
            bool exp_digits = false;
            while (m_pos < m_text.size() && m_text[m_pos] >= '0' && m_text[m_pos] <= '9') {
                ++m_pos;
                exp_digits = true;
            }
            if (!exp_digits) {
                fail("invalid exponent");
                return std::nullopt;
            }
        }
        std::string token(m_text.substr(start, m_pos - start));
        try {
            return Value(std::stod(token));
        } catch (...) {
            fail("invalid number");
            return std::nullopt;
        }
    }

    std::string_view m_text;
    size_t m_pos = 0;
    ParseError* m_error;
};

} // namespace

const std::string& Value::as_string() const {
    static const std::string empty;
    return m_type == Type::String ? m_string : empty;
}

const Value::Array& Value::as_array() const {
    static const Array empty;
    return m_type == Type::Array ? m_array : empty;
}

const Value::Object& Value::as_object() const {
    static const Object empty;
    return m_type == Type::Object ? m_object : empty;
}

const Value& Value::operator[](const std::string& key) const {
    if (m_type != Type::Object) return null_value();
    auto it = m_object.find(key);
    return it == m_object.end() ? null_value() : it->second;
}

bool Value::contains(const std::string& key) const {
    return m_type == Type::Object && m_object.find(key) != m_object.end();
}

Value& Value::set(const std::string& key, Value value) {
    if (m_type != Type::Object) {
        m_type = Type::Object;
        m_object.clear();
    }
    m_object[key] = std::move(value);
    return *this;
}

Value& Value::push(Value value) {
    if (m_type != Type::Array) {
        m_type = Type::Array;
        m_array.clear();
    }
    m_array.push_back(std::move(value));
    return *this;
}

const Value& Value::null() {
    return null_value();
}

std::string dump(const Value& value, bool pretty) {
    std::string out;
    dump_into(value, out, pretty, 0);
    return out;
}

std::optional<Value> parse(std::string_view text, ParseError* error) {
    if (error) *error = ParseError{};
    Parser parser(text, error);
    return parser.run();
}

} // namespace dupli::json
