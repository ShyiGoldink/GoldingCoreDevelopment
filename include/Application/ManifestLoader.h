#pragma once

#include <filesystem>

#include "DataStruct/Manifest.h"

namespace golding
{

/**
 * @brief 把 plugin.json 读成 Manifest。
 *
 * 只做读取与字段校验，不读取插件自己那份配置文件（如 `magic.json`）的内容，
 * 也不碰依赖关系——那是各插件自己的事。
 */
class ManifestLoader
{
public:
    /**
     * @brief 读取 <pluginDir>/plugin.json。
     *
     * 必需字段：name / title / version / apiVersion / requires.plugins。
     * `apiVersion` 是整数（数字或数字字符串），为插件用到的最小 core 能力版本。
     *
     * **顶层键是白名单**（上面的五个 + role / targets）：写了别的键判 `Invalid`，
     * 原因见 `ManifestResult::detail`。
     * 任一字段缺失或类型不对即为 Invalid；文件读不到为 Error。
     */
    static ManifestResult read(const std::filesystem::path& pluginDir);
};

} // namespace golding
