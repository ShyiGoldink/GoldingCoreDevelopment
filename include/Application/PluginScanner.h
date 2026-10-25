#pragma once

#include "Application/CoreInternal.h"

#include <filesystem>
#include <string>
#include <vector>

#include "DataStruct/Manifest.h"
#include "DataStruct/PluginSource.h"

namespace golding
{

/**一个被扫到的插件：目录 + 读出来的 manifest + 读取状态 + 来源层级。*/
struct ScannedPlugin
{
    std::filesystem::path directory;
    Manifest manifest;
    ManifestStatus status = ManifestStatus::Error;
    /**读不出来时的原因（给人看的一句话）；`Pass` 时为空。见 `ManifestResult::detail`。*/
    std::string detail;
    /**来源层级。面板插件选择时按它优先，所以要一路带下去。*/
    PluginTier tier = PluginTier::External;
    /**
     * 同名插件已经在前面的来源里出现过，这一份不生效。
     *
     * 典型场景：用户往 `plugin/` 里塞了一个与内置插件同名的版本——内置赢，
     * 外部那份让位。仍然保留在结果里而不是悄悄丢掉，由调用方决定怎么报。
     */
    bool shadowed = false;
};

/**把插件目录读成清单。只做读取，不做依赖解析，也不加载 dll。*/
class PluginScanner
{
public:
    /**
     * 逐个来源读 plugin.json；读不出来的也保留在结果里，靠 status 区分。
     *
     * 输出顺序沿用来源顺序（Bundled 在前），同名者按先到先得标 `shadowed`，
     * 调用方不要重排。
     */
    static std::vector<ScannedPlugin> scan(const std::vector<PluginSource>& sources);
};

} // namespace golding
