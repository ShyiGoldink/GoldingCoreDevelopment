#pragma once

#include <string>
#include <vector>

namespace golding
{

/**manifest 的读取状态。*/
enum class ManifestStatus
{
    Pass,    // 读取并校验通过
    Invalid, // 文件读到了，但内容不合规（JSON 语法错 / 缺必需字段 / 字段类型不对）
    Error,   // 文件读不到（不存在、无权限、读取中断）
};

/**plugin.json 的结构：README 第 47 行的必需字段与可选字段。*/
struct Manifest
{
    std::string name;
    std::string title;
    std::string version;

    /**
     * 插件用到的最小 core 能力版本，单调递增的数字。
     *
     * 与 `version`（插件自己的版本号，展示用）不同：`apiVersion` 参与校验，
     * 宿主声明自己支持到几，插件声明高于它就拒绝加载并说明当前支持到几。
     * 允许写成数字或数字字符串，见 ManifestLoader。
     */
    int apiVersion = 0;

    /**
     * ABI 版本**不在这里**：它只认 dll 里 descriptor 的那一份。
     * 同一个事实写两处必然对不上（此前 8 个插件有 6 个 json 写着旧版本，
     * 而没有任何代码会发现），所以彻底移出 manifest。
     */

    /**role：panel 表示"提供界面骨架"的插件；缺失视为普通插件。*/
    std::string role;

    /**requires.plugins：依赖的插件名。（成员不能叫 requires——C++20 起是关键字）*/
    std::vector<std::string> dependencies;

    /*
     * 这里曾有 `widgets{组件名: 配置路径}`（T-11 删除）。
     *
     * 删的理由不是"没时间做"，而是它没有制造者也没有消费者：`ManifestLoader`
     * 读进来，全仓库零命中；而"配置数据 + 插件自己解释"这条路已经由
     * `magic.json` 走通了（那种形态下**插件**是解释者，`plugin.json` 不必认识
     * 任何组件名）。留着一个读进来却什么都不做的字段，下一个人会以为它该工作。
     *
     * 现在 `plugin.json` 顶层键是**白名单**：写了未知键判 `Invalid` 并在
     * `ManifestResult::detail` 里说清是哪个键——静默忽略正是 `abiVersion`
     * 曾写在 json 里那种死法。
     */

    /**是否声明了 targets.win64.lib。*/
    bool hasDll = false;
    /**
     * `targets.win64.lib` 解析后的**绝对路径**（UTF-8）。
     *
     * `plugin.json` 里写的是相对插件目录的路径，`ManifestLoader` 读完当场解析，
     * 所以拿到这里的消费者（`HostApp::loadEntry`）不必知道基准目录是谁。
     * 写成绝对路径或用 `..` 逃出插件目录会让整份 manifest 判 `Invalid`。
     */
    std::string dllPath;
};

/**manifest 读取结果：结构 + 状态。*/
struct ManifestResult
{
    Manifest manifest;
    ManifestStatus status = ManifestStatus::Invalid;
    /**
     * 驳回原因（UTF-8，给人看的一句话）；`Pass` 时为空。
     *
     * 存在的理由：`plugin.json` 现在是**白名单**，写了未知键就判 `Invalid`。
     * 没有这句话，表现就只是启动日志里一行 `status=1`——作者得逐个字段猜。
     * 它只用于日志，不参与任何判定（判定只看 `status`）。
     */
    std::string detail;
};

} // namespace golding
