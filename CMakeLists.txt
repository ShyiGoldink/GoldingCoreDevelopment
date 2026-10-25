# 根 CMakeLists：**只有 core 与它的单测**。
#
# 这个文件里的每一行都必须在"没有 host/、没有 plugins/、没有 InkingUIKit"的检出里成立——
# 它就是插件开发包的全部构建入口（用法见 `README.md` 的「快速开始」）。宿主与插件在
# `host/`、`plugins/` 两个子目录里，由下面那个 `if` 决定要不要加进来。
cmake_minimum_required(VERSION 3.22)

project(GoldingCore VERSION 0.2.0 LANGUAGES CXX)

# 测试开关放在最外层：GOLDING_WITH_INKING=OFF 时也要能跑 SDK 的单测
# （GoldingPlugin.h 零 UI 依赖，这正是"双头方案"的第一层）。
enable_testing()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- SDK 头 + 它的身份常量 ----
#
# `GOLDING_SDK_VERSION` 的真源就是上面那行 `project(... VERSION ...)`，**只在这里写一次**：
# 经 target_compile_definitions 传给所有用到 SDK 的目标（core / 宿主 exe / 每个插件 / 单测）。
# 头里对这两个宏只做 #ifndef 兜底，兜底值 `0.0.0-dev` 会被单测断言"不等"——
# 忘了接线就会被抓住，而不是悄悄退化成兜底值。
add_library(golding_sdk INTERFACE)
target_include_directories(golding_sdk INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_definitions(golding_sdk INTERFACE
    GOLDING_SDK_ID="golding"
    GOLDING_SDK_VERSION="${PROJECT_VERSION}")

# 宿主核心库：纯 C++，不依赖 InkingUIKit，可无头自检
add_library(golding_core STATIC
    src/Tool/PathFinder.cpp
    src/Application/ManifestLoader.cpp
    src/Application/HostApp.cpp
    src/Application/PanelRootRegistry.cpp
    src/Application/PageRegistry.cpp
    src/Application/SettingsRegistry.cpp
    src/Application/RegionRegistry.cpp
    src/Application/CapabilityRegistry.cpp
    src/Application/PluginScanner.cpp
    src/Application/PluginLoader.cpp
    src/Application/PluginLibrary.cpp
    src/Application/Resolver.cpp
    src/Application/ServiceRegistry.cpp
)

target_link_libraries(golding_core PUBLIC golding_sdk)

# `GOLDING_CORE_INTERNAL`：core 内部头（`Application/*.h`、`HostApp.h`、`Tool/PathFinder.h`）
# 的守卫宏。这些头的实现编在 `golding_core` 里，而插件只链 `golding_sdk`——插件误引
# 的表现是"编译过、链接期 undefined reference"，离现场太远。守卫把它们变成**编译期**
# `#error`（见 `include/Application/CoreInternal.h`）。
#
# 只给这三个目标定义：`golding_core`（实现方）、`golding_host`（宿主 exe，它读
# 注册表接口）、`core_tests`（core 的单测）。
# **`sdk_host_tests` 不定义**——它只链 SDK，正是"外部使用者"的样本，给了宏
# 守卫就等于没测。
target_compile_definitions(golding_core PRIVATE GOLDING_CORE_INTERNAL)

if (MSVC)
    target_compile_options(golding_core PRIVATE /W4)
else()
    target_compile_options(golding_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---- SDK 单测（与窗口无关，两个分支都建）----
#
# 只依赖 SDK 的两个头，所以 `GOLDING_WITH_INKING=OFF` 下照样编得过、跑得起来——
# 这就是"纯逻辑插件不该被迫依赖 UI 库"的最小证据。
add_executable(sdk_host_tests tests/sdk_host_tests.cpp)
target_link_libraries(sdk_host_tests PRIVATE golding_sdk)
if (MSVC)
    target_compile_options(sdk_host_tests PRIVATE /W4)
else()
    target_compile_options(sdk_host_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME sdk_host COMMAND sdk_host_tests)

# core 的纯逻辑单测：路径解析 + manifest 读入 + 登记表。
#
# 与 sdk_host 的分工：那个测**跨 dll 契约**（descriptor / Host 转发），这个测
# **core 自己那几件纯函数**（`PathFinder::resolve` / `ManifestLoader` / 各登记表）。
# 两者都只链头 / 静态库，`GOLDING_WITH_INKING=OFF` 下照样跑——所以"core 可无头
# 自检"不再依赖 InkingUIKit 是否装得上。
add_executable(core_tests tests/core_tests.cpp)
target_link_libraries(core_tests PRIVATE golding_core)
# 与 core 同一条理由：core_tests 直接读 `Application/*.h`，它必须拿到守卫宏。
target_compile_definitions(core_tests PRIVATE GOLDING_CORE_INTERNAL)
if (MSVC)
    target_compile_options(core_tests PRIVATE /W4)
else()
    target_compile_options(core_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME core COMMAND core_tests)

# ---- 宿主 exe 与插件：产品侧，可选 ----
#
# InkingUIKit 默认不链接：
#   1) 纯数据模块（扫描 / manifest / 注册表）只用到标准库；
#   2) 它的包配置会 find SDL3 / SDL3_image / SDL3_ttf / SDL3_mixer / FFmpeg / Threads，
#      缺一个 configure 就失败。
# 需要开窗、装配界面、编译插件时打开这个开关。
option(GOLDING_WITH_INKING "链接 InkingUIKit（用到 window / scene / widget 时打开）" OFF)

if (GOLDING_WITH_INKING)
    # `host/` 存在 = 这是一个"完整仓库"的检出；不存在 = 开发包检出（只有 include/ src/ tests/ cmakes/）。
    # 判据用文件存在而不是选项，是因为"缺目录"是事实，"忘了关选项"是人为失误——前者不该让
    # configure 失败在这里。见 `docs/NOTES.md` 的「构建」。
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/host/CMakeLists.txt")
        add_subdirectory(host)
    else()
        message(STATUS "GOLDING_WITH_INKING=ON，但这个检出没有 host/：只构建 golding_core 与它的单测")
    endif()
endif()
