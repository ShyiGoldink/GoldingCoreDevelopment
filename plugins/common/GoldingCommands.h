#pragma once

// 命令面板插件的**服务契约**（`golding.commands`）。
//
// 为什么要有它：一个插件想暴露"一个动作"，目前只有两条路——注册顶栏区域项、
// 或者注册设置区块。顶栏的位置是有限的（插件一多就排不下），设置区块又是
// "填配置"的语义。命令面板给的是第三条路：**交一份纯数据，由面板负责展示**。
//
// 与 `GoldingMenu.h` / `GoldingServices.h` / `GoldingSettings.h` 同一条规矩：
// **契约在提供方，规格在头文件，谁用谁 include**。core 不认识"命令"这个词。
//
// ---------------------------------------------------------------------------
// 这个服务回答的问题
//
// "我有一个动作，想让它能被搜到、被一键执行"——动作是什么由插件说（一个函数
// 指针），怎么搜、怎么排、面板长什么样由命令面板说。**命令只描述"做什么"，
// 不认识界面**：`CommandDecl` 里没有任何位置 / 尺寸 / 图标字段。
//
// ---------------------------------------------------------------------------
// 依赖是**可选**的
//
// 注册命令的插件**不写 `needs`**：命令面板不在时它们照常工作，只是少了一条命令。
// `Use` 取不到就打一行 WARN 跳过——这是本仓库"缺能力降级"的标准形态
// （`docs/Options.md` 的三态之一）。
//
// ```cpp
// const golding::CommandService* commands =
//     host.Use<golding::CommandService>(golding::kCommandsServiceName,
//                                       golding::kCommandsServiceVersion);
// if (commands != nullptr) {
//     commands->add(nullptr, &kMyCommand);
// }
// ```
//
// ---------------------------------------------------------------------------
// 生命周期
//
// `id` / `title` / `keywords` 三个串会被**深拷贝**：调用方可能现场拼字符串
// （例如 `"打开 " + 名字`），面板不能借它们的指针。
//
// `onRun` 是函数地址，会被**长期持有**（它指向调用方的代码，不是数据），
// 所以必须是静态函数的地址；`hostContext` 恒传 nullptr——需要 HostApi 的插件
// 应当把自己那份 `golding::Host` 存成静态量再用（与 `GoldingMenu.h` 同一条约定）。
//
// `at()` 返回的指针由面板持有、活到进程结束；调用方不得释放。

#include <sdk/golding_plugin.h>

namespace golding
{

/**服务名（唯一键）。 */
inline constexpr const char* kCommandsServiceName = "golding.commands";

/**契约版本。v1：`add` / `count` / `at` 三条。 */
inline constexpr unsigned int kCommandsServiceVersion = 1;

/**
 * @brief 一条命令的声明。**只有"做什么"，没有"长什么样"。**
 */
struct CommandDecl
{
    /**唯一键；重复的 `add` 被驳回（返回非 0），不覆盖。可空 = 拒绝。*/
    const char* id;

    /**列表上显示的那一行。**空串的项会被跳过**（不画一个空行）。*/
    const char* title;

    /**
     * 额外检索词：空格分隔（拼音 / 英文别名用）。可空。
     *
     * 只影响"能不能搜到"，不影响显示——给中文命令补一组拼音就靠它。
     */
    const char* keywords;

    /**
     * 执行。可空（空 = 这条命令点了什么也不做）。
     *
     * `hostContext` 恒传 nullptr，与菜单的 `onActivate` 同一条约定。
     */
    void (*onRun)(void* hostContext);
};

/**命令服务（`golding.commands`）。 */
struct CommandService
{
    unsigned int abiVersion;

    /**
     * @brief 登记一条命令；0 = 成功。
     *
     * 非 0 的三种情形：`decl` 为空 / `id` 为空 / `id` 已被占用。
     * **不覆盖**——"后来者悄悄改掉前者"是最难查的一类 bug（与 `ShortcutRegistry`
     * 的先到先得同一条理由）。
     */
    int (*add)(void* self, const CommandDecl* decl);

    /**现在有几条命令。 */
    int (*count)(void* self);

    /**
     * 第 index 条命令（下标按**注册顺序**，与过滤无关）；越界返回 nullptr。
     *
     * 返回的指针在面板生命周期内稳定（追加命令不会让它失效）。
     */
    const CommandDecl* (*at)(void* self, int index);
};

} // namespace golding
