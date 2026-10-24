#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

#include "Application/ManifestLoader.h"
#include "Application/Json.h"
#include "Tool/PathFinder.h"

namespace golding
{

namespace
{

namespace json = detail::json;

bool ReadFile(const std::filesystem::path& path, std::string& out)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    if (file.bad())
    {
        return false;
    }
    out = buffer.str();
    return true;
}

/**去掉 UTF-8 BOM：Windows 记事本另存的文件常带这个。*/
void StripBom(std::string& text)
{
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
    {
        text.erase(0, 3);
    }
}

bool ReadString(const json::Value& object, const char* key, std::string& out)
{
    const json::Value* value = object.find(key);
    if (value == nullptr || value->type != json::Value::Type::string)
    {
        return false;
    }
    out = value->text;
    return true;
}

/**
 * 读一个整数字段，允许写成数字或数字字符串（json 里写 4 和 "4" 都收）。
 * 必须落在 int 范围内的整数值，否则返回 false。
 */
bool ReadIntField(const json::Value& object, const char* key, int& out)
{
    const json::Value* value = object.find(key);
    if (value == nullptr)
    {
        return false;
    }

    double raw = 0.0;
    if (value->type == json::Value::Type::number)
    {
        raw = value->number;
    }
    else if (value->type == json::Value::Type::string && !value->text.empty())
    {
        try
        {
            std::size_t consumed = 0;
            raw = std::stod(value->text, &consumed);
            if (consumed != value->text.size())
            {
                return false;
            }
        }
        catch (...)
        {
            return false;
        }
    }
    else
    {
        return false;
    }

    if (raw < 0.0 || raw > 2147483647.0 || static_cast<double>(static_cast<int>(raw)) != raw)
    {
        return false;
    }
    out = static_cast<int>(raw);
    return true;
}

/**
 * 读一个能力表：`{ "golding.overlay": 1 }`。值是版本号，允许数字或数字字符串。
 * 字段缺省时返回 true（两个字段都是可选的）。
 */
bool ReadCapabilityMap(const json::Value& root, const char* key, std::vector<ManifestCapability>& out,
                       std::string& why)
{
    const json::Value* node = root.find(key);
    if (node == nullptr)
    {
        return true;
    }
    if (node->type != json::Value::Type::object)
    {
        why = "必须是对象（能力名 → 版本）";
        return false;
    }

    for (const auto& member : node->object)
    {
        if (member.first.empty())
        {
            why = "里有空能力名";
            return false;
        }

        const json::Value& value = member.second;
        double raw = 0.0;
        if (value.type == json::Value::Type::number)
        {
            raw = value.number;
        }
        else if (value.type == json::Value::Type::string && !value.text.empty())
        {
            try
            {
                std::size_t consumed = 0;
                raw = std::stod(value.text, &consumed);
                if (consumed != value.text.size())
                {
                    why = "里 " + member.first + " 的版本不是整数";
                    return false;
                }
            }
            catch (...)
            {
                why = "里 " + member.first + " 的版本不是整数";
                return false;
            }
        }
        else
        {
            why = "里 " + member.first + " 的版本不是整数";
            return false;
        }

        if (raw < 0.0 || raw > 2147483647.0 || static_cast<double>(static_cast<unsigned int>(raw)) != raw)
        {
            why = "里 " + member.first + " 的版本超出范围";
            return false;
        }
        out.push_back(ManifestCapability{member.first, static_cast<unsigned int>(raw)});
    }
    return true;
}

} // namespace

ManifestResult ManifestLoader::read(const std::filesystem::path& pluginDir)
{
    ManifestResult result;

    std::string text;
    if (!ReadFile(pluginDir / "plugin.json", text))
    {
        result.status = ManifestStatus::Error;
        result.detail = "读不到 plugin.json（不存在 / 无权限 / 读取中断）";
        return result;
    }
    StripBom(text);

    json::Value root;
    if (!json::parse(text, root) || root.type != json::Value::Type::object)
    {
        result.status = ManifestStatus::Invalid;
        result.detail = "不是合法的 JSON 对象";
        return result;
    }

    // 驳回时就地带上原因：日志里只说 `status=1` 的话，作者得逐个字段猜。
    auto reject = [&result](std::string detail) {
        result.status = ManifestStatus::Invalid;
        result.detail = std::move(detail);
        return result;
    };

    // ---- 顶层键白名单 ----
    //
    // 这条与"abiVersion 只认 dll"同源：同一份事实写两处会烂，而**写了不生效**
    // 比写错更坏——它让人以为配置生效了。所以不收的键一律驳回并点名。
    static const char* const kKnownKeys[] = {"name", "title", "version", "apiVersion",
                                             "role", "requires", "provides", "needs", "targets"};
    for (const auto& entry : root.object)
    {
        bool known = false;
        for (const char* key : kKnownKeys)
        {
            if (entry.first == key)
            {
                known = true;
                break;
            }
        }
        if (!known)
        {
            return reject("未知字段 " + entry.first);
        }
    }

    Manifest& manifest = result.manifest;

    // ---- 必需字段 ----
    // abiVersion 不在这里：它只认 dll 里 descriptor 的那一份，见 Manifest.h 的说明。
    if (!ReadString(root, "name", manifest.name) || manifest.name.empty())
    {
        return reject("缺 name（或为空 / 不是字符串）");
    }
    if (!ReadString(root, "title", manifest.title))
    {
        return reject("缺 title（或不是字符串）");
    }
    if (!ReadString(root, "version", manifest.version))
    {
        return reject("缺 version（或不是字符串）");
    }
    if (!ReadIntField(root, "apiVersion", manifest.apiVersion))
    {
        return reject("缺 apiVersion（或不是 0 与 2147483647 之间的整数）");
    }

    // ---- 可选字段：role（panel = 提供界面骨架的插件）----
    if (const json::Value* role = root.find("role"))
    {
        if (role->type != json::Value::Type::string)
        {
            return reject("role 不是字符串");
        }
        manifest.role = role->text;
    }

    const json::Value* requiresNode = root.find("requires");
    if (requiresNode == nullptr || requiresNode->type != json::Value::Type::object)
    {
        return reject("缺 requires 对象");
    }
    const json::Value* requiredPlugins = requiresNode->find("plugins");
    if (requiredPlugins == nullptr || requiredPlugins->type != json::Value::Type::array)
    {
        return reject("缺 requires.plugins 数组");
    }
    for (const auto& item : requiredPlugins->array)
    {
        if (item.type != json::Value::Type::string)
        {
            return reject("requires.plugins 里有非字符串项");
        }
        manifest.dependencies.push_back(item.text);
    }

    // ---- 可选字段：能力声明（provides / needs）----
    //
    // 放在 `plugin.json` 而不是 descriptor：**扫描阶段就要能建"能力 → 提供方 + 版本"
    // 的表**，否则 `needs` 的最小版本在装配之前没有可比的对象（descriptor 要等
    // dll 加载完才读得到）。扫描之后、entry 之前，拓扑和版本表都是齐的。
    {
        std::string why;
        if (!ReadCapabilityMap(root, "provides", manifest.provides, why))
        {
            return reject("provides " + why);
        }
        if (!ReadCapabilityMap(root, "needs", manifest.needs, why))
        {
            return reject("needs " + why);
        }
    }

    // ---- 必需字段：targets.win64.lib ----
    //
    // 曾经把它当可选，留着"无 dll 的纯数据插件"这条路。那条路在 T-11 删掉
    // `widgets{}` 之后就没有载体了：没有 dll 的插件进不了依赖图、不能声明能力、
    // 也不会被调 entry，唯一效果是在档案表里多一行。所以现在**缺 dll 是配置错误**，
    // 不是另一种插件形态（`docs/Options.md`）。
    const json::Value* targets = root.find("targets");
    if (targets == nullptr || targets->type != json::Value::Type::object)
    {
        return reject("缺 targets 对象（插件必须声明 targets.win64.lib）");
    }
    const json::Value* win64 = targets->find("win64");
    if (win64 == nullptr || win64->type != json::Value::Type::object)
    {
        return reject("缺 targets.win64 对象（本宿主只支持 win64）");
    }
    const json::Value* lib = win64->find("lib");
    if (lib == nullptr || lib->type != json::Value::Type::string || lib->text.empty())
    {
        return reject("缺 targets.win64.lib（或为空 / 不是字符串）");
    }
    manifest.dllPath = lib->text;

    // dll 路径当场解析：`HostApp` 之后按绝对路径直接 open，不再自己拼。
    // 绝对路径或 `..` 逃出去都判声明错误，不当"没配"。
    {
        const std::filesystem::path resolved = PathFinder::resolve(pluginDir, manifest.dllPath);
        if (resolved.empty())
        {
            return reject("targets.win64.lib 是绝对路径或逃出了插件目录：" + manifest.dllPath);
        }
        manifest.dllPath = PathFinder::toUtf8(resolved);
    }

    result.status = ManifestStatus::Pass;
    return result;
}

} // namespace golding
