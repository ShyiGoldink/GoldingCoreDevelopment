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

function(golding_add_plugin name source)
    set(source_dir "${CMAKE_SOURCE_DIR}/plugins/core/${name}")
    if (ARGC GREATER 2)
        set(source_dir "${CMAKE_SOURCE_DIR}/plugins/${ARGV2}")
    endif()

    # 源码参数按**仓库根**相对路径写（与旧版调用点一致），这里统一补前缀：
    # 本文件被 `plugins/CMakeLists.txt` include，`CMAKE_CURRENT_SOURCE_DIR` 是 plugins/，
    # 直接用它拼会变成 plugins/plugins/...
    add_library(${name} SHARED "${CMAKE_SOURCE_DIR}/${source}")
    target_link_libraries(${name} PRIVATE golding_sdk inking::inking)
    target_include_directories(${name} PRIVATE "${CMAKE_SOURCE_DIR}/plugins/common")
    set_target_properties(${name} PROPERTIES
        PREFIX ""
        OUTPUT_NAME "${name}")

    # 运行时目录固定在**顶层**二进制目录，与 ctest 的工作目录同一层。
    set(runtime_dir "${CMAKE_BINARY_DIR}/plugin/core/${name}")
    add_dependencies(golding_host ${name})

    add_custom_command(TARGET ${name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${runtime_dir}/bin"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${source_dir}/plugin.json"
                "${runtime_dir}/plugin.json"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:${name}>"
                "${runtime_dir}/bin/${name}.dll")

    if (EXISTS "${source_dir}/assets")
        add_custom_command(TARGET ${name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${source_dir}/assets"
                    "${runtime_dir}/assets")
    endif()
endfunction()
