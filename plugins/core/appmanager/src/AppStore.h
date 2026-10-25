#pragma once

// ---------------------------------------------------------------------------
// appmanager 的**数据层**：清单 + 每个应用的小文件夹。
//
// 两层结构（用户点名的组织方式）：
//
//   %APPDATA%\GoldingCore\appmanager  （数据根）
//     apps.json            —— **只管"有哪些、什么顺序"**（一个单独文件）
//     apps\<id>\app.ini    —— 这个应用自己的字段（路径 / 标题 / 图标 / 背景）
//     apps\<id>\icon.*     —— 自定义图标（用户选的，复制进来）
//     apps\<id>\bg.*       —— 自定义背景（图片 / 视频）
//
// 为什么清单与详情分开：加一个应用、调一个应用的背景，是两个量级的操作。
// 清单小到可以直接手改；详情各自一个文件夹，坏一个不影响别的，删一个就是删目录。
//
// 这一层**不碰界面**（不知道按钮、不知道面板），所以可以被自检直接断言。
// 但它用 Windows API（ShellExecute / 环境变量），这是"启动程序"这件事的本性。
// ---------------------------------------------------------------------------

#include <filesystem>
#include <string>
#include <vector>

namespace appmanager
{

/** 一个应用的全部字段（从 `apps/<id>/app.ini` 读出来的那份）。 */
struct AppEntry
{
    /** 目录名兼唯一键（`[a-z0-9-]`）。清单里只存它。 */
    std::string id;
    std::string title;

    /** exe / lnk / bat 的绝对路径（UTF-8）。空 = 还没配，启动时给一条 WARN。 */
    std::string target;
    /** 启动参数（ShellExecute 的 lpParameters，可为空）。 */
    std::string args;
    /** 工作目录（空 = 不指定）。 */
    std::string workdir;

    /**
     * 已经在跑时先**切到那个窗口**，而不是再启动一个（默认 true）。
     *
     * 置 false 的是"本来就该多开 / 每次都要新窗口"的那类——`explorer.exe` 是典型：
     * 它的进程永远在，但用户点「文件管理器」要的是一个新窗口，不是把桌面切到前台。
     */
    bool reuseWindow = true;

    /** 自定义图标：相对 app 目录的文件名（如 `icon.png`）。空 = 用内置。 */
    std::string icon;
    /** 内置图标名（`code` / `game` / `music` / ...）。空 = 通用 `app`。 */
    std::string builtinIcon;

    /** `none` / `image` / `video`。 */
    std::string backgroundType = "none";
    /** 背景文件：相对 app 目录的文件名。 */
    std::string backgroundPath;
};

// ---- 路径 ----

/** 数据根 `%APPDATA%\GoldingCore\appmanager`（不保证存在）。 */
std::filesystem::path DataRoot();
std::filesystem::path AppsDir();
std::filesystem::path ManifestPath();
std::filesystem::path AppDir(const std::string& id);

/**
 * @brief 读清单 + 每个 `app.ini`。
 *
 * **目录不存在时建出来并播种**预置的那几个应用（见 `SeedDefaults`）——所以
 * 第一次跑起来就有一排可用的东西，而不是一个空侧栏。
 *
 * @param out    读出来的条目，顺序 = 清单顺序。
 * @param seeded 非空时写入"这一次是不是第一次播种"。
 * @return 读/建成功（清单坏了不算失败：备份成 `apps.json.bak` 再重建）。
 */
bool Load(std::vector<AppEntry>& out, bool* seeded = nullptr);

/** 写一条到 `apps/<id>/app.ini`（目录不存在会建）。 */
bool SaveEntry(const AppEntry& entry);

/** 把清单写回 `apps.json`（顺序即侧栏顺序）。 */
bool SaveManifest(const std::vector<AppEntry>& entries);

/**
 * @brief 从一个目标路径新建应用：生成 id / 建目录 / 写 ini / 追加清单。
 *
 * @param out     现有条目（会被追加）
 * @param created 非空时写入新建的那条（`id` 已经去重）
 */
bool CreateApp(const std::filesystem::path& target, std::vector<AppEntry>& out, AppEntry* created);

/**
 * @brief 把外部文件复制进 app 目录。
 *
 * @param kind `icon` / `background`（决定目标文件名与"先删掉旧的同类文件"）。
 * @return 相对文件名（如 `icon.png`）；失败返回空串。
 */
std::string CopyAsset(const std::string& id, const std::filesystem::path& source, const char* kind);

/** 删掉 app 目录里的一个相对文件（空名字是空操作）。 */
void RemoveAsset(const std::string& id, const std::string& relativeName);

/** 插件目录（`assets/icons/` 在它下面）；由插件在 `Register` 里设一次。 */
void SetPluginDir(const std::filesystem::path& directory);

/** 内置图标名 → 绝对路径。不认识 / 文件不在时退回通用 `app.svg`。 */
std::filesystem::path BuiltinIconPath(const std::string& name);

/** 这个应用该画哪个图标：自定义优先，其次内置，最后通用。可能为空（全都没有时）。 */
std::filesystem::path IconPath(const AppEntry& entry);

/** 背景文件的绝对路径（类型为 none / 空时返回空）。 */
std::filesystem::path BackgroundPath(const AppEntry& entry);

/** 启动它。@return 0 = 成功（含"切到了已有窗口"）。 */
int Launch(const AppEntry& entry);

/**
 * @brief 这个 exe 现在有几个正在运行的实例（按**完整路径**匹配，不含本进程）。
 *
 * 只读：不激活、不启动。自检用它做"进程枚举真的在工作"的观测口。
 */
int RunningInstanceCount(const std::string& exePath);

/**
 * @brief 这个 exe 有没有一个"主窗口"（可见、有标题、非工具窗）。
 *
 * 与 `ActivateRunning` 分开是因为**能不能抢到前台是环境问题**：自动化 / 后台
 * 进程会被系统的前台锁拒掉，但"窗口存在"这件事是确定的。自检用它当观测口。
 */
bool HasMainWindow(const std::string& exePath);

/**
 * @brief 把这个 exe 的一个已有窗口还原并切到前台。
 *
 * 找不到进程 / 进程没有主窗口 / 前台抢不到时都返回 false——调用方据此回退到
 * `ShellExecuteW`（"点了要有反应"比"尊重单实例"更重要）。
 */
bool ActivateRunning(const std::string& exePath);

// ---- 小工具（自检也用） ----

std::string ToUtf8(const std::filesystem::path& path);
std::filesystem::path FromUtf8(std::string_view text);

/** 路径是不是可导入的类型（exe / lnk / bat / cmd）。 */
bool IsLaunchablePath(const std::filesystem::path& path);

/** 文件名去扩展名，作为默认标题。 */
std::string TitleFromPath(const std::filesystem::path& path);

/** 扩展名是不是视频（mp4 / webm / mov / mkv / avi）。 */
bool IsVideoFile(const std::filesystem::path& path);

/** 种子表大小（自检要断言"预置了 10 个"）。 */
int SeedCount();

} // namespace appmanager
