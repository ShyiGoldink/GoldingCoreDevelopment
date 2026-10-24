#pragma once

#include <cstddef>
#include <deque>
#include <string>
#include <vector>

#include "Application/PluginState.h"
#include "DataStruct/Manifest.h"
#include "sdk/golding_plugin.h"

namespace golding
{

/**
 * @brief 插件静态宣言里宿主关心的那几项。
 *
 * 与 `GoldingPluginInfo` 分开：那个是**跨 dll 的 C 结构**（插件会读），
 * 这些只是宿主自己的记账，加字段不算 ABI 事件。
 *
 * `provides` / `needs` **来自 `plugin.json`**（扫描阶段可读），不是 descriptor——
 * 这样能力表连同版本在 entry 之前就是完整的。`kind` / `sdkId` / `sdkVersion`
 * 仍来自 descriptor。
 */
struct ManifestFacts
{
    const char* sdkId = "";
    const char* sdkVersion = "";
    const std::vector<ManifestCapability>* provides = nullptr;
    const std::vector<ManifestCapability>* needs = nullptr;
};

/**
 * @brief 插件档案表：core 侧唯一的"有哪些插件"的事实来源。
 *
 * 魔法阵、插件管理器、Resolver 都只读它，不各自去扫目录。
 * 存储用函数内静态量；字符串单独存在 deque 里，保证 `c_str()` 指针全程有效
 * （vector 扩容会把指针搬走）。
 */
class PluginLibrary
{
public:
    /**登记一个插件。所有字符串都会被拷贝一份，调用方不必持有。*/
    static void add(const char* name, const char* title, const char* version, const char* role, int abiVersion,
                    PluginState state, const std::vector<std::string>& depends, const ManifestFacts& facts);

    /**
     * 改一个已登记插件的状态。
     *
     * 加载阶段只能确定"manifest / dll / entry"这几种结果；"依赖是否满足"要等
     * `Resolver` 跑完才知道，所以状态必须允许回灌。下标越界时什么也不做。
     */
    static void setState(int index, PluginState state);

    /**清空（完全重加载用）。*/
    static void reset();

    static int count();
    static const GoldingPluginInfo* at(int index);

    /**第 index 个插件的依赖名。*/
    static const std::vector<std::string>& dependsOf(int index);

    // ---- 静态宣言里那几项（`plugin.json` 的 provides / needs，descriptor 的 sdkId）----

    /**`"<sdkId>/<sdkVersion>"`，档案那一行直接打它。*/
    static const char* sdkOf(int index);

    static const std::vector<ManifestCapability>& providesOf(int index);
    static const std::vector<ManifestCapability>& needsOf(int index);

    // ---- 插件目录（ABI v10）----

    /**
     * @brief 记下这个插件被扫描到的那**一个**目录（UTF-8 绝对路径）。
     *
     * 目录来自 `PathFinder`，不是插件自报——所以内置/外部分层、同名 shadowed
     * 之后"哪个才是权威目录"由 core 决定，插件不必自己推。
     */
    static void setDirectory(int index, const char* utf8);

    /**按名字找下标（名字即唯一键）；没有返回 -1。*/
    static int indexOf(const char* name);

    /**第 index 个插件的目录（UTF-8）；越界或未登记返回 nullptr。*/
    static const char* directoryOf(int index);
};

} // namespace golding
