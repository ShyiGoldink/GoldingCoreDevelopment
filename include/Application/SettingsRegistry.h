#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "Application/RegistryStats.h"
#include "sdk/golding_plugin.h"

namespace golding
{

/**
 * @brief 设置页区块登记表：区块 id → 插件提供的区块描述。
 *
 * 插件往"设置"里加东西的唯一入口。设置页插件按 order 把它们依次挂上去，
 * 因此新增一个设置区块不需要改设置页本身。
 */
class SettingsRegistry
{
public:
    enum class RegisterResult
    {
        Ok,       // 登记成功
        IdTaken,  // 同 id 已存在——驳回
        Invalid,  // id 为空 / create 为空
    };

    static RegisterResult registerSection(const GoldingSettingsDecl* section, const std::string& provider);

    static const GoldingSettingsDecl* findSection(const std::string& id);

    /**问"这个设置区块是谁注册的"，诊断用。没登记过返回空串。*/
    static const std::string& providerOf(const std::string& id);

    /**全部区块，按 order 升序（同 order 按 id 字典序）。*/
    static std::vector<const GoldingSettingsDecl*> allSections();

    static std::size_t count();

    /**计数与最后一次驳回原因（见 `RegistryStats`）。*/
    static const RegistryStats& stats();

    static void clear();
};

} // namespace golding
