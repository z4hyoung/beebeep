# YongBEEP 调研报告：BeeBEEP 增加蓝牙通信 + UI 优化

> 调研日期：2026-11-03 ｜ 调研方式：4 个并行研究团队（BeeBEEP 架构 / 跨平台蓝牙 / UI 优化 / 本机构建环境）
> 场景：Mac（Apple Silicon, macOS 25.6, BeeBEEP 5.8.5, 蓝牙 BCM4378）↔ 邻近 Windows 电脑，两机各在自己的 IP 网络，需传文本和小文件

---

## 一、五个改变方向的关键发现

### 1. 存在零代码方案：蓝牙 PAN（今天就能试）
macOS 和 Windows 都支持"蓝牙个人区域网（Bluetooth PAN）"。两机系统级蓝牙配对并建立 PAN 连接后，各自获得 169.254.x.x 链路本地 IP，IP 层互通。而 **BeeBEEP 官方自带"按 IP 连接好友"（Users → Connect to friend，默认 TCP 6475）**，配合相同的网络密码即可直接通信——一行代码不用改。
- 条件：放行 UDP 36475（发现）+ TCP 6475/6476；两端 BeeBEEP 网络密码一致
- 若两机都能上互联网，**Tailscale/ZeroTier** 是另一条零代码路线（官方文档明确支持，工单 #1173）
- 注意：跨平台 PAN 在 Win11 上需要实测验证（见 Stage 0）

### 2. BeeBEEP 6.0 计划 2026 年 12 月发布（距今约 1 个月），你的两个诉求官方都做了
- 官方工单 #1178 确认：6.0 蓝牙侧参考了 bitchat 白皮书，含 **蓝牙 Mesh**（hop limit、store-and-forward、封签私信）
- 6.0 = Qt6 + QML + CMake 全重写，**全新 UI**：气泡消息、主题 token 文件（`beebeep-custom-theme.qml`）、暗色模式"跟随系统"
- 协议 96 带 legacy bridge，新旧版本可互通；5.9.2（6.0 后约一个月）保留经典 UI 并提供原生 Apple Silicon 构建
- 首发 Windows 版
- **战略含义**：不要在 5.x fork 上做重度 UI 重构（L3 级），官方一个月后免费送；自己的改动要克制、可抛弃

### 3. 蓝牙技术选型修正：经典 RFCOMM 才是最佳底座（不是 BLE）
常见说法"macOS 上 Qt 只有 BLE"是错的（那是对 CoreBluetooth/iOS 的认知）。经 Qt 源码与官方文档双重验证：

| 方案 | 技术 | 吞吐 | 1MB 耗时 | 工作量 | 评价 |
|---|---|---|---|---|---|
| **A（推荐）** | Qt `QBluetoothServer`/`QBluetoothSocket` 经典 RFCOMM，两端对称，自定义帧协议 | 100–250 KB/s | 4–10s | 1–2 人周 PoC | macOS 后端 IOBluetooth（Qt 源码 `src/bluetooth/osx/` 完整实现），Windows 后端 WinRT 官方支持 RFCOMM；Qt 自带 btchat 示例就是模板 |
| B（备选） | BLE GATT：macOS 当 GATT server（peripheral）+ Windows 当 central | 50–110 KB/s | 10–20s | 2–3 人周 | 免配对即用；Windows 无 BLE peripheral 能力（Qt 5/6 均不支持），角色固定；吞吐受系统调度影响大 |
| C（兜底） | macOS 原生 IOBluetooth SPP + Windows 原生 WinRT RFCOMM（绕开 Qt 传输层） | 同 A | — | 3–5 人周 | 仅当 Qt 5.15 的 macOS 蓝牙后端在 Tahoe 25.6 实测不可用时启用；微软官方有 RFCOMM 收发文件示例可抄 |

关键查证结论：
- L2CAP CoC 提速路线在 Windows 断供（WinRT 只暴露 GATT，无 LE credit-based channel API）→ 出局
- RFCOMM 是可靠有序流，**不需要应用层重传**，只需帧协议（魔数+长度+类型+序号+CRC 防粘包）
- BLE 路线如需加密会触发系统配对弹窗，双端配对状态管理复杂
- **必须先验证的唯一风险点**：Qt 5.15 的 macOS 蓝牙后端在 macOS 25.6 (Tahoe) 上的可用性（历史上 Monterey 有过蓝牙回归）；以及 Windows 未配对设备的发现路径

### 4. BeeBEEP 源码结构：蓝牙插入点明确
- 继承链 `Connection : ConnectionSocket : QTcpSocket`（`src/core/`），**TCP 是硬编码的**，需要小重构：把 ConnectionSocket 的 TCP 依赖抽成 QIODevice 组合或为 QBluetoothSocket 做适配层
- `Message`/`Chat`/`ChatMessage` 消息模型与传输完全解耦，**可原样复用**；`Core` 单例调度上层几乎不动
- 默认端口：UDP 36475（发现）+ TCP 6475（消息）+ **TCP 6476（文件传输独立通道）**——蓝牙化需一并处理，低带宽场景可用编译开关 `BEEBEEP_DISABLE_FILE_TRANSFER` 让文件走消息通道
- 插件系统能力极窄（仅文本标记 TextMarker），**蓝牙必须改核心源码**
- 许可证 GPLv3：自己两台机器用无任何义务；**若分发修改版二进制必须提供对应源码**

### 5. UI 现状：5.8.5 没有皮肤/QSS 系统（常见误解）
- "换肤"实际 = ini 配置里的十几个颜色槽位 + 图标主题文件夹（`IconSourcePath`）+ 表情主题文件夹（`EmoticonSourcePath`）
- 有**手动**暗色开关（`UseDarkStyle`，5.8.4 加入），**不跟随系统**（跟随是 6.0 特性）
- 已用 macOS 系统字体（SF Pro）；表情可一键切系统 emoji 字体（`UseFontEmoticons=true`）
- 社区吐槽 Top3：观感过时、交互笨拙、暗色/主题长期缺位；另有 macOS 上中文输入法被新消息打断的已报告 bug
- Qt5 Widgets 在 macOS 的天花板：能做"整洁现代"（Fusion+配色+SVG 图标），做不到 100% AppKit 原生质感

---

## 二、本机构建环境就绪度

| 项 | 状态 |
|---|---|
| 编译器 clang 21 / git / make / CLT 27 | ✅ 就绪 |
| SDK 内 IOBluetooth + CoreBluetooth 框架 | ✅ 就绪 |
| 磁盘（153Gi 可用）、蓝牙芯片（BCM4378） | ✅ 就绪 |
| **Qt 开发环境（qmake 等）** | ❌ 零（App 内捆绑的 Qt 5.15.18 运行时不能用于开发） |
| cmake / ninja | ❌ 缺（qmake 直构可不装 cmake） |
| 隐患 | PATH 里有死路径 `~/Library/Android/sdk/cmake/...`；`/usr/local` 有损坏的 Intel brew 残留 |

补装清单：**Qt 5.15.x（建议官方在线安装器，brew 的 qt@5 已弃用）→ 源码 → （可选）cmake**。完整 Xcode 不需要。

---

## 三、源码与版本坐标

- 本机：BeeBEEP 5.8.5 (build 1535，arm64)，捆绑 Qt 5.15.18；ini 显示曾运行过 5.9.1-dev (r1575)
- 权威源码：SourceForge SVN（活跃，HEAD r1609 = 5.9.2-dev）
- 与本机版本完全一致的 GitHub 镜像：`github.com/Stkai/BeeBEEP`（5.8.5，已停更，适合做基线）
- 最新镜像：`github.com/hellitonsm/beebeep`（5.9.1-dev）
- 官方 GitHub `mastroddi/beebeep` 当前 404（应为 6.0 上线时启用）
- 构建：`qmake beebeep-desktop.pro && make`（macOS 有官方打包脚本 `scripts/macx_deploy_arm64_bundle.sh`；Windows 用 Qt Creator/MSVC + windeployqt）

---

## 四、建议路线图（分阶段、每阶段可独立止损）

### Stage 0 · 零代码验证（今天，30 分钟）
1. 两机系统蓝牙配对 → 尝试建立蓝牙 PAN → 拿到 169.254.x.x 互 ping
2. BeeBEEP 菜单 "Connect to friend" 输对方 IP:6475（两端网络密码一致）
3. **判定**：若 PAN 通且体验可接受 → 通信需求已解决，后续改造全部变成"锦上添花"；若 PAN 建不起来（Win11 对 PAN 支持需实测）→ 直接进 Stage 1

### Stage 1 · PoC 双线并行（约 1 周）
- **线 1（环境）**：装 Qt 5.15 → 从 Stkai 镜像拉 5.8.5 源码 → Mac 上先编译出原版（跑通工具链）
- **线 2（蓝牙技术验证，go/no-go 门）**：基于 Qt btchat 示例写 1 天的 RFCOMM 收发 demo，Mac↔Windows 实测：吞吐、已配对/未配对两条发现路径、macOS 25.6 上的稳定性
- PoC 失败降级路径：A → B（BLE）→ C（原生）

### Stage 2 · 集成开发（PoC 通过后，2–4 周）
1. `ConnectionSocket` 传输层抽象重构（TCP 保底，蓝牙为第二传输）
2. 蓝牙发现/连接 UI（用户列表标注"蓝牙"来源）+ 6476 文件通道处理（小文件改走消息通道）
3. **UI L1 配置包（不动代码，随版本附带）**：emoji 字体表情 + 现代亮/暗两套配色预设 + 消息密度微调
4. Windows 端用 Qt Creator 编译（顺手上 64 位，官方 5.x 只有 32 位）

### Stage 3 · UI L2 代码级（按需挑选，各项 1–5 天）
暗色自动跟随系统（ApplicationPaletteChange + NSAppearance 标题栏）｜Dock 未读角标｜修中文输入法被打断｜等宽代码块渲染

### 决策检查点：盯 BeeBEEP 6.0（2026-12）
6.0 发布后重新评估：若其蓝牙 Mesh + 新 UI 满足需求 → 迁移官方版，弃 fork；否则自己的 5.x fork 已按最小改动原则建设，迁移成本可控。**GPLv3 提醒：只要不分发给别人，自用无义务；一旦分发修改版，须提供源码。**

---

## 五、待你确认的 3 个决策点（当前按推荐值假设）

| # | 问题 | 我的假设 | 影响 |
|---|---|---|---|
| 1 | Windows 机是否归你管、能否装自编译 exe？ | 能（两端都归你管） | 若不能 → 应用内蓝牙路线排除，只剩 PAN / 等 6.0 |
| 2 | "小文件"的典型大小？ | <5MB | RFCOMM 下 5MB 约 20–50s；若常传几十 MB → 蓝牙本身不合适，建议另配优盘/局部网络方案 |
| 3 | UI 最想改什么？ | 暗色跟随 + emoji 表情（性价比最高两件） | 决定 Stage 3 挑哪些 |

---

## 附：主要来源
- BeeBEEP：[beebeep.net](https://www.beebeep.net)｜[Download](https://www.beebeep.net/download)｜[News](https://www.beebeep.net/news)｜[OpenSource](https://www.beebeep.net/open-source)｜[reaching-other-networks](https://www.beebeep.net/docs/reaching-other-networks)｜[工单 #1178 蓝牙 Mesh](https://sourceforge.net/p/beebeep/tickets/1178/)｜[工单 #1173 Tailscale](https://sourceforge.net/p/beebeep/tickets/1173)｜[SVN 源码](https://sourceforge.net/p/beebeep/code/HEAD/tree/)｜[Stkai 镜像(5.8.5)](https://github.com/Stkai/BeeBEEP)｜[hellitonsm 镜像(5.9.1-dev)](https://github.com/hellitonsm/beebeep)
- Qt 蓝牙：[Qt5 平台矩阵](https://doc.qt.io/qt-5/qtbluetooth-index.html)｜[Qt6 平台矩阵](https://doc.qt.io/qt-6/qtbluetooth-index.html)｜[QBluetoothServer](https://doc.qt.io/qt-6/qbluetoothserver.html)｜[QBluetoothSocket](https://doc.qt.io/qt-5/qbluetoothsocket.html)｜[btchat 示例](https://doc.qt.io/qt-6/qtbluetooth-btchat-example.html)｜[qtconnectivity macOS 后端源码](https://github.com/qt/qtconnectivity/tree/v5.15.2/src/bluetooth)
- Apple/微软：[IOBluetooth](https://developer.apple.com/documentation/iobluetooth)｜[CoreBluetooth](https://developer.apple.com/documentation/corebluetooth)｜[RFCOMM 收发文件(官方示例)](https://learn.microsoft.com/en-us/windows/apps/develop/devices-sensors/send-or-receive-files-with-rfcomm)｜[Bluetooth FAQ(免配对/Win32 可调 WinRT)](https://learn.microsoft.com/en-us/windows/apps/develop/devices-sensors/bluetooth-dev-faq)
- 吞吐实测：[Punch Through BLE 吞吐 FAQ](https://punchthrough.com/ble-throughput-optimization-faq)｜[Novelbits BT5 速度](https://novelbits.io/bluetooth-5-speed-maximum-throughput)｜[Memfault BLE primer](https://interrupt.memfault.com/blog/ble-throughput-primer)｜[SO: Windows 无 LE L2CAP CoC](https://stackoverflow.com/questions/66391397/use-ble-l2cap-coc-on-windows)
- UI：[BeeBEEP 6 主题文档](https://www.beebeep.net/docs/theming)｜[设置文档](https://www.beebeep.net/docs/settings)｜[SF 评论](https://sourceforge.net/projects/beebeep/reviews)｜[Qt macOS issues](https://doc.qt.io/qt-6/macos-issues.html)｜[KDAB: Say No to Qt Style Sheets](https://www.kdab.com/say-no-to-qt-style-sheets)｜[Qt5→6 迁移成本](https://alpitype.de/en/insights/qt5-to-qt6-migration)
