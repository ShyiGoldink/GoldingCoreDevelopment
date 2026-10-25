#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "AppStore.h"

#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <fstream>
#include <map>
#include <system_error>
#include <vector>

namespace appmanager
{

namespace
{

/** 插件目录（`assets/icons/` 在它下面）；`SetPluginDir` 写，`BuiltinIconPath` 读。 */
std::filesystem::path g_pluginDir;

std::wstring ExpandEnv(const wchar_t* text)
{
    wchar_t buffer[2048];
    const DWORD size = ExpandEnvironmentStringsW(text, buffer, static_cast<DWORD>(std::size(buffer)));
    if (size == 0)
    {
        return text;
    }
    if (size > std::size(buffer))
    {
        std::wstring big(size, L'\0');
        const DWORD again = ExpandEnvironmentStringsW(text, big.data(), size);
        if (again == 0)
        {
            return text;
        }
        big.resize(again > 0 ? again - 1 : 0);
        return big;
    }
    return std::wstring(buffer, size - 1);
}

std::wstring EnvOrEmpty(const wchar_t* name)
{
    wchar_t buffer[2048];
    const DWORD size = GetEnvironmentVariableW(name, buffer, static_cast<DWORD>(std::size(buffer)));
    return size > 0 && size < std::size(buffer) ? std::wstring(buffer, size) : std::wstring{};
}

std::string LowerExtension(const std::filesystem::path& path)
{
    std::string extension = ToUtf8(path.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

/** 读一份 ini（`key=value`，`#` 开头是注释）。不存在返回空 map，不报错。 */
std::map<std::string, std::string> ReadIni(const std::filesystem::path& file)
{
    std::map<std::string, std::string> values;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec))
    {
        return values;
    }
    std::ifstream stream(file, std::ios::binary);
    if (!stream)
    {
        return values;
    }
    std::string line;
    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#')
        {
            continue;
        }
        const std::size_t split = line.find('=');
        if (split == std::string::npos)
        {
            continue;
        }
        std::string key = line.substr(0, split);
        std::string value = line.substr(split + 1);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t'))
        {
            key.pop_back();
        }
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
        {
            value.erase(value.begin());
        }
        if (!key.empty())
        {
            values[key] = value;
        }
    }
    return values;
}

void WriteIni(const std::filesystem::path& file, const std::map<std::string, std::string>& values)
{
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream stream(file, std::ios::binary | std::ios::trunc);
    if (!stream)
    {
        std::printf("[appmanager] 写不进 %s\n", ToUtf8(file).c_str());
        return;
    }
    stream << "# appmanager 的单应用配置（key=value，UTF-8）\n";
    for (const auto& [key, value] : values)
    {
        stream << key << '=' << value << '\n';
    }
}

/** 极简清单读：在 `"apps"` 之后找 `[` … `]`，逐个取引号串。够用即可。 */
std::vector<std::string> ReadManifestIds(const std::filesystem::path& file)
{
    std::vector<std::string> ids;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec))
    {
        return ids;
    }
    std::ifstream stream(file, std::ios::binary);
    if (!stream)
    {
        return ids;
    }
    const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());

    const std::size_t appsKey = text.find("\"apps\"");
    if (appsKey == std::string::npos)
    {
        return ids;
    }
    const std::size_t begin = text.find('[', appsKey);
    if (begin == std::string::npos)
    {
        return ids;
    }
    const std::size_t end = text.find(']', begin);
    if (end == std::string::npos)
    {
        return ids;
    }

    std::size_t index = begin + 1;
    while (index < end)
    {
        const std::size_t quote = text.find('"', index);
        if (quote == std::string::npos || quote >= end)
        {
            break;
        }
        std::string id;
        std::size_t cursor = quote + 1;
        while (cursor < end && text[cursor] != '"')
        {
            if (text[cursor] == '\\' && cursor + 1 < end)
            {
                ++cursor;
            }
            id.push_back(text[cursor]);
            ++cursor;
        }
        if (!id.empty())
        {
            ids.push_back(id);
        }
        index = cursor + 1;
    }
    return ids;
}

bool WriteManifestIds(const std::filesystem::path& file, const std::vector<std::string>& ids)
{
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream stream(file, std::ios::binary | std::ios::trunc);
    if (!stream)
    {
        return false;
    }
    stream << "{\n  \"version\": 1,\n  \"apps\": [";
    for (std::size_t index = 0; index < ids.size(); ++index)
    {
        stream << (index == 0 ? "" : ", ") << '"' << ids[index] << '"';
    }
    stream << "]\n}\n";
    return static_cast<bool>(stream);
}

/** 把一个中文 / 带空格的文件名压成 ASCII slug；压不出来就用 `app`。 */
std::string SlugFrom(const std::string& text)
{
    std::string slug;
    for (const char raw : text)
    {
        const unsigned char c = static_cast<unsigned char>(raw);
        if (c >= 'a' && c <= 'z')
        {
            slug.push_back(static_cast<char>(c));
        }
        else if (c >= 'A' && c <= 'Z')
        {
            slug.push_back(static_cast<char>(c - 'A' + 'a'));
        }
        else if (c >= '0' && c <= '9')
        {
            slug.push_back(static_cast<char>(c));
        }
        else if ((c == '-' || c == '_' || c == ' ') && !slug.empty() && slug.back() != '-')
        {
            slug.push_back('-');
        }
    }
    while (!slug.empty() && slug.back() == '-')
    {
        slug.pop_back();
    }
    if (slug.size() > 40)
    {
        slug.resize(40);
    }
    return slug.empty() ? std::string("app") : slug;
}

std::string UniqueId(const std::vector<AppEntry>& entries, const std::string& base)
{
    auto taken = [&entries](const std::string& candidate) {
        return std::any_of(entries.begin(), entries.end(),
                           [&candidate](const AppEntry& entry) { return entry.id == candidate; });
    };
    if (!taken(base))
    {
        return base;
    }
    for (int suffix = 2; suffix < 1000; ++suffix)
    {
        const std::string candidate = base + '-' + std::to_string(suffix);
        if (!taken(candidate))
        {
            return candidate;
        }
    }
    return base + "-x";
}

/** 播种表的一条。`targets` 以 nullptr 结尾，按顺序取第一个存在的。 */
struct SeedSpec
{
    const char* id;
    const char* title;
    const char* icon;
    /** 已在运行时要不要切到已有窗口（`explorer` 例外：它总是新开一个窗口）。 */
    bool reuseWindow;
    const wchar_t* targets[4];
};

// 预置的十项。路径是**候选**（本机装在哪各不一样），一个都不在就留空，
// 由用户在设置里指——不猜、也不拿一个不存在的路径去启动。
const SeedSpec kSeeds[] = {
    {"vscode", "VS Code", "code", true,
     {L"%LOCALAPPDATA%\\Programs\\Microsoft VS Code\\Code.exe", L"C:\\Program Files\\Microsoft VS Code\\Code.exe",
      nullptr, nullptr}},
    {"wegame", "WeGame", "game", true,
     {L"C:\\Program Files (x86)\\WeGame\\wegame.exe", L"D:\\WeGame\\wegame.exe",
      L"%LOCALAPPDATA%\\Programs\\WeGame\\wegame.exe", nullptr}},
    {"deepseek-harness", "DeepSeek Harness", "terminal", true,
     {L"D:\\BlueFatFish\\DeepSeek Harness.exe",
      L"%LOCALAPPDATA%\\Programs\\DeepSeek Harness\\DeepSeek Harness.exe", nullptr, nullptr}},
    {"workbuddy", "WorkBuddy", "app", true,
     {L"D:\\WorkBuddy\\WorkBuddy.exe", L"%LOCALAPPDATA%\\Programs\\WorkBuddy\\WorkBuddy.exe", nullptr, nullptr}},
    {"netease-cloudmusic", "网易云音乐", "music", true,
     {L"C:\\Program Files\\NetEase\\CloudMusic\\cloudmusic.exe",
      L"C:\\Program Files (x86)\\Netease\\CloudMusic\\cloudmusic.exe", nullptr, nullptr}},
    {"bilibili", "哔哩哔哩", "video", true,
     {L"C:\\Program Files\\bilibili\\哔哩哔哩.exe", L"%LOCALAPPDATA%\\Programs\\bilibili\\哔哩哔哩.exe",
      nullptr, nullptr}},
    // explorer 的进程**永远在**（它就是桌面）：切前台等于切到桌面，不是用户要的，
    // 所以这一项每次都新开一个资源管理器窗口。
    {"explorer", "文件管理器", "folder", false, {L"C:\\Windows\\explorer.exe", nullptr, nullptr, nullptr}},
    {"weixin", "微信", "chat", true,
     {L"C:\\Program Files\\Tencent\\Weixin\\Weixin.exe", L"C:\\Program Files (x86)\\Tencent\\WeChat\\WeChat.exe",
      nullptr, nullptr}},
    {"qq", "QQ", "chat", true,
     {L"D:\\Tencent\\QQ\\QQ.exe", L"C:\\Program Files\\Tencent\\QQNT\\QQ.exe", nullptr, nullptr}},
    {"browser", "浏览器", "globe", true,
     {L"C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe",
      L"C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe", L"C:\\Program Files\\Mozilla Firefox\\firefox.exe",
      nullptr}},
};

/** 把第一条存在的候选路径填进 entry.target。 */
void ResolveSeedTarget(AppEntry& entry, const SeedSpec& spec)
{
    std::error_code ec;
    for (const wchar_t* candidate : spec.targets)
    {
        if (candidate == nullptr)
        {
            break;
        }
        const std::filesystem::path expanded(ExpandEnv(candidate));
        if (std::filesystem::is_regular_file(expanded, ec) && !ec)
        {
            entry.target = ToUtf8(expanded);
            return;
        }
    }
    entry.target.clear();
}

bool SeedDefaults(std::vector<AppEntry>& out)
{
    std::error_code ec;
    std::filesystem::create_directories(AppsDir(), ec);

    for (const SeedSpec& spec : kSeeds)
    {
        AppEntry entry;
        entry.id = spec.id;
        entry.title = spec.title;
        entry.builtinIcon = spec.icon;
        entry.reuseWindow = spec.reuseWindow;
        ResolveSeedTarget(entry, spec);
        if (entry.target.empty())
        {
            std::printf("[appmanager] 预置 %s 没找到安装路径（可在它的设置里指定）\n", spec.id);
        }
        SaveEntry(entry);
        out.push_back(entry);
    }
    return SaveManifest(out);
}

/** 删掉 app 目录里 `kind.*` 的旧文件（换图标 / 换背景时同名不同扩展要清掉）。 */
void RemoveOldKind(const std::filesystem::path& dir, const std::string& kind)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec))
    {
        return;
    }
    for (const std::filesystem::directory_entry& item : std::filesystem::directory_iterator(dir, ec))
    {
        if (ec)
        {
            return;
        }
        const std::string name = ToUtf8(item.path().filename());
        if (name.rfind(kind + '.', 0) == 0)
        {
            std::filesystem::remove(item.path(), ec);
        }
    }
}

// ---- 进程与窗口（"进已有进程"的全部实现） ----
//
// 为什么按**完整路径**而不是进程名匹配：`Code.exe` 可能是别的软件，而我们要的是
// 用户点的那一个。`QueryFullProcessImageNameW` 给的是绝对路径，两边都比小写。
//
// 为什么要遍历**所有**匹配的 pid：像 VS Code 那样一个应用会有主进程 + 渲染进程，
// 渲染进程没有主窗口。只取第一个匹配常常落到无窗口的那个上，表现出来就是
// "点了没反应、又开了一个"。

bool EqualsNoCase(const std::wstring& left, const std::wstring& right)
{
    return left.size() == right.size() && _wcsicmp(left.c_str(), right.c_str()) == 0;
}

std::vector<unsigned long> FindProcesses(const std::wstring& exePath)
{
    std::vector<unsigned long> pids;
    if (exePath.empty())
    {
        return pids;
    }
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return pids;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    const DWORD self = GetCurrentProcessId();
    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            if (entry.th32ProcessID == self)
            {
                continue;
            }
            // 只查路径：权限不够的进程直接跳过（系统进程里中文路径也不罕见）。
            const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
            if (process == nullptr)
            {
                continue;
            }
            wchar_t buffer[1024];
            DWORD size = static_cast<DWORD>(std::size(buffer));
            if (QueryFullProcessImageNameW(process, 0, buffer, &size) != FALSE &&
                EqualsNoCase(std::wstring(buffer, size), exePath))
            {
                pids.push_back(entry.th32ProcessID);
            }
            CloseHandle(process);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return pids;
}

/** 某个进程的"主窗口"：可见、无 owner、不是工具窗、有标题。 */
HWND FindMainWindow(unsigned long pid)
{
    struct Context
    {
        unsigned long pid;
        HWND found;
    };
    Context context{pid, nullptr};
    EnumWindows(
        [](HWND window, LPARAM lparam) -> BOOL {
            Context* ctx = reinterpret_cast<Context*>(lparam);
            unsigned long windowPid = 0;
            GetWindowThreadProcessId(window, &windowPid);
            if (windowPid != ctx->pid || IsWindowVisible(window) == FALSE)
            {
                return TRUE;
            }
            if (GetWindow(window, GW_OWNER) != nullptr)
            {
                return TRUE; // 拥有者窗口是对话框之类，不是主窗
            }
            const LONG_PTR exStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
            if ((exStyle & WS_EX_TOOLWINDOW) != 0)
            {
                return TRUE;
            }
            if (GetWindowTextLengthW(window) == 0)
            {
                return TRUE; // 无标题的辅助窗（托盘 / 隐层）
            }
            ctx->found = window;
            return FALSE;
        },
        reinterpret_cast<LPARAM>(&context));
    return context.found;
}

/** 还原 + 切前台。抢不到前台时临时挂一次输入队列（Windows 的前台锁常规绕法）。 */
bool FocusWindow(HWND window)
{
    if (window == nullptr)
    {
        return false;
    }
    if (IsIconic(window) != FALSE)
    {
        ShowWindow(window, SW_RESTORE);
    }
    else
    {
        ShowWindow(window, SW_SHOW);
    }
    if (SetForegroundWindow(window) != FALSE)
    {
        return true;
    }

    const DWORD targetThread = GetWindowThreadProcessId(window, nullptr);
    const DWORD thisThread = GetCurrentThreadId();
    bool attached = false;
    if (targetThread != 0 && targetThread != thisThread)
    {
        attached = AttachThreadInput(thisThread, targetThread, TRUE) != FALSE;
    }
    SetForegroundWindow(window);
    BringWindowToTop(window);
    if (attached)
    {
        AttachThreadInput(thisThread, targetThread, FALSE);
    }
    if (GetForegroundWindow() == window)
    {
        return true;
    }

    // 最后一条兜底：短暂置顶。拿不到焦点时（后台进程抢前台会被系统拒绝）
    // 至少把窗口提到最前面，用户看得见、点得到。
    SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetWindowPos(window, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    BringWindowToTop(window);
    return GetForegroundWindow() == window;
}

} // namespace

// ---- 路径 ----

std::filesystem::path DataRoot()
{
    std::filesystem::path root(EnvOrEmpty(L"APPDATA"));
    if (root.empty())
    {
        std::error_code ec;
        root = std::filesystem::temp_directory_path(ec);
    }
    return root / L"GoldingCore" / L"appmanager";
}

std::filesystem::path AppsDir() { return DataRoot() / L"apps"; }

std::filesystem::path ManifestPath() { return DataRoot() / L"apps.json"; }

std::filesystem::path AppDir(const std::string& id) { return AppsDir() / FromUtf8(id); }

// ---- 读写 ----

bool Load(std::vector<AppEntry>& out, bool* seeded)
{
    out.clear();
    if (seeded != nullptr)
    {
        *seeded = false;
    }

    std::error_code ec;
    std::filesystem::create_directories(AppsDir(), ec);

    std::vector<std::string> ids = ReadManifestIds(ManifestPath());
    if (ids.empty())
    {
        // 没有清单 = 第一次跑（或清单被手删了）。备份可疑文件再播种。
        if (std::filesystem::is_regular_file(ManifestPath(), ec))
        {
            std::filesystem::rename(ManifestPath(), ManifestPath().replace_extension(L".json.bak"), ec);
        }
        if (!SeedDefaults(out))
        {
            return false;
        }
        if (seeded != nullptr)
        {
            *seeded = true;
        }
        return true;
    }

    for (const std::string& id : ids)
    {
        const std::map<std::string, std::string> values = ReadIni(AppDir(id) / L"app.ini");
        AppEntry entry;
        entry.id = id;
        auto read = [&values](const char* key) {
            const auto found = values.find(key);
            return found == values.end() ? std::string{} : found->second;
        };
        entry.title = read("title");
        if (entry.title.empty())
        {
            entry.title = id;
        }
        entry.target = read("target");
        entry.args = read("args");
        entry.workdir = read("workdir");
        entry.icon = read("icon");
        entry.builtinIcon = read("builtin");
        // 缺省 = 切到已有窗口（老配置 / 手写 ini 没这个键时也走新行为）。
        entry.reuseWindow = read("reuse") != "0";
        entry.backgroundType = read("background.type");
        if (entry.backgroundType.empty())
        {
            entry.backgroundType = "none";
        }
        entry.backgroundPath = read("background.path");
        out.push_back(std::move(entry));
    }
    return true;
}

bool SaveEntry(const AppEntry& entry)
{
    std::error_code ec;
    std::filesystem::create_directories(AppDir(entry.id), ec);
    std::map<std::string, std::string> values;
    values["title"] = entry.title;
    values["target"] = entry.target;
    values["args"] = entry.args;
    values["workdir"] = entry.workdir;
    values["icon"] = entry.icon;
    values["builtin"] = entry.builtinIcon.empty() ? "app" : entry.builtinIcon;
    values["background.type"] = entry.backgroundType.empty() ? "none" : entry.backgroundType;
    values["background.path"] = entry.backgroundPath;
    values["reuse"] = entry.reuseWindow ? "1" : "0";
    WriteIni(AppDir(entry.id) / L"app.ini", values);
    return true;
}

bool SaveManifest(const std::vector<AppEntry>& entries)
{
    std::vector<std::string> ids;
    ids.reserve(entries.size());
    for (const AppEntry& entry : entries)
    {
        ids.push_back(entry.id);
    }
    return WriteManifestIds(ManifestPath(), ids);
}

bool CreateApp(const std::filesystem::path& target, std::vector<AppEntry>& out, AppEntry* created)
{
    if (!IsLaunchablePath(target))
    {
        return false;
    }
    std::error_code ec;
    const std::filesystem::path absolute = std::filesystem::absolute(target, ec);
    if (ec)
    {
        return false;
    }

    AppEntry entry;
    entry.title = TitleFromPath(absolute);
    entry.target = ToUtf8(absolute);
    entry.builtinIcon = "app";
    entry.id = UniqueId(out, SlugFrom(entry.title));

    SaveEntry(entry);
    out.push_back(entry);
    if (!SaveManifest(out))
    {
        return false;
    }
    if (created != nullptr)
    {
        *created = entry;
    }
    return true;
}

std::string CopyAsset(const std::string& id, const std::filesystem::path& source, const char* kind)
{
    std::error_code ec;
    if (!std::filesystem::is_regular_file(source, ec) || ec)
    {
        return {};
    }
    const std::filesystem::path dir = AppDir(id);
    std::filesystem::create_directories(dir, ec);
    RemoveOldKind(dir, kind);

    const std::string extension = LowerExtension(source);
    if (extension.empty())
    {
        return {};
    }
    const std::string name = std::string(kind) + extension;
    std::filesystem::copy_file(source, dir / FromUtf8(name), std::filesystem::copy_options::overwrite_existing, ec);
    return ec ? std::string{} : name;
}

void RemoveAsset(const std::string& id, const std::string& relativeName)
{
    if (relativeName.empty())
    {
        return;
    }
    std::error_code ec;
    std::filesystem::remove(AppDir(id) / FromUtf8(relativeName), ec);
}

void SetPluginDir(const std::filesystem::path& directory) { g_pluginDir = directory; }

std::filesystem::path BuiltinIconPath(const std::string& name)
{
    const std::filesystem::path icons = g_pluginDir / L"assets" / L"icons";
    std::error_code ec;
    std::filesystem::path candidate = icons / FromUtf8((name.empty() ? std::string("app") : name) + ".svg");
    if (std::filesystem::is_regular_file(candidate, ec) && !ec)
    {
        return candidate;
    }
    candidate = icons / L"app.svg";
    if (std::filesystem::is_regular_file(candidate, ec) && !ec)
    {
        return candidate;
    }
    return {};
}

std::filesystem::path IconPath(const AppEntry& entry)
{
    if (!entry.icon.empty())
    {
        const std::filesystem::path custom = AppDir(entry.id) / FromUtf8(entry.icon);
        std::error_code ec;
        if (std::filesystem::is_regular_file(custom, ec) && !ec)
        {
            return custom;
        }
    }
    return BuiltinIconPath(entry.builtinIcon);
}

std::filesystem::path BackgroundPath(const AppEntry& entry)
{
    if (entry.backgroundPath.empty())
    {
        return {};
    }
    const std::filesystem::path file = AppDir(entry.id) / FromUtf8(entry.backgroundPath);
    std::error_code ec;
    return std::filesystem::is_regular_file(file, ec) && !ec ? file : std::filesystem::path{};
}

int Launch(const AppEntry& entry)
{
    if (entry.target.empty())
    {
        std::printf("[appmanager] %s 还没配路径，启动不了\n", entry.id.c_str());
        return 1;
    }

    // 先试"进已有进程"：用户点的是一次启动，不是"再开一个"。
    // 切不到（没进程 / 没主窗口 / 前台抢不到）才真的启动一个。
    if (entry.reuseWindow && ActivateRunning(entry.target))
    {
        std::printf("[appmanager] 已在运行，已切到它的窗口：%s\n", entry.target.c_str());
        return 0;
    }

    const std::wstring target = FromUtf8(entry.target).wstring();
    const std::wstring parameters = FromUtf8(entry.args).wstring();
    const std::wstring workdir = FromUtf8(entry.workdir).wstring();

    const HINSTANCE result =
        ShellExecuteW(nullptr, L"open", target.c_str(), parameters.empty() ? nullptr : parameters.c_str(),
                      workdir.empty() ? nullptr : workdir.c_str(), SW_SHOWNORMAL);
    const std::intptr_t code = reinterpret_cast<std::intptr_t>(result);
    if (code <= 32)
    {
        std::printf("[appmanager] 启动失败（ShellExecute=%lld）：%s\n", static_cast<long long>(code),
                    entry.target.c_str());
        return 1;
    }
    std::printf("[appmanager] 启动 %s（%s）\n", entry.title.c_str(), entry.target.c_str());
    return 0;
}

int RunningInstanceCount(const std::string& exePath)
{
    return static_cast<int>(FindProcesses(FromUtf8(exePath).wstring()).size());
}

bool HasMainWindow(const std::string& exePath)
{
    const std::wstring target = FromUtf8(exePath).wstring();
    if (target.empty())
    {
        return false;
    }
    for (const unsigned long pid : FindProcesses(target))
    {
        if (FindMainWindow(pid) != nullptr)
        {
            return true;
        }
    }
    return false;
}

bool ActivateRunning(const std::string& exePath)
{
    const std::wstring target = FromUtf8(exePath).wstring();
    if (target.empty())
    {
        return false;
    }
    for (const unsigned long pid : FindProcesses(target))
    {
        if (FocusWindow(FindMainWindow(pid)))
        {
            return true;
        }
    }
    return false;
}

// ---- 小工具 ----

std::string ToUtf8(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

std::filesystem::path FromUtf8(std::string_view text)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

bool IsLaunchablePath(const std::filesystem::path& path)
{
    const std::string extension = LowerExtension(path);
    return extension == ".exe" || extension == ".lnk" || extension == ".bat" || extension == ".cmd";
}

std::string TitleFromPath(const std::filesystem::path& path)
{
    const std::string stem = ToUtf8(path.stem());
    return stem.empty() ? ToUtf8(path.filename()) : stem;
}

bool IsVideoFile(const std::filesystem::path& path)
{
    const std::string extension = LowerExtension(path);
    return extension == ".mp4" || extension == ".webm" || extension == ".mov" || extension == ".mkv" ||
           extension == ".avi";
}

int SeedCount() { return static_cast<int>(std::size(kSeeds)); }

} // namespace appmanager
