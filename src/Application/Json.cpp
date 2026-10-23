#include "Application/Json.h"

#include <cctype>
#include <cstdlib>

namespace golding
{
namespace detail
{
namespace json
{

const Value* Value::find(const std::string& key) const
{
    if (type != Type::object)
    {
        return nullptr;
    }
    for (const auto& entry : object)
    {
        if (entry.first == key)
        {
            return &entry.second;
        }
    }
    return nullptr;
}

namespace
{

/**嵌套上限：挡住恶意或损坏文件把栈打穿。*/
constexpr int kMaxDepth = 64;

class Parser
{
public:
    explicit Parser(const std::string& text) : text_(text) {}

    bool ParseDocument(Value& out)
    {
        SkipSpace();
        if (!ParseValue(out, 0))
        {
            return false;
        }
        SkipSpace();
        return pos_ == text_.size();
    }

private:
    void SkipSpace()
    {
        while (pos_ < text_.size())
        {
            const char ch = text_[pos_];
            if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r')
            {
                break;
            }
            ++pos_;
        }
    }

    bool Eat(char expected)
    {
        if (pos_ >= text_.size() || text_[pos_] != expected)
        {
            return false;
        }
        ++pos_;
        return true;
    }

    bool ParseValue(Value& out, int depth)
    {
        if (depth > kMaxDepth || pos_ >= text_.size())
        {
            return false;
        }

        const char ch = text_[pos_];
        if (ch == '{')
        {
            return ParseObject(out, depth);
        }
        if (ch == '[')
        {
            return ParseArray(out, depth);
        }
        if (ch == '"')
        {
            out.type = Value::Type::string;
            return ParseString(out.text);
        }
        if (ch == 't')
        {
            return ParseLiteral("true", out, true);
        }
        if (ch == 'f')
        {
            return ParseLiteral("false", out, false);
        }
        if (ch == 'n')
        {
            if (text_.compare(pos_, 4, "null") != 0)
            {
                return false;
            }
            pos_ += 4;
            out.type = Value::Type::null;
            return true;
        }
        return ParseNumber(out);
    }

    bool ParseLiteral(const char* literal, Value& out, bool value)
    {
        const std::size_t length = std::char_traits<char>::length(literal);
        if (text_.compare(pos_, length, literal) != 0)
        {
            return false;
        }
        pos_ += length;
        out.type = Value::Type::boolean;
        out.boolean = value;
        return true;
    }

    bool ParseObject(Value& out, int depth)
    {
        if (!Eat('{'))
        {
            return false;
        }
        out.type = Value::Type::object;
        SkipSpace();
        if (pos_ < text_.size() && text_[pos_] == '}')
        {
            ++pos_;
            return true;
        }

        while (true)
        {
            SkipSpace();
            std::string key;
            if (!ParseString(key))
            {
                return false;
            }
            SkipSpace();
            if (!Eat(':'))
            {
                return false;
            }
            SkipSpace();
            Value member;
            if (!ParseValue(member, depth + 1))
            {
                return false;
            }
            out.object.emplace_back(std::move(key), std::move(member));

            SkipSpace();
            if (pos_ >= text_.size())
            {
                return false;
            }
            if (text_[pos_] == ',')
            {
                ++pos_;
                continue;
            }
            if (text_[pos_] == '}')
            {
                ++pos_;
                return true;
            }
            return false;
        }
    }

    bool ParseArray(Value& out, int depth)
    {
        if (!Eat('['))
        {
            return false;
        }
        out.type = Value::Type::array;
        SkipSpace();
        if (pos_ < text_.size() && text_[pos_] == ']')
        {
            ++pos_;
            return true;
        }

        while (true)
        {
            SkipSpace();
            Value element;
            if (!ParseValue(element, depth + 1))
            {
                return false;
            }
            out.array.push_back(std::move(element));

            SkipSpace();
            if (pos_ >= text_.size())
            {
                return false;
            }
            if (text_[pos_] == ',')
            {
                ++pos_;
                continue;
            }
            if (text_[pos_] == ']')
            {
                ++pos_;
                return true;
            }
            return false;
        }
    }

    bool ParseString(std::string& out)
    {
        if (!Eat('"'))
        {
            return false;
        }
        out.clear();
        while (pos_ < text_.size())
        {
            const char ch = text_[pos_++];
            if (ch == '"')
            {
                return true;
            }
            if (ch != '\\')
            {
                if (static_cast<unsigned char>(ch) < 0x20)
                {
                    return false; // 未转义的控制字符
                }
                out.push_back(ch);
                continue;
            }
            if (pos_ >= text_.size())
            {
                return false;
            }
            switch (text_[pos_++])
            {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u':
            {
                unsigned code = 0;
                if (!ParseHex4(code))
                {
                    return false;
                }
                if (code >= 0xD800 && code <= 0xDBFF)
                {
                    // 高代理：必须跟一个低代理
                    if (pos_ + 1 >= text_.size() || text_[pos_] != '\\' || text_[pos_ + 1] != 'u')
                    {
                        return false;
                    }
                    pos_ += 2;
                    unsigned low = 0;
                    if (!ParseHex4(low) || low < 0xDC00 || low > 0xDFFF)
                    {
                        return false;
                    }
                    code = 0x10000u + ((code - 0xD800u) << 10) + (low - 0xDC00u);
                }
                else if (code >= 0xDC00 && code <= 0xDFFF)
                {
                    return false; // 孤立的低代理
                }
                AppendUtf8(code, out);
                break;
            }
            default:
                return false;
            }
        }
        return false;
    }

    bool ParseHex4(unsigned& code)
    {
        if (pos_ + 4 > text_.size())
        {
            return false;
        }
        code = 0;
        for (int i = 0; i < 4; ++i)
        {
            const char ch = text_[pos_++];
            unsigned digit = 0;
            if (ch >= '0' && ch <= '9')
            {
                digit = static_cast<unsigned>(ch - '0');
            }
            else if (ch >= 'a' && ch <= 'f')
            {
                digit = static_cast<unsigned>(ch - 'a') + 10u;
            }
            else if (ch >= 'A' && ch <= 'F')
            {
                digit = static_cast<unsigned>(ch - 'A') + 10u;
            }
            else
            {
                return false;
            }
            code = code * 16u + digit;
        }
        return true;
    }

    static void AppendUtf8(unsigned code, std::string& out)
    {
        if (code < 0x80u)
        {
            out.push_back(static_cast<char>(code));
        }
        else if (code < 0x800u)
        {
            out.push_back(static_cast<char>(0xC0u | (code >> 6)));
            out.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
        }
        else if (code < 0x10000u)
        {
            out.push_back(static_cast<char>(0xE0u | (code >> 12)));
            out.push_back(static_cast<char>(0x80u | ((code >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
        }
        else
        {
            out.push_back(static_cast<char>(0xF0u | (code >> 18)));
            out.push_back(static_cast<char>(0x80u | ((code >> 12) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | ((code >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
        }
    }

    bool ParseNumber(Value& out)
    {
        const std::size_t start = pos_;

        if (pos_ < text_.size() && text_[pos_] == '-')
        {
            ++pos_;
        }

        if (pos_ >= text_.size())
        {
            return false;
        }
        if (text_[pos_] == '0')
        {
            ++pos_;
        }
        else if (std::isdigit(static_cast<unsigned char>(text_[pos_])) != 0)
        {
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_])) != 0)
            {
                ++pos_;
            }
        }
        else
        {
            return false;
        }

        if (pos_ < text_.size() && text_[pos_] == '.')
        {
            ++pos_;
            if (pos_ >= text_.size() || std::isdigit(static_cast<unsigned char>(text_[pos_])) == 0)
            {
                return false;
            }
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_])) != 0)
            {
                ++pos_;
            }
        }

        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E'))
        {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-'))
            {
                ++pos_;
            }
            if (pos_ >= text_.size() || std::isdigit(static_cast<unsigned char>(text_[pos_])) == 0)
            {
                return false;
            }
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_])) != 0)
            {
                ++pos_;
            }
        }

        const std::string slice = text_.substr(start, pos_ - start);
        out.type = Value::Type::number;
        out.number = std::strtod(slice.c_str(), nullptr);
        return true;
    }

    const std::string& text_;
    std::size_t pos_ = 0;
};

} // namespace

bool parse(const std::string& text, Value& out)
{
    Parser parser(text);
    return parser.ParseDocument(out);
}

} // namespace json
} // namespace detail
} // namespace golding
