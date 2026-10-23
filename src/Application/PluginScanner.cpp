#include <filesystem>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Application/ManifestLoader.h"
#include "Application/PluginScanner.h"

namespace golding
{

std::vector<ScannedPlugin> PluginScanner::scan(const std::vector<PluginSource>& sources)
{
    std::vector<ScannedPlugin> plugins;
    plugins.reserve(sources.size());

    // 名字是插件的唯一键。先到先得，所以来源顺序（Bundled 在前）就是优先级。
    std::unordered_set<std::string> seen;

    for (const auto& source : sources)
    {
        const ManifestResult result = ManifestLoader::read(source.directory);
        ScannedPlugin plugin;
        plugin.directory = source.directory;
        plugin.manifest = result.manifest;
        plugin.status = result.status;
        plugin.detail = result.detail;
        plugin.tier = source.tier;

        // 只有读得出来的才有可靠的名字可判重。
        if (plugin.status == ManifestStatus::Pass && !plugin.manifest.name.empty())
        {
            if (!seen.insert(plugin.manifest.name).second)
            {
                plugin.shadowed = true;
            }
        }

        plugins.push_back(std::move(plugin));
    }

    return plugins;
}

} // namespace golding
