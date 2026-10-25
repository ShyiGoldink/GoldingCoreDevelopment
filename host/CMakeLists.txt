# 宿主 exe（产品侧）。
#
# 这个目录只在 `GOLDING_WITH_INKING=ON` **且**本文件存在时才被根 CMakeLists 加进来
# （`add_subdirectory(host)`）。它一出现，InkingUIKit（连带 SDL3 / FFmpeg）就成了
# 构建期必需依赖；只做 core / 写插件的检出把它删掉，`-DGOLDING_WITH_INKING=OFF`
# 仍然能 configure、能测。
#
# 边界：`HostApp.{h,cpp}` 是 core 的一部分（零 UI 依赖，编进 `golding_core`）；
# 这里只剩 `main.cpp`——"把 InkingUIKit 的窗口接到 HostApp 上"。
cmake_minimum_required(VERSION 3.22)

# ---- 1. 定位 InkingUIKit 预编译包 ----
#
# 为什么要有候选表：漏配的表现是 configure 阶段一句"找不到 inkingConfig.cmake"，
# 而真正的原因（SDK 落在哪个盘）只有使用者知道。候选表至少把本机的常见位置覆盖住，
# 且**只做提示**——显式给 `-DGOLDING_INKING_DIR=...` 或走 CMAKE_PREFIX_PATH 永远优先。
#
# 注意：包必须含 bin/ include/ lib/ 三件；只给源码树没有 lib/cmake/inking 是不行的
# （那份配置里还带着 SDL3 / FFmpeg 的依赖声明）。
set(_golding_inking_dir "")
if (DEFINED GOLDING_INKING_DIR AND NOT GOLDING_INKING_DIR STREQUAL "")
    set(_golding_inking_dir "${GOLDING_INKING_DIR}")
else()
    foreach(_candidate IN ITEMS
            "${CMAKE_SOURCE_DIR}/third_party/InkingUIKit/inking-0.5.1-win64"
            "$ENV{INKING_SDK_DIR}"
            "D:/InkingUIKit/dist/inking-0.5.1-win64"
            "C:/InkingUIKit/dist/inking-0.5.1-win64"
            "${CMAKE_SOURCE_DIR}/third_party/InkingUIKit/inking-0.5.0-win64"
            "D:/InkingUIKit/dist/inking-0.5.0-win64"
            "C:/InkingUIKit/dist/inking-0.5.0-win64"
            "${CMAKE_SOURCE_DIR}/third_party/InkingUIKit/inking-0.4.0-win64"
            "D:/InkingUIKit/dist/inking-0.4.0-win64"
            "C:/InkingUIKit/dist/inking-0.4.0-win64")
        if (_golding_inking_dir STREQUAL "" AND NOT _candidate STREQUAL ""
            AND EXISTS "${_candidate}/lib/cmake/inking/inkingConfig.cmake")
            set(_golding_inking_dir "${_candidate}")
        endif()
    endforeach()
endif()

if (NOT _golding_inking_dir STREQUAL "")
    message(STATUS "InkingUIKit SDK：${_golding_inking_dir}")
    list(PREPEND CMAKE_PREFIX_PATH "${_golding_inking_dir}")
else()
    message(STATUS "InkingUIKit SDK：候选目录都没有，交给 CMAKE_PREFIX_PATH / inking_DIR 解析")
endif()

# `find_package(inking)` 会继续 find SDL3 / SDL3_image / SDL3_ttf / SDL3_mixer / FFmpeg 的
# **开发包**（MinGW ABI 的硬前提，SDK 自己带不了编译器）。它们装在 MSYS2 UCRT64 里。
# 这一条同样只是提示：已经有 CMAKE_PREFIX_PATH 的人不受影响。
foreach(_msys IN ITEMS "$ENV{MSYS2_ROOT}/ucrt64" "C:/msys64/ucrt64" "D:/msys64/ucrt64")
    if (NOT _msys STREQUAL "" AND EXISTS "${_msys}/lib/cmake/SDL3/SDL3Config.cmake")
        list(APPEND CMAKE_PREFIX_PATH "${_msys}")
    endif()
endforeach()

find_package(inking CONFIG REQUIRED)

# ---- 2. 宿主 exe ----
add_executable(golding_host main.cpp)
target_link_libraries(golding_host PRIVATE golding_core inking::inking)

if (MSVC)
    target_compile_options(golding_host PRIVATE /W4)
else()
    target_compile_options(golding_host PRIVATE -Wall -Wextra -Wpedantic)
endif()

# 产物落**顶层**二进制目录，不放 `build/host/`：
# `PathFinder` 按"exe 旁边的 plugin/"找插件，挪一层就会变成 `build/host/plugin/`，
# 一个插件都扫不到（`docs/NOTES.md` 的「构建」）。这条属性是那个事实的唯一定义处。
set_target_properties(golding_host PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# ---- 3. 运行期 DLL ----
#
# 发布包自带完整闭包（libinking + SDL3 + 字体/图片链路）。
# **必须拷**：漏拷的表现是静默退出（0xC0000135），没有报错窗口。
# 拷整个 bin/*.dll 而不是列名字，是为了 SDK 下次加依赖时不用改这里。
if (DEFINED INKING_SDK_DIR AND EXISTS "${INKING_SDK_DIR}/bin")
    file(GLOB GOLDING_INKING_RUNTIME_DLLS "${INKING_SDK_DIR}/bin/*.dll")
    if (GOLDING_INKING_RUNTIME_DLLS)
        add_custom_command(TARGET golding_host POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${GOLDING_INKING_RUNTIME_DLLS}
                    "$<TARGET_FILE_DIR:golding_host>"
            COMMENT "拷贝 Inking 运行期 DLL")
    endif()
else()
    message(WARNING "没拿到 INKING_SDK_DIR/bin：运行期 DLL 不会被拷到 exe 旁边，"
                    "直接运行会因为缺 dll 静默退出。")
endif()

# ---- 4. 插件 ----
#
# 构建规则在 `cmakes/GoldingPlugin.cmake`，插件清单在 `plugins/CMakeLists.txt`。
# 这个检出目前还没有 plugins/，所以存在性判断在这里——不能因为"还没有插件"
# 就让宿主 configure 不过。
if (EXISTS "${CMAKE_SOURCE_DIR}/plugins/CMakeLists.txt")
    add_subdirectory("${CMAKE_SOURCE_DIR}/plugins" "${CMAKE_BINARY_DIR}/plugins")
else()
    message(STATUS "没有 plugins/：只构建宿主 exe（运行起来界面为空，属合法状态）")
endif()

# ---- 5. ctest：每个窗口层模式一个 test ----
#
# 无头跑（SDL_VIDEO_DRIVER=dummy）。断言在插件自己的 `ready` 里，宿主这边验的是
# "能启动、能装配、退出码如实"。`--frames` 给小值，跑的是真帧（事件泵 / 渲染全走）。
function(golding_add_host_test name)
    add_test(NAME "host.${name}" COMMAND golding_host ${ARGN})
    set_tests_properties("host.${name}" PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        TIMEOUT 60
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy")
endfunction()

golding_add_host_test(default --frames=2)
golding_add_host_test(fullscreen --fullscreen --frames=2)
golding_add_host_test(bootmode --expanded --frames=1)
# 面板自己的启动模式：侧栏一进来就是收起的。走的就是 `bootMode` 那条通道，
# 所以这条测试同时钉住了"bootMode 在装配**之前**就交给插件"这个顺序。
golding_add_host_test(collapsed --collapsed --frames=1)
# 指针插件的演示模式：不读真实输入，按帧把图案与"按下"各走一遍。它同时也是
# 唯一会真的推进指针层动画（每帧 Tick + 重画 + 移动）的测试——平常那条路要
# 指针进窗口才会走，无头环境里永远不触发。
# 140 帧以后是"弹窗被关闭 / 替换"那一段（关用户面板 → 重开设置 → 从按钮里
# 再 push → 再关）。少一帧就漏掉一次真实的点击回调栈。
golding_add_host_test(cursordemo --cursor-demo --frames=160)
