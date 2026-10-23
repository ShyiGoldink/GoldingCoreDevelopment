#pragma once

#include <filesystem>
#include <vector>

namespace golding
{

/**
 * @brief 插件来源层级。
 *
 * `Bundled` 是随宿主一起分发的那一套（`<exe>/plugin/core/<name>/`），
 * `External` 是用户或第三方后来装进来的（`<exe>/plugin/<name>/`）。
 *
 * 分层的唯一用途是**优先级**：面板插件优先从 `Bundled` 里找。
 * 换一套 UI = 换掉 `plugin/core/` 里的面板插件，宿主 exe 一个字节不动。
 */
enum class PluginTier
{
    Bundled,
    External,
};

/**一个插件来源目录。*/
struct PluginSource
{
    std::filesystem::path directory;
    PluginTier tier = PluginTier::External;
};

} // namespace golding
