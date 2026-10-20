# KryptexTray

[English](#english) | [简体中文](#简体中文)

---

## English

A lightweight Windows tray app that shows your Kryptex account balance, daily/monthly earnings, hashrate and online worker status on a frosted-glass desktop overlay.

### Features

- Tray menu with four items: current amount, overlay toggle, refresh, exit.
- Reads the logged-in Kryptex dashboard and hardware pages over HTTPS with a cookie you provide.
- Account balance in BTC plus the fiat estimate shown by Kryptex.
- Daily/monthly profitability, total hashrate per coin, and per-worker hashrate, earnings, share counts and miner version.
- Online workers only: offline workers disappear from the list and the panel shrinks automatically.
- Frosted-glass overlay: a blurred, darkened snapshot of the desktop behind the panel, a subtle live alpha, and fully opaque text.
- Non-topmost, click-through, never steals focus. Source Han Sans (思源黑体) with an automatic fallback chain.

### Requirements

- Windows 10 or Windows 11.
- Visual Studio 2022 with **Desktop development with C++** (MSVC + Windows SDK).
- A Kryptex account. Mine with your Kryptex account username (`krx...`) so the earnings land in the account balance read by this app.

### Build

```bat
build.bat
```

The executable is written to `build\KryptexTray.exe`. There are no third-party dependencies; the app uses WinHTTP, GDI and the Win32 API only.

### Configure

1. Copy `.env.example` to `.env` in the project root (next to `build.bat`).
2. Open `https://www.kryptex.com/site/dashboard` in Chrome while logged in.
3. Press `F12` → **Network** → reload the page → click the dashboard request → **Request Headers** → copy the value after `Cookie:`.
4. Paste it as `KRYPTEX_COOKIE=...` in `.env`.

Optional keys:

```ini
KRYPTEX_COOKIE=...
KRYPTEX_URL=https://www.kryptex.com/site/dashboard
REFRESH_SECONDS=60
```

The app looks for `.env` next to the executable, then in the parent directory, then in the current working directory. It re-reads `.env` before every refresh, so you can replace an expired cookie without restarting.

### Usage

Run `build\KryptexTray.exe` and right-click the tray icon:

1. Current amount
2. Show overlay / Hide overlay
3. Refresh
4. Exit

The overlay is positioned at the top-right of the primary work area, is click-through, and stays below normal windows. To add it to startup, create a shortcut to the executable in `shell:startup`.

### How It Works

- `GET /site/dashboard` returns server-rendered HTML; the balance and fiat estimate are extracted with regular expressions.
- `GET /site/hardware` embeds a `workerDataset` JSON script with every worker's coin, online flag, 30-minute/24-hour hashrate, daily/monthly profit, share counts and miner version. Totals are summed from that dataset.
- The overlay is a layered window with per-pixel alpha, rendered into a 32-bit DIB and committed with `UpdateLayeredWindow`.
- Background pixels use alpha 235; text, dividers and status dots use alpha 255.
- The frosted backdrop is a snapshot of the screen behind the panel: a radius-6 box blur darkened to 32%. It is refreshed when the overlay is shown, when its size changes, and when you press **Refresh**.

### Fonts

The app prefers **思源黑体** (Source Han Sans), then falls back in this order:

`Source Han Sans SC` → `Source Han Sans CN` → `Source Han Sans` → `Noto Sans SC` → `Noto Sans CJK SC` → `Microsoft YaHei UI` → `Microsoft YaHei` → `Segoe UI`

All overlay text is 9 px. The selected face is logged to `build\font.log` at startup for diagnostics.

### Security

- `.env` contains a login cookie. It is ignored by `.gitignore`; never commit or share it.
- The app does not read cookies from your browser. You paste the cookie manually.
- The app never logs the cookie or response bodies. `font.log` only contains font names.
- Anyone with the cookie can access your Kryptex account. Replace it after logging out of all sessions if it leaks.

### License

MIT. See [LICENSE](LICENSE).

---

## 简体中文

一个轻量的 Windows 托盘小工具，在桌面浮窗里显示 Kryptex 账户余额、日/月收益、算力和在线 worker 状态，带磨砂玻璃背景。

### 功能

- 托盘菜单四项：当前金额、显示/关闭浮窗、刷新、退出。
- 用你提供的 Cookie 通过 HTTPS 读取已登录的 Kryptex 后台账户页和硬件页。
- 显示 BTC 余额和 Kryptex 给出的人民币估值。
- 显示日/月盈利能力、各币种总算力，以及每个 worker 的算力、收益、份额和矿工版本。
- 只显示在线 worker：离线 worker 不占列表空间，浮窗高度自动收缩。
- 磨砂玻璃浮窗：截取并模糊、压暗背后的桌面作为底图，叠加轻微实时透明，文字完全不透明。
- 不置顶、鼠标穿透、不抢焦点。字体优先思源黑体，并带自动降级链。

### 环境要求

- Windows 10 / Windows 11。
- Visual Studio 2022，安装 **使用 C++ 的桌面开发** 工作负载（MSVC + Windows SDK）。
- Kryptex 账户。请用账户名（`krx...`）挖矿，收益才会进入本程序读取的账户余额。

### 编译

```bat
build.bat
```

产物为 `build\KryptexTray.exe`。没有第三方依赖，只使用 WinHTTP、GDI 和 Win32 API。

### 配置

1. 把 `.env.example` 复制成项目根目录的 `.env`（和 `build.bat` 同级）。
2. 在 Chrome 里登录并打开 `https://www.kryptex.com/site/dashboard`。
3. 按 `F12` → **Network** → 刷新页面 → 点开 dashboard 请求 → **Request Headers** → 复制 `Cookie:` 后面的整串值。
4. 粘贴到 `.env` 的 `KRYPTEX_COOKIE=...`。

可选配置：

```ini
KRYPTEX_COOKIE=...
KRYPTEX_URL=https://www.kryptex.com/site/dashboard
REFRESH_SECONDS=60
```

`.env` 的查找顺序：exe 同目录 → 上一级目录 → 当前工作目录。程序每次刷新前都会重新读取 `.env`，Cookie 过期后直接覆盖保存，再点「刷新」即可，不用重启。

### 使用

运行 `build\KryptexTray.exe`，右键托盘图标：

1. 当前金额
2. 浮窗展示 / 关闭浮窗
3. 刷新
4. 退出

浮窗位于主屏工作区右上角，鼠标可穿透，层级低于普通窗口。想开机自启，给 exe 建一个快捷方式放进 `shell:startup`。

### 工作原理

- `GET /site/dashboard` 返回服务端渲染的 HTML，用正则提取 BTC 余额和人民币估值。
- `GET /site/hardware` 里嵌有一段 `workerDataset` JSON，包含每个 worker 的币种、在线状态、30 分钟/24 小时算力、日/月收益、有效/过期/无效份额和矿工版本。汇总数据由这段 JSON 计算。
- 浮窗是带逐像素 alpha 的 layered window，先绘制到 32 位 DIB，再通过 `UpdateLayeredWindow` 提交。
- 背景 alpha 为 235；文字、分隔线和状态点为 255。
- 磨砂底图是浮窗背后的屏幕快照：半径 6 的方框模糊，再压暗到 32%。打开浮窗、尺寸变化或手动点「刷新」时会重新抓取。

### 字体

优先 **思源黑体**，找不到时依次降级：

`Source Han Sans SC` → `Source Han Sans CN` → `Source Han Sans` → `Noto Sans SC` → `Noto Sans CJK SC` → `Microsoft YaHei UI` → `Microsoft YaHei` → `Segoe UI`

浮窗文字统一 9px。启动时会把选中的字体名写入 `build\font.log` 方便排查。

### 安全说明

- `.env` 里是登录 Cookie，已被 `.gitignore` 排除，不要提交或分享。
- 程序不会读取浏览器 Cookie，需要你手动粘贴。
- 程序不会记录 Cookie 或响应正文；`font.log` 只包含字体名。
- 拿到 Cookie 就能访问你的 Kryptex 账户；如果泄露，请在 Kryptex 退出所有会话后更换。

### 许可证

MIT，详见 [LICENSE](LICENSE)。
