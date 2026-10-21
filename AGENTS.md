# AGENTS.md

Instructions for AI coding agents and human contributors working on KryptexTray.

## Project

KryptexTray is a single-binary Win32 tray app that displays Kryptex account data on a semi-transparent overlay. It targets Windows 10/11, uses C++17 and MSVC, and has no third-party runtime dependencies.

## Layout

```
src/main.cpp     Entire application: config, WinHTTP client, JSON parser,
                 worker aggregation, Win32 tray, layered overlay renderer.
build.bat        MSVC build script (uses vswhere to locate Visual Studio).
.env.example     Cookie template; copy to .env (ignored by git).
README.md        Bilingual user documentation.
LICENSE          MIT.
```

`build/` is generated and gitignored. `build\KryptexTray.exe` is the deliverable; `build\font.log` is a diagnostic log written at startup.

## Build and run

```bat
build.bat
build\KryptexTray.exe
```

The build uses `/std:c++17 /utf-8 /W3 /O2`, links `winhttp`, `user32`, `gdi32` and `shell32`, and produces a windowed subsystem executable. There is no test project; validate with a build plus a manual smoke test (tray icon, menu, overlay toggle, data refresh).

## Architecture

1. `LoadConfig` reads `.env` from the executable directory, its parent, or the current directory. It must never print or persist the cookie value.
2. `RefreshWorker` runs on a detached thread once per `REFRESH_SECONDS` (default 60) or when the user clicks Refresh.
   - `GET KRYPTEX_URL` (default `/site/dashboard`) and parse the balance with regexes.
   - `GET https://www.kryptex.com/site/hardware` and extract the `workerDataset` `<script type="application/json">` block.
   - `JsonReader` is a deliberately small parser for flat worker objects. Do not replace it with a large dependency unless the data model changes.
3. `ParseHardware` builds `WorkerInfo` records and aggregates online count, daily/monthly profit and per-coin 30-minute hashrate.
4. The UI thread receives `WM_REFRESH_DONE` and updates the tray tooltip and overlay.

## Overlay rendering rules

- The overlay is `WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`, positioned top-right, never topmost.
- Rendering goes to a 32-bit top-down DIB; `UpdateLayeredWindow` commits it. Do not reintroduce `SetLayeredWindowAttributes`; it conflicts with per-pixel alpha.
- Background pixels: alpha `backgroundAlpha` (currently 220). Foreground pixels (text, border, dividers, dots): alpha 255.
- `UpdateLayeredWindow` expects premultiplied alpha. Keep background RGB at 0 (black) or premultiply any colored background correctly.
- Only online workers are rendered. `OverlayHeight()` is based on `g_online`, not `g_workers.size()`.
- `WM_NCHITTEST` returns `HTTRANSPARENT`; keep the window non-activating.
- Font priority: 思源黑体, Source Han Sans SC/CN, Noto Sans SC/CJK SC, Microsoft YaHei UI/YaHei, Segoe UI. All overlay text is 9 px. The selected face is written to `font.log`.

## Fragile external contracts

- The dashboard balance comes from server-rendered HTML. The current regexes are `(\d+\.\d+)\s*BTC` and `≈\s*([^<\r\n]{1,24})`. If Kryptex changes the page, update `ParseBalance`.
- The hardware page embeds `workerDataset` as JSON. If the script id or field names change, update `ExtractWorkerDataset` / `ParseHardware`.
- Kryptex has no public personal-account API. Do not switch to scraping browser cookies; the user supplies the cookie via `.env`.

## Security invariants

- Never commit `.env`, cookies, tokens, account IDs paired with secrets, or raw authenticated responses.
- Never log the cookie or response bodies. `font.log` may contain font names only.
- Do not read cookies from Chrome, Edge or any browser profile.
- Do not add telemetry or network calls other than the two Kryptex GET requests.

## Code conventions

- C++17, Win32, `UNICODE`, no exceptions across thread boundaries.
- Keep helpers `static` and the project in a single translation unit unless there is a clear reason to split it.
- Match the existing style: 4-space indentation, brace on the same line, `wchar_t` for UI strings, UTF-8 source (the build passes `/utf-8`).
- Avoid comments that restate the code. Document only non-obvious Win32/DWM/alpha behavior.
- Keep the `.env` parser tolerant: trim whitespace, strip matching quotes, ignore comments and blank lines.

## Change checklist

1. `build.bat` completes without warnings.
2. Tray icon appears; all four menu items work.
3. Overlay toggles, is click-through, and stays below normal windows.
4. Balance, profitability, hashrate and online workers match the Kryptex pages.
5. Offline workers do not appear; the panel height matches the online count.
6. No secret appears in `git status` or in the diff.

## Commit style

Use Conventional Commits and end every commit message with:

```
Co-authored-by: CommandCodeBot <noreply@commandcode.ai>
```

Example:

```
feat: add Kryptex tray monitor with semi-transparent overlay

Co-authored-by: CommandCodeBot <noreply@commandcode.ai>
```

---

# AGENTS.md（中文）

面向参与 KryptexTray 开发的 AI 代理和人类贡献者。

## 项目

KryptexTray 是一个单文件 Win32 托盘程序，用半透明浮窗显示 Kryptex 账户数据。目标平台 Windows 10/11，C++17 + MSVC，无第三方运行时依赖。

## 目录

```
src/main.cpp     全部逻辑：配置、WinHTTP、JSON 解析、worker 汇总、托盘、浮窗渲染。
build.bat        MSVC 构建脚本（vswhere 定位 Visual Studio）。
.env.example     Cookie 模板；复制为 .env（已被 git 忽略）。
README.md        双语用户文档。
LICENSE          MIT。
```

`build/` 为生成目录，已被忽略；`build\KryptexTray.exe` 是产物，`build\font.log` 是启动时写入的字体诊断日志。

## 构建与运行

```bat
build.bat
build\KryptexTray.exe
```

构建参数为 `/std:c++17 /utf-8 /W3 /O2`，链接 `winhttp`、`user32`、`gdi32`、`shell32`，生成窗口子系统程序。项目没有自动化测试，改动后用构建 + 手动冒烟验证。

## 架构

1. `LoadConfig` 从 exe 同目录、上一级目录或当前目录读取 `.env`，绝不能打印或持久化 Cookie。
2. `RefreshWorker` 在后台线程按 `REFRESH_SECONDS`（默认 60 秒）或手动刷新触发：
   - `GET KRYPTEX_URL`（默认 `/site/dashboard`），用正则提取余额。
   - `GET https://www.kryptex.com/site/hardware`，提取 `workerDataset` 的 JSON。
   - `JsonReader` 是专门为扁平 worker 对象写的小型解析器；除非数据结构变化，不要引入大型依赖。
3. `ParseHardware` 生成 `WorkerInfo`，并汇总在线数、日/月收益和各币种 30 分钟算力。
4. UI 线程收到 `WM_REFRESH_DONE` 后更新托盘和浮窗。

## 浮窗渲染规则

- 浮窗样式：`WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`，位于右上角，绝不置顶。
- 先绘制到 32 位 top-down DIB，再用 `UpdateLayeredWindow` 提交。不要再引入 `SetLayeredWindowAttributes`，它会和逐像素 alpha 冲突。
- 背景 alpha 为 `backgroundAlpha`（当前 220）；文字、边框、分隔线、状态点为 255。
- `UpdateLayeredWindow` 使用预乘 alpha；背景 RGB 保持 0，或确保有颜色的背景已正确预乘。
- 只渲染在线 worker；`OverlayHeight()` 基于 `g_online`，不是 `g_workers.size()`。
- `WM_NCHITTEST` 返回 `HTTRANSPARENT`，窗口不抢焦点。
- 字体优先级：思源黑体、Source Han Sans SC/CN、Noto Sans SC/CJK SC、Microsoft YaHei UI/YaHei、Segoe UI；浮窗文字统一 9px；选中的字体名写入 `font.log`。

## 外部契约（易变）

- 余额来自服务端渲染 HTML，当前正则：`(\d+\.\d+)\s*BTC`、`≈\s*([^<\r\n]{1,24})`。页面变化时更新 `ParseBalance`。
- 硬件页的 `workerDataset` 是 JSON；字段或 script id 变化时更新 `ExtractWorkerDataset` / `ParseHardware`。
- Kryptex 没有公开的个人账户 API。不要改成读取浏览器 Cookie；Cookie 由用户通过 `.env` 提供。

## 安全红线

- 绝不提交 `.env`、Cookie、Token、含秘密的账户信息或认证后的原始响应。
- 不记录 Cookie 或响应正文；`font.log` 只能有字体名。
- 不读取 Chrome/Edge 等浏览器的 Cookie。
- 不添加遥测，不添加除两个 Kryptex GET 之外的网络请求。

## 代码风格

- C++17、Win32、`UNICODE`，线程边界不抛异常。
- 辅助函数保持 `static`；除非有明确理由，项目维持单翻译单元。
- 4 空格缩进，左大括号同行；UI 字符串用 `wchar_t`；源码 UTF-8（构建带 `/utf-8`）。
- 不写复述代码的注释，只记录不明显的 Win32/alpha 行为。
- `.env` 解析要宽容：去空格、去成对引号、忽略注释和空行。

## 改动检查清单

1. `build.bat` 无警告完成。
2. 托盘图标出现，四个菜单项都可用。
3. 浮窗可开关、鼠标穿透、层级低于普通窗口。
4. 余额、收益、算力、在线 worker 与 Kryptex 页面一致。
5. 离线 worker 不显示，浮窗高度与在线数匹配。
6. `git status` 和 diff 中没有秘密。

## 提交规范

使用 Conventional Commits，并在每条提交信息末尾加上：

```
Co-authored-by: CommandCodeBot <noreply@commandcode.ai>
```
