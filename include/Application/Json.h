#pragma once

// 极简 JSON 读取器：服务于 plugin.json 与**核心插件**的构件配置串。
//
// 它从 src/ 挪到了 include/（T-09）：核心插件（`plugins/core/widgets`）要解析构件配置，
// 与其让每个插件自带一份手写解析（同一份 JSON 三种读法），不如共用这一份。
// API 刻意保持**极窄**（就一个 `parse` + `Value::find`），以后换 nlohmann/json 时
// 只改这一层，调用方一个字都不用动。
//
// 支持：object / array / string（含 \uXXXX 与代理对）/ number / true / false / null。
// 不支持：注释、尾逗号、NaN / Infinity、深度超过 kMaxDepth 的嵌套。

#include <string>
#include <utility>
#include <vector>

namespace golding
{
namespace detail
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
    const Value* find(const std::string& key) const;
};

/**解析整段文本；失败返回 false，out 内容不可信。*/
bool parse(const std::string& text, Value& out);

} // namespace json
} // namespace detail
} // namespace golding
