#pragma once

namespace golding
{

/**
 * @brief 一个插件最终处在什么状态。
 *
 * 取值与 SDK 的 `GoldingPluginInfo.state` 一一对应——跨 dll 只能传 int，
 * 这张表就是那串整数的**唯一**定义处。宿主内部一律用枚举，只在填档案时转 int。
 *
 * 只描述结果，不替调用方判断：`Unresolved` 覆盖"缺依赖"与"依赖成环"两种情形，
 * 想知道是哪一种，去看 `ResolveResult::unresolved` 里那一项的原因。
 */
enum class PluginState
{
    Ready = 0,           // manifest 合法、dll 加载成功、依赖满足、entry 成功
    ManifestInvalid = 1, // plugin.json 读不出来，或内容不合规
    LibraryError = 2,    // dll 打不开（不存在 / 位数不对 / 缺依赖库）
    AbiMismatch = 3,     // descriptor 的 abiVersion 与宿主不一致

    // 4 已废弃（2026-10-24）：曾表示"没有 dll 的纯数据插件"。
    // 那个形态没有载体（`widgets{}` 在 T-11 已删），也进不了依赖图，已整体取消——
    // 缺 `targets.win64.lib` 现在直接判 `ManifestInvalid`（`docs/Options.md`）。
    // **编号保留为空缺，不重排**：这些整数值是跨 dll 契约（`GoldingPluginInfo.state`），
    // 重排会让所有历史日志与插件侧的判断错位。

    EntryFailed = 5,     // entry 返回了非 0
    Unresolved = 6,      // 依赖缺失，或依赖成环——无法排进装配顺序
    ApiTooNew = 7,       // manifest 声明的 apiVersion 高于宿主支持，能力给不了
    SdkMismatch = 8,     // descriptor 的 sdkId 与宿主不是同一套 SDK
};

/**转成 SDK 里那个 int。*/
inline int toInt(PluginState state)
{
    return static_cast<int>(state);
}

} // namespace golding
