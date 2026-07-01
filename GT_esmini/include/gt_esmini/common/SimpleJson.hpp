#pragma once

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace gt_esmini
{
namespace simplejson
{

struct Value
{
    enum class Type { Null, Bool, Number, String, Array, Object };
    using Array  = std::vector<Value>;
    using Object = std::map<std::string, Value>;

    Type        type = Type::Null;
    bool        bool_value = false;
    double      number_value = 0.0;
    std::string string_value;
    Array       array_value;
    Object      object_value;

    static Value Bool(bool value) { Value v; v.type = Type::Bool; v.bool_value = value; return v; }
    static Value Number(double value) { Value v; v.type = Type::Number; v.number_value = value; return v; }
    static Value String(const std::string& value) { Value v; v.type = Type::String; v.string_value = value; return v; }
    static Value ArrayValue(const Array& value) { Value v; v.type = Type::Array; v.array_value = value; return v; }
    static Value ObjectValue(const Object& value) { Value v; v.type = Type::Object; v.object_value = value; return v; }

    bool IsBool() const { return type == Type::Bool; }
    bool IsNumber() const { return type == Type::Number; }
    bool IsString() const { return type == Type::String; }
    bool IsObject() const { return type == Type::Object; }

    const Value* Find(const std::string& key) const
    {
        if (!IsObject()) return nullptr;
        const auto it = object_value.find(key);
        return it == object_value.end() ? nullptr : &it->second;
    }

    // Coerce a string to a number if (and only if) its ENTIRE content parses as
    // one (leading/trailing whitespace tolerated). Returns false otherwise.
    static bool StringToNumber(const std::string& text, double& out)
    {
        const char* begin = text.c_str();
        char* end = nullptr;
        const double value = std::strtod(begin, &end);
        if (end == begin) return false;  // no conversion at all
        while (*end != '\0' && std::isspace(static_cast<unsigned char>(*end))) ++end;
        if (*end != '\0') return false;  // trailing garbage (e.g. "1.2.3")
        out = value;
        return true;
    }

    // Accepts a real JSON bool, or the strings "true"/"false" (case-insensitive)
    // that pre-refactor line parsers tolerated.
    bool GetBool(const std::string& key, bool& out) const
    {
        const Value* value = Find(key);
        if (!value) return false;
        if (value->IsBool()) { out = value->bool_value; return true; }
        if (value->IsString())
        {
            std::string lowered = value->string_value;
            for (char& c : lowered) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lowered == "true") { out = true; return true; }
            if (lowered == "false") { out = false; return true; }
        }
        return false;
    }

    // Accepts a real JSON number, or a string whose entire content is a number.
    bool GetDouble(const std::string& key, double& out) const
    {
        const Value* value = Find(key);
        if (!value) return false;
        if (value->IsNumber()) { out = value->number_value; return true; }
        if (value->IsString()) return StringToNumber(value->string_value, out);
        return false;
    }

    bool GetInt(const std::string& key, int& out) const
    {
        double value = 0.0;
        if (!GetDouble(key, value)) return false;
        out = static_cast<int>(value);
        return true;
    }

    bool GetString(const std::string& key, std::string& out) const
    {
        const Value* value = Find(key);
        if (!value || !value->IsString()) return false;
        out = value->string_value;
        return true;
    }
};

class Parser
{
public:
    Parser(const std::string& text, std::string* error) : text_(text), error_(error) {}

    bool Parse(Value& out)
    {
        SkipWhitespace();
        if (!ParseValue(out)) return false;
        SkipWhitespace();
        return pos_ == text_.size() || Fail("unexpected trailing characters");
    }

private:
    bool ParseValue(Value& out)
    {
        SkipWhitespace();
        if (pos_ >= text_.size()) return Fail("unexpected end of input");
        const char ch = text_[pos_];
        if (ch == '{') return ParseObject(out);
        if (ch == '[') return ParseArray(out);
        if (ch == '"')
        {
            std::string value;
            if (!ParseString(value)) return false;
            out = Value::String(value);
            return true;
        }
        if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch))) return ParseNumber(out);
        if (ConsumeLiteral("true")) { out = Value::Bool(true); return true; }
        if (ConsumeLiteral("false")) { out = Value::Bool(false); return true; }
        if (ConsumeLiteral("null")) { out = Value{}; return true; }
        return Fail("unexpected token");
    }

    bool ParseObject(Value& out)
    {
        if (!Expect('{')) return false;
        Value::Object object;
        SkipWhitespace();
        if (Consume('}')) { out = Value::ObjectValue(object); return true; }
        while (pos_ < text_.size())
        {
            std::string key;
            if (!ParseString(key) || !Expect(':')) return false;
            Value value;
            if (!ParseValue(value)) return false;
            if (!object.emplace(key, value).second) return Fail("duplicate object key");
            SkipWhitespace();
            if (Consume('}')) { out = Value::ObjectValue(object); return true; }
            if (!Expect(',')) return false;
            // Tolerate a trailing comma: `,` followed by the closing brace.
            SkipWhitespace();
            if (Consume('}')) { out = Value::ObjectValue(object); return true; }
        }
        return Fail("unterminated object");
    }

    bool ParseArray(Value& out)
    {
        if (!Expect('[')) return false;
        Value::Array array;
        SkipWhitespace();
        if (Consume(']')) { out = Value::ArrayValue(array); return true; }
        while (pos_ < text_.size())
        {
            Value value;
            if (!ParseValue(value)) return false;
            array.push_back(value);
            SkipWhitespace();
            if (Consume(']')) { out = Value::ArrayValue(array); return true; }
            if (!Expect(',')) return false;
            // Tolerate a trailing comma: `,` followed by the closing bracket.
            SkipWhitespace();
            if (Consume(']')) { out = Value::ArrayValue(array); return true; }
        }
        return Fail("unterminated array");
    }

    bool ParseString(std::string& out)
    {
        if (!Expect('"')) return false;
        out.clear();
        while (pos_ < text_.size())
        {
            const char ch = text_[pos_++];
            if (ch == '"') return true;
            if (ch != '\\')
            {
                out += ch;
                continue;
            }
            if (pos_ >= text_.size()) return Fail("unterminated string escape");
            const char esc = text_[pos_++];
            switch (esc)
            {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u':
                if (pos_ + 4 > text_.size()) return Fail("invalid unicode escape");
                out += '?';
                pos_ += 4;
                break;
            default:
                return Fail("invalid string escape");
            }
        }
        return Fail("unterminated string");
    }

    bool ParseNumber(Value& out)
    {
        const size_t start = pos_;
        Consume('-');
        if (pos_ >= text_.size()) return Fail("invalid number");
        if (text_[pos_] == '0')
        {
            ++pos_;
        }
        else if (std::isdigit(static_cast<unsigned char>(text_[pos_])))
        {
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_;
        }
        else
        {
            return Fail("invalid number");
        }
        if (pos_ < text_.size() && text_[pos_] == '.')
        {
            ++pos_;
            if (pos_ >= text_.size() || !std::isdigit(static_cast<unsigned char>(text_[pos_]))) return Fail("invalid number fraction");
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_;
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E'))
        {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
            if (pos_ >= text_.size() || !std::isdigit(static_cast<unsigned char>(text_[pos_]))) return Fail("invalid number exponent");
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_;
        }
        const std::string token = text_.substr(start, pos_ - start);
        char* end = nullptr;
        const double value = std::strtod(token.c_str(), &end);
        if (!end || *end != '\0') return Fail("invalid number conversion");
        out = Value::Number(value);
        return true;
    }

    bool ConsumeLiteral(const char* literal)
    {
        const std::string value(literal);
        if (text_.compare(pos_, value.size(), value) != 0) return false;
        pos_ += value.size();
        return true;
    }

    bool Consume(char expected)
    {
        if (pos_ < text_.size() && text_[pos_] == expected)
        {
            ++pos_;
            return true;
        }
        return false;
    }

    bool Expect(char expected)
    {
        SkipWhitespace();
        if (Consume(expected)) return true;
        std::string message = "expected '";
        message += expected;
        message += "'";
        return Fail(message);
    }

    // Treats standard JSON whitespace AND `//`-to-EOL line comments as skippable.
    // Hand-edited config files commonly carry both; the pre-refactor line parsers
    // tolerated them.
    void SkipWhitespace()
    {
        while (pos_ < text_.size())
        {
            const char ch = text_[pos_];
            if (std::isspace(static_cast<unsigned char>(ch)))
            {
                ++pos_;
                continue;
            }
            if (ch == '/' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '/')
            {
                pos_ += 2;
                while (pos_ < text_.size() && text_[pos_] != '\n') ++pos_;
                continue;
            }
            break;
        }
    }

    bool Fail(const std::string& message) const
    {
        if (error_)
        {
            std::ostringstream os;
            os << message << " at byte " << pos_;
            *error_ = os.str();
        }
        return false;
    }

    const std::string& text_;
    size_t pos_ = 0;
    std::string* error_ = nullptr;
};

inline bool Parse(const std::string& text, Value& out, std::string* error = nullptr)
{
    Parser parser(text, error);
    return parser.Parse(out);
}

inline bool LoadFile(const std::string& path, Value& out, std::string* error = nullptr)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        if (error) *error = "failed to open file";
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return Parse(buffer.str(), out, error);
}

}  // namespace simplejson
}  // namespace gt_esmini
