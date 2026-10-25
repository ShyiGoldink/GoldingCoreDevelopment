# 开机自启动（autostart）：原理与详情

> 给**下一个改它的人**看：它到底写了什么文件、为什么选脚本而不是注册表、哪几处不能想当然。
> 用户侧的一句话用法在 `README.md` 的「开机自启动」那一节。

---

## 1. 一句话

往「设置」里注册一块内容，上面的开关**写 / 删一个启动脚本**；脚本的内容就是
"用 `--fullscreen` 启动本程序"。

---

## 2. 它写什么

```
%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\GoldingCoreAutostart.cmd
```

内容（`scriptBody`）：

```bat
@echo off
rem GoldingCore 开机全屏自启（由 autostart 插件写入；删掉本文件即禁用）
start "" "D:\GoldingCore\build\golding_host.exe" --fullscreen
```

- 路径是**当前进程的 exe**（`GetModuleFileNameW`），不是写死的；
- `--fullscreen` 是宿主自己认的参数（`host/main.cpp` 的 `parseArgs`），
  其他启动参数想加就在这里拼；
- `start ""` 的第一个空引号是给"窗口标题"留的位置，漏了它带引号的路径会被当成标题。

---

## 3. 为什么是脚本，不是注册表 `Run`

| | 启动脚本（当前做法） | 注册表 `HKCU\...\Run` |
| --- | --- | --- |
| 用户能看见吗 | 能（就在「启动」文件夹里） | 要去 `regedit` 找 |
| 能改吗 | 能，随便编辑 | 能，但门槛高 |
| 能删吗 | 删文件即禁用 | 要删键值 |
| 权限 | 不需要管理员 | 同（HKCU） |
| 排查 | 内容是明文，一眼看清 | 也是一行命令 |

结论：两者能力等价，但"用户可见可改"这一点让脚本更符合这个项目的取向（`cursor`
插件把用户素材放 `%APPDATA%` 也是同一条理由）。代价是脚本多一个 shell 进程
（`cmd /c`），开机那一两秒里可以忽略。

---

## 4. 为什么按**ANSI 代码页**写，而不是 UTF-8

`cmd.exe` 解析 `.cmd` 用的是**当前代码页**（中文系统 = 936）。用 UTF-8 写，路径里的
中文会变成乱码，表现是**开机时黑窗口闪一下就没有了**——最难查的一类。

所以：

- 写：`wideToAnsi(scriptBody(...))` → `WideCharToMultiByte(CP_ACP, ...)`；
- 读：`ansiToWide(文件字节)` → `MultiByteToWideChar(CP_ACP, ...)`。

换台机器（英文系统 CP_1252）同样成立：读写的都是那台机器的 `CP_ACP`。

---

## 5. "已启用"怎么判

```cpp
bool isEnabled() { return scriptMatches(scriptPath(), currentExe()); }
```

两条都要满足：

1. 脚本文件存在；
2. 脚本内容的**宽字符**里含有当前 exe 的完整路径。

为什么要第 2 条：程序换了安装目录之后，旧脚本还指着老路径——那时"开机自启"实际上
是**启动了一个不存在的程序**。按路径比对，这种情况会显示"未启用"，用户再点一次就
写对了。

---

## 6. 自检策略

`runReady` 里**只读真实启动项**（`isEnabled()`），写 / 删都发生在**临时目录**：

```
%TEMP%\golding-autostart-probe.cmd
  写 → scriptMatches 为真 → 删 → 不存在
```

三条纪律：

- 不因为"跑了一次自检"就把用户的开机自启打开或关掉；
- 用同一个 `writeScript` / `scriptMatches` 走真代码路径（不是另写一份判断）；
- 临时文件和真实脚本同名不同目录，删错也不会伤到用户的启动项。

---

## 7. 已知限制与扩展点

| 限制 | 说明 | 建议 |
| --- | --- | --- |
| 只认当前用户 | 写在 `HKCU` 对应的 `%APPDATA%` 下 | 要"全机自启"得写 `HKLM`，需要管理员 |
| 用户手删脚本 = 失效 | 这是设计（删即禁用） | 界面上"未启用"会如实反映 |
| 杀软可能拦 `.cmd` | 开机执行脚本是常见被拦行为 | 换成 `Run` 键可以绕开，但会丢掉"可见可改" |
| 只有全屏/不全屏两种 | 参数写死在 `scriptBody` | 想加"最小化启动""指定页面"就在这里拼参数 |
| 没有"启动失败"的反馈 | 开机时失败无人告知 | 可以在脚本里 `>> "%TEMP%\golding-autostart.log" 2>&1` |

**加一个启动参数**：只改 `scriptBody` 那一行（`start "" "<exe>" --fullscreen <更多>`）。
宿主不认识的参数会被当成 `bootMode` 交给面板插件解释——要加参数请先确认宿主/面板认它，
否则等于没加。
