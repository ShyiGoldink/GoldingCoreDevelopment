#pragma once

#include "Application/CoreInternal.h"

#include <cstddef>
#include <string>
#include <vector>

#include "Application/RegistryStats.h"
#include "sdk/golding_plugin.h"

namespace golding
{

/**
 * @brief 区域表：面板插件声明"有哪些地方可以注册"，其它插件往里放条目。
 *
 * 这是"换面板插件 = 换一套 UI"的落点。core 只负责存表、查表、同名驳回，
 * **不认识任何具体区域名**——`menubar` / `sidebar` 都只是约定，不是 core 的概念。
 *
 * 与其它几张表同一套路：静态类 + 函数内静态量 + 同名驳回。
 */
class RegionRegistry
{
public:
    enum class RegisterResult
    {
        Ok,
        NameTaken,     // 区域 id 重复，或区域内条目 id 重复
        UnknownRegion, // 条目指向的区域不存在
        Invalid,       // 指针为空 / id 为空
    };

    /**声明一个区域。通常由面板插件在 entry 里调用。*/
    static RegisterResult registerRegion(const GoldingRegionDecl* region, const std::string& provider);

    /**往区域里放一项。区域必须已存在。*/
    static RegisterResult registerItem(const GoldingRegionItemDecl* item, const std::string& provider);

    static const GoldingRegionDecl* findRegion(const std::string& id);

    /**问"这个区域是谁声明的"，诊断用。没登记过返回空串。*/
    static const std::string& regionProviderOf(const std::string& id);

    /**全部区域，按 order 升序（同 order 按 id 字典序）。*/
    static std::vector<const GoldingRegionDecl*> allRegions();

    /**某区域内的条目数；区域不存在返回 0。*/
    static std::size_t itemCount(const std::string& regionId);

    /**某区域内的第 index 项（已按 order 排好，同 order 先到先得）；越界返回 nullptr。*/
    static const GoldingRegionItemDecl* itemAt(const std::string& regionId, std::size_t index);

    /**问"这个区域里的这一项是谁注册的"，诊断用。没登记过返回空串。*/
    static const std::string& itemProviderOf(const std::string& regionId, const std::string& itemId);

    static std::size_t regionCount();

    /**
     * @brief 计数与最后一次驳回原因。
     *
     * 区域声明与区域条目**共用这一份**：它们是同一张表的两种写入，
     * 拆成两套会让"我提交了 5 项、被吃掉几项"要问两次。
     */
    static const RegistryStats& stats();

    static void clear();
};

} // namespace golding
