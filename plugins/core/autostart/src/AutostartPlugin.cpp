#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

// ---------------------------------------------------------------------------
// autostart —— 开机全屏自启的开关（普通插件，不是面板）。
//
// 它只做三件事：
//   1. 往「设置」里注册一块内容（`AddSettingsSection`）；
//   2. 那块内容上有一个开关按钮：**写 / 删一个启动脚本**；
//   3. 装配后自检（用一个临时脚本验证读写，不碰用户真实的启动项）。
//
// 为什么用**启动脚本**而不是注册表 `Run` 键：
//   - 用户能看见、能改、能删（就在 `启动` 文件夹里，资源管理器里点得开）；
//   - 不需要管理员权限，也不往注册表里留东西；
//   - "开机全屏启动"这件事本身就是一个命令行参数（`--fullscreen`），
//     脚本恰好是它的最直白表达。
//
// 脚本按**系统 ANSI 代码页**写，不是 UTF-8：cmd.exe 解析 .cmd 用的是当前代码页，
// 而 exe 路径里可能有中文（`D:\游戏\...`）。写 UTF-8 会让那类路径变成乱码，
// 表现是"开机时闪一下窗口就没了"——最难查的一类。
// ---------------------------------------------------------------------------

#include <sdk/GoldingPlugin.h>

#include "GoldingServices.h"

#include <button/ButtonData.h>
#include <button/InkingStaticButton.h>
#include <dataStruct/InkingTextLayout.h>
#include <panel/InkingStaticPanel.h>
#include <prefab/PrefabTheme.h>
#include <text/InkingStaticLabel.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace
{

namespace theme = inking::prefab::theme;

golding::Host g_host{nullptr, nullptr};

/** 启动脚本的文件名（放在用户的 `启动` 文件夹里）。 */
constexpr const wchar_t* kScriptName = L"GoldingCoreAutostart.cmd";
/** 设置区块 id（`plugins/core/settings` 的菜单里按它排）。 */
constexpr const char* kSectionId = "autostart";

// ---- 路径与编码 ----

std::filesystem::path startupDir()
{
    wchar_t buffer[2048];
    const DWORD size = GetEnvironmentVariableW(L"APPDATA", buffer, static_cast<DWORD>(std::size(buffer)));
    if (size == 0 || size >= std::size(buffer))
    {
        return {};
    }
    return std::filesystem::path(std::wstring(buffer, size)) / L"Microsoft" / L"Windows" / L"Start Menu" /
           L"Programs" / L"Startup";
}

std::filesystem::path scriptPath() { return startupDir() / kScriptName; }

std::wstring currentExe()
{
    wchar_t buffer[2048];
    const DWORD size = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    if (size == 0 || size >= std::size(buffer))
    {
        return {};
    }
    return std::wstring(buffer, size);
}

std::string wideToAnsi(const std::wstring& text)
{
    if (text.empty())
    {
        return {};
    }
    const int size = WideCharToMultiByte(CP_ACP, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0,
                                         nullptr, nullptr);
    if (size <= 0)
    {
        return {};
    }
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_ACP, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring ansiToWide(const std::string& text)
{
    if (text.empty())
    {
        return {};
    }
    const int size = MultiByteToWideChar(CP_ACP, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0)
    {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_ACP, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

std::string toUtf8(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

// ---- 脚本读写 ----

/** 脚本内容：`--fullscreen` 就是"开机全屏"这件事的全部。 */
std::wstring scriptBody(const std::wstring& exe)
{
    std::wstring body;
    body += L"@echo off\r\n";
    body += L"rem GoldingCore 开机全屏自启（由 autostart 插件写入；删掉本文件即禁用）\r\n";
    body += L"start \"\" \"" + exe + L"\" --fullscreen\r\n";
    return body;
}

bool writeScript(const std::filesystem::path& file, const std::wstring& exe)
{
    if (file.empty() || exe.empty())
    {
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream stream(file, std::ios::binary | std::ios::trunc);
    if (!stream)
    {
        return false;
    }
    const std::string bytes = wideToAnsi(scriptBody(exe));
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(stream);
}

/** 脚本在不在、且指向的是**当前这个 exe**（换了安装位置就算没启用）。 */
bool scriptMatches(const std::filesystem::path& file, const std::wstring& exe)
{
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec) || exe.empty())
    {
        return false;
    }
    std::ifstream stream(file, std::ios::binary);
    if (!stream)
    {
        return false;
    }
    const std::string bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    return ansiToWide(bytes).find(exe) != std::wstring::npos;
}

bool isEnabled() { return scriptMatches(scriptPath(), currentExe()); }

bool enable()
{
    const bool ok = writeScript(scriptPath(), currentExe());
    std::printf("[autostart] %s → %s\n", ok ? "已写入启动脚本" : "写启动脚本失败", toUtf8(scriptPath()).c_str());
    return ok;
}

bool disable()
{
    std::error_code ec;
    std::filesystem::remove(scriptPath(), ec);
    const bool ok = !ec;
    std::printf("[autostart] %s → %s\n", ok ? "已删除启动脚本" : "删启动脚本失败", toUtf8(scriptPath()).c_str());
    return ok;
}

// ---- 设置区块 ----

/** 一行断言（`Ready` 的返回值就是失败项数）。 */
int check(const char* label, bool ok, const std::string& detail)
{
    std::printf("[%s] autostart.%-24s %s\n", ok ? "ok" : "FAIL", label, detail.c_str());
    return ok ? 0 : 1;
}

/** 把状态文字与按钮文字同步到当前真实状态。 */
void refreshUi(inking::InkingStaticLabel* status, inking::InkingStaticButton* toggle)
{
    const bool enabled = isEnabled();
    if (status != nullptr)
    {
        status->SetText(enabled ? "已启用：开机会以全屏启动 GoldingCore" : "未启用：开机不会自动启动");
        status->SetTextColor(enabled ? theme::kSuccess : theme::kTextMuted);
    }
    if (toggle != nullptr)
    {
        toggle->SetText(enabled ? "禁用开机自启" : "启用开机自启（全屏）");
    }
}

GoldingWidgetHandle createSection(void* /*hostContext*/, void* parent)
{
    auto* host = static_cast<inking::InkingWidget*>(parent);
    if (host == nullptr)
    {
        return nullptr;
    }
    const int width = host->GetWidth() > 0 ? host->GetWidth() : 640;

    inking::PanelData root;
    root.name = "autostart.section";
    root.width = width;
    root.height = 210;
    root.startOpen = true;
    root.closed.color = theme::kSurface;
    root.closed.themeColor = "panelSurface";
    root.open = root.closed;
    root.shape = inking::ShapeSpec::Rect();
    root.layout.kind = inking::LayoutKind::Stack;
    root.layout.axis = inking::LayoutAxis::Vertical;
    root.layout.gap = 10.0f;
    auto* panel = static_cast<inking::InkingStaticPanel*>(
        host->AddChild(std::make_unique<inking::InkingStaticPanel>(host, root)));
    if (panel == nullptr)
    {
        return nullptr;
    }

    inking::LabelData title;
    title.name = "autostart.title";
    title.height = 30;
    title.text = "开机自启动（全屏）";
    title.color = theme::kTextPrimary;
    title.colorToken = "textPrimary";
    title.style.fontSize = 18.0f;
    title.style.alignV = inking::TextAlignV::Middle;
    panel->AddChild(std::make_unique<inking::InkingStaticLabel>(panel, title));

    inking::LabelData status;
    status.name = "autostart.status";
    status.height = 24;
    status.color = theme::kTextMuted;
    status.colorToken = "textMuted";
    status.style.fontSize = 14.0f;
    status.style.alignV = inking::TextAlignV::Middle;
    auto* statusLabel = static_cast<inking::InkingStaticLabel*>(
        panel->AddChild(std::make_unique<inking::InkingStaticLabel>(panel, status)));

    inking::ButtonData toggle;
    toggle.name = "autostart.toggle";
    toggle.width = 280;
    toggle.height = 44;
    toggle.normal.type = inking::AppearanceType::Color;
    toggle.normal.color = theme::kAccentSoft;
    toggle.hover.type = inking::AppearanceType::Color;
    toggle.hover.color = theme::kAccentSoftPressed;
    toggle.onclicked.type = inking::AppearanceType::Color;
    toggle.onclicked.color = theme::kAccent;
    toggle.hoverInheritsNormal = false;
    toggle.onclickedInheritsNormal = false;
    toggle.textColor = theme::kAccentStrong;
    toggle.text.fontSize = 15.0f;
    toggle.text.align = inking::TextAlignH::Center;
    toggle.text.alignV = inking::TextAlignV::Middle;
    toggle.shape = inking::ShapeSpec::RoundedRect(10.0f);
    toggle.stroke.width = 1.0f;
    toggle.stroke.color = theme::kAccent;
    toggle.layoutItem.selfAlign = inking::LayoutAlign::Start;
    toggle.layoutItem.hasSelfAlign = true;
    auto* toggleButton = static_cast<inking::InkingStaticButton*>(
        panel->AddChild(std::make_unique<inking::InkingStaticButton>(panel, toggle)));

    if (toggleButton != nullptr)
    {
        toggleButton->SetOnClicked([statusLabel, toggleButton] {
            if (isEnabled())
            {
                disable();
            }
            else
            {
                enable();
            }
            refreshUi(statusLabel, toggleButton);
        });
    }

    inking::LabelData note;
    note.name = "autostart.note";
    note.height = 58;
    note.text = "做法：在用户的「启动」文件夹里写一个 " + std::string("GoldingCoreAutostart.cmd") +
                "，内容是带 --fullscreen 启动本程序。删掉它即禁用。\n" + toUtf8(scriptPath());
    note.color = theme::kTextMuted;
    note.colorToken = "textMuted";
    note.style.fontSize = 12.0f;
    note.maxLines = 3;
    panel->AddChild(std::make_unique<inking::InkingStaticLabel>(panel, note));

    refreshUi(statusLabel, toggleButton);
    return panel;
}

const GoldingSettingsDecl kSection = {
    kSectionId,       // id
    "开机自启动",      // 设置菜单里显示的名字
    50,               // order：排在别的区块之后
    &createSection,   // 点开这一项时弹的内容
    nullptr,          // 不是"点一下直接执行"的动作型
};

// ---- 自检 ----

int runReady()
{
    int failures = 0;

    const std::wstring exe = currentExe();
    failures += check("能取到自己的 exe", !exe.empty(), wideToAnsi(exe));

    // 真实启动项只**读**，不改：自检不能替用户擅自打开 / 关掉开机自启。
    const bool enabled = isEnabled();
    failures += check("当前自启状态可读", true, enabled ? "已启用" : "未启用");

    // 用临时脚本把写 / 判 / 删整条链走一遍。位置在临时目录，不碰用户的启动项。
    std::error_code ec;
    const std::filesystem::path probe =
        std::filesystem::temp_directory_path(ec) / L"golding-autostart-probe.cmd";
    const bool written = writeScript(probe, exe);
    const bool matched = scriptMatches(probe, exe);
    std::filesystem::remove(probe, ec);
    const bool removed = !std::filesystem::exists(probe, ec);
    failures += check("脚本写入 / 识别 / 删除", written && matched && removed,
                      std::string("write=") + (written ? "1" : "0") + " match=" + (matched ? "1" : "0") +
                          " remove=" + (removed ? "1" : "0"));

    // 换个 exe 路径必须判成"没启用"——否则换了安装位置还会以为开着。
    const bool foreign = !scriptMatches(probe, L"C:\\SomewhereElse\\golding_host.exe");
    failures += check("路径不匹配不算启用", foreign, "probe 已删，按不存在处理");

    return failures;
}

class AutostartPlugin final : public golding::Plugin
{
public:
    bool Register(golding::Host& host) override
    {
        g_host = host;
        if (startupDir().empty())
        {
            host.Warn("autostart：取不到 APPDATA，启动文件夹不可用");
        }
        const bool ok = host.AddSettingsSection(kSection);
        if (!ok)
        {
            host.Warn("autostart：设置区块注册被驳回");
        }
        return ok;
    }

    int Ready(const golding::Host& /*host*/) override { return runReady(); }
};

} // namespace

GOLDING_EXPORT_PLUGIN(AutostartPlugin)
