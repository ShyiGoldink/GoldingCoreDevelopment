#pragma once

#include "Application/CoreInternal.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "DataStruct/PluginSource.h"

namespace golding
{

/**插件扫描状态。*/
enum class PluginsStatus
{
    Pass,      // 正常，找到插件
    NoPlugins, // 合法空
    Error,     // 扫描出错
};

/**插件来源扫描结果：目录列表 + 扫描状态。*/
struct PluginsResult
{
    /**
     * 有序，**Bundled 在前**。
     *
     * 这个顺序是"面板插件优先取内置"的唯一依据，由 PathFinder 保证，
     * 调用方不要自己再排一遍。
     */
    std::vector<PluginSource> sources;
    PluginsStatus status = PluginsStatus::NoPlugins;
};

/**
 * @brief
 *文件路径小工具
 *用户帮助找到正确的路径
 *例如exe路径，便于找到合适的路径 
 */
class PathFinder{
public:
    /**获取当前可执行文件的所在目录。*/
    std::filesystem::path executableDir() const;
    /**获取可执行文件旁的 plugin 目录。*/
    std::filesystem::path pluginDir() const;
    /**获取 plugin 目录下包含 plugin.json 的插件目录，按层级排好序。*/
    PluginsResult plugins() const;

    /**
     * @brief 把"插件内的相对路径"解析成**绝对路径**。`plugin.json` 里声明的文件
     *        （目前是 `targets.win64.lib`）全部走这一条。
     *
     * 存在的理由：`plugin.json` 里只写相对路径（作者不知道安装到哪个盘），
     * 而拼路径的基准目录只有扫描过的人知道。所以**解析归 core，消费者拿绝对路径**——
     * 插件自己不需要"我在哪"的通道，也不会各自拼出三种基准。
     *
     * 三条规则，缺一条就会出现"路径拼出来了但指到别处"：
     * 1. **绝对路径直接拒绝**——插件目录外的文件不属于插件；
     * 2. **逃出 `base` 的 `..` 拒绝**（`a/../b` 允许，它没逃出去）；
     * 3. 按词法归一，**不要求文件存在**——存在性是调用方的事（dll 可以缺，
     *    配置缺了要报错，两者判定不同）。
     *
     * @return 绝对路径；非法输入返回空 path（`empty()` 为真）。**空 = 声明错误**，
     *         调用方不能当成"没配"。
     */
    static std::filesystem::path resolve(const std::filesystem::path& base, std::string_view relative);

    /**
     * @brief 路径 → UTF-8 窄串。
     *
     * 别用 `path.string()`：MinGW 下它按 "C" locale 解码，中文路径抛
     * `Illegal byte sequence`，而这个仓库的安装路径里中文并不罕见。
     */
    static std::string toUtf8(const std::filesystem::path& path);

    /**UTF-8 窄串 → 路径。与 `toUtf8` 成对；别用 `path(std::string)`，那是 ANSI。*/
    static std::filesystem::path fromUtf8(std::string_view text);
};

} // namespace golding
