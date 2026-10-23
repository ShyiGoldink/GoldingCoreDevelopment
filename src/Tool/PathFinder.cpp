#include <algorithm>
#include <system_error>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif // 根据不同的系统引入不同的文件

#include "Tool/PathFinder.h"

namespace golding
{

namespace
{

/**
 * 收一个目录下的直接子目录（要求含 plugin.json）。
 *
 * 目录不存在不算错误——`plugin/core/` 允许整个缺省（用户可能就不装内置那一套）。
 * 读不动才算：`hardError` 置位，调用方据此整体失败，不交半成品。
 */
void collect(const std::filesystem::path& dir, PluginTier tier, std::vector<PluginSource>& out, bool& hardError)
{
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec))
    {
        if (ec)
        {
            hardError = true;
        }
        return;
    }
    if (!std::filesystem::is_directory(dir, ec))
    {
        if (ec)
        {
            hardError = true;
        }
        return;
    }

    std::filesystem::directory_iterator it(dir, ec);
    if (ec)
    {
        hardError = true;
        return;
    }

    std::vector<PluginSource> found;
    const std::filesystem::directory_iterator end;
    while (it != end)
    {
        const std::filesystem::directory_entry& entry = *it;

        // 单个子目录读不了就跳过它自己，不影响其余插件。
        std::error_code entryEc;
        if (entry.is_directory(entryEc) && !entryEc &&
            std::filesystem::is_regular_file(entry.path() / "plugin.json", entryEc) && !entryEc)
        {
            found.push_back(PluginSource{entry.path(), tier});
        }

        it.increment(ec);
        if (ec)
        {
            hardError = true; // 列表已不完整
            return;
        }
    }

    // 层内按路径排序：面板插件"同层取首个"因此是确定的，不是任意顺序。
    std::sort(found.begin(), found.end(),
              [](const PluginSource& left, const PluginSource& right) { return left.directory < right.directory; });
    out.insert(out.end(), found.begin(), found.end());
}

} // namespace

std::filesystem::path PathFinder::executableDir() const
{
    std::filesystem::path dir;
#ifdef _WIN32
    std::vector<wchar_t> buffer(MAX_PATH);
    while (true)
    {
        const DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (size == 0)
        {
            break;
        }
        if (size < buffer.size())
        {
            dir = std::filesystem::path(std::wstring(buffer.data(), size)).parent_path();
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);

    if (size > 0)
    {
        std::vector<char> buffer(size);
        if (_NSGetExecutablePath(buffer.data(), &size) == 0)
        {
            dir = std::filesystem::path(buffer.data()).parent_path();
        }
    }
#else
    std::vector<char> buffer(4096);
    const ssize_t size = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (size > 0)
    {
        buffer[static_cast<std::size_t>(size)] = '\0';
        dir = std::filesystem::path(buffer.data()).parent_path();
    }
#endif

    if (dir.empty())
    {
        return {};
    }
    std::error_code ec;
    auto canon = std::filesystem::weakly_canonical(dir, ec);
    return ec ? dir : canon;
}

std::filesystem::path PathFinder::pluginDir() const
{
    const auto dir = executableDir();
    return dir.empty() ? std::filesystem::path{} : dir / "plugin";
}

PluginsResult PathFinder::plugins() const
{
    PluginsResult result;
    const std::filesystem::path dir = pluginDir();

    // 定位不到 plugin 目录，无从扫描。
    if (dir.empty())
    {
        result.status = PluginsStatus::Error;
        return result;
    }

    bool hardError = false;

    // 顺序即优先级：内置在前，外部在后。面板插件取第一个可用的，也就取到了内置的。
    // 注：扫 dir 时 dir/core 是个没有 plugin.json 的目录，会被自然跳过。
    collect(dir / "core", PluginTier::Bundled, result.sources, hardError);
    collect(dir, PluginTier::External, result.sources, hardError);

    if (hardError)
    {
        // 列表已不完整，不交半成品。
        result.sources.clear();
        result.status = PluginsStatus::Error;
        return result;
    }

    result.status = result.sources.empty() ? PluginsStatus::NoPlugins : PluginsStatus::Pass;
    return result;
}

std::filesystem::path PathFinder::resolve(const std::filesystem::path& base, std::string_view relative)
{
    if (base.empty() || relative.empty())
    {
        return {};
    }

    const std::filesystem::path rel{std::string(relative)};

    // 规则 1：绝对路径拒绝。`C:foo` 这种"有盘符没根"的也一起拒——它是相对当前盘的
    // 相对路径，基准不可控，等同于没写基准。
    if (rel.is_absolute() || rel.has_root_name() || rel.has_root_directory())
    {
        return {};
    }

    std::error_code ec;
    const std::filesystem::path baseAbs = std::filesystem::absolute(base, ec).lexically_normal();
    if (ec)
    {
        return {};
    }
    const std::filesystem::path combined = (baseAbs / rel).lexically_normal();

    // 规则 2：归一之后必须仍在 base 之下。逐段比，不拼字符串——
    // 拼字符串会把 `.../plugin/home2` 误判成在 `.../plugin/home` 之下。
    auto basePart = baseAbs.begin();
    auto combinedPart = combined.begin();
    for (; basePart != baseAbs.end(); ++basePart, ++combinedPart)
    {
        if (combinedPart == combined.end() || *combinedPart != *basePart)
        {
            return {};
        }
    }

    // 规则 3：不查存在性，直接把词法归一的结果交出去。
    return combined;
}

std::string PathFinder::toUtf8(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

std::filesystem::path PathFinder::fromUtf8(std::string_view text)
{
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

} // namespace golding
