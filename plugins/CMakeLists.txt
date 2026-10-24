# 插件清单。
#
# 构建规则在 `cmakes/GoldingPlugin.cmake`（`golding_add_plugin(name source)`），
# 本文件只负责"有哪些插件"。新增一个插件 = 在 `plugins/core/<名字>/` 放
# `plugin.json` + 源码，然后在这里加一行。
#
# 源码参数按**仓库根**相对写：函数内部会补 `CMAKE_SOURCE_DIR`（见 `Options.md`）。
# 产物统一落到 `<build>/plugin/core/<名字>/`，运行时目录本身就是"内置插件"这个事实。

include("${CMAKE_SOURCE_DIR}/cmakes/GoldingPlugin.cmake")

golding_add_plugin(GoldingGamestoryShell
    plugins/core/GoldingGamestoryShell/src/GoldingGamestoryShell.cpp)

golding_add_plugin(settings
    plugins/core/settings/src/SettingsPlugin.cpp)



