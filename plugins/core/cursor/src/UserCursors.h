#pragma once

// 用户指针（用户自己加的指针）的**仓库层**：目录 + 清单 + 图片尺寸探测。
//
// 这一层**不碰界面**：它只回答"有哪些用户指针 / 加一个 / 改热点 / 删一个"，
// 界面在 `UserCursorUi.h`。分开是因为清单格式是**契约**，而契约不该被界面
// 的实现细节（按钮、列表、弹窗）牵着走。
//
// ---------------------------------------------------------------------------
// 格式（定案，v1）
//
// **一个用户指针 = 用户指针目录里的一张图片**（PNG / JPG / SVG）。
//
// 用户指针目录：
//   - Windows： `%APPDATA%\GoldingCore\cursors\`
//   - 其它：    `~/.golding/cursors/`
//
// 为什么不在插件目录或 exe 旁边：那两处是**程序**的地盘（更新会覆盖），
// 用户自己加的东西该跟着用户走，不该被一次升级抹掉。
//
// **清单 `cursors.index`**（与图片同目录）：一行一条，字段用 **TAB** 分隔：
//
//     pen.png <TAB> 12 <TAB> 8 [<TAB> 显示名]
//
//   - 第 1 字段：文件名（**纯文件名**，不含目录、不许 `..`）
//   - 第 2、3 字段：热点（hotX / hotY，单位=图片像素，相对图片左上角）
//   - 第 4 字段（可选）：显示名；空 = 用文件名去扩展名
//
// **为什么是 TSV 而不是 JSON**：插件层**拿不到 core 的 JSON 解析器**（core 是
// 编进宿主 exe 的静态库，不给插件链接），而 TSV 一行一条、`split` 就能用；
// 它同时是人能直接打开改的文本——"用户自己加指针"这件事，用户有权自己编辑
// 清单。JSON 在这里只是把一个能自己读的文件变成一个必须靠程序改的文件。
//
// **清单只管热点**，文件列表以**目录**为准：清单里没有的图片照样能用（热点取
// 中心），清单里有而目录里没有的条目直接忽略（文件被删了）。这样"删一个指针"
// 就是删文件，不必两处同步。
//
// 热点 = 图案里"真正指着的那一点"（箭头的笔尖、准星的中心）。默认取图片中心。

#include <filesystem>
#include <string>
#include <vector>

namespace cursor
{

/** 清单文件名（用户指针目录里）。 */
inline constexpr const char* kManifestFileName = "cursors.index";

/** 指针 id 的前缀：用户指针的 id 一律是 `user:<文件名去扩展名>`。 */
inline constexpr const char* kUserIdPrefix = "user:";

/** 一条用户指针。 */
struct UserCursor
{
    std::string id;          ///< 服务里的 id：`user:pen`
    std::string fileName;    ///< 用户指针目录里的文件名（纯文件名）
    std::string displayName; ///< 给界面看的名字
    int hotX = 0;            ///< 热点（图片像素，相对图片左上角）
    int hotY = 0;
    int width = 0;           ///< 原图像素宽
    int height = 0;          ///< 原图像素高
    std::string absolutePath; ///< 绝对路径（UTF-8）
};

/**
 * @brief 用户指针仓库：目录 + 清单。
 *
 * 线程模型：全在 UI 线程里用，没有锁。`Scan()` 每次都重新读目录与清单
 * （用户可能在外面改了文件），所以"刷新"是一次 `Scan()`。
 */
class UserCursorStore
{
public:
    explicit UserCursorStore(std::filesystem::path directory) : _directory(std::move(directory)) {}

    /**用户指针目录。*/
    const std::filesystem::path& directory() const noexcept { return _directory; }

    /**目录的 UTF-8 文本（给界面显示）。*/
    std::string directoryText() const;

    /**目录不存在就建它（含父目录）。失败时把原因写进 `error`。*/
    bool EnsureDirectory(std::string* error);

    /**
     * @brief 扫目录 + 读清单，得到当前的用户指针列表。
     *
     * 目录不存在 / 读不出来都返回**空列表**（不是错误）："还没加过指针"是
     * 正常状态。清单里指向的文件不在了，那一条直接跳过。
     */
    std::vector<UserCursor> Scan();

    /**
     * @brief 导入一张图片：复制进用户指针目录 + 在清单里加一行（热点=中心）。
     *
     * 同名文件自动加序号（`pen.png` → `pen-2.png`），**不覆盖**用户的文件。
     * 读不出尺寸（格式不支持 / 文件损坏）时**不导入**——先失败再落盘，
     * 免得目录里躺一个永远显示不出来的文件。
     */
    bool Import(const std::filesystem::path& source, UserCursor* imported, std::string* error);

    /**改热点（写清单）。清单里没有这一条就追加一条。*/
    bool SetHotspot(const std::string& fileName, int hotX, int hotY, std::string* error);

    /**删一个用户指针：删文件 + 删清单那行。文件不在时只删清单。*/
    bool Remove(const std::string& fileName, std::string* error);

    /**用户指针目录的默认位置（见文件头）。*/
    static std::filesystem::path DefaultDirectory();

private:
    std::filesystem::path _directory;
};

/**支持的图片扩展名（`.png` / `.jpg` / `.jpeg` / `.svg`，大小写不敏感）。*/
bool IsSupportedImage(const std::filesystem::path& path);

/**
 * @brief 读图片的像素尺寸（PNG / JPEG / SVG）。
 *
 * 自己解析文件头，不依赖框架：框架的 `InkingStaticImage` 只认"框多大"，
 * 不告诉你图本身多大，而**热点与 1:1 显示都要用原图尺寸**。
 *
 * @return 读不出来返回 false（格式不支持 / 文件损坏 / 读不到）。
 */
bool ProbeImageSize(const std::filesystem::path& path, int& width, int& height);

/**路径 → UTF-8 窄串。*/
std::string ToUtf8(const std::filesystem::path& path);

/**UTF-8 窄串 → 路径。*/
std::filesystem::path FromUtf8(const std::string& text);

} // namespace cursor
