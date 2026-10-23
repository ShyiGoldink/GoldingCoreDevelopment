#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace golding
{

/**解析用的最小输入：名字 + 依赖名。*/
struct PluginNode
{
    std::string name;
    std::vector<std::string> depends;
};

/**
 * 整体结论。只有两态——因为"缺依赖"和"成环"的**处置完全一样**：
 * 那个插件标不可用，其余照常装。区别在诊断信息里，不在处置上。
 */
enum class ResolveStatus
{
    Pass,    // 全部插件都能装
    Partial, // 有插件装不上，其余可装
};

/**一个插件为什么装不上。*/
enum class ResolveReason
{
    MissingDependency,     // 直接依赖的名字找不到
    InCycle,               // 自己在依赖环上 —— 根因
    DependencyUnavailable, // 自己不在环上，但依赖了装不上的插件 —— 派生
};

/**一个装不上的插件。*/
struct UnresolvedNode
{
    std::size_t index = 0;
    ResolveReason reason = ResolveReason::MissingDependency;
    /**reason == MissingDependency 时有值：缺的那些依赖名，按声明顺序，已去重。*/
    std::vector<std::string> missing;
};

struct ResolveResult
{
    ResolveStatus status = ResolveStatus::Pass;
    /**装配顺序：依赖在前，同层保持输入顺序，**只含能装的插件**。*/
    std::vector<std::size_t> order;
    /**装不上的插件，按输入顺序。派生原因可顺着被依赖者往上查到根因。*/
    std::vector<UnresolvedNode> unresolved;
};

/**
 * @brief 依赖解析：缺失检测 + 拓扑排序 + 环检测。
 *
 * 纯函数，不碰文件系统、不加载 dll——所以可以无头自检。
 *
 * **`order` 只含能装的插件**：调用方直接按它遍历即可，不必再判断可用性。
 * 装不上的进 `unresolved`，处置交给调用方（当前策略：标记不可用，其余照常——
 * "一个插件坏了只影响它自己"）。
 */
class Resolver
{
public:
    static ResolveResult resolve(const std::vector<PluginNode>& nodes);
};

} // namespace golding
