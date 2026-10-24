#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "Application/RegistryStats.h"

namespace golding
{

/**
 * @brief 同 order 时的先后规则。
 *
 * 两个值各对应一种**已经存在的**行为，不是新发明：
 *   - 页 / 设置：同 order 按 id 字典序 → 结果与注册先后无关，谁先加载都一样；
 *   - 区域条目：同 order 先到先得 → 面板按注册顺序摆，重排会让用户眼前跳一下。
 */
enum class SlotTieBreak
{
    ById,
    ByArrival,
};

/**
 * @brief 一张"id 唯一、按 order 排好、带提供方"的槽位表。
 *
 * 页表、设置表、某个区域里的条目表，三者是**同一个东西**：id 唯一、按 order 排、
 * 要能回答"这是谁注册的"、同名驳回、清空。此前是三份各写一遍的实现——排序规则
 * 抄两遍、去重抄三遍，任何一条规则改动要记得改三处（`docs/Options.md`）。
 * 收成一个模板之后，三处只是它的三个实例，规则只剩一份。
 *
 * 它**不是**一张通用容器，故意不做的事：
 *   - 不负责分组：区域之间怎么排、哪些区域存在，仍由 `RegionRegistry` 管；
 *   - 不负责统计的生命周期：`RegistryStats` 由调用方持有并传进来。这样区域表能
 *     把"区域级驳回"和"条目级驳回"记进同一份计数，而不是两张各记一半。
 *
 * `TDecl` 只要具备 `id` / `order` 两个字段（`create` 是否必需由 `Rules` 决定）。
 */
template <typename TDecl>
class SlotTable
{
public:
    enum class Result
    {
        Ok,
        IdTaken,
        Invalid,
    };

    /**一张表的规则。同一张表在整个进程里必须用同一份规则，中途换会让已排好的顺序失效。*/
    struct Rules
    {
        SlotTieBreak tieBreak = SlotTieBreak::ById;
        /**是否要求 `create` 非空：页 / 设置没有 create 就画不出来；区域条目可以只有动作。*/
        bool requireCreate = true;
        /**是否允许重名（区域的 `allowDuplicateIds`）。*/
        bool allowDuplicateIds = false;
        /**
         * 驳回原因的前缀，拼在人话前面（`"区域 menubar 内"`）。留空表示"这张表就是全局的"。
         * 有它，区域条目的驳回才能说清是**哪个区域里**重名——否则一堆 `同 id 已存在：file`
         * 根本定位不到地方。
         */
        std::string scope;
    };

    Result insert(const TDecl* decl, const std::string& provider, const Rules& rules, RegistryStats& stats)
    {
        if (decl == nullptr || decl->id == nullptr || decl->id[0] == '\0' || (rules.requireCreate && decl->create == nullptr))
        {
            stats.reject(rules.scope + "登记项指针 / id 为空，或缺少必需的 create");
            return Result::Invalid;
        }

        const std::string id(decl->id);
        if (!rules.allowDuplicateIds)
        {
            for (const Row& row : _rows)
            {
                if (row.id == id)
                {
                    stats.reject(rules.scope + "同 id 已存在：" + id);
                    return Result::IdTaken;
                }
            }
        }

        // 插到"第一个应当排在它后面"的位置，整个 vector 始终保持有序。
        // 同 order 时：ById 继续比 id；ByArrival 一律排在已有项之后。
        const auto after = [&rules](const Row& row, const TDecl* value) {
            if (row.decl->order != value->order)
            {
                return row.decl->order < value->order ? false : true;
            }
            if (rules.tieBreak == SlotTieBreak::ById)
            {
                return row.id > std::string(value->id);
            }
            return false;
        };
        auto position = _rows.begin();
        while (position != _rows.end() && !after(*position, decl))
        {
            ++position;
        }

        _rows.insert(position, Row{decl, id, provider});
        stats.accept();
        return Result::Ok;
    }

    const TDecl* find(const std::string& id) const
    {
        for (const Row& row : _rows)
        {
            if (row.id == id)
            {
                return row.decl;
            }
        }
        return nullptr;
    }

    /**问"这一项是谁注册的"；没有返回空串。*/
    const std::string& providerOf(const std::string& id) const
    {
        static const std::string empty;
        for (const Row& row : _rows)
        {
            if (row.id == id)
            {
                return row.provider;
            }
        }
        return empty;
    }

    /**已按 order 排好的全表。*/
    std::vector<const TDecl*> all() const
    {
        std::vector<const TDecl*> decls;
        decls.reserve(_rows.size());
        for (const Row& row : _rows)
        {
            decls.push_back(row.decl);
        }
        return decls;
    }

    /**第 index 项（已排序）；越界返回 nullptr。*/
    const TDecl* at(std::size_t index) const
    {
        return index < _rows.size() ? _rows[index].decl : nullptr;
    }

    std::size_t size() const noexcept { return _rows.size(); }

    void clear() { _rows.clear(); }

private:
    struct Row
    {
        const TDecl* decl = nullptr;
        std::string id;
        std::string provider;
    };

    std::vector<Row> _rows;

};

} // namespace golding
