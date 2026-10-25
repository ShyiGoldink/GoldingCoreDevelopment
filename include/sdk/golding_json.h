#pragma once

// 极简 JSON 读取器：服务于 plugin.json 与插件自己的配置（shape.json / apps.json…）。
//
// 它原先是 core 内部的 `Application/Json.h` + `src/Application/Json.cpp`（437 行），
// 被编进 `golding_core`；而插件只链 `golding_sdk` ⇒ 头可见、符号不在 ⇒ 插件只能
// 自带一份手写解析（T-01 之前同一份 JSON 在仓库里有三种读法）。搬到这里之后：
//   - 全 `inline`、零导出符号：插件 `#include <sdk/golding_json.h>` 即可，不新增 dll 依赖；
//   - 命名空间从 `golding::detail::json` 改成 `golding::json`：进了 SDK 就是公开面，
//     `detail` 反而误导（读者会以为"随时可改"）。
//
// API 刻意保持**极窄**（读：`parse` + `Value::find`；写：`dump`），以后换实现时
// 只改这一层，调用方一个字都不用动。
//
// 支持：object / array / string（含 \uXXXX 与代理对）/ number / true / false / null。
// 不支持：注释、尾逗号、NaN / Infinity、深度超过 kMaxDepth 的嵌套。
// **不抛异常**（本仓铁律：异常不得穿 dll 边界）：失败一律返回 false。

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace golding
{
namespace json
{

struct Value
{
    enum class Type
    {
        null,
        boolean,
        number,
        string,
        array,
        object,
    };

    Type type = Type::null;
    bool boolean = false;
    double number = 0.0;
    std::string text;
    std::vector<Value> array;
    /**保持声明顺序；同名键保留全部，取首个。*/
    std::vector<std::pair<std::string, Value>> object;

    /**在 object 里按键查找；不是 object 或没这个键都返回 nullptr。*/
    const Value* find(const std::string& key) const
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
};

namespace detail
{

/**嵌套上限：挡住恶意或损坏文件把栈打穿。*/
inline constexpr int kMaxDepth = 64;

/**
 * 手写递归下降解析器。
 *
 * 放在**具名** `detail` 命名空间（而不是匿名）：匿名命名空间会让每个 TU 各生成一份
 * 互不相同的类型，头文件里的类定义本来是允许跨 TU 重复的，换成匿名反而招来
 * "同一实体两个身份"的麻烦。
 */
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
                if (code >= 0xD800u && code <= 0xDBFFu)
                {
                    // 高代理：必须跟一个低代理
                    if (pos_ + 1 >= text_.size() || text_[pos_] != '\\' || text_[pos_ + 1] != 'u')
                    {
                        return false;
                    }
                    pos_ += 2;
                    unsigned low = 0;
                    if (!ParseHex4(low) || low < 0xDC00u || low > 0xDFFFu)
                    {
                        return false;
                    }
                    code = 0x10000u + ((code - 0xD800u) << 10) + (low - 0xDC00u);
                }
                else if (code >= 0xDC00u && code <= 0xDFFFu)
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

} // namespace detail

/**解析整段文本；失败返回 false，out 内容不可信。*/
inline bool parse(const std::string& text, Value& out)
{
    detail::Parser parser(text);
    return parser.ParseDocument(out);
}

// ---- 写侧 ----
//
// 只有一个 `dump`。不提供 pretty（缩进会把"格式化"变成第二个语义决策点），
// 不提供构造器（`Value` 成员公开，拼装十行就够，加了反而多一套要维护的 DSL）。
//
// 转义策略定死：只转 `"`、`\` 与 `<0x20` 的控制字符（后者里 `\n \t \r \b \f`
// 用具名转义，其余用 `\u00XX`），其余**原样输出 UTF-8、不转 `\u`**——
// 与 `parse` 能读回来的最小集合对称。

/**按 JSON 字符串规则把 `text` 追加进 `out`（不含两侧的引号由调用方处理）。*/
inline void appendEscaped(std::string& out, const std::string& text)
{
    out.push_back('"');
    for (const char raw : text)
    {
        switch (raw)
        {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(raw) < 0x20u)
            {
                // 其余控制字符没有具名转义，JSON 也不允许裸着出现 ⇒ 只能 \u00XX
                char hex[8] = {};
                std::snprintf(hex, sizeof(hex), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(raw)));
                out += hex;
            }
            else
            {
                out.push_back(raw); // 非 ASCII 原样输出 UTF-8：`parse` 照样读得回来
            }
            break;
        }
    }
    out.push_back('"');
}

namespace detail
{

/**`%.17g` 保往返精度；整数值不输出小数点（`31.0` → `31`）。*/
inline void appendNumber(double number, std::string& out)
{
    if (!std::isfinite(number))
    {
        // `parse` 产不出非有限数；这里兜住"手拼的 Value"。宁可丢一个值，
        // 也不写出一份 `nan` / `inf` 让下一次 parse 整份失败。
        out += "null";
        return;
    }

    char buffer[40] = {};
    if (std::floor(number) == number && std::fabs(number) < 1e17)
    {
        std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(number));
    }
    else
    {
        std::snprintf(buffer, sizeof(buffer), "%.17g", number);
    }
    out += buffer;
}

inline void dumpValue(const Value& value, std::string& out)
{
    switch (value.type)
    {
    case Value::Type::null:
        out += "null";
        break;
    case Value::Type::boolean:
        out += value.boolean ? "true" : "false";
        break;
    case Value::Type::number:
        appendNumber(value.number, out);
        break;
    case Value::Type::string:
        appendEscaped(out, value.text);
        break;
    case Value::Type::array:
        out.push_back('[');
        for (std::size_t i = 0; i < value.array.size(); ++i)
        {
            if (i != 0)
            {
                out.push_back(',');
            }
            dumpValue(value.array[i], out);
        }
        out.push_back(']');
        break;
    case Value::Type::object:
        out.push_back('{');
        for (std::size_t i = 0; i < value.object.size(); ++i)
        {
            if (i != 0)
            {
                out.push_back(',');
            }
            appendEscaped(out, value.object[i].first);
            out.push_back(':');
            dumpValue(value.object[i].second, out);
        }
        out.push_back('}');
        break;
    }
}

} // namespace detail

/**序列化成紧凑 JSON（`,"` 之间无空格）。返回的文本一定能被 `parse` 读回来。*/
inline std::string dump(const Value& value)
{
    std::string out;
    detail::dumpValue(value, out);
    return out;
}

} // namespace json
} // namespace golding
