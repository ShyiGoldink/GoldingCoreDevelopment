#pragma once

// CoreInternal.h —— core 内部头的**编译期**守卫。
//
// 要解决的问题：构建把整棵 `include/` 暴露给了插件（`golding_sdk` 是 INTERFACE +
// `target_include_directories(include/)`），但 `Application/*.h` / `HostApp.h` /
// `Tool/PathFinder.h` 的实现编在 `golding_core` 里，而插件只链 `golding_sdk`。
// 于是插件误引内部头的表现是"编译全过、链接期 undefined reference"——一个
// **离错误现场十万八千里**的失败。代价已经付过一次：spellbook 因此自带了一份
// 340 行手写 JSON 解析器（见 `docs/TASK-SDK-JSON.md` §2 条目 3）。
//
// 用法：每个 core 内部头在 `#pragma once` 之后第一行 `#include` 本文件。
// 宏 `GOLDING_CORE_INTERNAL` 只给 `golding_core` / `golding_host` / `core_tests`
// 三个目标定义（见根 `CMakeLists.txt` 与 `host/CMakeLists.txt`）；**`sdk_host_tests`
// 不定义**——它正是"外部使用者"的样本，给了宏就等于把守卫测没了。
//
// 本文件自己不需要守卫（它不含会被误引的实现），所以下面那句 `#error` 只在
// 别人把它带上时才生效。
//
// 不守卫的范围：`include/DataStruct/*.h` 是纯数据结构（没有对应的 .cpp，
// 没有需要链接的符号），引它不构成"头可见、符号不在"的陷阱，故不在守卫之列。

#if !defined(GOLDING_CORE_INTERNAL)
#error "这是 core 的内部头，不对插件开放：插件请只用 <sdk/…>（现在是 GoldingPlugin.h / GoldingWidgets.h / golding_plugin.h / golding_json.h）。缺能力就把它提成 SDK 头，不要直接引 core 内部头——符号编在 golding_core 里，插件链不到，链接期才会炸。"
#endif
