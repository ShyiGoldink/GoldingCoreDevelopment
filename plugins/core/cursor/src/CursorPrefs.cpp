// 指针偏好的实现：TSV 解析 + 文件读写。
//
// 见 `CursorPrefs.h` 顶上的格式说明——那里的约定是**契约**，这里是它的一份实现。
// 路径编码一律走 `UserCursors.h` 的 `ToUtf8` / `FromUtf8`（`path.string()` 在
// MinGW 的中文目录下会抛，见 `docs/NOTES.md`）。

#include "CursorPrefs.h"

#include "UserCursors.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

namespace cursor
{

namespace
{

/**字段切分：TAB 分隔（空字段保留，靠下标取）。*/
std::vector<std::string> SplitTabs(const std::string& line)
{
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (true)
    {
        const std::size_t tab = line.find('\t', start);
        if (tab == std::string::npos)
        {
            fields.push_back(line.substr(start));
            return fields;
        }
        fields.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
}

/**去首尾空白（用户手写进来的是 `style = cross` 这种也认）。*/
std::string Trim(const std::string& text)
{
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
    {
        return {};
    }
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

/**`1` / `0` / `true` / `on`（大小写不敏感）；认不出来返回 `fallback`。*/
bool ParseBool(const std::string& text, bool fallback)
{
    const std::string value = Trim(text);
    if (value == "1" || value == "true" || value == "TRUE" || value == "on")
    {
        return true;
    }
    if (value == "0" || value == "false" || value == "FALSE" || value == "off")
    {
        return false;
    }
    return fallback;
}

float ParseScale(const std::string& text, float fallback)
{
    const std::string value = Trim(text);
    if (value.empty())
    {
        return fallback;
    }
    char* end = nullptr;
    const float parsed = std::strtof(value.c_str(), &end);
    if (end == value.c_str() || *end != '\0' || !std::isfinite(parsed))
    {
        return fallback;
    }
    // 与 `cursorSetScale` 同一个范围：文件里写 99 也不该把指针放大到看不见。
    return std::clamp(parsed, 0.5f, 3.0f);
}

/**scale 的写法：`1` 而不是 `1.000000`（人要看）。*/
std::string FormatScale(float scale)
{
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "%.4g", static_cast<double>(scale));
    return buffer;
}

} // namespace

// ---------------------------------------------------------------------------
// 文本 ↔ 偏好
// ---------------------------------------------------------------------------

std::string FormatPrefs(const CursorPrefs& prefs)
{
    std::ostringstream stream;
    stream << "# cursor.ini —— 指针插件自己的偏好（每行：键 <TAB> 值）\n";
    stream << "# style 是内置图案 id（arrow / ring / cross / dot / spark / quill）\n";
    stream << "# 或用户指针 id（user:<文件名去扩展名>）；下面这几行删掉 = 回到出厂状态。\n";
    if (!prefs.styleId.empty())
    {
        stream << "style\t" << prefs.styleId << '\n';
    }
    stream << "enabled\t" << (prefs.enabled ? 1 : 0) << '\n';
    stream << "effects\t" << (prefs.effects ? 1 : 0) << '\n';
    stream << "scale\t" << FormatScale(prefs.scale) << '\n';
    return stream.str();
}

CursorPrefs ParsePrefs(const std::string& text)
{
    CursorPrefs prefs;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back(); // CRLF：记事本保存过的文件就是这样
        }
        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#')
        {
            continue;
        }
        // 键值之间优先认 TAB；手写成 `key = value` 也认（用户有编辑权）。
        std::vector<std::string> fields = SplitTabs(trimmed);
        std::string key = Trim(fields[0]);
        std::string value;
        if (fields.size() > 1)
        {
            value = Trim(fields[1]);
        }
        else
        {
            const std::size_t equals = key.find('=');
            if (equals == std::string::npos)
            {
                continue; // 既没 TAB 也没 `=`：坏行
            }
            value = Trim(key.substr(equals + 1));
            key = Trim(key.substr(0, equals));
        }

        if (key == "style")
        {
            prefs.styleId = value;
        }
        else if (key == "enabled")
        {
            prefs.enabled = ParseBool(value, prefs.enabled);
        }
        else if (key == "effects")
        {
            prefs.effects = ParseBool(value, prefs.effects);
        }
        else if (key == "scale")
        {
            prefs.scale = ParseScale(value, prefs.scale);
        }
        // 其余键：可能是更新版本加的，忽略而不是报错。
    }
    return prefs;
}

// ---------------------------------------------------------------------------
// 文件
// ---------------------------------------------------------------------------

std::filesystem::path CursorPrefsStore::DefaultFile()
{
    // 与 `cursors/` 同一个根：`.../GoldingCore/cursor.ini`。
    // 这样"用户指针目录"和"当前选了哪一只"是邻居，用户打开文件夹就看到两者。
    return UserCursorStore::DefaultDirectory().parent_path() / FromUtf8(kPrefsFileName);
}

std::string CursorPrefsStore::pathText() const
{
    return ToUtf8(_file);
}

bool CursorPrefsStore::Load(CursorPrefs* prefs, std::string* error)
{
    if (prefs == nullptr)
    {
        return false;
    }
    *prefs = CursorPrefs{};

    std::error_code code;
    if (!std::filesystem::exists(_file, code))
    {
        return true; // 还没挑过：默认偏好就是正确答案，不是错误
    }

    std::ifstream stream(_file, std::ios::binary);
    if (!stream)
    {
        if (error != nullptr)
        {
            *error = "读不出偏好文件：" + pathText();
        }
        return false;
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    *prefs = ParsePrefs(buffer.str());
    return true;
}

bool CursorPrefsStore::Save(const CursorPrefs& prefs, std::string* error)
{
    std::error_code code;
    const std::filesystem::path directory = _file.parent_path();
    if (!directory.empty() && !std::filesystem::is_directory(directory, code))
    {
        std::filesystem::create_directories(directory, code);
        if (code)
        {
            if (error != nullptr)
            {
                *error = "建不出偏好目录：" + ToUtf8(directory) + "（" + code.message() + "）";
            }
            return false;
        }
    }

    std::ofstream stream(_file, std::ios::binary | std::ios::trunc);
    if (!stream)
    {
        if (error != nullptr)
        {
            *error = "写不进偏好文件：" + pathText();
        }
        return false;
    }
    stream << FormatPrefs(prefs);
    if (!stream)
    {
        if (error != nullptr)
        {
            *error = "写偏好文件时中断：" + pathText();
        }
        return false;
    }
    return true;
}

} // namespace cursor
