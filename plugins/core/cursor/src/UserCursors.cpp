// 用户指针仓库的实现：目录 + 清单（TSV）+ 图片尺寸探测。
//
// 见 `UserCursors.h` 顶上的格式说明——那里的约定是**契约**，这里是它的一份实现。

#include "UserCursors.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <Windows.h>
#include <shlobj.h>
#endif

namespace cursor
{

namespace
{

/**清单的一行。*/
struct ManifestEntry
{
    std::string fileName;
    int hotX = 0;
    int hotY = 0;
    std::string displayName;
};

/**文件名是不是"纯文件名"（不含目录、不许 `..`）。清单是用户可编辑的文本，路径穿越必须挡。*/
bool IsPlainFileName(const std::string& name)
{
    if (name.empty() || name == "." || name == "..")
    {
        return false;
    }
    return name.find('/') == std::string::npos && name.find('\\') == std::string::npos &&
           name.find(':') == std::string::npos;
}

/**扩展名转小写（含点，如 ".png"）。*/
std::string LowerExtension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

/**去掉扩展名的文件名（"pen.png" → "pen"）。*/
std::string FileStem(const std::string& fileName)
{
    const std::size_t dot = fileName.rfind('.');
    return (dot == std::string::npos) ? fileName : fileName.substr(0, dot);
}

/**插入序号（"pen.png" + 2 → "pen-2.png"）。*/
std::string WithSuffix(const std::string& fileName, int suffix)
{
    const std::size_t dot = fileName.rfind('.');
    if (dot == std::string::npos)
    {
        return fileName + "-" + std::to_string(suffix);
    }
    return fileName.substr(0, dot) + "-" + std::to_string(suffix) + fileName.substr(dot);
}

/**读整个文件（失败返回 false）。*/
bool ReadAllBytes(const std::filesystem::path& path, std::vector<unsigned char>& bytes)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        return false;
    }
    stream.seekg(0, std::ios::end);
    const std::streamoff size = stream.tellg();
    if (size < 0)
    {
        return false;
    }
    stream.seekg(0, std::ios::beg);
    bytes.resize(static_cast<std::size_t>(size));
    if (!bytes.empty())
    {
        stream.read(reinterpret_cast<char*>(bytes.data()), size);
    }
    return static_cast<bool>(stream) || bytes.empty();
}

/**读文件开头的一段（给 SVG 用：它可能很大）。*/
bool ReadHead(const std::filesystem::path& path, std::size_t count, std::string& text)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        return false;
    }
    std::vector<char> buffer(count);
    stream.read(buffer.data(), static_cast<std::streamsize>(count));
    text.assign(buffer.data(), static_cast<std::size_t>(stream.gcount()));
    return true;
}

/**大端 u32（PNG 用）。*/
std::uint32_t ReadBigU32(const std::vector<unsigned char>& bytes, std::size_t offset)
{
    if (offset + 4 > bytes.size())
    {
        return 0;
    }
    return (static_cast<std::uint32_t>(bytes[offset + 0]) << 24) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
           static_cast<std::uint32_t>(bytes[offset + 3]);
}

/**大端 u16（JPEG 用）。*/
int ReadBigU16(const std::vector<unsigned char>& bytes, std::size_t offset)
{
    if (offset + 2 > bytes.size())
    {
        return 0;
    }
    return (static_cast<int>(bytes[offset + 0]) << 8) | static_cast<int>(bytes[offset + 1]);
}

/**从 SVG 的根标签里取 width / height（单位当 px；取不到就用 viewBox）。*/
bool ParseSvgSize(const std::string& text, int& width, int& height)
{
    const std::size_t start = text.find("<svg");
    if (start == std::string::npos)
    {
        return false;
    }
    const std::size_t end = text.find('>', start);
    const std::string tag = (end == std::string::npos) ? text.substr(start) : text.substr(start, end - start);

    const auto numberAfter = [&tag](const char* key, int& out) {
        const std::size_t at = tag.find(key);
        if (at == std::string::npos)
        {
            return false;
        }
        std::size_t cursor = at + std::strlen(key);
        while (cursor < tag.size() && (tag[cursor] == '"' || tag[cursor] == '\'' || tag[cursor] == ' '))
        {
            ++cursor;
        }
        const int value = std::atoi(tag.c_str() + cursor);
        if (value <= 0)
        {
            return false;
        }
        out = value;
        return true;
    };

    int svgWidth = 0;
    int svgHeight = 0;
    const bool hasWidth = numberAfter("width=", svgWidth);
    const bool hasHeight = numberAfter("height=", svgHeight);
    if (hasWidth && hasHeight)
    {
        width = svgWidth;
        height = svgHeight;
        return true;
    }

    // 退到 viewBox="minX minY w h"。
    const std::size_t viewBox = tag.find("viewBox=");
    if (viewBox == std::string::npos)
    {
        return false;
    }
    std::size_t cursor = viewBox + std::strlen("viewBox=");
    while (cursor < tag.size() && (tag[cursor] == '"' || tag[cursor] == '\'' || tag[cursor] == ' '))
    {
        ++cursor;
    }
    double values[4] = {0.0, 0.0, 0.0, 0.0};
    for (double& value : values)
    {
        if (cursor >= tag.size() || tag[cursor] == '"' || tag[cursor] == '\'')
        {
            return false;
        }
        value = std::strtod(tag.c_str() + cursor, nullptr);
        while (cursor < tag.size() && tag[cursor] != ' ' && tag[cursor] != '"' && tag[cursor] != '\'')
        {
            ++cursor;
        }
    }
    if (values[2] <= 0.0 || values[3] <= 0.0)
    {
        return false;
    }
    width = static_cast<int>(values[2]);
    height = static_cast<int>(values[3]);
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// 路径与编码
// ---------------------------------------------------------------------------

std::string ToUtf8(const std::filesystem::path& path)
{
#if defined(__cpp_char8_t)
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
#else
    return path.string();
#endif
}

std::filesystem::path FromUtf8(const std::string& text)
{
#if defined(__cpp_char8_t)
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
#else
    return std::filesystem::path(text);
#endif
}

// ---------------------------------------------------------------------------
// 图片尺寸探测
// ---------------------------------------------------------------------------

bool IsSupportedImage(const std::filesystem::path& path)
{
    const std::string extension = LowerExtension(path);
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".svg";
}

bool ProbeImageSize(const std::filesystem::path& path, int& width, int& height)
{
    width = 0;
    height = 0;

    const std::string extension = LowerExtension(path);
    if (extension == ".svg")
    {
        std::string head;
        if (!ReadHead(path, 16384, head))
        {
            return false;
        }
        return ParseSvgSize(head, width, height);
    }

    std::vector<unsigned char> bytes;
    if (!ReadAllBytes(path, bytes) || bytes.size() < 24)
    {
        return false;
    }

    if (extension == ".png")
    {
        static const unsigned char kSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        if (std::memcmp(bytes.data(), kSignature, sizeof(kSignature)) != 0)
        {
            return false;
        }
        width = static_cast<int>(ReadBigU32(bytes, 16));
        height = static_cast<int>(ReadBigU32(bytes, 20));
        return width > 0 && height > 0;
    }

    if (extension == ".jpg" || extension == ".jpeg")
    {
        if (bytes[0] != 0xFF || bytes[1] != 0xD8)
        {
            return false;
        }
        // 逐段走：找 SOFn（帧头），那里有宽高。其它段按长度跳过。
        std::size_t cursor = 2;
        while (cursor + 3 < bytes.size())
        {
            if (bytes[cursor] != 0xFF)
            {
                ++cursor;
                continue;
            }
            const unsigned char marker = bytes[cursor + 1];
            if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7))
            {
                cursor += 2;  // 独立标记，没有长度
                continue;
            }
            if (marker == 0xD9 || marker == 0xDA)
            {
                break;  // 图像数据开始，后面没有尺寸了
            }
            const int segmentLength = ReadBigU16(bytes, cursor + 2);
            if (segmentLength < 2)
            {
                return false;
            }
            const bool isFrameHeader = (marker >= 0xC0 && marker <= 0xCF) && marker != 0xC4 && marker != 0xC8 &&
                                       marker != 0xCC;
            if (isFrameHeader)
            {
                height = ReadBigU16(bytes, cursor + 5);
                width = ReadBigU16(bytes, cursor + 7);
                return width > 0 && height > 0;
            }
            cursor += 2 + static_cast<std::size_t>(segmentLength);
        }
        return false;
    }

    return false;
}

// ---------------------------------------------------------------------------
// 清单读写
// ---------------------------------------------------------------------------

namespace
{

/**读清单。文件不在 = 空清单（不是错误）。*/
std::vector<ManifestEntry> ReadManifest(const std::filesystem::path& directory)
{
    std::vector<ManifestEntry> entries;
    std::ifstream stream(directory / FromUtf8(kManifestFileName));
    if (!stream)
    {
        return entries;
    }
    std::string line;
    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();  // CRLF
        }
        if (line.empty() || line[0] == '#')
        {
            continue;
        }
        std::vector<std::string> fields;
        std::size_t start = 0;
        while (true)
        {
            const std::size_t tab = line.find('\t', start);
            if (tab == std::string::npos)
            {
                fields.push_back(line.substr(start));
                break;
            }
            fields.push_back(line.substr(start, tab - start));
            start = tab + 1;
        }
        if (fields.size() < 3)
        {
            continue;  // 坏行：跳过，不猜
        }
        if (!IsPlainFileName(fields[0]))
        {
            continue;  // 路径穿越 / 非法名：跳过
        }
        ManifestEntry entry;
        entry.fileName = fields[0];
        entry.hotX = std::atoi(fields[1].c_str());
        entry.hotY = std::atoi(fields[2].c_str());
        if (fields.size() > 3 && !fields[3].empty())
        {
            entry.displayName = fields[3];
        }
        entries.push_back(entry);
    }
    return entries;
}

/**写清单（按文件名排序，输出稳定）。*/
bool WriteManifest(const std::filesystem::path& directory, std::vector<ManifestEntry> entries,
                   std::string* error)
{
    std::sort(entries.begin(), entries.end(),
              [](const ManifestEntry& left, const ManifestEntry& right) {
                  return left.fileName < right.fileName;
              });

    const std::filesystem::path target = directory / FromUtf8(kManifestFileName);
    std::ofstream stream(target, std::ios::binary | std::ios::trunc);
    if (!stream)
    {
        if (error != nullptr)
        {
            *error = "写不进清单：" + ToUtf8(target);
        }
        return false;
    }
    stream << "# cursors.index —— 用户指针的热点表（每行：文件名 <TAB> hotX <TAB> hotY [<TAB> 显示名]）\n";
    for (const ManifestEntry& entry : entries)
    {
        stream << entry.fileName << '\t' << entry.hotX << '\t' << entry.hotY;
        if (!entry.displayName.empty())
        {
            stream << '\t' << entry.displayName;
        }
        stream << '\n';
    }
    return static_cast<bool>(stream);
}

} // namespace

// ---------------------------------------------------------------------------
// 仓库
// ---------------------------------------------------------------------------

std::filesystem::path UserCursorStore::DefaultDirectory()
{
#ifdef _WIN32
    // 漫游应用数据（%APPDATA%）：用户自己加的东西归用户，不进程序目录
    //（程序目录会被更新覆盖，用户的东西不该跟着被抹掉）。
    //
    // 用 GUID 字面量而不是 `FOLDERID_RoamingAppData`：MinGW 的 `shlobj.h`
    // 只在某些 `_WIN32_WINNT` 下才暴露那个常量，而这个值是**稳定的接口**。
    static const GUID kRoamingAppData = {0x3eb685db, 0x65f9, 0x4cf6,
                                         {0xa0, 0x3a, 0x0e, 0x3d, 0x47, 0x6a, 0x19, 0x48}};
    PWSTR raw = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(kRoamingAppData, 0, nullptr, &raw)) && raw != nullptr)
    {
        const std::filesystem::path result = std::filesystem::path(raw) / "GoldingCore" / "cursors";
        CoTaskMemFree(raw);
        return result;
    }
    // 拿不到就退到环境变量（**宽字符**版：窄串在中文目录下会抛，
    // 见 InkingUIKit 的 AGENTS.md §6 第 53 条）。
    if (const wchar_t* roaming = _wgetenv(L"APPDATA"))
    {
        return std::filesystem::path(roaming) / "GoldingCore" / "cursors";
    }
#endif
    if (const char* home = std::getenv("HOME"))
    {
        return std::filesystem::path(FromUtf8(std::string(home))) / ".golding" / "cursors";
    }
    // 兜底：当前工作目录。难看，但总有个地方可写。
    return std::filesystem::current_path() / "cursors";
}

std::string UserCursorStore::directoryText() const
{
    return ToUtf8(_directory);
}

bool UserCursorStore::EnsureDirectory(std::string* error)
{
    std::error_code code;
    if (std::filesystem::is_directory(_directory, code))
    {
        return true;
    }
    std::filesystem::create_directories(_directory, code);
    if (code)
    {
        if (error != nullptr)
        {
            *error = "建不出用户指针目录：" + ToUtf8(_directory) + "（" + code.message() + "）";
        }
        return false;
    }
    return true;
}

std::vector<UserCursor> UserCursorStore::Scan()
{
    std::vector<UserCursor> result;
    std::error_code code;
    if (!std::filesystem::is_directory(_directory, code))
    {
        return result;
    }

    const std::vector<ManifestEntry> manifest = ReadManifest(_directory);

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(_directory, code))
    {
        std::error_code entryError;
        if (!entry.is_regular_file(entryError) || !IsSupportedImage(entry.path()))
        {
            continue;
        }
        const std::string fileName = ToUtf8(entry.path().filename());

        UserCursor item;
        item.fileName = fileName;
        item.absolutePath = ToUtf8(entry.path());
        item.id = std::string(kUserIdPrefix) + FileStem(fileName);
        item.displayName = FileStem(fileName);

        // 清单里有的条目覆盖默认值（热点与显示名）。
        for (const ManifestEntry& manifestEntry : manifest)
        {
            if (manifestEntry.fileName == fileName)
            {
                item.hotX = manifestEntry.hotX;
                item.hotY = manifestEntry.hotY;
                if (!manifestEntry.displayName.empty())
                {
                    item.displayName = manifestEntry.displayName;
                }
                break;
            }
        }

        int width = 0;
        int height = 0;
        if (ProbeImageSize(entry.path(), width, height))
        {
            item.width = width;
            item.height = height;
        }
        else
        {
            // 读不出尺寸 = 这张图我们用不了（显示不出）。跳过，不进列表。
            continue;
        }
        if (!item.absolutePath.empty())
        {
            result.push_back(item);
        }
    }

    std::sort(result.begin(), result.end(),
              [](const UserCursor& left, const UserCursor& right) { return left.displayName < right.displayName; });
    return result;
}

bool UserCursorStore::Import(const std::filesystem::path& source, UserCursor* imported, std::string* error)
{
    std::error_code code;
    if (!std::filesystem::is_regular_file(source, code))
    {
        if (error != nullptr)
        {
            *error = "不是一个文件：" + ToUtf8(source);
        }
        return false;
    }
    if (!IsSupportedImage(source))
    {
        if (error != nullptr)
        {
            *error = "只支持 PNG / JPG / SVG";
        }
        return false;
    }

    int width = 0;
    int height = 0;
    if (!ProbeImageSize(source, width, height))
    {
        if (error != nullptr)
        {
            *error = "读不出图片尺寸（格式不支持或文件损坏）：" + ToUtf8(source.filename());
        }
        return false;
    }

    if (!EnsureDirectory(error))
    {
        return false;
    }

    // 同名不覆盖：加序号。
    std::string fileName = ToUtf8(source.filename());
    int suffix = 2;
    while (std::filesystem::exists(_directory / FromUtf8(fileName), code))
    {
        fileName = WithSuffix(ToUtf8(source.filename()), suffix++);
    }

    const std::filesystem::path target = _directory / FromUtf8(fileName);
    std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, code);
    if (code)
    {
        if (error != nullptr)
        {
            *error = "复制失败：" + code.message();
        }
        return false;
    }

    // 清单：热点默认取中心。
    std::vector<ManifestEntry> manifest = ReadManifest(_directory);
    bool replaced = false;
    for (ManifestEntry& entry : manifest)
    {
        if (entry.fileName == fileName)
        {
            entry.hotX = width / 2;
            entry.hotY = height / 2;
            replaced = true;
            break;
        }
    }
    if (!replaced)
    {
        manifest.push_back(ManifestEntry{fileName, width / 2, height / 2, ""});
    }
    if (!WriteManifest(_directory, manifest, error))
    {
        // 清单写不进去就把刚复制来的文件撤掉：不留一个"有图没条目"的孤儿。
        std::error_code removeError;
        std::filesystem::remove(target, removeError);
        return false;
    }

    if (imported != nullptr)
    {
        imported->id = std::string(kUserIdPrefix) + FileStem(fileName);
        imported->fileName = fileName;
        imported->displayName = FileStem(fileName);
        imported->hotX = width / 2;
        imported->hotY = height / 2;
        imported->width = width;
        imported->height = height;
        imported->absolutePath = ToUtf8(target);
    }
    return true;
}

bool UserCursorStore::SetHotspot(const std::string& fileName, int hotX, int hotY, std::string* error)
{
    if (!IsPlainFileName(fileName))
    {
        if (error != nullptr)
        {
            *error = "非法的文件名";
        }
        return false;
    }
    std::vector<ManifestEntry> manifest = ReadManifest(_directory);
    bool found = false;
    for (ManifestEntry& entry : manifest)
    {
        if (entry.fileName == fileName)
        {
            entry.hotX = hotX;
            entry.hotY = hotY;
            found = true;
            break;
        }
    }
    if (!found)
    {
        manifest.push_back(ManifestEntry{fileName, hotX, hotY, ""});
    }
    return WriteManifest(_directory, manifest, error);
}

bool UserCursorStore::Remove(const std::string& fileName, std::string* error)
{
    if (!IsPlainFileName(fileName))
    {
        if (error != nullptr)
        {
            *error = "非法的文件名";
        }
        return false;
    }

    std::error_code code;
    const std::filesystem::path target = _directory / FromUtf8(fileName);
    if (std::filesystem::exists(target, code))
    {
        std::filesystem::remove(target, code);
        if (code)
        {
            if (error != nullptr)
            {
                *error = "删不掉文件：" + code.message();
            }
            return false;
        }
    }

    std::vector<ManifestEntry> manifest = ReadManifest(_directory);
    manifest.erase(std::remove_if(manifest.begin(), manifest.end(),
                                  [&fileName](const ManifestEntry& entry) { return entry.fileName == fileName; }),
                   manifest.end());
    return WriteManifest(_directory, manifest, error);
}

} // namespace cursor
