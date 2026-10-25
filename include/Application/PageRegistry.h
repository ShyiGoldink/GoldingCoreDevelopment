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
 * @brief 页面登记表：页面 id → 插件提供的页面描述。
 *
 * 与 `PanelRootRegistry` 同一套路：静态类 + 函数内静态量、同 id 驳回。
 * 骨架插件通过 HostApi 的 pageCount / pageAt 读它来生成导航。
 */
class PageRegistry
{
public:
    /**登记结果。*/
    enum class RegisterResult
    {
        Ok,       // 登记成功
        IdTaken,  // 同 id 已存在——驳回
        Invalid,  // id 为空 / create 为空
    };

    static RegisterResult registerPage(const GoldingPageDecl* page, const std::string& provider);

    /**按 id 查；没有返回 nullptr。*/
    static const GoldingPageDecl* findPage(const std::string& id);

    /**问"这个页面是谁注册的"，诊断用。没登记过返回空串。*/
    static const std::string& providerOf(const std::string& id);

    /**全部页面，按 order 升序（同 order 按 id 字典序）。*/
    static std::vector<const GoldingPageDecl*> allPages();

    static std::size_t count();

    /**计数与最后一次驳回原因（见 `RegistryStats`）。*/
    static const RegistryStats& stats();

    static void clear();
};

} // namespace golding
