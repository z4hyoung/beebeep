# YongBEEP GUI 侧深度分析报告（微信式极简改造）

- 源码基线：BeeBEEP 5.9.2-dev（`src/core/Version.h`），Qt5.15/Qt6 Widgets C++
- 分析范围：`src/gui`、`src/desktop`，兼及 `src/core`（Settings/Chat/ChatMessage/Core 发送链）、构建文件（`.pro/.pri`）、`locale`
- 规范：只读分析，全部结论来自实际读到的代码；行号格式 `file:line`，`src/` 前缀省略为相对 `source/src/`

---

## A) 主窗口结构地图

### A.1 真正的主窗口类是 `GuiMain`（不是 GuiMainWindow）

`desktop/GuiMain.h:59`：`class GuiMain : public QMainWindow`，objectName 为 `"GuiMainWindow"`（`desktop/GuiMain.cpp:88`）。在 `desktop/Main.cpp:260` 栈上创建：`GuiMain mw;`。

### A.2 主窗口构成（清单）

| 部件 | 类型 | 装载位置 | 代码 |
|---|---|---|---|
| 中央区 | `QTabWidget* mp_tabMain`，Tab 位置 **South（底部标签）** | `setCentralWidget()` | `desktop/GuiMain.cpp:94-97` |
| Tab1 活动 | `GuiHome* mp_home`（系统消息流 GuiSystemViewer） | addTab 图标 activities.png | `desktop/GuiMain.cpp:1568-1573` |
| Tab2 用户 | `GuiUserList* mp_userList`（QTreeWidget） | addTab user-list.png | `desktop/GuiMain.cpp:1575-1578` |
| Tab3 聊天 | `GuiChatList* mp_chatList`（QTreeWidget） | addTab chat-list.png | `desktop/GuiMain.cpp:1580-1583` |
| Tab4 群组 | `GuiGroupList* mp_groupList` | addTab group.png | `desktop/GuiMain.cpp:1585-1588` |
| Tab5 已存 | `GuiSavedChatList* mp_savedChatList` | addTab saved-chat-list.png | `desktop/GuiMain.cpp:1590-1593` |
| 文件传输 dock | `QDockWidget* mp_dockFileTransfers`（内装 `GuiFileTransfer`） | `addDockWidget(Qt::BottomDockWidgetArea)`，默认 hide | `desktop/GuiMain.cpp:1600-1610` |
| 工具栏 | `QToolBar* mp_barMain`（状态/头像/搜索用户/新消息/写消息/建群/文件传输/文件共享） | `addToolBar` | `desktop/GuiMain.cpp:105-108, 1550-1561` |
| 菜单栏 | mp_menuMain / mp_menuSettings / mp_menuInfo（+ mp_menuChat 由聊天窗口触发弹出） | `menuBar()->addMenu` | `desktop/GuiMain.cpp:1524-1528`；菜单本体在 `createMenus()` `desktop/GuiMain.cpp:855-1520` |
| 状态栏 | `statusBar()`（构造时先建） | — | `desktop/GuiMain.cpp:92` |
| 托盘 | `GuiSystemTray* mp_trayIcon` + mp_menuTrayIcon | — | `desktop/GuiMain.cpp:110, 1509-1519` |
| 浮动聊天窗口 | `QList<GuiFloatingChat*> m_floatingChats`（每个是独立 QMainWindow） | 按需创建 | `desktop/GuiMain.h:296` |

菜单栏右角挂版本 QLabel（`desktop/GuiMain.cpp:1529-1548`）；`Main.cpp:247` 设置了 `Qt::AA_DontUseNativeMenuBar`（菜单不进 macOS 系统菜单栏，在窗口内显示——对极简化有利）。

### A.3 聊天区不在主窗口里！关键结论

**聊天内容区 `GuiChat` 从不嵌入 GuiMain**。点任何会话（用户/聊天列表/群）都走 `GuiMain::showChat(VNumber)`（`desktop/GuiMain.cpp:2966-2982`）→ `createFloatingChat(c)`（`desktop/GuiMain.cpp:4194-4229`）→ 新建/复用一个 `GuiFloatingChat` 独立窗口。

`GuiFloatingChat`（`desktop/GuiFloatingChat.cpp:43-116`）结构：
- 中央 `GuiChat* mp_chat`（`setCentralWidget`, :100）
- 右侧成员头像工具栏 `mp_barMembers`（仅群聊显示, :54-60, :213）
- 底部聊天工具栏 `mp_barChat`（:62-70），动作由 `GuiChat::setupToolBar()` 填充（`desktop/GuiChat.cpp:193-225`）
- 左侧两个 dock：表情面板 `mp_dockEmoticons`、预设消息 `mp_dockPresetMessageList`（:72-98）
- 状态栏 `statusBar()`（:101）

### A.4 `ShowChatsInOneWindow`（单窗口模式）的真实行为

- 设置项：`Settings.cpp:1526-1527`（`AlwaysOpenNewFloatingChat`/`ShowChatsInOneWindow`，默认 false）、保存 `Settings.cpp:1919`、读取接口 `Settings.h:1341-1342`。
- 行为在 `createFloatingChat`（`desktop/GuiMain.cpp:4200-4201`）：若开关为真且已存在任一浮动窗口，则**复用第一个浮动窗口**（`fl_chat = m_floatingChats.first()`），随后 `setChat(c)` 切换内容——即"所有聊天共用一个带切换的窗口"，而不是把 GuiChat 嵌进主窗口。
- 菜单项："Open chats in a single window"，`desktop/GuiMain.cpp:1091-1094`（settingsChanged case 7，`desktop/GuiMain.cpp:1668-1677` 会关闭多余浮动窗口）。

**对微信式改造的含义**：现状是"主窗口（5 个底 Tab）+ 独立聊天窗口"模型；要变成"左侧会话列表 + 右侧聊天区"的单窗口，必须把 `GuiChat` 嵌入主窗口布局（GuiChat 本身是普通 QWidget，`desktop/GuiChat.h:38`，可直接作为右栏），并废弃 GuiFloatingChat 装载路径。

---

## B) 聊天渲染链路（ChatMessage → 屏幕）

### B.1 链路总览

```
Core 收到/发出消息 → Core::dispatchSystemMessage/CoreChat 组装 ChatMessage（已是 HTML 片段）
  → Core 发信号 newChatMessage(Chat,ChatMessage)                     core/CoreDispatcher.cpp:86-87
  → GuiMain::onNewChatMessage                                        desktop/GuiMain.cpp:2450
  → GuiFloatingChat::showChatMessage → GuiChat::appendChatMessage    desktop/GuiChat.cpp:712
  → GuiChat::chatMessageToText                                       desktop/GuiChat.cpp:529-553
      ├─ 用户消息: GuiChatMessage::formatMessage                     desktop/GuiChatMessage.cpp:72-165
      └─ 系统消息: GuiChatMessage::formatSystemMessage               desktop/GuiChatMessage.cpp:167-201
  → cursor.insertHtml() 插入 mp_teChat（QTextBrowser 的 QTextDocument） desktop/GuiChat.cpp:739-741
```

**渲染载体是 `QTextBrowser`（mp_teChat，`desktop/GuiChat.ui:210`）+ HTML 流式追加，不是逐条消息的自定义 Widget，没有 model/delegate，没有气泡**。整窗重载用 `setChat()`→`setHtml()`（`desktop/GuiChat.cpp:602-679`，setHtml 在 :672）。

### B.2 各类消息如何变成 HTML

- **普通文本**：ChatMessage 构造时就把纯文本转 HTML——`core/ChatMessage.cpp:80` 调 `Protocol::formatHtmlText`（`core/Protocol.cpp:2283` 起）：转义、`*bold* /_underline_ /italic/` markdown（:2362-2370）、`[quote]`→`<span class='bee-quote'>`（:2371-2372）、linkify（:2375）、**表情解析在这里完成**：`EmoticonManager::parseEmoticons`（`core/Protocol.cpp:2370`；实现 `gui/EmoticonManager.cpp:251-320`，输出 `<img>` HTML）。
- **用户消息格式化**：`GuiChatMessage::formatMessage`（`desktop/GuiChatMessage.cpp:72-165`）只负责拼"名字+时间戳+正文"：彩色用户名（:120-128）、紧凑/换行模式（:130-139）、按用户分组省略名字（:105,110）、重要消息前后加 `<br>`（:144-160）。**没有任何头像、任何左右对齐、任何背景块**。
- **系统消息**：`formatSystemMessage`（`desktop/GuiChatMessage.cpp:167-201`）：单色 `<font color=系统色>` 一行 + `<br>`，天然就是"居中灰字"的候选（当前是靠颜色区分，不居中）。
- **文件消息**：由 Core 生成带链接的系统消息，`core/CoreFileTransfer.cpp:157-193`：`<a href="beefile://...">文件名</a>`；图片额外内嵌预览 `<img src=本地路径>`（:168, :271）；语音消息生成 `bevoicemessage:` 协议链接（:175-181）。GUI 侧不特殊处理，点击走 `GuiChat::checkAnchorClicked`（`desktop/GuiChat.cpp:521-527`）→ `GuiMain::openUrlFromChat`。
- **表情**：见上，输入框插入表情用 `GuiMessageEdit::addEmoticon`（`desktop/GuiMessageEdit.cpp:98-139`，记录 m_emoticonsAdded，发送时还原回文本 :60-78）。

### B.3 气泡样式的确切落点

微信式三态（自己右侧带头像 / 对方左侧 / 系统居中）在"QTextBrowser + HTML"架构下有两类做法：

**路线 1（低成本，保留 QTextBrowser）**：改 `GuiChatMessage::formatMessage/formatSystemMessage`（`desktop/GuiChatMessage.cpp:72, 167`），用 HTML 表格布局模拟气泡：
- 判断"自己/对方"：`cm.isFromLocalUser()`（ChatMessage 接口）或 `u.isLocal()`（formatMessage 已有该信息，:110 用它决定显示 "You"）。
- 头像：`Bee::avatarForUser(u, QSize(...), show_photo)`（用法见 `desktop/GuiChatItem.cpp:107`）转 `QTextImageFormat`/`<img src="file:///缓存路径">`；需先把头像落盘（Avatar 生成 `gui/Avatar.cpp`）或用 `QTextDocument::addResource`。
- 对齐：`<table width=100%><tr><td width=64>头像</td><td>…` + `align=right`；系统消息 `<div align=center>`。
- 气泡底色圆角：QTextBrowser 富文本 **不支持 border-radius**，只能用背景色块近似（`<span style="background-color:...">`），或给 QTextDocument 设置 CSS defaultStyleSheet（已有先例 `GuiChat::setChatStylesheet`，`desktop/GuiChat.cpp:594-600` 的 `.bee-quote`）。
- 需要同步改的调用方：`GuiChat::chatMessageToText`（`desktop/GuiChat.cpp:529`）、整窗重载 `setChat`（:602）、导出 `GuiChatMessage::chatToHtml`（`desktop/GuiChatMessage.cpp:203`，GuiChat::saveChat 与 GuiHome 复用）。

**路线 2（彻底，换 QListWidget+delegate 或 QListView）**：重写 `GuiChat` 的浏览区，把 ChatMessage 列表交给自绘 delegate（气泡 QPainterPath 圆角 + 头像 + 已读），mp_teChat 只留作选中复制。工作量大（选择/复制/链接点击/查找/打印全部要重做，现有能力见 `GuiChat::customContextMenu/findTextInChat/printChat`，`desktop/GuiChat.cpp:369-403, 1276-1308, 1234-1248`）。

**建议**：路线 1 先行，落点集中在 `GuiChatMessage.cpp` 一个文件的两个函数 + `GuiChat::chatMessageToText` 传参；路线 2 作为二期。

---

## C) 会话列表与未读

### C.1 数据模型

**无 model/delegate**。`GuiChatList`（`desktop/GuiChatList.cpp:33-65`）是 QWidget + `.ui`，核心是 `QTreeWidget* mp_twChatList`（:42-49 配置：单列、隐藏表头、可排序、图标尺寸=头像尺寸）。数据直接从 `ChatManager::instance().constChatList()` 拉（`updateChats` :77-84），每行是 `GuiChatItem : QTreeWidgetItem`（`desktop/GuiChatItem.h:31`）。

### C.2 列表项内容与"最后一条消息摘要"现状

`GuiChatItem::updateItem`（`desktop/GuiChatItem.cpp:81-154`）：
- 名称：私聊取对方用户名（:119），群聊取 c.name()（:115）；头像 `Bee::avatarForUser`（:107），离线置灰 `Bee::convertToGrayScale`（:147）。
- **未读数显示方式是文本前缀 `"(N) "`**（:130-131）+ 图标闪烁（`onTickEvent` :156-165，未读时在头像与 beebeep-message.png 间切换）。
- 排序键存在 UserRole：`ChatUnreadMessages/ChatMessages/ChatLastMessageTimestamp/ChatOnlineUsers/ChatName`（枚举 `desktop/GuiChatItem.h:35`；排序 `operator<` `desktop/GuiChatItem.cpp:44-74`：默认聊天置顶→未读→时间→消息数→在线数→名称）。
- **没有"最后一条消息文本摘要"列，时间戳只用于排序不显示**——微信式列表所需两要素都缺，需要新增。

可用的数据源：`Chat::lastMessageTimestamp()`（`core/Chat.h` inline；接收时 `core/CoreDispatcher.cpp:87` 写入）；最后一条消息可从 `c.messages().last()` 取（Chat API，`core/Chat.h:54` 附近 messages()），摘要需 `Bee::removeHtmlTags(cm.message())`（先例 `desktop/GuiMain.cpp:2416`）。

### C.3 未读数维护链

- 计数器在 `Chat`：`m_unreadMessages`，`Chat::addUnreadMessage()/readAllMessages()`（`core/Chat.h:112-114`）。
- 增加：收到消息时 `core/CoreDispatcher.cpp:86`（及 CoreParser.cpp:643,660,676）。
- 清零：`Core::readAllMessagesInChat`（`core/CoreChat.cpp:864-892`）→ `ChatManager::setChat` + `emit chatChanged`；GUI 触发点是浮动窗口获得焦点 `GuiFloatingChat::onApplicationFocusChanged → emit readAllMessages(chatId)`（`desktop/GuiFloatingChat.cpp:357-398`）→ `GuiMain::readAllMessagesInChat`（`desktop/GuiMain.cpp:4572`）。
- UI 刷新：`GuiMain::onChatChanged`（`desktop/GuiMain.cpp:3683-3697`）→ `mp_chatList->updateChat(c)`、托盘 `updateNewMessageAction`（`desktop/GuiMain.cpp:4592-4614`，同时切换主窗口图标）。

### C.4 微信式列表项的落点

两种做法：a) 给 `mp_twChatList` 设自定义 `QStyledItemDelegate`（setItemDelegate），在 paint() 里画 头像/名称/摘要/时间/未读红点角标，`GuiChatItem::updateItem` 改为存数据不拼文本——**推荐**，改动集中在 `GuiChatList.cpp/GuiChatItem.cpp`；b) 用 `QTreeWidget::setItemWidget` 每行挂自绘 QWidget（简单但行高与虚拟化差）。

---

## D) 输入区

### D.1 控件构成

底部输入区在 `GuiChat.ui` 的 `mp_frameMessage`（QFrame）：
- `GuiMessageEdit* mp_teMessage`（`.ui:72`）：**QTextEdit 子类**（`desktop/GuiMessageEdit.h:33`），富文本关闭、支持表情插入/历史消息（上下方向键 Ctrl+↑↓）/拼写高亮/补全。
- `QPushButton* mp_pbSend`（`.ui:94`）、`QPushButton* mp_pbRecordVoiceMessage`（`.ui:132`，无 voicechat 编译时永久禁用，`desktop/GuiChat.cpp:100-103`）、`QCheckBox* mp_cbSendAndClose`（`.ui:167`，发送后最小化/关闭）。
- 与聊天浏览区由 `QSplitter`（垂直）上下分栏（`desktop/GuiChat.cpp:66-76`）。
- 表情选择器不在输入栏内，而是 `GuiFloatingChat` 左侧 dock `GuiEmoticons`（`desktop/GuiFloatingChat.cpp:86-98`），选中后 `GuiChat::addEmoticon`（`desktop/GuiChat.cpp:837-841`）。

### D.2 回车发送

`GuiMessageEdit::keyPressEvent`（`desktop/GuiMessageEdit.cpp:279-374`）：
- Enter/Return：`useReturnToSendMessage()` 默认真，Ctrl/Shift+Enter 或关闭该选项则插入换行；否则 `emit returnPressed()`（:317-333）。
- `GuiChat` 构造连接 `returnPressed → sendMessage`（`desktop/GuiChat.cpp:186`）；`GuiChat::sendMessage()`（:495-514）取 `mp_teMessage->message()`，`emit newMessage(chatId, msg, false)` 并清空、可选发送后关闭。
- `GuiMain::setupChatConnections`（`desktop/GuiMain.cpp:221`）把 newMessage 接到 `GuiMain::sendMessage`（:2362-2371）→ `beeCore->sendChatMessage(...)`。

### D.3 附件/文件发送调用链（到 Core::sendFile）

工具栏按钮（浮动窗口底部 bar）`GuiChat::setupToolBar`（`desktop/GuiChat.cpp:209-211`）注册 Send file/Send folder/Send code；另有拖放（`dropEvent` :990-994 → `checkAndSendUrls` :937-988）与粘贴图片（:910-934）。

```
GuiChat::sendFile()                          desktop/GuiChat.cpp:889-892
  emit sendFileFromChatRequest(chatId, "")   → GuiMain::sendFileFromChat  desktop/GuiMain.cpp:2629-2643
    checkFilePath(): 空路径则 FileDialog::getOpenFileNames 多选        desktop/GuiMain.cpp:2672-2690
    beeCore->sendFilesFromChat(chat_id, paths)             core/CoreFileTransfer.cpp:290-331
      遍历聊天成员 → Core::sendFileToUser(u, path, ...)    core/CoreFileTransfer.cpp:362-...
        （目录→BuildFileList；扩展名/存在/在线校验失败时 dispatchSystemMessage 报错）
单文件直发：GuiMain::sendFile(User,path,chat_id)          desktop/GuiMain.cpp:2692-2745
  → beeCore->sendFile(user_id, file_path, "", false, chat_id)  core/CoreFileTransfer.cpp:336-360
```
发送结果以 ChatMessage::FileTransfer 系统消息回显在聊天里（含 beefile: 链接/图片预览，见 B.2）。

**微信式 [附件] 按钮落点**：直接复用 `GuiChat::sendFile()` 的信号链即可，只需把控件从工具栏挪进 `mp_frameMessage`（.ui 改布局），并把 `mp_pbRecordVoiceMessage/mp_cbSendAndClose` 从 .ui 移除或隐藏。[表情] 按钮同理：把 `GuiEmoticons` 从 dock 改为输入栏上方的弹出 QPopover/QMenu 式面板（`GuiFloatingChat.cpp:86-98` 的 dock 装载代码删除）。

---

## E) 功能砍除地图

### E.1 构建开关全景（BEEBEEP_* 宏）

定义位置与作用：

| 宏 | 定义处 | 作用 / 砍除动作 |
|---|---|---|
| `BEEBEEP_DEBUG` | `src/src.pro:4`（debug 构建） | 307 处调试日志；release 已无 |
| `BEEBEEP_USE_VOICE_CHAT` | `src.pro:46`（win32\|unix 无条件）→ `src/voicechat/voicechat.pri:3` | 语音消息（录音/播放器）。砍：删 `src.pro:44-47` 的 `DEFINES += ...` 与 `src.pro:85-88` 的 include 条目，同时移除 `QT += multimedia` 中 voice 部分（注意 beep 还要 multimedia，见下） |
| `BEEBEEP_USE_SHAREDESKTOP` | `src/sharedesktop/sharedesktop.pri:2`（`src.pro:84` win32\|unix 引入） | 桌面共享+截图。砍：删 `src.pro:84`；GuiShareDesktop/CoreShareDesktop 不再编译 |
| `BEEBEEP_USE_QXT` | `src/qxt/qxt.pri:2`（`src.pro:83`） | 全局快捷键（X11/Carbon）。砍：删 `src.pro:83` |
| `BEEBEEP_USE_HUNSPELL` | `src/hunspell/hunspell.pri:2`（`src.pro:75` **无条件**） | 拼写检查/补全。砍：删 `src.pro:75` include，并从 `utils/utils.pri` 去掉 SpellChecker*.cpp（或保留文件，其内部 `#ifdef` 已兼容：`utils/SpellChecker.cpp:24` 全文包在 ifdef 内，实际可仅删 hunspell.pri 一行 + utils.pri 两行） |
| `BEEBEEP_USE_MULTICAST_DNS` | `src/mdns/mdns.pri:4`（`src.pro:108` 仅 macx） | mDNS 发现。砍：删 `src.pro:108` |
| `BEEBEEP_USE_WEBENGINE` | `src.pro:38`（默认注释） | WebEngine 新闻页。保持注释即可 |
| `BEEBEEP_DISABLE_FILE_TRANSFER` | `src.pro:35`（注释） | 编译期禁文件传输（**不要打开**，目标是保留文件传输） |
| `BEEBEEP_DISABLE_SEND_MESSAGE` | `src.pro:36`（注释） | 编译期禁发消息（勿开） |
| `BEEBEEP_DISABLE_VIDEO_CALL` / `BEEBEEP_USE_VIDEOCALL` | `src.pro:37,90-97`（整个 videocall 模块已注释停用） | 视频通话；`src/videocall/` 目录已死代码，可整目录删除 |
| `BEEBEEP_FOR_RASPBERRY_PI` | `src.pro:66-68` | 树莓派（禁 2x 表情资源） |

残留依赖注意：
1. **voicechat.pri 里编了 core 文件** `core/CoreVoiceChat.cpp`（voicechat.pri SOURCES），GuiMain.cpp:76-79、GuiChat.h:32-34、GuiChat.cpp:51-58 等的 `#ifdef BEEBEEP_USE_VOICE_CHAT` 守卫齐全，宏移除后源代码可干净裁掉；但 `GuiChat.ui` 的 `mp_frameVoicePlayer/mp_pbRecordVoiceMessage` 与 `GuiChat.cpp:59 mp_frameVoicePlayer->hide()`（守卫外）依赖 .ui 生成的成员，**删宏不删 .ui 也能编译**（按钮会被 :101-103 置灰）——彻底移除时需同步删 .ui 节点。
2. `QT += multimedia`（`src.pro:44-47`）同时服务 beep 音效（`gui/AudioManager.cpp`，QSoundEffect）。砍语音后**必须保留 multimedia 与 AudioManager**，否则新消息提示音失效（`Main.cpp:222-226`）。
3. `desktop.pri:132-137`：`contains(DEFINES,BEEBEEP_USE_WEBENGINE)` 才加 GuiWebView——条件引用已规范。

### E.2 UI 层需隐藏/移除的菜单·工具栏·action 清单（按功能分组，行号=GuiMain.cpp 除注明外）

**群聊/广播**
- 工具栏+菜单 `mp_actCreateGroupChat` 建群：`825-826`、工具栏 `1557`；GuiGroupList 整个 Tab（`1585-1588`）
- `mp_actBroadcast`（Search users 广播）：`807-808`、工具栏 `1553`
- `sendBroadcastMessage()`：声明 `GuiMain.h:153`
- 群编辑 `editGroupChat/createGroupChat`：`134-135`
- `mp_actCreateMessage`（写预设消息）：`840-843`、工具栏 `1556`；GuiPresetMessageList dock（GuiFloatingChat.cpp:72-85）
- ChatList 上下文"Create new group chat/Write a message"：`GuiChatList.cpp:167-170, 282-285`

**语音消息**
- `mp_menuVoiceMessage` 设置菜单：`1252-1266`；录音按钮 `GuiChat.ui:132`；slots `sendVoiceMessageToChat/showVoiceEncoderSettings` `GuiMain.h:223-226`；GuiVoicePlayer frame（GuiChat.ui:226）

**桌面共享/截图**
- 设置菜单 Desktop sharing：`1332-1353`
- 聊天工具栏 share/screenshot：`GuiChat.cpp:212-215`；actions `GuiChat.cpp:175-182`；slots `1378-1421`
- 主菜单截图 `mp_actViewScreenShot`：`837-838`；插件菜单引用 `3047`；`showScreenShotWindow` `GuiMain.h:197`
- ShareDesktop 相关 slots：`GuiMain.h:179-186`

**游戏/插件**
- `mp_menuPlugins` + PluginManager：`858-859, 885`；`updadePluginMenu` `3038-3084`；plugins/ 目录（tetrisgame 等）与 `PluginManager::parseText`（Protocol.cpp:2379）

**拼写检查**
- 聊天工具栏 spellchecker/completer：`GuiChat.cpp:202-205`；设置菜单 Dictionary：`1167`；`selectDictionatyPath` `GuiMain.h:172`

**Buzz/帮助请求/蜂巢(Hive 活动)**
- Buzz：slots `sendBuzzToUser/showBuzzFromUser` `GuiMain.h:190-191`；音效设置 `1275-1278`；VCard buzz 按钮（GuiVCard connect `3019`）
- Help：`mp_actHelpRequest` `845-846, 868`；help station 标签 `1540-1544`；slots `192-193`
- GuiHome 活动 Tab：`1568-1573`（微信式可整体去掉，系统消息并入会话或状态栏）

**ShareBox/文件共享**
- `mp_actViewFileSharing/mp_actEnableFileSharing`：`831-832, 1559`；设置项 `1179-1183`；`showFileSharingWindow` `GuiMain.h:195`
- GuiShareBox/GuiShareLocal/GuiShareNetwork/GuiShareBoxFileInfo* 四个类（desktop.pri:40-42, 89-91）
- ChatList 上下文无此项；GuiUserList 拖拽发送 `sendFilesToChatRequest`（GuiMain.cpp:172）保留

**其他可藏**
- Saved chats Tab：`1590-1593`；`GuiSavedChat*` 三个类
- `mp_actViewLog` 日志窗口 `834-835, 879`；NetworkTest `877`；Workgroups `992`；WebView `848-852, 880-883`； donate/tips/fact 菜单 `1483-1499`；断线重连向导 `showRestartConnectionAlertMessage` 等
- 聊天工具栏：字体/字色/过滤/设置/Return 键切换/SaveAs（`GuiChat.cpp:196-216`）——微信式只留 [表情][附件][发送]
- 聊天右键菜单打印/查找（`GuiChat.cpp:396-401`）可留可去

### E.3 删除顺序建议

先关构建开关（voicechat/sharedesktop/qxt/hunspell/mdns，E.1）验证编译；再删 E.2 UI（菜单/工具栏/Tab）；最后清 desktop.pri/gui.pri 的死文件条目（GuiScreenShot、GuiShareBox*、GuiGroupList、GuiSavedChat*、GuiPresetMessageList、GuiCreateGroup、GuiWorkgroups、GuiRefusedChat、GuiPluginManager 等）。注意 `desktop/GuiHome.cpp` 被 `GuiMain::onNewChatMessage:2460`、`updateTabTitles:304` 引用，删 GuiHome 需同步改这两处与 `createMainWidgets:1568-1573`。

---

## F) 设置对话框结构 → 精简为一页

### F.1 现状：没有设置对话框，是"菜单即设置"

BeeBEEP 的设置不是 QTabDialog，而是 `GuiMain::createMenus()`（`desktop/GuiMain.cpp:855-1520`）构建的 20+ 个 QMenu/QAction，全部以 `act->setData(int)` + `settingsChanged(QAction*)` 的巨型 switch（`desktop/GuiMain.cpp:1632-2358`，约 700 行）分发。子菜单清单：
`Interface`(895-929)、`On start`(931-952)、`On close`(954-964)、`On connection`(966-985)、`Network`(987-1004)、`Users`(1006-1056)、`Chat`(1057-1169，含 Save messages/Colors/On sending)、`File transfer`(1171-1250)、`Voice message`(1252-1266)、`Notifications`(1268-1330，含 Tray icon 子菜单)、`Desktop sharing`(1332-1353)、顶层杂项(1355-1367)、用户列表右键 `mp_menuUserList`(1369-1430)、状态菜单 `mp_menuStatus`(1432-1460)、聊天设置弹出菜单 `showChatSettingsMenu`(3881-3946)。

独立对话框仅有：GuiEditVCard（资料：昵称/姓名/头像/颜色，`desktop/GuiEditVCard.cpp:42-101`）、GuiLanguage、GuiShortcut、GuiWizard（首启，QDialog 单页改昵称，`GuiWizard.cpp:40-68`）、GuiNetwork（网络配置）等。

### F.2 精简为"一页"的改法

1. 新建 `GuiSettings` QDialog（或 QWidget 嵌主窗口），五个字段对应现有 Settings API，全部已存在：
   - 昵称：`Settings::localUser()` VCard nickName + `beeCore->setLocalUserVCard(...)`（复用 `GuiMain::changeVCard` 流程 `desktop/GuiMain.cpp:2984-3000` 与 GuiEditVCard 的头像/改名控件，可直接内嵌 GuiEditVCard 的 ui 片段）
   - 头像：同上（GuiEditVCard mp_pbChangePhoto/removePhoto，`GuiEditVCard.cpp:51-52`）
   - **"蓝牙"：BeeBEEP 无任何蓝牙功能**（全源码 grep "bluetooth" 0 命中），该项应删除或换成"网络连接（GuiNetwork）"入口
   - 暗色：`Settings::setUseDarkStyle` + `GuiMain::loadStyle()`（`desktop/GuiMain.cpp:5322-5350`，Fusion+darkPalette）
   - 下载目录：`Settings::downloadDirectory/setDownloadDirectory`，参考 `GuiMain::selectDownloadDirectory`（菜单项在 `1250`）
2. `GuiMain::createMenus()`/`createToolAndMenuBars()` 中 mp_menuSettings 整体不再加入 menuBar（删 `1527`），保留 settingsChanged switch 中仍被新页复用的 case（暗色/下载目录/保存设置）。
3. 迁移成本低：所有 setter 已在 Settings 单例，新页只做"读-显示-写-save"。

---

## G) 中文化

### G.1 加载机制

- `Main.cpp:54-72 SetTranslator()`：按 `Settings::languageFilePath(folder, lang)` 拼出 `<folder>/beebeep_<lang>.qm` 并 `QTranslator::load` + installTranslator；失败再试默认目录（`Main.cpp:170-175`）。
- `Settings::languageFilePath`：`core/Settings.cpp:1008-1010`（`"%1/beebeep_%2.qm"`）。
- **默认语言取系统**：`core/Settings.cpp:1459-1461` `m_language = QLocale::system().name()` 然后 **`resize(2)` 截断成两字母**。
- .ts→.qm：`locale/locale.pri` 把 27 个 ts 列入 `BEEBEEP_TRANSLATIONS`，用 `QMAKE_LRELEASE` 规则生成 .qm（`updateqm` compiler 配置）；更新脚本 `locale/update_ts_macosx.sh` 跑 `lupdate -recursive ../beebeep-desktop.pro`。
- 语言选择 UI：`desktop/GuiLanguage.cpp:66-96` 扫描 .qm 文件名第二段作为语言码。

### G.2 zh_CN 覆盖度（关键发现）

- **没有 zh_CN.ts；中文文件叫 `beebeep_cn.ts/.qm`**（`locale/` 目录，locale.pri:31 列出 `beebeep_cn.ts`）。
- 覆盖度：`beebeep_cn.ts` 共 1302 条 message，unfinished 43 条 + vanished 14 条 ≈ **96.7% 已翻译**（统计自实际 grep）。
- **默认匹配 bug**：中文系统 `QLocale::system().name()` = `zh_CN`，截断后为 `"zh"`，去找 `beebeep_zh.qm` → 不存在 → **中文用户默认落回英文**，必须手动在 GuiLanguage 里选 "cn"。
- 修复方案（三选一）：a) 把 cn 文件重命名为 zh（`locale.pri:31` 同步改，最干净，Qt 惯例 zh_CN 可再细分为 zh_CN.qm）；b) 在 `Settings.cpp:1459` 后加映射 `if( lang=="zh" ) lang="cn";`；c) 干脆默认 `m_language="cn"`（YongBEEP 定制可硬编码中文）。

### G.3 新增字符串如何进入翻译

所有 UI 文案都用 `tr("...")`（GuiMain/GuiChat 等 QObject 上下文）；新窗口类只要 `Q_OBJECT` + tr()，跑一次 `lupdate -recursive beebeep-desktop.pro`（或 update_ts 脚本）即进 `beebeep_xx.ts` 模板，再拷到 `beebeep_cn.ts` 翻译；若硬编码中文不翻译也可（定制版直接写中文，绕过 .ts）。注意带参数的用 `tr("%1...").arg()`，日期格式已有 `QT_TRANSLATE_NOOP("Date",...)` 先例（`GuiChatMessage.cpp:50,60,65`）。

---

## H) 微信式改造落点建议

### H.1 路线对比

**路线 A：改造 GuiMain 本体**
- 做法：`createMainWidgets()`（`desktop/GuiMain.cpp:1563-1611`）重写——中央 widget 从 `QTabWidget(South)` 换成 `QSplitter(水平)`：左 = 新 `GuiChatList`（或现有+delegate，见 C.4），右 = `GuiChat`（去掉 GuiFloatingChat 装载，`showChat()` 改为 `mp_chatWidget->setChat(c)` 并高亮左栏）；`createMenus/createToolAndMenuBars` 按 E.2 裁剪；文件传输 dock 保留（:1600-1610）。
- 优点：Core 信号连接（`GuiMain.cpp:129-167`，20+ 个 connect）、托盘、会话管理、发送链全部复用；`GuiChat` 是 QWidget 可直接嵌入；改动集中 3 个函数 + createMenus。
- 缺点：GuiMain.cpp 5409 行，删功能要小心插槽引用（E.2/E.3 清单即防漏）；浮窗路径（GuiFloatingChat、m_floatingChats、showChatsInOneWindow case7）需整体短路。
- 工作量估计：UI 骨架 2-3 天，裁菜单/插槽 2-3 天，气泡 HTML（B.3 路线1）2-3 天，列表 delegate 2 天，联调 2 天 ≈ **2 周左右出可用地版**。

**路线 B：新建精简主窗口类（GuiWeChatMain）**
- 做法：新类只挂必要 Core 信号（newChatMessage/chatChanged/userChanged/fileDownloadRequest/fileTransferProgress 五个足够起步），复用 `GuiChat`、`GuiChatList`、`GuiSystemTray`、`GuiEditVCard` 现成类；`Main.cpp:260` 换成新窗口；旧 GuiMain 保留不编译或删除。
- 优点：不被 5400 行历史拖累，界面从零按微信布局写（左栏 260px 头像列表 + 右侧聊天 + 底部输入三件套），认知负担小；旧代码可整体从 desktop.pri 摘除（连带 E.2 大部分菜单消失）。
- 缺点：要重接 20+ Core 信号与托盘/重连/关机流程（`closeEvent/forceShutdown/checkWindowFlagsAndShow`，`GuiMain.cpp:237-280, 354-400`）；GuiFloatingChat 与 GuiChat 的信号桥（`setupChatConnections` `GuiMain.cpp:219-235`）要照抄；暗色主题/几何保存等横切功能要搬。
- 工作量估计：骨架+信号迁移 3-4 天，其余同 A ≈ **2-2.5 周**，但长期维护面小得多。

**建议**：路线 A 起步（风险低、Core 接线全保留），在 A 验证气泡/列表/输入栏三件套后，若最终要彻底裁掉旧代码再向 B 演进。两条路线的"聊天区三件套"（气泡 GuiChatMessage、列表 delegate、输入栏 .ui 改造）工作完全共用。

### H.2 macOS Dock 未读角标落点

- 现状：**无 Dock 角标**。`GuiSystemTray` 只换托盘图标闪烁（`desktop/GuiSystemTray.cpp:63-101, 116-137`）；macOS 上 QSystemTrayIcon 并不显示为 Dock 角标，且 Qt5/Qt6 Widgets 无公开 badge API（全源码 grep "badge/dockTile" 0 命中）。
- 已有汇聚点（加角标的最佳挂点）：
  1. `GuiMain::updateNewMessageAction()`（`desktop/GuiMain.cpp:4592-4614`）——每次未读状态变化都会走到，且能拿 `ChatManager::instance().unreadMessages()`（总数，`core/ChatManager.cpp:121-126`）；
  2. `GuiSystemTray::setUnreadMessages(chat_id, ur)`（`desktop/GuiSystemTray.cpp:63-75`）——单聊未读入口。
- 实现建议：新增 `desktop/DockBadge_mac.mm`（Objective-C++，NSApplication.dockTile.badgeLabel），在 `desktop.pri:105` 后加 `macx: SOURCES += desktop/DockBadge_mac.mm`；C++ 侧暴露 `void setDockBadge(int)`（非 mac 空实现，可放 BeeApplication），在上述 1 处调用：unread>0 时 `badgeLabel=[NSString stringWithFormat:@"%d", n]`，否则置 nil。注意现有 `BeeApplication_mac.cpp` 是纯 C++（IOKit/PowerManagement，`desktop/BeeApplication_mac.cpp:25-80`），qmake 会把 .mm 按 ObjC++ 编译，需在 .pro 确认 `CONFIG += objectived_cpp` 或直接用 .mm 后缀即可。

---

## 附：关键文件清单（改造主战场）

| 文件 | 角色 |
|---|---|
| `src/desktop/GuiMain.cpp/.h` | 主窗口、菜单/设置、Core 信号枢纽（5409/416 行） |
| `src/desktop/GuiChat.cpp/.h/.ui` | 聊天区+输入区（1492 行） |
| `src/desktop/GuiChatMessage.cpp` | 消息→HTML（气泡落点，252 行） |
| `src/desktop/GuiFloatingChat.cpp/.h` | 聊天外层窗口（被嵌主窗口后可废） |
| `src/desktop/GuiChatList.cpp` + `GuiChatItem.cpp` | 会话列表（微信列表改造点） |
| `src/desktop/GuiMessageEdit.cpp/.h` | 输入框（回车/表情/拖放） |
| `src/desktop/GuiEmoticons.cpp` | 表情面板 |
| `src/desktop/GuiEditVCard.cpp/.ui` | 昵称/头像编辑（设置页素材） |
| `src/desktop/GuiSystemTray.cpp` | 托盘+未读图标（Dock 角标邻居） |
| `src/core/Settings.cpp/.h` | 全部设置项+语言加载逻辑 |
| `src/src.pro` + `src/*/*.pri` | 编译开关总闸 |
| `locale/beebeep_cn.ts` + `locale/locale.pri` | 中文化 |
