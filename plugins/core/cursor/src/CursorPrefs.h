#pragma once

// 指针插件自己的**偏好**（挑的是哪只指针 / 开关 / 尺寸）：文件 + 解析。
//
// 与 `UserCursors.h` 的分工：那边管"用户自己加了哪些图"（图片 + 热点清单），
// 这边管"用户选了哪一只、三个开关是什么"。两者都不碰界面。
//
// ---------------------------------------------------------------------------
// 为什么要有它
//
// 用户挑好指针，重启程序又变回默认箭头——"选择"这件事必须落盘。之前它只活在
// 进程内存里（插件没有析构钩子，退出即丢）。
//
// ---------------------------------------------------------------------------
// 格式（定案，v1）
//
// **`cursor.ini`**，一行一对键值，字段用 **TAB** 分隔，`#` 开头是注释：
//
//     style<TAB>user:pen
//     enabled<TAB>1
//     effects<TAB>1
//     scale<TAB>1
//
//   - `style`：内置图案 id（`arrow` / `ring` / `cross` / `dot` / `spark` / `quill`）
//     或用户指针 id（`user:<文件名去扩展名>`）。空 / 缺 = 出厂默认（第一种内置图案）。
//   - `enabled`：`1` 用自定义指针、`0` 用系统指针。
//   - `effects`：`1` 按下特效（涟漪 / 拖尾）开。
//   - `scale`：内置图案的尺寸系数，钳在 `0.5..3`。
//
// **为什么还是 TSV**：插件层拿不到 core 的 JSON 解析器（core 编进宿主 exe，
// 不给插件链接）；而且它与同目录的 `cursors.index` 是同一套写法，一个文件夹里
// 不用学两种格式。人也该能直接打开改。
//
// **位置**：`%APPDATA%\GoldingCore\cursor.ini`（与 `cursors\` 同一个根；
// 其它平台 `~/.golding/cursor.ini`）。放用户目录而不是插件目录，理由同图片：
// **升级会覆盖插件目录**，用户的选择不该被一次升级抹掉。
//
// **坏值不猜**：未知键、坏行、读不出来的值一律跳过、保留默认。这是用户可编辑的
// 文件，一个手误不该让程序崩，也不该让其它三项一起回出厂。

#include <filesystem>
#include <string>

namespace cursor
{

/**偏好文件名。*/
inline constexpr const char* kPrefsFileName = "cursor.ini";

/**一份偏好。默认值 = 出厂状态。*/
struct CursorPrefs
{
    /**当前指针的 id（内置图案 id 或 `user:xxx`）；空 = 出厂默认。*/
    std::string styleId;
    bool enabled = true;
    bool effects = true;
    float scale = 1.0f;
};

/**把偏好写成文件文本（含文件头注释）。输出稳定，同一份偏好永远同一个文件内容。*/
std::string FormatPrefs(const CursorPrefs& prefs);

/**
 * @brief 把文件文本解析成偏好。
 *
 * 缺省字段保留默认值；未知键 / 坏行 / 坏值跳过（不中止、不猜）。
 */
CursorPrefs ParsePrefs(const std::string& text);

/**
 * @brief 偏好文件的读写。
 *
 * 线程模型：全在 UI 线程里用。读写在用户动作之后（低频），没有缓存——"用户在
 * 外面改了这个文件"下次读自然生效。
 */
class CursorPrefsStore
{
public:
    explicit CursorPrefsStore(std::filesystem::path file) : _file(std::move(file)) {}

    const std::filesystem::path& path() const noexcept { return _file; }
    std::string pathText() const;

    /**
     * @brief 读文件。
     *
     * 文件不存在 = **默认偏好**，不算错误（还没挑过）。真正读不出来
     * （权限 / IO）才返回 false 并写 `error`。
     */
    bool Load(CursorPrefs* prefs, std::string* error);

    /**写文件（目录不存在就建）。`error` 可为空。*/
    bool Save(const CursorPrefs& prefs, std::string* error);

    /**默认位置（见文件头）。*/
    static std::filesystem::path DefaultFile();

private:
    std::filesystem::path _file;
};

} // namespace cursor
