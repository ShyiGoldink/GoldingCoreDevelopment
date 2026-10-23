#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "Application/RegistryStats.h"
#include "sdk/golding_plugin.h"

namespace golding
{

/**
 * @brief 组件注册表：组件名 → 插件提供的类描述。
 *
 * 与宿主库的 `ButtonLibrary` 同一套路：静态类 + 函数内静态量存储
 * （避开静态初始化/析构顺序问题）、同名**驳回**而不覆盖。
 *
 * 只能在 UI 线程使用，不加锁。
 */
class PluginRegistry
{
public:
    /**注册结果。失败原因分得清，才能给出有用的日志。*/
    enum class RegisterResult
    {
        Ok,        // 注册成功
        NameTaken, // 同名已存在——驳回，不覆盖
        Invalid,   // 名字为空 / 指针为空
    };

    /**登记一个组件类。provider 仅用于日志与排错，可以是插件名。*/
    static RegisterResult registerClass(const GoldingWidgetClass* widgetClass, const std::string& provider);

    /**按名字查组件类；没登记过返回 nullptr。*/
    static const GoldingWidgetClass* findClass(const std::string& name);

    /**问"这个组件是谁注册的"，诊断用。没登记过返回空串。*/
    static const std::string& providerOf(const std::string& name);

    /**所有登记过的组件名，主要给自检与魔法阵用。*/
    static std::vector<std::string> allNames();

    /**表里有多少项。*/
    static std::size_t count();

    /**
     * @brief 计数与最后一次驳回原因。
     *
     * 与 `count()` 的分工：`count()` 是**现在**有多少项，`stats()` 是**过程**里
     * 接受了多少、驳回了多少、最后一次为什么驳回。自检要断言"应当驳回 1 次"，
     * 只能靠后者。
     */
    static const RegistryStats& stats();

    /**清空整张表（完全重加载时用）。*/
    static void clear();
};

} // namespace golding
