#pragma once

#include <cstddef>
#include <deque>
#include <string>
#include <vector>

#include "Application/PluginLibrary.h"
#include "Application/PluginState.h"
#include "Application/Resolver.h"
#include "Application/PluginLoader.h"
#include "Application/PluginScanner.h"
#include "Tool/PathFinder.h"
#include "sdk/golding_plugin.h"

namespace golding
{

/**
 * @brief 宿主的启动编排：扫描 → 加载 → 档案 → 依赖解析 → entry → 选面板。
 *
 * 这是"开窗口 + 注册插件 + 处理依赖"里的**产品部分**，与 InkingUIKit 无关，
 * 因此可以无头自检。它不建窗口、不画界面——那些留给 main（依赖 InkingUIKit 单例
 * 与具体 UI 骨架，且会随面板插件变化）。
 *
 * 一次进程一个实例。`discover()` + `registerAll()` 走完阶段 1，之后：
 *   - `panel()` 拿到面板插件（可能为 nullptr，合法）；
 *   - 通过 `library()` / `resolved()` 读结果。
 */
class HostApp
{
public:
    /**一个插件加载完之后的记账。*/
    struct LoadedEntry
    {
        ScannedPlugin scanned;
        LoadedPlugin loaded;
        /**必须活到进程结束：插件会长期持有指针（骨架创建时靠它读页面表）。*/
        GoldingHostApi api{};
        std::string provider;
        bool entryOk = false;
        /**
         * 静态能力校验没过（`needs` 里有人没声明的能力）。
         *
         * 与状态分开存：档案里要显示成 `Unresolved(6)`，但那个状态原来是给
         * 依赖图用的，光看它分不清"依赖缺失"和"能力缺失"，所以这里再记一笔，
         * registerAll / notifyReady 都跳过它。
         */
        bool blocked = false;
    };

    HostApp() = default;
    ~HostApp();

    /**阶段 1a：定位插件来源。失败也返回，用 status() 判断。*/
    bool discover();

    /**阶段 1b：扫描 + 加载 dll + 建档 + 依赖解析。*/
    bool prepare();

    /**阶段 1c：按拓扑序逐个调 entry（注册组件/页面/服务/区域）。*/
    void registerAll();

    /**
     * @brief 阶段 2 之后：装配完了，按拓扑序调每个插件的 `ready`，返回**失败项总数**。
     *
     * 骨架（面板插件的 `create`）必须先建好并交给 `setUiRoot`，否则
     * "根铺满了吗"这类断言没有对象可看。
     */
    int notifyReady();

    /**装配完成的界面根（面板 `create` 的返回值）。`nullptr` = 界面层缺席。*/
    void setUiRoot(void* root);
    void* uiRoot() const;

    /**宿主的启动模式串（命令行那个）。原样交给插件，谁认谁解释。*/
    void setBootMode(std::string mode);
    const std::string& bootMode() const;

    /**选中的面板插件；没有（合法）返回 nullptr。*/
    const LoadedEntry* panel() const { return _panel; }

    /**依赖解析结果。*/
    const ResolveResult& resolved() const { return _resolved; }

    /**
     * @brief 进依赖图的插件个数（= 加载阶段过了、状态 Ready 的那些）。
     *
     * `entries().size() - nodeCount()` 就是"加载阶段就被否掉、根本没进图"的个数
     * （ABI/SDK 不匹配、manifest 坏、没有 dll…）。**别拿 entries() 的下标当
     * `resolved().order` 的下标用**——两者只在"一个都没被否掉"时恰好相等。
     */
    std::size_t nodeCount() const { return _nodes.size(); }

    /**拓扑序里的插件名。参数是 `resolved().order` / `unresolved` 里的**节点下标**。*/
    const std::string& providerOfNode(std::size_t nodeIndex) const;

    /**全部已加载插件（含坏掉的，它们也进了档案表）。*/
    const std::deque<LoadedEntry>& entries() const { return _loaded; }

    /**插件来源。*/
    const PluginsResult& plugins() const { return _plugins; }

    /**给插件填 HostApi 用的回调集。*/
    static void fillHostApi(GoldingHostApi& api, void* hostContext);

private:
    /**按名字加载一个 dll，把结果填进 entry，返回是否可装配（Pass 且 apiVersion 满足）。*/
    bool loadEntry(LoadedEntry& entry);

    /**静态校验 `needs`：没人声明提供 → 该插件降级（不装配），其余照常。*/
    void validateNeeds();

    /**
     * 交叉校验"声明了 `provides`"与"运行时真注册了"。
     *
     * 两个源必然有脱节风险，这正是当年 `plugin.json` 里那份 `abiVersion` 的死法
     * （8 个插件 6 个写着旧版本、无人发现）。成本极低：两次字符串比较。
     */
    void validateProvides();

    /**选面板插件：只认 role=="panel" 且装成功的，首个命中即内置优先，多个告警。*/
    void selectPanel();

    PluginsResult _plugins;
    std::deque<LoadedEntry> _loaded;
    std::vector<PluginNode> _nodes;
    std::vector<std::size_t> _nodeToLoaded;
    /**loaded 下标 → 档案下标，回灌状态用。*/
    std::vector<int> _libraryIndex;
    ResolveResult _resolved;
    const LoadedEntry* _panel = nullptr;
    /**`prepare` 只能成功一次。见实现处的注释：重复调用会静默留下半个旧世界。*/
    bool _prepared = false;
};

} // namespace golding
