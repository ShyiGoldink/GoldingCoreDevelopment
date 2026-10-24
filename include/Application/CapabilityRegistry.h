#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace golding
{

/**
 * @brief 能力目录：**声明面**与**运行时面**放在一起，两张都归宿主。
 *
 * 它有两层，刻意不合并：
 *
 *   1. **声明面**（`declare`）：装配前把每个插件 `manifest.provides` 里的名字汇总起来。
 *      只有名字、没有 vtable——所以"它要的能力有没有人**声称**提供"在 entry 之前
 *      就能判出来。这是能力宣言能提前报错的唯一依据。
 *   2. **运行时面**（`registerCapability`）：插件在 entry 里真正注册的 vtable。
 *      存储后端仍是 `ServiceRegistry`（不推翻那张表），这里只额外记下**谁**提供的。
 *
 * 两层必须交叉校验（T-07 查 needs、T-08 查 provides）：
 *   声明了却没注册 = 那个插件坏了；注册了却没声明 = 允许（内部能力），只提示。
 *
 * 与另五张表同一套写法：静态类 + 函数内静态量 + 同名驳回。
 */
class CapabilityRegistry
{
public:
    enum class RegisterResult
    {
        Ok,
        NameTaken,
        Invalid,
    };

    // ---- 声明面 ----

    /**
     * @brief 登记"某个插件声称提供某个能力，版本是 version"。
     *
     * 名字与版本都来自 `plugin.json` 的 `provides`（扫描阶段可读），所以这张表在
     * **entry 之前**就是完整的——`needs` 的最小版本正是拿它来比。
     * 同名重复声明只记第一条。
     */
    static void declare(const char* name, unsigned int version, const char* declarer);

    /**有人**声称**提供了这个能力吗（不保证真的注册了）。*/
    static bool isDeclared(const char* name);

    /**声称提供的能力版本；没声明过返回 0（先用 `isDeclared` 区分）。*/
    static unsigned int versionOf(const char* name);

    /**声称提供这个能力的是谁；没人声称返回 nullptr。*/
    static const char* declarerOf(const char* name);

    // ---- 运行时面 ----

    /**
     * 登记一个真注册的服务：落 `ServiceRegistry`，同时记下提供方。
     *
     * 同名驳回的语义沿用 `ServiceRegistry`（服务名与组件名一样是唯一键）。
     */
    static RegisterResult registerCapability(const char* name, const void* vtable, const char* provider);

    /**按名字取 vtable；没有返回 nullptr。*/
    static const void* query(const char* name);

    /**谁**真的**注册了这个能力；没有返回 nullptr。*/
    static const char* providerOf(const char* name);

    /**真有 vtable 的能力名（按注册顺序）。*/
    static std::vector<std::string> allNames();

    // ---- 降级策略 ----

    /**
     * 校验开关（默认 true）。
     *
     * 关掉之后 `needs` 只留痕、不把插件标成 `Unresolved`——留一条不需要改代码的
     * 退路：某个环境里能力确实缺席、而那个插件还能降级工作时用得上。
     */
    static void setStrict(bool strict);
    static bool strict();

    /**清空两层（完全重加载用）。*/
    static void reset();
};

} // namespace golding
