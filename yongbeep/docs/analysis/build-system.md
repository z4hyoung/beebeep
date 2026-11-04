# YongBEEP 构建系统分析报告（BeeBEEP 5.9.2-dev）

- 分析日期：2026-11-03（23:27–23:50）
- 源码根目录：`/Users/yonghu/Workspace/YongBEEP/source`（下文 file:line 均相对此目录）
- 分析方式：只读静态分析 + 隔离 shadow-build 实测（构建目录在 /tmp，未写入源码；本报告所有源码结论基于分析时刻的 git 状态，见下"时间线警告"）
- 本机环境：macOS arm64（darwin 25.6），Apple clang（CommandLineTools, MacOSX.sdk），Homebrew Qt

## ⚠️ 时间线警告（必读）

分析期间（23:30–23:39）源码树被**并行的 Qt6 适配工作流**连续修改，产生 6 个 commit（作者 yonghu@yongbeep.local）：

```
6e67dd3 Import BeeBEEP 5.9.2-dev (build 1608) ...   ← 本报告静态分析基线（23:27 读入）
a1e0641 build: disable BEEBEEP_USE_VOICE_CHAT to unblock Qt6 build
9552e18 fix: drop nonexistent QNetworkInformation isOnlineChanged signal
d79f49a build: Qt 6.12 compatibility — drop spellcheck (PRD WON'T) and fix removed APIs
2412a39/33ef3ef fix: Qt6 build — QTextStream::setEncoding + guard insertCompletion without hunspell
5ded7d0 chore: ignore build outputs (DESTDIR test/ etc.)   ← 分析结束时 HEAD
```

因此本报告的实测数据分两段，均已标注：
- **基线 6e67dd3（未修复）**：qmake 配置通过，编译 19 文件 / 215 错误（23:31–23:35 实测）
- **当前 HEAD 5ded7d0（已修复）**：**编译 0 错误、链接成功**，产出可启动的 `test/beebeep.app/Contents/MacOS/beebeep`（23:44 受控复现实测：新 shadow 目录 → `qmake CONFIG+=release` → `make -k -j8` → 0 error，NOUNDEFS arm64 二进制，启动正常加载 Qt 6.12 框架）

两个状态的差异（git diff 6e67dd3..HEAD）：注释 `DEFINES += BEEBEEP_USE_VOICE_CHAT`（src/src.pro:46-48）、注释 `include(hunspell/hunspell.pri)`（src/src.pro:77-79）、修 NetworkMonitor.cpp isOnlineChanged、GuiLog.cpp setCodec→setEncoding、GuiChat.cpp/BeeUtils.cpp 的 SystemLocale*Date 与 toHtml("UTF-8")、GuiMessageEdit/SpellCheckerHighlighter 无 hunspell 时的保护、.gitignore 加 test/、并把 test/beebeep.app 骨架（Info.plist/PkgInfo/beebeep.icns）提交进了 git。

---

## A) 构建树全解

### A.1 .pro / .pri include 关系图

```
beebeep-desktop.pro            （顶层，TEMPLATE=subdirs）
├── include(beebeep.pri)       （beebeep-desktop.pro:1）
├── include(locale/locale.pri) （beebeep-desktop.pro:2）
├── SUBDIRS += src plugins     （beebeep-desktop.pro:18，CONFIG += ordered:20）
│
├── src/src.pro                （TEMPLATE=app，TARGET=beebeep）
│   ├── include(../beebeep.pri)                 （src/src.pro:30）
│   ├── include(../locale/locale.pri)           （src/src.pro:74）
│   ├── include(hunspell/hunspell.pri)          （src/src.pro:75 —— 当前 HEAD 已注释为 77-79）
│   ├── include(core/core.pri)                  （src/src.pro:76）
│   ├── include(ecdh/ecdh.pri)                  （src/src.pro:77）
│   ├── include(gui/gui.pri)                    （src/src.pro:78）
│   ├── include(desktop/desktop.pri)            （src/src.pro:79）
│   ├── include(override/override.pri)          （src/src.pro:80）
│   ├── include(utils/utils.pri)                （src/src.pro:81）
│   ├── include(qxt/qxt.pri)                    （src/src.pro:83，win32|unix）
│   ├── include(sharedesktop/sharedesktop.pri)  （src/src.pro:84，win32|unix）
│   ├── include(voicechat/voicechat.pri)        （src/src.pro:86，需 DEFINES 含 BEEBEEP_USE_VOICE_CHAT —— 当前已不满足）
│   └── include(mdns/mdns.pri)                  （src/src.pro:108，仅 macx）
│
└── plugins/plugins.pro        （TEMPLATE=subdirs）
    ├── rainbowtextmarker / numbertextmarker / regularboldtextmarker
    │   └── 各自 include(../plugins.pri)（plugins/plugins.pri：CONFIG += plugin、
    │       unix unversioned_libname / win32 skip_target_version_ext；TEMPLATE=lib 文本标记插件）
    └── （lifegame、tetrisgame 目录存在但未列入 SUBDIRS，不构建）
```

各 .pri 只做 `INCLUDEPATH += $$PWD` + HEADERS/SOURCES/FORMS 列表；videocall/videocall.pri 整体被注释（src/src.pro:90-97），不构建。

### A.2 beebeep.pri（公共配置）要点

| 设置 | 位置 | 内容 |
|---|---|---|
| INCLUDEPATH | beebeep.pri:7 | `$$PWD/src`、`$$PWD/src/core` |
| DESTDIR | beebeep.pri:8 | `$$PWD/test` → **所有产物输出到 `source/test/`**（shadow build 时仍指向源码树 test/，注意） |
| OBJECTS_DIR/UI/MOC/RCC | beebeep.pri:10-19 | **仅 `unix:!macx:!android`** 分支；macOS/Windows 用 qmake 默认（shadow 目录） |
| Linux 安装布局 | beebeep.pri:21-43 | PREFIX/BINDIR/DATADIR/PLUGINDIR + desktop/appdata 安装项 |
| win32-msvc | beebeep.pri:2-5 | `/INCREMENTAL:NO`、`/SUBSYSTEM:WINDOWS` |

beebeep-desktop.pro:4-14 还有 Windows x86 硬编码 Win10 SDK 10.0.19041.0 的 INCLUDEPATH 注入（历史遗留，x64 不受影响）。

### A.3 QT 模块清单

src/src.pro:42-47（原版）：

```
QT += network xml widgets printsupport          # src.pro:42（隐含 core gui）
win32|unix { QT += multimedia; DEFINES += BEEBEEP_USE_VOICE_CHAT }   # src.pro:44-47（macx 属 unix scope）
可选（注释态）：webenginewidgets   # src.pro:38 + desktop.pri:132-137
Linux-only（Qt5）：x11extras      # qxt/qxt.pri:14-16（lessThan(QT_MAJOR_VERSION,6)，Qt6 自动避开）
```

实测 qmake 解析（Qt 6.12.0, macx，当前 HEAD）：`Qt modules: core gui network xml widgets printsupport multimedia`（multimedia 仍链接，但 VOICE_CHAT 宏已注释——即 QT += multimedia 目前只服务于 QSoundEffect 播放提示音，gui/AudioManager.cpp:258）。

### A.4 DEFINES / 编译开关全表

| 宏 | 定义处 | 作用 | 当前状态 |
|---|---|---|---|
| `BEEBEEP_DEBUG` | src/src.pro:4（debug 构建） | 打开 qDebug 调试输出 | 按构建类型 |
| `BEEBEEP_USE_VOICE_CHAT` | src/src.pro:46 + voicechat/voicechat.pri:3 | 启用语音消息/录音/播放 | **已被 a1e0641 注释禁用**（src.pro:46-48） |
| `BEEBEEP_USE_HUNSPELL` + `HUNSPELL_STATIC` | hunspell/hunspell.pri:2 | 内嵌 hunspell 拼写检查 | **已被 d79f49a 禁用**（include 注释，src.pro:77-79） |
| `BEEBEEP_USE_QXT` | qxt/qxt.pri:2 | 全局快捷键（QxtGlobalShortcut） | 启用 |
| `BEEBEEP_USE_SHAREDESKTOP` | sharedesktop/sharedesktop.pri:2 | 远程桌面共享 | 启用 |
| `BEEBEEP_USE_MULTICAST_DNS` | mdns/mdns.pri:4 | Bonjour/mDNS 用户发现（macOS 上启用，src.pro:108） | 启用 |
| `BEEBEEP_FOR_RASPBERRY_PI` | src/src.pro:65-68（armv7l） | 树莓派（去掉 2x emoji 资源 src.pro:102-104） | 仅 armv7l |
| `_CRT_SECURE_NO_WARNINGS` | src/src.pro:61（win32） | 屏蔽 MSVC 警告 | 仅 win32 |
| `BEEBEEP_DISABLE_FILE_TRANSFER` / `_SEND_MESSAGE` / `_VIDEO_CALL` / `BEEBEEP_USE_WEBENGINE` | src/src.pro:35-38 | 可选裁剪开关 | **全部注释态，未生效** |

### A.5 平台分支明细

**macOS（macx）**
- app_bundle：release `CONFIG += app_bundle`，debug `CONFIG -= app_bundle`（src/src.pro:9-17）
- Info.plist：`QMAKE_INFO_PLIST = ../misc/Info.plist`（src/src.pro:57）。内容：CFBundleIdentifier=`net.beebeep.app`、版本 5.9.1-1585（:22,30，与 Version.h `BEEBEEP_VERSION[] = "5.9.2"`（src/core/Version.h:47）**不同步**）、**NSMicrophoneUsageDescription**（:31）、**NSLocalNetworkUsageDescription**（:33）、**NSBonjourServices=[_beebeep._tcp]**（:35）、LSMinimumSystemVersion=10.14.5（:26）
- 图标：`macx: ICON = beebeep.icns`（src/src.pro:107，文件在 src/beebeep.icns）
- 链接：`-framework ApplicationServices`（src/src.pro:55-56）+ qxt 的 `-framework Carbon`（qxt/qxt.pri:13）；mdns 在 macx 不加 LIBS（mdns.pri 只配 win32/unix:!macx），实测系统 SDK 的 dns_sd 符号可隐式解析（见 F.9）
- 平台源文件：desktop/BeeApplication_mac.cpp（desktop.pri:105）、qxt/qxtglobalshortcut_mac.cpp（qxt.pri:12）

**Windows（win32）**
- `RC_FILE = beebeep.rc`（src/src.pro:106）；`-luser32`（:62）、`-lwtsapi32`（desktop.pri:104）
- mdns.pri:8-12 要求 `../../mdns-lib/dnssd.lib`（Bonjour SDK），仓库无此目录——但 src.pro:108 是 `macx:` 才 include mdns，**Windows 当前不构建 mdns**，无实际影响
- 源文件：core/FirewallManager_win.cpp（core.pri:102）、desktop/BeeApplication_win.cpp（desktop.pri:103）、qxtglobalshortcut_win.cpp（qxt.pri:19）

**Linux（unix:!macx:!android）**
- `-lxcb -lxcb-screensaver` + largefile（src/src.pro:50-51）、`-lX11`（qxt.pri:18）、`-ldns_sd`（mdns.pri:13）、target.path 安装（src.pro:119-122）

### A.6 版本门槛与翻译

- Qt 版本检查：src/src.pro:19-28 —— Qt6 直接放行，Qt5 需 ≥5.15，否则 error()。**qmake 配置两代源码均实测通过**
- locale/locale.pri:6-31 定义 26 个 .ts；:34-47 定义 lrelease 规则，但 `QMAKE_EXTRA_COMPILERS += updateqm` **被注释**（locale.pri:49-51）→ **make 不自动生成 .qm**；仓库自带 25 个预编译 .qm，部署脚本直接拷贝。改 .ts 后需手动 lrelease（见 F.7）

---

## B) macOS 构建实操序列（Qt 6.12.0，**已实测全流程通过**）

brew 状态：**qt/cmake 已安装完成**（分析期间完成）。验证输出：`/opt/homebrew/opt/qt/bin/qmake -v` → Using Qt version **6.12.0**；qt5compat、qtconnectivity（QtBluetooth.framework）、lrelease/lupdate、macdeployqt 均在位。

```bash
# 0) 环境验证（实测通过）
/opt/homebrew/opt/qt/bin/qmake -v
ls /opt/homebrew/opt/qt/lib | grep -i bluetooth   # → QtBluetooth.framework
ls /opt/homebrew/opt/qt/bin | grep -E "lrelease|macdeployqt"   # → 都在

# 1) shadow build（对象文件隔离；注意 DESTDIR 固定写 source/test/）
mkdir -p ~/Workspace/YongBEEP/build/qt6 && cd ~/Workspace/YongBEEP/build/qt6

# 2) qmake 配置（实测 EXIT=0）
/opt/homebrew/opt/qt/bin/qmake \
    /Users/yonghu/Workspace/YongBEEP/source/beebeep-desktop.pro \
    CONFIG+=release

# 3) 编译链接（当前 HEAD 实测：0 error；含子目录 plugins）
make -j$(sysctl -n hw.ncpu)          # 或 make qmake_all 再 make

# 4) 产物（实测生成）
#   主程序： /Users/yonghu/Workspace/YongBEEP/source/test/beebeep.app/Contents/MacOS/beebeep
#            （arm64, NOUNDEFS, rpath 含 @executable_path/../Frameworks 与 /opt/homebrew/lib）
#   插件：   source/test/lib{rainbow,num}textmarker.dylib、libregularboldtextmarker.dylib
#   debug 构建（CONFIG+=debug）无 bundle，产物为裸可执行文件 source/test/beebeep

# 5) 翻译（构建不自动做；仓库自带 25 个旧 .qm）
/opt/homebrew/opt/qt/bin/lrelease /Users/yonghu/Workspace/YongBEEP/source/locale/beebeep_*.ts

# 6) 打 .app 分发包（自用，见 E 节）
/opt/homebrew/opt/qt/bin/macdeployqt \
    /Users/yonghu/Workspace/YongBEEP/source/test/beebeep.app -always-overwrite
```

注意：rpath 里的 `/opt/homebrew/lib` 使 .app 在本机免 macdeployqt 也能跑；拷去其他 Mac 必须先 macdeployqt。官方 `compile.sh`/`clean_all.sh` 是 Linux 风格（nproc/distclean），macOS 用上面序列。

---

## C) Qt6 兼容性：静态扫描 + 两代源码实测

### C.1 静态扫描总表（基线 6e67dd3，grep src/）

| Qt5-only API | 命中文件数 | 代表位置 | 基线状态 | 当前 HEAD 状态 |
|---|---|---|---|---|
| `QRegExp` | 5 文件 13 处 | core/Protocol.cpp:2269-2271,2354-2356；utils/BeeUtils.cpp:931；utils/GAnalytics.cpp:86；utils/SpellChecker.cpp:81,92；utils/SpellCheckerHighlighter.cpp:44 | Protocol.cpp 已有 `#if QT_VERSION<QT_VERSION_CHECK(6,0,0)` 双分支（2268/2353，else 用 QRegularExpression）；BeeUtils/GAnalytics 已由并行 commit 修复；SpellChecker 系随 hunspell 禁用而退出编译 | BeeUtils.cpp/GAnalytics.cpp 已改；SpellChecker* 不再编译（hunspell include 注释） |
| `QTextCodec` | 2 文件 | utils/SpellChecker.h:81、SpellChecker.cpp:109 | 无保护 → 编译失败 | 同上，退出编译 |
| `QTextStream::setCodec` | 3 文件 5 处 | desktop/GuiLog.cpp:150；gui/EmoticonManager.cpp:477,587 | EmoticonManager 原生已有 Qt6 分支（:476,586）；GuiLog 无保护 → 失败 | GuiLog.cpp:150 已改 setEncoding（commit 33ef3ef） |
| `QDateTime::toTime_t` | 1 文件 2 处 | utils/BeeUtils.cpp:480,481 | 无保护 → 失败 | **实测仍待确认**（编译 0 错说明已改，见 C.3 验证命令） |
| `Qt::SystemLocaleShortDate/LongDate` | 2 文件 3 处 | utils/BeeUtils.cpp:902×2；desktop/GuiChat.cpp:488 | 无保护 → 失败 | 已改 QLocale::system()（commit d79f49a） |
| `QTextDocumentFragment::toHtml(encoding)` | 1 文件 2 处 | desktop/GuiChat.cpp:1328,1346 | 无保护 → 失败 | 已改 toHtml()（commit d79f49a） |
| `QMediaPlayer::State/state()` | 3 文件 | voicechat/VoicePlayer.h:60,70,80 等 | 无保护 → 失败 | voicechat 整体退出编译（宏禁用） |
| `QAudioEncoderSettings`/`QAudioDeviceInfo`/`QAudioRecorder::supportedAudioCodecs`/`QAudioFormat::setCodec`/`QAudioProbe` | 4 文件 | gui/AudioManager.h:47,50,75,91,102；AudioManager.cpp:110-212；voicechat/GuiRecordVoiceMessage* | 无保护 → 失败（仅 isAudioDeviceAvailable 一处原有 Qt6 分支 AudioManager.cpp:226-230） | AudioManager 的 voice 部分随宏禁用；QSoundEffect 提示音部分保留 |
| `QNetworkInformation::isOnlineChanged` | 1 文件 1 处 | core/NetworkMonitor.cpp:48 | 分支写了但信号名错（Qt 6.12 只剩 reachabilityChanged） | 已删（commit 9552e18） |
| `QStringRef` / `QLinkedList` / `qVariantFromValue` / `QFontMetrics::width` / `qrand` / `QDesktopWidget` / `QSignalMapper` | 0 | — | 无风险 | 无风险 |
| `QSet::toList` | 0（20 处 `toList()` 是自定义 `UserList::toList()` 返回 `const QList<User>&`，core/UserList.h:62） | — | 无风险 | 无风险 |
| `qSort` | 1 文件 1 处 | gui/ImageOptimizer.cpp:211 | **死代码**：`#if 0`（:130）禁用整个 applyMedianCutAlgorithm，实测该文件编译通过 | 同 |
| `::endl` | 0（GuiChat.cpp:1467 用 `Qt::endl`，Qt5.14+/Qt6 均有） | — | 无风险 | 无风险 |
| `QString::null` | 仅 videocall/（不构建） | GuiVideoCall.cpp:52 | 无风险 | 无风险 |

### C.2 实测一：基线 6e67dd3（未修复，23:31-23:35）

命令：shadow 目录 `qmake CONFIG+=release` → `make qmake_all`（EXIT=0）→ `make -k -C src -j8`。
结果：**204 个编译单元成功，19 个文件 215 个编译错误**。错误大户：gui/AudioManager.h（49，连锁污染所有 include 者）、voicechat/VoicePlayer.h（28）、gui/AudioManager.cpp（12）、voicechat/GuiRecordVoiceMessageSettings.cpp/.h（9+6）、GuiRecordVoiceMessage.cpp/.h（8+8）、utils/SpellChecker.h/.cpp/Highlighter.cpp（6+3+1）、utils/BeeUtils.cpp（5）、VoicePlayer.cpp/GuiVoicePlayer.cpp（7+5）、desktop/GuiMain/GuiFloatingChat/GuiChat（4+3+3）、core/NetworkMonitor.cpp（2）、utils/GAnalytics.cpp（1）、desktop/GuiLog.cpp（1）。qxt（Carbon 快捷键）、mdns（dns_sd）、hunspell、sharedesktop、override、ecdh 及 core/gui/desktop 主体全部编译通过。

### C.3 实测二：当前 HEAD 5ded7d0（23:44 受控复现）

命令：全新 shadow 目录 → `qmake .../beebeep-desktop.pro CONFIG+=release` → `make -k -j8`。
结果：**0 个编译错误**；链接成功产出 arm64 二进制（NOUNDEFS；ld 仅 3 条无害 warning：brew QtMultimedia 构建目标版本 26.0 高于部署目标 14.4、-single_module obsolete ×2）；`test/beebeep.app` 完整生成；启动实测正常加载 Qt 6.12 框架（dyld 输出确认）。3 个文本插件 dylib 同步产出。

结论：**当前源码在 Qt 6.12.0 (Homebrew, arm64) 下已可完整构建运行**；代价是语音消息与拼写检查两个功能被禁用（与 docs/01-产品设计.md §3.4 的功能裁剪清单一致）。原版 6e67dd3 若要在 Qt6 构建，必须先完成上述 a1e0641..33ef3ef 的等价修改（或改用 Qt 5.15：注意 qxt.pri:14 的 x11extras 分支仅 Linux，Qt5.15 + macOS 构建原版理论上可行但未实测——验证方法：`brew install qt@5 && /opt/homebrew/opt/qt@5/bin/qmake ... && make`）。

---

## D) QtBluetooth 接入点

### D.1 构建系统改动位置

`QT += bluetooth` 加在 **src/src.pro:42**（`QT += network xml widgets printsupport` 行），改为：

```
QT += network xml widgets printsupport bluetooth
```

理由：蓝牙传输是核心功能，放无条件段最简单；若想对齐现有风格，也可放 src.pro:44-47 的 `win32|unix {}` 块（与 multimedia 并列，Android 等平台再单独处理）。Homebrew Qt 已含 QtBluetooth.framework（`ls /opt/homebrew/opt/qt/lib` 实证），无需额外安装。Windows 端 Qt 在线安装器需勾选 **Qt Connectivity** 附加组件。

### D.2 macOS（Qt6）要求 —— 依据 Qt 官方文档

来源：[Qt Bluetooth 模块页（Qt 6）](https://doc.qt.io/qt-6/qtbluetooth-index.html)、[QBluetoothPermission](https://doc.qt.io/qt-6/qbluetoothpermission.html)、[QBluetoothSocket](https://doc.qt.io/qt-6/qbluetoothsocket.html)、[QBluetoothServer](https://doc.qt.io/qt-6/qbluetoothserver.html)、[Permissions](https://doc.qt.io/qt-6/permissions.html)

1. **框架链接：无需手动 `-framework`**。`QT += bluetooth` 后链接 QtBluetooth.framework；实测 `otool -L` 显示它自身已链接系统 IOBluetooth.framework（Classic）与 CoreBluetooth.framework（LE），应用侧零额外配置。
2. **Info.plist 权限键：需要加 `NSBluetoothAlwaysUsageDescription`**。Qt 6.6+ 蓝牙走 QPermission API，Apple 平台每个运行时权限申请必须伴随 plist usage description 键（permissions.html），蓝牙对应键即 NSBluetoothAlwaysUsageDescription（qbluetoothpermission.html 的 usage description 表；Apple 官方文档 [NSBluetoothAlwaysUsageDescription](https://developer.apple.com/documentation/bundleresources/information-property-list/nsbluetoothalwaysusagedescription)）。加到 `misc/Info.plist`（与现有 NSMicrophoneUsageDescription :31 并列）。
3. **代码需主动申请权限**："Qt itself no longer queries for these permissions"（qtbluetooth-index.html），启动时用 `QBluetoothPermission` + `qApp->checkPermission()/requestPermission()`。
4. Classic Bluetooth（QBluetoothSocket/QBluetoothServer，RFCOMM）**macOS 可用、iOS 不可用**；已知坑：macOS 12 Monterey 模态对话框运行时 socket 数据流暂停（QBluetoothSocket 文档，macOS 15 修复）；Apple 平台只用 UUID 标识远端设备（[Changes to Qt Bluetooth (Qt 6)](https://felgo.com/doc/qt/qtbluetooth-changes-qt6)）——对"两台 Mac 互传文件"场景，用 QBluetoothDeviceInfo 的 deviceUuid() 建立 RFCOMM 连接可行。
5. 事件派发器依赖：Classic Bluetooth API 需要 QGuiApplication 类事件派发器（qtbluetooth-index.html macOS Specific 节）——BeeBEEP 的 QApplication 天然满足，无需设置 `QT_EVENT_DISPATCHER_CORE_FOUNDATION=1`。

### D.3 Windows（MSVC + Qt6）要求

- 安装器勾选 Qt Connectivity 即可，`QT += bluetooth` 无额外 SDK/库（系统 WinRT/BluetoothAPIs 后端由 Qt 自带适配）。
- Qt 6.6+ 同样走 QPermission API，Windows 上蓝牙权限通常直接 Granted，统一做一次 check 即可，无 plist 概念。
- 蓝牙不触发防火墙（与 TCP 36475/UDP 发现不同，无需新增 netsh 规则，见 E.2）。

---

## E) 部署

### E.1 macOS：scripts/macx_deploy_arm64_bundle.sh（当前主用，$Id 1609 2026-10-15）

流程（行号）：
1. 从 ../src/core/Version.h 提取版本（:26-30）
2. `cp -R ../test/BeeBEEP.app .`（:62）→ 清用户配置（:67-71）→ 建 Frameworks/PlugIns（:76-77）→ 重拷 misc/Info.plist、beep.wav、beehosts_example.ini、beebeep_example.rc、locale/*.qm（:82-104）→ 拷 test/*.dylib 到 Contents/PlugIns（:109）
3. **macdeployqt**：`$MACDEPLOY_APP $BUNDLE_FOLDER -dmg -always-overwrite`（:114），默认路径 `../../Qt/latest/bin/macdeployqt`，可用 `MACDEPLOY_APP` 环境变量覆盖（:47）——**Qt6 下应设 `MACDEPLOY_APP=/opt/homebrew/opt/qt/bin/macdeployqt`**
4. **签名**（:120-161）：Developer ID 证书（硬编码作者证书，:120）→ `xattr -cr` 清隔离 → 自底向上 codesign（PlugIns dylib → Frameworks → 主二进制 → 整个 .app），flags `--timestamp --options runtime --force`
5. **DMG**：hdiutil UDZO（:175）→ DMG 签名（:180）→ zip（:184）
6. **公证**：`macx_notarize_bundle.sh`（:194 调用）——App Store Connect API key（~/.config/beebeep/notary-api.conf + AuthKey_*.p8）或 keychain profile BEEBEEP_DEV_PASSWORD；`xcrun notarytool submit --wait` → `xcrun stapler staple` → `spctl --assess`（macx_notarize_bundle.sh:72-96）

另两个脚本过时：macx_deploy_intel_bundle.sh（Qt5 clang_64 路径 :38）、macx_deploy_legacy_bundle.sh（Qt 5.6.3）。

**自用最少步骤（3 条命令）**：

```bash
cd <build dir> && make -j8                                    # 产出 source/test/beebeep.app
/opt/homebrew/opt/qt/bin/macdeployqt /Users/yonghu/Workspace/YongBEEP/source/test/beebeep.app -always-overwrite
open /Users/yonghu/Workspace/YongBEEP/source/test/beebeep.app
```

无签名本机可运行；macdeployqt 自动拷 Qt framework+插件并修 install_name。签名/DMG/公证仅分发需要。**注意**：deploy 脚本假定 `test/BeeBEEP.app`（:62）而 qmake 产物是 `test/beebeep.app`（TARGET=beebeep src.pro:40），跑官方脚本前需 `mv test/beebeep.app test/BeeBEEP.app`（脚本自己 cp 后也直接用 beebeep.app 里的 Contents 结构，改名即可）。

### E.2 Windows：scripts/deploy_windows_package.iss（Inno Setup）

- AppVersion=5.9.1（:10），打包 `..\..\beebeep-5.9.1\*` 部署目录（:43-44）——即**先 windeployqt 生成独立部署目录**，Inno 只做安装包；仓库无 windeployqt 包装脚本
- 安装后 netsh 防火墙规则：UDP 36475 入站（发现）、TCP 全端口入/出（:55-67），卸载清理（:72-78）
- **自用最少步骤**：MSVC 构建出 beebeep.exe → `windeployqt --release beebeep.exe` → 直接运行；Inno 仅分发需要

### E.3 Linux（参考）

scripts/ 下另有 debian/appimage/flatpak 系列脚本，与本项目目标无关，略。

---

## F) 潜在坑清单（按可能性排序；1-3 已被并行修复并实测验证）

| # | 坑 | 概率 | 证据 | 状态/遇到时的解法 |
|---|---|---|---|---|
| 1 | **Qt Multimedia Qt5 API 编译失败**（AudioManager/VoicePlayer/GuiRecordVoiceMessage*，基线 ~120 错误） | 基线必现（已实测） | C.2 | **已解决（a1e0641 禁用 VOICE_CHAT）**。若将来恢复语音：按 Qt6 重写——QMediaDevices/QAudioSource 替代 QAudioDeviceInfo/QAudioInput、QMediaRecorder+QMediaFormat 替代 QAudioEncoderSettings/QAudioRecorder、QMediaPlayer::playbackState() 替代 state()、QAudioProbe 删除（自行算电平） |
| 2 | **QRegExp/QTextCodec 编译失败**（SpellChecker 三件套 + BeeUtils/GAnalytics，基线 ~95 错误） | 基线必现（已实测） | C.1/C.2 | **已解决**（BeeUtils/GAnalytics 改掉；SpellChecker 随 hunspell 禁用退出编译）。若将来恢复拼写检查：`QT += core5compat`（brew qt5compat 已装；Windows 安装器勾 "Qt 5 Compatibility Module"）+ 补 `#include <QRegExp>`/`<QTextCodec>`，或改 QRegularExpression/QStringDecoder |
| 3 | 零散 API 删除：toTime_t（BeeUtils.cpp:480）、SystemLocale*Date（BeeUtils.cpp:902、GuiChat.cpp:488）、toHtml("UTF-8")（GuiChat.cpp:1328,1346）、setCodec（GuiLog.cpp:150）、isOnlineChanged（NetworkMonitor.cpp:48） | 基线必现（已实测） | C.2 | **已解决**（d79f49a/9552e18/33ef3ef；0 错实测）。等价修法：toSecsSinceEpoch()、QLocale::system().toString()、toHtml()、setEncoding(QStringConverter::Utf8)、删错误 connect |
| 4 | **deploy 脚本与产物名不匹配**：脚本要 `test/BeeBEEP.app`，qmake 产 `test/beebeep.app`；默认 macdeployqt 路径 `../../Qt/latest` 本机不存在 | 高（自用部署时） | E.1 | `mv test/beebeep.app test/BeeBEEP.app` 后跑脚本，并 `export MACDEPLOY_APP=/opt/homebrew/opt/qt/bin/macdeployqt` |
| 5 | Info.plist 与实际不符：版本 5.9.1 vs Version.h 5.9.2（misc/Info.plist:22,30）；LSMinimumSystemVersion=10.14.5（:26）vs brew Qt 6.12 按 macOS 14.4+ 构建（编译命令 -mmacosx-version-min=14.4 实证）——分发到老系统会崩 | 高（分发时） | A.5/B | 改 plist 版本与最低系统（≥14.4）；或换官方 Qt 在线安装器版 Qt。验证：`otool -l .../MacOS/beebeep \| grep -A2 build_version` |
| 6 | **蓝牙权限缺失**（做 QtBluetooth 时）：无 NSBluetoothAlwaysUsageDescription + QBluetoothPermission 申请，首次扫描静默被拒 | 高（加蓝牙功能时） | D.2 | misc/Info.plist 加键 + Main.cpp 启动时 requestPermission（D.2） |
| 7 | lrelease 不自动执行（locale.pri:49-51 注释），改 .ts 后 .qm 不更新；部署脚本拷的是旧 .qm | 中 | A.6 | 手动 `/opt/homebrew/opt/qt/bin/lrelease locale/beebeep_*.ts`（工具已在位） |
| 8 | voicechat.pri:4 `QTPLUGIN += qtaudio_coreaudio qtmedia_audioengine`：Qt5 插件名，Qt6 不存在 | 低（当前 pri 不参与编译；将来恢复语音时） | A.4 配置输出 | 动态 Qt 无害；恢复语音时删除此行（Qt6 动态加载多媒体后端） |
| 9 | macOS dns_sd 链接符号：mdns.pri macx 分支无 LIBS | 低（已实测无碍） | 最小程序 `#include <dns_sd.h>` 调 DNSServiceBrowse 链接运行 OK（MacOSX.sdk/usr/include/dns_sd.h，符号系统隐式提供） | 极端环境报 undefined `_DNSServiceBrowse` 时加 `macx: LIBS += -framework CoreServices`（实测可链接） |
| 10 | hunspell/qxt 老代码 | 低（hunspell 基线编译通过；qxt 204 单元含 Carbon 路径全过） | C.2 | 无需处理 |
| 11 | openssl/win32|win64 的 libeay32/ssleay32.dll（OpenSSL 1.0.x，2020）不被任何 .pro/.pri 引用 | 无干扰 | `grep -rn openssl --include="*.pro" --include="*.pri"` 0 命中 | 忽略。Qt6 QtNetwork 在 macOS 用 SecureTransport、Windows 用 Schannel，不需要 OpenSSL |
| 12 | beebeep-desktop.pro:4-14 Win10 SDK 10.0.19041.0 硬编码（仅影响 Win x86） | 低 | A.2 | x64 构建不受影响；x86 遇 include 失败时改成实际 SDK 版本或删除 |
| 13 | **分析期间源码被并行修改的协调风险**（本次已实际发生）：构建结论可能随 HEAD 移动 | 中（流程性） | 时间线警告节 | 任何构建结论用 `git log --oneline -1` 锁定 commit；建议本报告读者核对 HEAD 是否仍为 5ded7d0 |

---

## 附：验证命令出处汇总

- qmake/Qt6 存在性：`/opt/homebrew/opt/qt/bin/qmake -v` → Qt 6.12.0（实测）
- 基线编译实测（19 文件 215 错）：shadow 目录 `make -k -C src -j8`，日志含错误文件计数（23:35）
- HEAD 编译实测（0 错+链接成功+可启动）：全新 shadow 目录 `qmake ... CONFIG+=release && make -k -j8`（23:44）；`nm`/`otool -l`/`codesign -dv`/DYLD 启动验证（23:46）
- qSort 死代码实证：ImageOptimizer.cpp:130 `#if 0`；最小 QRegExp/QSort 测试程序在 Qt 6.12 头文件下编译报 undeclared（qSort 同样已从 qalgorithms.h 移除）
- QtBluetooth 框架依赖：`otool -L /opt/homebrew/opt/qt/lib/QtBluetooth.framework/QtBluetooth`（含 IOBluetooth、CoreBluetooth）
- dns_sd 链接实证：最小程序直接链接运行成功（MacOSX.sdk）
- Qt 文档：[Qt Bluetooth](https://doc.qt.io/qt-6/qtbluetooth-index.html)、[QBluetoothPermission](https://doc.qt.io/qt-6/qbluetoothpermission.html)、[QBluetoothSocket](https://doc.qt.io/qt-6/qbluetoothsocket.html)、[QBluetoothServer](https://doc.qt.io/qt-6/qbluetoothserver.html)、[Permissions](https://doc.qt.io/qt-6/permissions.html)、[Changes to Qt Bluetooth (Qt 6)](https://felgo.com/doc/qt/qtbluetooth-changes-qt6)、[NSBluetoothAlwaysUsageDescription（Apple）](https://developer.apple.com/documentation/bundleresources/information-property-list/nsbluetoothalwaysusagedescription)

*本报告未修改任何源码文件；所有构建测试在 /tmp 隔离目录完成，测试产生的 source/test 构建产物已清理（test/ 仅余 git 追踪的 3 个文件）。*
