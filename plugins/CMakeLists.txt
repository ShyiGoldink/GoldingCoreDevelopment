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

# 命令面板（`golding.commands` 的提供方）。两个源文件：插件本体 + 面板本体。
golding_add_plugin(commands
    plugins/core/commands/src/CommandsPlugin.cpp
    plugins/core/commands/src/CommandPalette.cpp)

golding_add_plugin(settings
    plugins/core/settings/src/SettingsPlugin.cpp)

golding_add_plugin(spellbook
    plugins/core/spellbook/src/SpellbookPlugin.cpp)

# 自定义鼠标指针：替换系统光标的一层画布（`golding.cursor` 的提供方）。
# 四个源文件：插件本体 + 用户指针仓库（目录/清单/图片尺寸）+ 用户指针管理界面
# + 偏好文件（记住用户挑的那一只）。
golding_add_plugin(cursor
    plugins/core/cursor/src/CursorPlugin.cpp
    plugins/core/cursor/src/UserCursors.cpp
    plugins/core/cursor/src/UserCursorUi.cpp
    plugins/core/cursor/src/CursorPrefs.cpp)

# 程序管理：侧栏一行一个应用、右侧一页一个应用（启动 + 设置），数据分两层
# （`apps.json` 管有哪些、`apps/<id>/` 管具体数据）。三个源文件：数据层 /
# 界面层 / 接线与自检。
golding_add_plugin(appmanager
    plugins/core/appmanager/src/AppStore.cpp
    plugins/core/appmanager/src/AppManagerUi.cpp
    plugins/core/appmanager/src/AppManagerPlugin.cpp)

# 开机自启动：往「设置」里加一块内容，用一个启动脚本控制"开机全屏自启"。
golding_add_plugin(autostart
    plugins/core/autostart/src/AutostartPlugin.cpp)
