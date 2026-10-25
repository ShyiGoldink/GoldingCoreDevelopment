# 插件构建函数（`golding_add_plugin`）。
#
# 由 `plugins/CMakeLists.txt` include。这里**只定义函数、不建目标**：
# 谁 include 谁负责调用，调用点与清单放在一起更好改。
#
# 前提两条：`golding_sdk` / `golding_core` 已定义（根 CMakeLists），
# `inking::inking` 已 find（host/CMakeLists.txt），`golding_host` 已定义
# （POST_BUILD 的依赖关系要挂到它身上）。
#
# 关于"插件源码能不能在仓库外"：**暂时不考虑**。插件按约定有自己专属的文件夹
# （`plugins/core/<name>/`、`plugins/dev/<name>/`），拿名字拼路径就是对的；
# 等真有外部插件工程时再加一个"绝对目录"参数，那时才知道它长什么样。

function(golding_add_plugin name)
    set(source_dir "${CMAKE_SOURCE_DIR}/plugins/core/${name}")

    # 源码与"放在哪一层"是同一个参数表：`.cpp/.cc/.cxx` 结尾的是源码，其余
    # （例如 `dev`）是 `plugins/<它>/<name>/` 那一层。这样单文件与多文件插件
    # 用同一个函数调用，不必再为"这个插件有两个 .cpp"加一个变体。
    set(sources "")
    foreach(arg IN LISTS ARGN)
        if (arg MATCHES "\\.(cpp|cc|cxx)$")
            list(APPEND sources "${CMAKE_SOURCE_DIR}/${arg}")
        else()
            set(source_dir "${CMAKE_SOURCE_DIR}/plugins/${arg}/${name}")
        endif()
    endforeach()

    if (sources STREQUAL "")
        message(FATAL_ERROR "golding_add_plugin(${name})：没给任何源文件")
    endif()

    # 源码参数按**仓库根**相对路径写（与旧版调用点一致），这里统一补前缀（见上）：
    # 本文件被 `plugins/CMakeLists.txt` include，`CMAKE_CURRENT_SOURCE_DIR` 是 plugins/，
    # 直接用它拼会变成 plugins/plugins/...
    add_library(${name} SHARED ${sources})
    target_link_libraries(${name} PRIVATE golding_sdk inking::inking)

    # 与 core / 宿主同一档警告：插件是**进程内**的代码，一个未初始化变量就是
    # 一次崩溃（没有隔离），所以"编译零警告"在插件这一层同样要当成门槛。
    if (MSVC)
        target_compile_options(${name} PRIVATE /W4)
    else()
        target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    target_include_directories(${name} PRIVATE "${CMAKE_SOURCE_DIR}/plugins/common")
    set_target_properties(${name} PROPERTIES
        PREFIX ""
        OUTPUT_NAME "${name}")

    # 运行时目录固定在**顶层**二进制目录，与 ctest 的工作目录同一层。
    set(runtime_dir "${CMAKE_BINARY_DIR}/plugin/core/${name}")
    add_dependencies(golding_host ${name})

    # `plugin.json` 是 POST_BUILD 拷的，而 POST_BUILD 只在**目标真的重链接**时跑。
    # 不把 json 记成链接依赖，就会出现"改了 manifest 却毫无反应"——运行的是上一份，
    # 而且没有任何提示（`provides` / `needs` / `role` 都栽在这上面）。
    set_property(TARGET ${name} APPEND PROPERTY LINK_DEPENDS "${source_dir}/plugin.json")

    add_custom_command(TARGET ${name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${runtime_dir}/bin"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${source_dir}/plugin.json"
                "${runtime_dir}/plugin.json"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:${name}>"
                "${runtime_dir}/bin/${name}.dll")

    # `shape.json`：插件自己的法阵形状（可选）。和 plugin.json 一样是 POST_BUILD
    # 拷的，同样要记成链接依赖，否则"改了形状却没反应"。
    if (EXISTS "${source_dir}/shape.json")
        set_property(TARGET ${name} APPEND PROPERTY LINK_DEPENDS "${source_dir}/shape.json")
        add_custom_command(TARGET ${name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${source_dir}/shape.json"
                    "${runtime_dir}/shape.json")
    endif()

    if (EXISTS "${source_dir}/assets")
        add_custom_command(TARGET ${name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${source_dir}/assets"
                    "${runtime_dir}/assets")
    endif()
endfunction()
