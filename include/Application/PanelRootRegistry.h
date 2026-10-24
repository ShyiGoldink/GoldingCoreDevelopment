#pragma once

#include <string>

#include "Application/RegistryStats.h"
#include "sdk/golding_plugin.h"

namespace golding
{

/**
 * @brief 面板根工厂：**一个槽位，不是一个登记表**。
 *
 * 面板插件（`role: "panel"`）通过 `registerWidgetClass` 交出"怎么建界面根"，
 * 宿主在装配阶段取出来调一次。整张表只有一项，所以它的职责只剩三件：
 * 存、取、说清是谁提供的。
 *
 * 为什么不保持"通用组件表"的形状：`GoldingWidgetClass` 的 `configName` 参数
 * 随 `widgets{}` 一起废弃（T-11），`destroy` 从未被调用，全仓也没有第二个组件类
 * 被创建。留着"支持 N 个组件"的形状，会让读代码的人以为那条路还在——
 * 本项目在"读了不用的字段"上栽过两次（`plugin.json` 的 `abiVersion`、
 * `requires.plugins`），代价都比删掉高。
 *
 * 与宿主库的 `ButtonLibrary` 同一套路：静态类 + 函数内静态量存储
 * （避开静态初始化/析构顺序问题）。只能在 UI 线程使用，不加锁。
 */
class PanelRootRegistry
{
public:
    /**设置结果。失败原因分得清，才能给出有用的日志。*/
    enum class Result
    {
        Ok,         // 存下了
        AlreadySet, // 已经有一个了——驳回，不覆盖
        Invalid,    // 指针 / 名字为空，或没有 create
    };

    /**
     * @brief 设置面板根工厂。
     *
     * provider 只用于日志与排错，是插件名。**第二次调用一律 `AlreadySet`**：
     * 槽位只有一个，换人得先 `clear()`——那是完全重加载的事，而本项目
     * 的重加载等于重启进程。
     */
    static Result setRoot(const GoldingWidgetClass* widgetClass, const std::string& provider);

    /**取面板根工厂；没设置过返回 nullptr。*/
    static const GoldingWidgetClass* root();

    /**问"这个根是谁提供的"，诊断用。没设置过返回空串。*/
    static const std::string& provider();

    /**
     * @brief 计数与最后一次驳回原因。
     *
     * 与 `root()` 的分工：`root()` 是**现在**有没有值，`stats()` 是**过程**里
     * 接受了几次、因为什么驳回。自检要断言"第二次应当驳回"，只能靠后者。
     */
    static const RegistryStats& stats();

    /**清空槽位（完全重加载时用）。*/
    static void clear();
};

} // namespace golding
