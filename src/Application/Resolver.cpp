#include <algorithm>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Application/Resolver.h"

namespace golding
{

namespace
{

bool contains(const std::vector<std::string>& list, const std::string& value)
{
    return std::find(list.begin(), list.end(), value) != list.end();
}

/**一个节点的判定结果。还没判的就是 `decided == false`。*/
struct Verdict
{
    bool decided = false;
    ResolveReason reason = ResolveReason::MissingDependency;
    std::vector<std::string> missing;
};

using IndexByName = std::unordered_map<std::string, std::size_t>;

/**
 * 从 `current` 出发（只走尚未进 order 的节点），能否回到 `start`。
 * 能回到就说明 `start` 自己在环上。节点数是个位数，这个朴素 DFS 足够。
 */
bool canReturnTo(std::size_t start, std::size_t current, const std::vector<PluginNode>& nodes,
                 const IndexByName& indexByName, const std::vector<char>& inOrder, std::vector<char>& visited)
{
    for (const auto& dependency : nodes[current].depends)
    {
        const auto found = indexByName.find(dependency);
        if (found == indexByName.end())
        {
            continue; // 名字找不到的边已在缺失检测里处理过
        }
        const std::size_t next = found->second;
        if (next == start)
        {
            return true;
        }
        if (inOrder[next] != 0 || visited[next] != 0)
        {
            continue;
        }
        visited[next] = 1;
        if (canReturnTo(start, next, nodes, indexByName, inOrder, visited))
        {
            return true;
        }
    }
    return false;
}

} // namespace

ResolveResult Resolver::resolve(const std::vector<PluginNode>& nodes)
{
    ResolveResult result;

    // 名字 → 下标。同名只认第一个（扫描阶段已按目录去重，这里只是兜底）。
    IndexByName indexByName;
    for (std::size_t index = 0; index < nodes.size(); ++index)
    {
        indexByName.emplace(nodes[index].name, index);
    }

    std::vector<Verdict> verdicts(nodes.size());

    // ---- 第一步：缺失检测。依赖名找不到的直接判死，不参与拓扑 ----
    for (std::size_t index = 0; index < nodes.size(); ++index)
    {
        for (const auto& dependency : nodes[index].depends)
        {
            if (dependency == nodes[index].name)
            {
                continue; // 自己依赖自己，忽略（防呆）
            }
            if (indexByName.find(dependency) != indexByName.end())
            {
                continue;
            }

            Verdict& verdict = verdicts[index];
            verdict.decided = true;
            verdict.reason = ResolveReason::MissingDependency;
            if (!contains(verdict.missing, dependency))
            {
                verdict.missing.push_back(dependency);
            }
        }
    }

    // ---- 第二步：在"没判死"的节点上跑 Kahn ----
    std::vector<std::size_t> indegree(nodes.size(), 0);
    std::vector<std::vector<std::size_t>> dependents(nodes.size());

    for (std::size_t index = 0; index < nodes.size(); ++index)
    {
        if (verdicts[index].decided)
        {
            continue; // 缺依赖的节点不贡献出边；别人指向它的边照建（见下）
        }
        for (const auto& dependency : nodes[index].depends)
        {
            const auto found = indexByName.find(dependency);
            if (found == indexByName.end())
            {
                continue;
            }
            const std::size_t provider = found->second;
            if (provider == index)
            {
                continue;
            }
            ++indegree[index];
            // 提供方可能已判死：那样这条边永远消不掉，依赖它的节点自然进不了 order。
            dependents[provider].push_back(index);
        }
    }

    // ready 只进 FIFO，用读指针推进，避免 erase(begin()) 的搬移
    std::vector<std::size_t> ready;
    ready.reserve(nodes.size());
    for (std::size_t index = 0; index < nodes.size(); ++index)
    {
        if (!verdicts[index].decided && indegree[index] == 0)
        {
            ready.push_back(index);
        }
    }

    std::vector<char> inOrder(nodes.size(), 0);
    for (std::size_t head = 0; head < ready.size(); ++head)
    {
        const std::size_t current = ready[head];
        result.order.push_back(current);
        inOrder[current] = 1;

        for (const std::size_t dependent : dependents[current])
        {
            if (indegree[dependent] > 0)
            {
                --indegree[dependent];
                if (indegree[dependent] == 0)
                {
                    ready.push_back(dependent);
                }
            }
        }
    }

    // ---- 第三步：没进 order 又没判死的，区分"在环上"与"被环连累" ----
    for (std::size_t index = 0; index < nodes.size(); ++index)
    {
        if (verdicts[index].decided || inOrder[index] != 0)
        {
            continue;
        }

        std::vector<char> visited(nodes.size(), 0);
        visited[index] = 1;
        const bool onCycle = canReturnTo(index, index, nodes, indexByName, inOrder, visited);

        verdicts[index].decided = true;
        verdicts[index].reason = onCycle ? ResolveReason::InCycle : ResolveReason::DependencyUnavailable;
    }

    // ---- 汇总：按输入顺序输出，调用方不必再排序 ----
    for (std::size_t index = 0; index < nodes.size(); ++index)
    {
        if (!verdicts[index].decided)
        {
            continue;
        }
        UnresolvedNode node;
        node.index = index;
        node.reason = verdicts[index].reason;
        node.missing = std::move(verdicts[index].missing);
        result.unresolved.push_back(std::move(node));
    }

    result.status = result.unresolved.empty() ? ResolveStatus::Pass : ResolveStatus::Partial;
    return result;
}

} // namespace golding
