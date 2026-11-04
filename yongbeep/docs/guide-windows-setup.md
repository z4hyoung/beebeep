# Windows 侧环境搭建与 M1 实测指引

> 目的：在那台 Windows 电脑上跑起来 (a) BtPoc 蓝牙吞吐测试器 (b) 将来的 YongBEEP 主程序
> 预计耗时：装机 45–60 分钟（一次性），跑测试 15 分钟
> 前提：Windows 10 1903+ 或 Windows 11，管理员权限，能上网

---

## 一、装工具（一次性，约 45 分钟）

### 1. Visual Studio 2022 Community（提供 MSVC 编译器）
1. 下载：https://visualstudio.microsoft.com/zh-hans/downloads/ → Community 2022（免费）
2. 安装时勾选工作负载：**"使用 C++ 的桌面开发"**（默认勾选项保留即可）
3. ⚠️ 更新（2026-11）：**MinGW 工具链也已可用**——蓝牙传输已改为原生 Win32 后端（`src/bluetooth/BtWinWorker.*`：BluetoothAPIs + AF_BTH 套接字），不再依赖 QtBluetooth 的 WinRT 后端；MinGW 构建（如 Qt 6.5.3 win64_mingw + MinGW 13.1）即可完整编译运行。MSVC 路径仍然有效，二选一即可

### 2. Qt 6（在线安装器，免费但需注册 Qt 账号）
1. 注册账号：https://login.qt.io/register
2. 下载在线安装器：https://www.qt.io/download-open-source → "Download the Qt Online Installer"
3. 安装时组件选择：
   - **Qt 6.12**（或列表里最新的 6.x）→ 勾选 **MSVC 2022 64-bit**
   - Additional Libraries → **Qt Connectivity**（蓝牙模块；MSVC 套件默认已含，没看到就不用管）
   - **Qt Creator**（安装器默认勾选，保留）
4. 装完打开 Qt Creator 一次，工具 → 选项 → Kits，确认检测到 MSVC 2022 的 Kit 且无黄色警告

## 二、拿代码到 Windows

两种方式任选：
- **方式 A（推荐）**：等 Mac 侧推送 GitHub 后 `git clone <你的仓库地址>`（Windows 装个 Git for Windows: https://git-scm.com/download/win）
- **方式 B（先跑 PoC 等不及）**：Mac 上 `poc-bt/` 文件夹（只有 3 个文件）用 U 盘/微信传到 Windows

## 三、编译并运行 BtPoc（M1 实测）

1. Qt Creator → 打开项目 → 选 `BtPoc.pro`
2. Kit 选 **MSVC 2022 64-bit**，构建模式 **Release**
3. 点左下 ▶ 运行——首次会弹"允许应用使用蓝牙"，选**允许**
4. 窗口出现即成功（Mac 侧同样编译好了 `build/poc-bt/BtPoc.app`）

## 四、双机实测流程（对应 poc-bt/README.md 的 T1–T5）

| 步骤 | Mac | Windows |
|---|---|---|
| 0 系统配对 | 系统设置→蓝牙→配对 Windows | 设置→蓝牙→配对 Mac |
| T1 文本 | 点"开始监听" | 点"扫描设备"→选中 Mac→"连接"→输入文本→"发文本" |
| T2 吞吐 | 观察日志 ★ 行 | 连接后点"跑 1MB 吞吐"（重复 3 次取均值） |
| T3 免配对 | — | 删除配对后重新扫描连接，记录现象 |
| T4 稳定性 | — | 连续 10 次 1MB；传输中关蓝牙再开 |
| T5 反转 | 点"扫描设备"→连 Windows | 点"开始监听" |

**判定**：T2 双向均值 ≥100 KB/s → Go（进入蓝牙传输集成）；Mac 侧失败 → 转 BLE 方案；Windows 侧失败 → 评估混合方案。结果填进 `poc-bt/README.md` 的结论记录表。

## 五、常见问题

| 现象 | 处理 |
|---|---|
| Qt Creator 找不到 MSVC Kit | VS2022 没装 C++ 负载，回 §一.1 补装 |
| listen 失败 / 扫描错误 | Windows 蓝牙开关没开；或用了 MinGW Kit（换 MSVC） |
| 扫描看不到对方 | 确认对方"监听"已开、双方都可被发现（系统蓝牙设置）；经典蓝牙发现本身较慢（10–30s），多点几次扫描 |
| 连接卡住不报错 | 大概率未配对且系统弹窗被忽略——看任务栏，或先完成系统配对再连 |
| 防火墙弹窗 | 允许（PoC 只在本机环回+蓝牙，不走网络） |

## 六、（将来）编译 YongBEEP 主程序

同一套环境：Qt Creator 打开仓库根目录的 `beebeep-desktop.pro` → MSVC Kit → Release → 构建。产物在 `test/yongbeep.exe` 附近，后续会给出 windeployqt 打包步骤（M5 阶段）。
