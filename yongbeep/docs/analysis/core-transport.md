# YongBEEP 核心传输层深度分析报告

- 分析对象：`/Users/yonghu/Workspace/YongBEEP/source`（BeeBEEP 5.9.2-dev，Qt5.15/Qt6 Widgets C++），聚焦 `src/core`。
- 分析目的：为"经典蓝牙 RFCOMM 传输层（QtBluetooth）+ 微信式极简 UI（文本 + ≤5MB 文件）"技术设计文档供料。
- 规范：只读分析，未修改任何源码；所有结论均附 `file:line` 引用（相对 `src/` 目录）。

---

## A) 网络栈现状地图：从发现到消息收发的完整链路

### A.1 组件与生命周期总览（谁 new / 谁 delete）

| 对象 | 创建位置 | 销毁位置 | 说明 |
|---|---|---|---|
| `Core`（单例） | `desktop/` 层构造 | `Core::~Core` 仅清 `mp_instance`（core/Core.cpp:91-95） | 持有下列所有网络组件 |
| `Listener* mp_listener` | `Core::Core()` `new Listener(this)`（core/Core.cpp:58） | parent-children 随 Core 析构；`stop()/restart()` 只 `close()`（core/Core.cpp:409,449） | QTcpServer 子类，仅监听 6475 |
| `Broadcaster* mp_broadcaster` | `new Broadcaster(this)`（core/Core.cpp:59） | 同上，`stopBroadcasting()` 只关 socket（core/Broadcaster.cpp:216） | QUdpSocket 发现 |
| `FileTransfer* mp_fileTransfer` | `new FileTransfer(this)`（core/Core.cpp:60） | `stopFileTransferServer()`→`stopListener()`（core/FileTransfer.cpp:61-77） | QTcpServer 子类，监听 6476 |
| `Connection*`（每远端一条） | `Core::createConnection()` `new Connection(this)` 后 append 进 `m_connections`（core/CoreConnection.cpp:79-84） | `Core::closeConnection()`：`disconnect(); abortConnection(); deleteLater()`（core/CoreConnection.cpp:235-238） | 继承链 `Connection → ConnectionSocket → QTcpSocket` |
| `FileTransferPeer*`（每次传输一个） | 上行：`FileTransfer::incomingConnection()`（core/FileTransfer.cpp:178）；下行：`FileTransfer::downloadFile()`（core/FileTransfer.cpp:337） | `FileTransfer::deletePeer()` `deleteLater()`（core/FileTransfer.cpp:365-382） | 内部持有一个 `ConnectionSocket* mp_socket`（core/FileTransferPeer.cpp:41），parent 为 peer，随 peer 析构 |
| `TickManager` | `desktop/BeeApplication.cpp:195` | 同文件 stopTicks | 1s 一跳，驱动全部 `onTickEvent` |

### A.2 发现阶段（UDP）

1. `Core::start()`：`mp_listener->listen(hostAddressToListen, defaultListenerPort=6475)`（core/Core.cpp:218；端口常量 core/Config.h:54），失败则退回上次端口/随机端口（core/Core.cpp:222-235）；随后 `mp_broadcaster->startBroadcastServer()` 绑定 UDP 36475（`DEFAULT_BROADCAST_PORT`，core/Config.h:53；绑定 core/Broadcaster.cpp:88），并加入组播组 224.0.64.75（core/Broadcaster.cpp:103-170；常量 core/Config.h:58）。
2. 周期广播：`Core::onTickEvent` → `sendBroadcastMessage()`（core/Core.cpp:706-707）→ `Broadcaster::sendBroadcast()`（core/Broadcaster.cpp:229-235）→ 组播优先（`sendMulticastDatagram`，core/Broadcaster.cpp:615-637），失败置 `m_newBroadcastRequested`，下一 tick 由 `updateAddressesToSearchUsers()` 收集子网广播/hosts 文件/手工地址/Hive/离线用户（core/Broadcaster.cpp:477-570）。
3. 广播内容是明文 `Message::Beep`："BEE-BEEP" 头 + 本机 listener 端口（`Protocol::broadcastMessage`，core/Protocol.cpp:283-290）。6.x 新代际用 8 字节魔数 `0xBEEBEEBEEBEEBEEB` 的二进制 datagram，5.9.2 只读出其端口并报 `newGenerationPeerFound`（core/Broadcaster.cpp:43-53, 365-400）。
4. 接收：`readyRead → readBroadcastDatagram()`（连接 core/Broadcaster.cpp:94）解析出 `sender_ip + listener_port` 后 **`emit newPeerFound(QHostAddress, int)`**（core/Broadcaster.cpp:398/438）；非广播形式（直连 ip:port，例如 Hive、手工添加）在 `contactNetworkAddresses()` 里直接 emit（core/Broadcaster.cpp:664）。

### A.3 建立连接（TCP 6475）

- 主动端：`Core` 构造时连接 `mp_broadcaster.newPeerFound → Core::newPeerFound`（core/Core.cpp:80）。`Core::newPeerFound` 做防重/防自连检查（loopback core/CoreConnection.cpp:91；本机地址+本端口 core/CoreConnection.cpp:94；hosts 白名单 core/CoreConnection.cpp:102；`preventMultipleConnectionsFromSingleHostAddress` core/CoreConnection.cpp:108-115；已连接用户 core/CoreConnection.cpp:117-126），然后 `createConnection() + setupNewConnection(c) + c->connectToNetworkAddress(na)`（core/CoreConnection.cpp:130-132）。
- 被动端：`Listener::incomingConnection(qintptr)` 重写只 `emit newConnection(socket_descriptor)`（core/Listener.cpp:31-34），Core 构造时连接到 `Core::checkNewConnection`（core/Core.cpp:82）。`checkNewConnection` 创建 Connection、`initSocket(descriptor, serverPort)`（core/CoreConnection.cpp:136-150），hosts 校验失败则 `closeConnection`。
- `ConnectionSocket::connectToNetworkAddress()` 记录对端地址、置超时检查、生成 ECDH 密钥、`connectToHost()`（core/ConnectionSocket.cpp:74-85）；`connected()` 信号触发 `sendQuestionHello()`（连接 core/ConnectionSocket.cpp:52）。
- `initSocket()` 用 `setSocketDescriptor()` 接管被动 socket，并从 `peerAddress()/peerPort()` 回填 `m_networkAddress`（core/ConnectionSocket.cpp:57-72）。

### A.4 握手（HELLO / ECDH）与鉴权

1. 主动端连上即发 HELLO（`sendQuestionHello`，core/ConnectionSocket.cpp:384-411），内容含本机 listener 端口、昵称、状态、账号、ECDH 公钥 pkey1、版本、hash、颜色、workgroups、Qt 版本、datastream 版本等（`Protocol::helloMessage`，core/Protocol.cpp:335-366），Message id 字段即协议版本号（core/Protocol.cpp:359, 307-311）。
2. 双方都在 `readBlock()` 里、当 `m_userId == ID_INVALID` 时把首个块交给 `checkHelloMessage()`（core/ConnectionSocket.cpp:265-269）。被动端回 `sendAnswerHello`（core/ConnectionSocket.cpp:505-506→413-431）。
3. `checkHelloMessage` 完成：workgroup 过滤（core/ConnectionSocket.cpp:457-469）、加密/压缩协商（core/ConnectionSocket.cpp:471-503）、协议版本对齐（取小，core/ConnectionSocket.cpp:509-546）、`createCipherKey` 生成对称密钥（ECDH level4：`m_ecdhKeys.generateSharedKey` + SHA/SHA3 派生，core/ConnectionSocket.cpp:586-632；`Protocol::createCipherKey` core/Protocol.cpp:2378-2391），最后 **`emit authenticationRequested(array_data)`**（core/ConnectionSocket.cpp:583）。
4. `Core::setupNewConnection` 已把该信号连到 `Core::checkUserAuthentication`（core/CoreConnection.cpp:161）。后者用 `Protocol::createUser(m, c->peerAddress())` 从 HELLO 建 `User`（core/CoreConnection.cpp:264；core/Protocol.cpp:597-719，其中密码校验 `hello_message.data() == Settings::hash(user_name)` 在 core/Protocol.cpp:619-623），再 `recognizeUser` 按 hash/账号/昵称与本地用户列表匹配（core/CoreConnection.cpp:274；core/Protocol.cpp 中 recognizeUser 实现），处理重名/重复连接/占位用户合并（core/CoreConnection.cpp:287-416），`UserManager::setUser`、加入 default chat、创建 private chat，然后 `addConnectionReadyForUse(c,u)` → `setReadyForUse(u.id())` 并连接 `newMessage → Core::parseMessage`（core/CoreConnection.cpp:164-171, 446），最后发 VCard、`emit userChanged/userConnectionStatusChanged`（core/CoreConnection.cpp:448-449）。

### A.5 数据收发与保活

- 帧格式：`readBlock()` 按 `DATA_BLOCK_SIZE_32`（quint32，proto>60）或 `DATA_BLOCK_SIZE_16`（quint16）读块长，再 `QDataStream >> QByteArray` 事务化读出（core/ConnectionSocket.cpp:136-274）；`sendData()` → 压缩(可选) → 加密(可选) → `serializeData()` 打块长前缀 → `write()+flush()`（core/ConnectionSocket.cpp:345-382, 287-335）。加密是 Rijndael AES-256 逐 16 字节块（`Protocol::encryptByteArray` core/Protocol.cpp:2472-2529），**要求明文长度为 16 的倍数**（`fromMessage` 尾部补空格 core/Protocol.cpp:137-138；qCompress 后补空格 core/ConnectionSocket.cpp:358-359）。
- 应用层：`Connection::sendMessage` = `Protocol::fromMessage` + `sendData`（core/Connection.cpp:38-56）；接收 `dataReceived → Connection::parseData` → `toMessage` → Ping/Pong 本地处理，其余 `emit newMessage(userId, m)`（core/Connection.cpp:34, 58-88）。
- 保活：`TickManager`（1s，core/TickManager.cpp:48-62, 78-85）→ `Core::onTickEvent`（core/Core.cpp:667-728）→ 每个 `Connection::onTickEvent`（core/Core.cpp:709-710）→ 每 7 tick `pingRequest` → `Connection::sendPing`（空闲>21s 或发 Ping 失败则 `abortRequest`→`setConnectionClosed`，core/Connection.cpp:95-129；常量 core/Config.h:105-107）。
- 断开：`disconnected()/error()/abortRequest` 都接 `Core::setConnectionClosed/setConnectionError`（core/CoreConnection.cpp:158-160）→ `closeConnection` 置 Offline、清 FileShare/传输、`deleteLater`（core/CoreConnection.cpp:194-238）。

---

## B) ConnectionSocket 与 QTcpSocket 的耦合点全清单

重构为"组合流对象（组合一个 QIODevice）"时需要逐一处理的位置。行号均为实际代码行。

### B.1 类定义层面

| # | 位置 | 耦合点 |
|---|---|---|
| 1 | core/ConnectionSocket.h:33 | `class ConnectionSocket : public QTcpSocket` —— 继承本身 |
| 2 | core/ConnectionSocket.cpp:34 | 构造初始化列表 `QTcpSocket( parent )` |
| 3 | core/ConnectionSocket.h:136 | `isConnected()` 内联用 `isOpen()` + `state()==ConnectedState/BoundState` |
| 4 | core/ConnectionSocket.h:137 | `isConnecting()` 用 `state()==HostLookupState/ConnectingState` |

### B.2 构造函数（socket 选项/代理）

| # | 位置 | 耦合点 |
|---|---|---|
| 5 | core/ConnectionSocket.cpp:41,43 | `setSocketOption(QAbstractSocket::KeepAliveOption, …)` |
| 6 | core/ConnectionSocket.cpp:45 | `setSocketOption(QAbstractSocket::LowDelayOption, …)` |
| 7 | core/ConnectionSocket.cpp:49 | `setProxy(QNetworkProxy::NoProxy)` |

### B.3 信号槽（QTcpSocket 自有信号）

| # | 位置 | 耦合点 |
|---|---|---|
| 8 | core/ConnectionSocket.cpp:52 | `connect(this, SIGNAL(connected()), this, SLOT(sendQuestionHello()))` —— 客户端 HELLO 触发器，BT 侧需映射到 `QBluetoothSocket::connected` |
| 9 | core/ConnectionSocket.cpp:53 | `connect(this, SIGNAL(readyRead()), this, SLOT(readBlock()))` —— QIODevice 级，可平移 |
| 10 | core/ConnectionSocket.cpp:54 | `connect(this, SIGNAL(bytesWritten(qint64)), this, SLOT(onBytesWritten(qint64)))` —— QIODevice 级 |

### B.4 连接建立/拆除（成员函数调用）

| # | 位置 | 耦合点 |
|---|---|---|
| 11 | core/ConnectionSocket.cpp:60 | `setSocketDescriptor(socket_descriptor)` —— 服务端路径，BT 需改为注入已连接的 QBluetoothSocket* |
| 12 | core/ConnectionSocket.cpp:61-62 | `peerAddress()` / `peerPort()` —— 回填 m_networkAddress |
| 13 | core/ConnectionSocket.cpp:84 | `connectToHost(hostAddress, hostPort)` —— 客户端路径 |
| 14 | core/ConnectionSocket.cpp:93 | `abort()`（abortConnection 内） |
| 15 | core/ConnectionSocket.cpp:98,101 | `isOpen()` / `close()`（closeConnection 内） |
| 16 | core/ConnectionSocket.cpp:651 | `disconnectFromHost()`（checkConnectionTimeout 内）+ 手工 `emit disconnected()`（652，为触发 Core 侧清理） |
| 17 | core/ConnectionSocket.cpp:771-772 | `flush()` + `disconnectFromHost()`（checkTestMessage 结束测试连接） |

### B.5 I/O 读写

| # | 位置 | 耦合点 |
|---|---|---|
| 18 | core/ConnectionSocket.cpp:142,279,664,696 | `bytesAvailable()`（readBlock/flushAll/activityIdle/onTickEvent 四处） |
| 19 | core/ConnectionSocket.cpp:155 | `QDataStream data_stream( this )` —— **把 this 当 QIODevice**，readBlock 的核心 |
| 20 | core/ConnectionSocket.cpp:369 | `write( data_serialized )` —— sendData 落盘点 |
| 21 | core/ConnectionSocket.cpp:374,284,771 | `flush()`（sendData/flushAll/checkTestMessage） |

### B.6 Core / FileTransferPeer / GuiNetworkTest 对继承 API 的外部依赖

| # | 位置 | 耦合点 |
|---|---|---|
| 22 | core/Connection.h:31 | `class Connection : public ConnectionSocket` —— 重构必须保持 Connection 的基类接口不变或同步改 |
| 23 | core/CoreConnection.cpp:56 | `c->peerPort()` / `c->peerAddress()`（hasConnection 去重） |
| 24 | core/CoreConnection.cpp:155 | `c->peerAddress().isNull()`（setupNewConnection 调试日志） |
| 25 | core/CoreConnection.cpp:158 | `SIGNAL(error(QAbstractSocket::SocketError))` —— **QTcpSocket 错误信号**，BT 需要等价错误通道 |
| 26 | core/CoreConnection.cpp:159 | `SIGNAL(disconnected())` |
| 27 | core/CoreConnection.cpp:178 | `c->errorString()` |
| 28 | core/CoreConnection.cpp:264 | `Protocol::createUser( m, c->peerAddress() )` —— 鉴权用 peer IP；蓝牙侧无 IP，需要替代对端标识（见 D） |
| 29 | core/FileTransferPeer.cpp:41 | `mp_socket = new ConnectionSocket( this )` —— 6476 文件通道**复用同一个 ConnectionSocket 类**（含 HELLO/ECDH） |
| 30 | core/FileTransferPeer.cpp:42 | `SIGNAL(error(QAbstractSocket::SocketError)) → socketError` |
| 31 | core/FileTransferPeer.cpp:53,204 | `mp_socket->isOpen()` |
| 32 | core/FileTransferPeer.cpp:56 | `mp_socket->socketDescriptor()`（仅日志） |
| 33 | core/FileTransferPeer.cpp:273 | `mp_socket->errorString()` |
| 34 | core/FileTransferPeer.cpp:324 | `createUser( m, mp_socket->peerAddress() )` —— 同 #28 |
| 35 | core/FileTransferPeer.cpp:118,123 | `initSocket(descriptor,…)` / `connectToNetworkAddress(fi.networkAddress())` —— 文件通道两种建连方式 |
| 36 | core/FileTransferPeer.h:146-147 | `peerAddress()` / `isNewGenerationPeer()` 透传 |
| 37 | desktop/GuiNetworkTest.cpp:90-156 | 网络测试对话框直接 new ConnectionSocket 并用 `error()`/`errorString()`/`disconnected()`（#25-27 的 GUI 复制品） |
| 38 | core/Broadcaster.cpp:53 信号 `newPeerFound(QHostAddress,int)` | QHostAddress 类型贯穿 `Core::newPeerFound/checkNetworkAddress`（core/CoreConnection.cpp:71-77,86），蓝牙发现注入点需兼容或扩展该签名（见 D） |

> 说明：`ConnectionSocket.h:40` 的 `connectToNetworkAddress(const NetworkAddress&)` 与 `initSocket(qintptr, quint16)` 是仅有的两个"建连入口"，其余公有 API（`sendData/flushAll/closeConnection/abortConnection/isConnected/...`，core/ConnectionSocket.h:38-70）都是流无关的——这是重构可保持上层零改动的关键。

---

## C) 重构建议：ConnectionSocket 改为"组合流对象"

### C.1 保持不变的公有 API（Core/上层零改动的边界）

`Connection`/`Core`/`FileTransferPeer` 实际使用的 ConnectionSocket 公有面（来自 B.6 扫描）：

- 建连：`connectToNetworkAddress(NetworkAddress)`、`initSocket(qintptr, quint16)`（后者需为 BT 提供重载，见 C.3）
- 数据：`sendData(QByteArray)→bool`、`flushAll()`、`dataReceived(QByteArray)` 信号
- 状态：`isConnected()/isConnecting()/isEncrypted()/isCompressed()/isServerSocket()/isTestConnection()/setTestConnection()/protocolVersion()/isNewGenerationPeer()/userId()/setUserId()/activityIdle()/latestActivityDateTime()/networkAddress()/fileTransferBufferSize()/onTickEvent(int)`
- 控制：`closeConnection()/abortConnection()/abortRequest()/pingRequest()/authenticationRequested()/connectionTestCompleted(QString)`
- 错误/断开：**必须由新实现继续发出** `error(QAbstractSocket::SocketError)`、`disconnected()` 这两个 Qt 信号（CoreConnection.cpp:158-159 直接按签名 connect），BT 适配层负责把 `QBluetoothSocket::errorOccurred` 映射成 `QAbstractSocket::SocketError` 枚举（如 `RemoteHostClosedError/NetworkError`），`errorString()` 同步提供。

只要新基类继续提供 `peerAddress()/peerPort()/errorString()` 三个 Qt 方法的**同名遮蔽版本**（BT 下返回合成地址，见 D.4），`Core`/`FileTransferPeer`/`GuiNetworkTest` 无需改动。

### C.2 新类草图：传输抽象 + 两个实现

```
// 新增 core/StreamDevice.h —— 传输层抽象（纯 QIODevice 视角）
class StreamDevice : public QObject {           // 信号统一转发，签名与 Qt 兼容
  Q_OBJECT
signals:
  void connected();                             // 对应 QTcpSocket::connected
  void readyRead();                             // QIODevice 语义
  void bytesWritten( qint64 );
  void errorOccurred( QAbstractSocket::SocketError );  // 桥接 Core 现有 SIGNAL(error(...)) 连接
  void disconnected();
public:
  virtual void connectToHost( const QHostAddress&, quint16 ) = 0;   // TCP
  virtual void connectToService( const QBluetoothAddress&, const QBluetoothUuid& ) = 0; // RFCOMM
  virtual bool setSocketDescriptor( qintptr ) = 0;                  // TCP 服务端
  virtual void adoptSocket( QBluetoothSocket* ) = 0;                // BT 服务端(已连接对象注入)
  virtual void abort() = 0;  virtual void close() = 0;
  virtual void disconnectFromHost() = 0;  virtual void flush() = 0;
  virtual QIODevice* ioDevice() = 0;         // readBlock 的 QDataStream 挂载点(B.5 #19)
  virtual QHostAddress peerAddress() const = 0;   // BT 返回合成地址(见 D.4)
  virtual quint16 peerPort() const = 0;           // BT 返回 RFCOMM channel 或伪端口
  virtual QString errorString() const = 0;
  virtual bool isConnectingState() const = 0;     // 替代 state() 判断(仅 Connected/Connecting 两态即可)
};

// 改造 core/ConnectionSocket：class ConnectionSocket : public StreamDevice（不再继承 QTcpSocket）
//   持有 StreamDevice* mp_stream；readBlock 的 QDataStream(this) 改为 QDataStream(mp_stream->ioDevice())
//   sendData 的 write()/flush()、bytesAvailable() 全部改走 mp_stream
//   构造中的 setSocketOption/setProxy 移入 Tcp 实现

class ConnectionSocketTcp : public StreamDevice { QTcpSocket* mp_sock; ... };      // 6475/6476 现状平移
class BtSocketAdapter   : public StreamDevice { QBluetoothSocket* mp_bt; ... };   // QBluetoothSocket(RFCOMM)
```

分阶段策略（风险最小）：
1. 第一步只做"继承改组合"且 TCP 行为逐字节不变（`serializeData/readBlock` 不动一行，仅换设备指针）；
2. 第二步引入 `BtSocketAdapter`，`ConnectionSocket` 增加构造参数或 `setStreamDevice()`；
3. `FileTransferPeer`（core/FileTransferPeer.cpp:41）默认仍造 TCP 实现——但按 E 方案，极简版 ≤5MB 文件不再走 6476 独立通道，BT 文件块直接复用消息 Connection（见 E.3）。

### C.3 sendData 分帧/加密逻辑：可原样保留

- 分帧（块长前缀 + QByteArray 序列化）完全在 `serializeData()`（core/ConnectionSocket.cpp:287-335）内，只依赖 `QDataStream(&data_block, WriteOnly)` 内存设备，**与传输无关**；
- 解帧 `readBlock()` 只依赖可读 QIODevice（core/ConnectionSocket.cpp:155）；
- 加密/压缩在 `sendData` 前半段（core/ConnectionSocket.cpp:350-365）与 `readBlock` 后半段（core/ConnectionSocket.cpp:249-259），只依赖 `Protocol` 单例，**与传输无关**。
- 唯一传输相关的量：`fileTransferBufferSize()` 供上传读块（core/FileTransferUpload.cpp:203），RFCOMM MTU（~1KB）远小于默认 65456（core/Settings.cpp:1647），但 QBluetoothSocket 自带写缓冲，分帧层无需感知 MTU——保留现值即可，或对 BT 流用更小的读块以控制内存。

### C.4 握手（Hello/ECDH）时序对新流的适配点

1. **HELLO 触发**：TCP 靠 `connected()` 信号（core/ConnectionSocket.cpp:52）；BT 客户端在 `QBluetoothSocket::connected` 后由 adapter 发同样 `connected()`，服务端在 `adoptSocket()` 时**必须同步补发**（TCP 服务端没有 connected() 信号、由对端先发 HELLO 被动应答——见 core/ConnectionSocket.cpp:505 `!isHelloSent()` 才 sendAnswerHello；BT 服务端照抄该不对称逻辑即可，即服务端不主动发 HELLO）。
2. **超时**：`m_checkConnectionTimeout` 只在客户端路径置位（core/ConnectionSocket.cpp:79），16 tick 无认证则断（core/ConnectionSocket.cpp:639-654，常量 core/Config.h:108）；BT 配对/SDP 发现可能超 16 秒，BT 路径应在 `connectToService` 前预先完成 SDP discovery，或放宽该 tick 阈值。
3. **ECDH 无需改动**：`m_ecdhKeys.create()/publicKey()/generateSharedKey()`（core/ConnectionSocket.cpp:68,83,393,415,617）纯计算。注意 `createCipherKey` 依赖 `m_datastreamVersion`（握手第 12 字段，core/Protocol.cpp:319-333）——两端同为 YongBEEP 时值一致，无问题。
4. **身份绑定**：`checkUserAuthentication → createUser(m, c->peerAddress())`（core/CoreConnection.cpp:264）把 IP 写进 `User::networkAddress()`（core/Protocol.cpp:706）。BT 下 peerAddress 为合成地址（D.4），`User` 识别靠 hash/账号而非 IP（core/CoreConnection.cpp:274），功能不受影响，但 `hasConnection(peer_ip,port)` 去重（core/CoreConnection.cpp:52-69）要求 BT 合成地址在"一台设备一条连接"意义下唯一且稳定。
5. **文件通道握手**：6476 上 `FileTransferPeer` 也走完整 HELLO/ECDH（core/FileTransferPeer.cpp:42-44 连接 authenticationRequested/dataReceived），因此 BT 适配若保留独立文件通道也要能跑握手；采用 E 方案（消息通道内传文件）则天然规避。

---

## D) 发现与用户创建链路：蓝牙设备的注入点

### D.1 现有"发现 → 建连"钩子链（LAN）

```
Broadcaster::newPeerFound(QHostAddress,int)                [core/Broadcaster.cpp:438/664, Broadcaster.h:53]
  → Core::newPeerFound(QHostAddress,int)                   [连接于 core/Core.cpp:80；实现 core/CoreConnection.cpp:86-133]
      → createConnection() + setupNewConnection(c)         [core/CoreConnection.cpp:130-131, 79-84, 152-162]
      → c->connectToNetworkAddress(na)                     [core/ConnectionSocket.cpp:74]
（服务端方向）
Listener::newConnection(qintptr)                           [core/Listener.cpp:33, 连接于 core/Core.cpp:82]
  → Core::checkNewConnection(qintptr)                      [core/CoreConnection.cpp:135-150]
      → c->initSocket(descriptor, serverPort)
（鉴权完成）
ConnectionSocket::authenticationRequested(QByteArray)      [core/ConnectionSocket.cpp:583]
  → Core::checkUserAuthentication                          [core/CoreConnection.cpp:240-496]
      → Protocol::createUser(m, peerAddress)               [core/Protocol.cpp:597]
      → recognizeUser / UserManager::setUser               [core/CoreConnection.cpp:274,424]
      → addConnectionReadyForUse(c,u)                      [core/CoreConnection.cpp:446→164-171]
      → emit userChanged / userConnectionStatusChanged     [core/CoreConnection.cpp:448-449]
```

### D.2 蓝牙发现注入方案（新增 BtDiscoverer，镜像 Broadcaster 的输出面）

新增 `core/BtDiscoverer`（建议）：
- 用 `QBluetoothDeviceDiscoveryAgent` + SDP 查询 RFCOMM 服务 UUID；
- 发现对端后 **`emit newPeerFound(const QHostAddress&, int)` 不适用**（无 IP）——两种做法：
  - 方案 1（推荐）：给 Core 增加并行槽 `btPeerFound(QBluetoothAddress, ...)`，内部走与 `Core::newPeerFound` 相同的防重检查（core/CoreConnection.cpp:91-126 的检查逐条对应移植：自连判断改为比对本机 BT 地址；`isUserConnected(NetworkAddress)` 换成按合成地址/设备地址查），最后 `createConnection() + setupNewConnection(c) + c->connectToBtPeer(addr)`；
  - 方案 2（改动更小、但语义脏）：把 BT MAC 编码进 `QHostAddress`（如映射进 IPv6 本地链路空间 fe80::/64 + 伪端口=RFCOMM channel），完全复用 `newPeerFound(QHostAddress,int)` 签名。**注意** `NetworkManager::isLocalHostAddress/isHostAddressAllowed`（core/CoreConnection.cpp:94,102）会按真实网卡地址过滤，方案 2 需要在这两处放行合成地址，侵入面反而更大。
- 信号连接处：仿照 core/Core.cpp:80 在 `Core::Core()` 增加 `connect(mp_btDiscoverer, SIGNAL(btPeerFound(...)), this, SLOT(btPeerFound(...)))`；tick 驱动仿照 `mp_broadcaster->onTickEvent`（core/Core.cpp:704）。
- 服务端（被连）侧：新增 `BtListener`（`QBluetoothServer` + RFCOMM），有新连接时 `newConnection(QBluetoothSocket*)` → Core `checkNewBtConnection(socket*)` → `createConnection(); c->adoptBtSocket(socket); setupNewConnection(c)`，等价于 core/CoreConnection.cpp:135-150。

### D.3 用户状态/上线离线广播机制（可整体复用）

- 上线广播本质 = HELLO 里带状态（core/Protocol.cpp:340）+ 建连后 Core 发本机 VCard（core/CoreConnection.cpp:491）；新连接用户触发 `checkGroupChatAfterUserReconnect/checkOfflineMessagesForUser`（core/Core.cpp:671-687）。
- 状态变更：`Core::setLocalUserStatus` → `sendLocalUserStatus()` 对**所有** `m_connections` 广播 `Message::User + UserStatus` 标志（core/CoreUser.cpp:45-56, 105-110；消息构造 core/Protocol.cpp:396-402）。BT 与 TCP 连接都在 `m_connections` 列表里，自动同播。
- 离线：`closeConnection` 置 `User::Offline` + `emit userChanged/userConnectionStatusChanged` + 清 default chat/FileShare/传输（core/CoreConnection.cpp:194-238）。BT socket 断开只要正确发出 `disconnected()`（C.1）即走同一清理路径。

---

## E) 文件传输机制与"≤5MB 走加密消息通道"落地方案

### E.1 现状：独立 TCP 6476 通道

- 端口：`DEFAULT_FILE_TRANSFER_PORT=6476`（core/Config.h:55；监听 core/FileTransfer.cpp:41）。
- 协商（消息通道上的信令）：
  - 发送方把 `FileInfo` 序列化为 `Message::File`（"BEE-FILE"，头 core/Protocol.cpp:63）通过 **6475 消息连接**发给对方：`Core::sendFileToUser` → `c->sendMessage(fileInfoToMessage(...))`（core/CoreFileTransfer.cpp:409-415；字段格式 core/Protocol.cpp:1397-1438：hostPort/size/fileId/password/hash/shareFolder/isShareBox/chatPrivateId/lastModified/mime/contentType/startingPosition/duration[/占位14/拒绝理由15]）。
  - 接收方 `Core::parseFileMessage`（core/CoreParser.cpp:157-240）校验扩展名/开关后 `emit fileDownloadRequest(u, fi)` 给 GUI 确认，或 ShareBox/语音消息直接 `mp_fileTransfer->downloadFile()`。
  - 拒绝回 `Message::File + Refused`（core/CoreFileTransfer.cpp:452-481；core/Protocol.cpp:1373-1386）。
- 数据面（6476）：`FileTransfer`（QTcpServer）`incomingConnection` 建上行 peer（core/FileTransfer.cpp:176-183），下载方 `downloadFile()` 建下行 peer 并入队（core/FileTransfer.cpp:335-351）。每个 `FileTransferPeer` 内部**new 一个 ConnectionSocket**（core/FileTransferPeer.cpp:41）复用 HELLO/ECDH/加密（authenticationRequested→`checkUserAuthentication`→`recognizeUser`，core/FileTransferPeer.cpp:311-337）。
- 下载方握手后发 `fileInfoToMessage` 请求（带 startingPosition 续传，core/FileTransferDownload.cpp:112），上传端 `FileTransfer::checkUploadRequest` 校验 fileId+password 后 `startUpload`（core/FileTransfer.cpp:216-333）；之后：
  - 上行：`sendFileHeader`（core/FileTransferUpload.cpp:96-133）→ 循环 `sendUploadData`：每次 `m_file.read(fileTransferBufferSize())`（默认 65456，core/Settings.cpp:1647）→ `mp_socket->sendData(原始文件字节)`（core/FileTransferUpload.cpp:203-205；**块本身经 ConnectionSocket 加密+分帧**）；
  - 下行：每收到一块就 `sendDownloadDataConfirmation`（"bytes|total|flag" 文本，core/Protocol.cpp:2110-2134），上传端 `checkUploading` 校验后发下一块（core/FileTransferUpload.cpp:135-172）——**停等式流控**。
  - 下载完成：临时文件（cache/fileHash.part，core/FileTransferDownload.cpp:217-220）rename 到目标（core/FileTransferPeer.cpp:135-195）。
- 进度/消息通知：`FileTransferPeer::progress/message` 信号 → `FileTransfer` 转发（core/FileTransfer.cpp:204-205,343）→ `Core::checkFileTransferProgress/checkFileTransferMessage`（core/CoreFileTransfer.cpp:137,235）→ `emit fileTransferProgress/fileTransferMessage`（core/CoreFileTransfer.cpp:232,243）→ GUI `GuiFileTransfer::setProgress/setMessage`（desktop/GuiFileTransfer.h:41-42，QTreeWidget 项内进度条）。

### E.2 落地方案：≤5MB 文件分块走加密消息通道（Bluetooth + 统一极简路径）

**核心判断**：消息通道的 `sendData` 本身已提供压缩+加密+分帧（C.3），且 `DATA_BLOCK_SIZE_32` 上限 2GB（core/Config.h:69），承载 5MB 毫无压力；`Connection::sendMessage` 对 >512KB 仅打警告不禁止（core/Connection.cpp:41-42）。但一次性整发 5MB 会：① 占满 QBluetoothSocket 写缓冲无法暂停；② 无进度；③ `onBytesWritten` 只更新活动时间（core/ConnectionSocket.cpp:337-343），停等无流控。因此按块拆发。

**1) 新增 Message 类型（注册位置）**：
- `Message::Type` 枚举追加在 `Received` 之后、`NumTypes` 之前（core/Message.h:34-36，注释明言"不要改顺序"，只能尾部追加）：`FileChunk`（数据块）、可复用现有 `Message::File` 头部协商（见下）。
- 头字符串：`Protocol::messageHeader()` switch 追加 `case Message::FileChunk: return "BEE-FCHU";`（core/Protocol.cpp:50-75），`messageType()` 反向追加（core/Protocol.cpp:77-119）——旧版收到未知头返回 `Undefined`，`Connection::parseData` 会**丢弃但保持连接**（core/Connection.cpp:63-69 注释即为此设计），前向兼容成立。
- 消息 ID 常量（可选，`Config.h` 的 `ID_*_MESSAGE` 区块 core/Config.h:112-132）追加 `ID_FILECHUNK_MESSAGE = 27`。
- Core 分发：`Core::parseMessage` switch 追加 `case Message::FileChunk: parseFileChunkMessage(u,m);`（core/CoreParser.cpp:50-99）。

**2) 信令流程（最小改动）**：
- 发送方仍用现有 `Core::sendFileToUser` 生成 `FileInfo`（含 fileId/password/hash/size，core/CoreFileTransfer.cpp:409-414），但发的是带新标志的 `Message::File`：建议复用 `fileInfoToMessage` 输出后 `addFlag` 新增 `Message` 标志位（枚举尾部追加 `InlineFile = NumFlags 前`，core/Message.h:37-39）表示"内联传输，勿连 6476"。
- 接收方 `parseFileMessage` 检测 `InlineFile`：≤5MB（`Settings` 新阈值）则走内联接收器（新建 `core/InlineFileTransfer`，挂到 Core，按 fileId 聚合块、写 `temporaryFilePath()` 同款 cache 临时文件、完成后同样 rename+`fileDownloadRequest`/自动接收）；>5MB 或对端为老版本（`u.protocolVersion()` 小于新版本号）则回退 6476 老路径。
- 块消息格式建议（放 `Message::text` 为 QByteArray 无法直接放——**注意 Message 文本是 QString**，core/Message.h:74；方案：块字节放 `m_text`？不行，`fromMessage` 以 UTF-8 join（core/Protocol.cpp:136）。推荐做法：**块数据 base64 后放 text**，或扩展 `Message` 增加 `QByteArray m_binary` 字段并在 `fromMessage/toMessage` 尾部按协议版本条件序列化（proto≥新版本才写该字段，旧版无感知）):
  ```
  data 区（DATA_FIELD_SEPARATOR=U+2028 分隔，core/Protocol.cpp:39）:
    fileId(VNumber) | chunkIndex(quint32) | totalChunks(quint32) | byteOffset(qint64) | chunkSize
  text/binary 区: 原始文件字节（建议 32~64KiB/块，对齐 16 字节加密块，即 32768/65536 天然对齐 core/Config.h:70）
  ```
- 确认与流控：沿用停等——接收方每聚合一块回 `Message::Received` 风格的轻量 ACK（复用 `Message::FileChunk` + `Message::Request` 标志或新增小消息），发送方收到再发下一块；或简化为窗口=4 滑窗。暂停/取消：复用 `Message::Refused` 标志 + fileId。
- 完成通知：校验 `fileHash`（`Protocol::fileInfoHash`，core/Protocol.h:82）后按 `setTransferCompleted` 同样语义收尾。

**3) 大小上限与压缩/加密的相互作用**：
- 顺序是 压缩→加密（core/ConnectionSocket.cpp:352-365）：先 `qCompress` 再 Rijndael；接收端先解密再 `qUncompress`（core/ConnectionSocket.cpp:249-259）。**逐块压缩会破坏随机读取与 hash 校验粒度**——建议内联文件在"应用层不压缩"（`Message::Compressed` 连接级协商不受影响，core/ConnectionSocket.cpp:503），因为 `qCompress` 作用于每条消息整体，块级压缩收益低且使 `chunkSize` 与偏移语义复杂化。加密无此问题：每条消息独立加密，块边界即消息边界。
- 上限控制：`Core::sendFileToUser` 在 `file.size() > 5MB` 且目标是 BT/内联时拒绝（现有拒绝消息模式 core/CoreFileTransfer.cpp:377-382 可套用）；`DATA_BLOCK_SIZE_32_LIMIT`（core/Config.h:69）不是约束（5MB<<2GB）。
- `Connection::sendMessage` 的 524288 警告阈值（core/Connection.cpp:41）建议对 `FileChunk` 豁免或调大日志级别，避免刷屏。

**4) 进度通知复用 GuiFileTransfer**：
- 新 `InlineFileTransfer` 对外发出与 `FileTransferPeer` **完全相同签名**的信号：`progress(peer_id, user_id, FileInfo, bytes, elapsed)` / `message(... TransferState)`（core/FileTransferPeer.h:75-76）。
- Core 侧直接复用 `checkFileTransferProgress/checkFileTransferMessage`（core/CoreFileTransfer.cpp:137-244）——它们只依赖 user_id+FileInfo+状态，不关心 6476；GUI 的 `GuiFileTransfer::setProgress/setMessage`（desktop/GuiFileTransfer.h:41-42）零改动即得到进度条、暂停/取消菜单（transferCanceled/transferPaused 信号回连 Core::cancelFileTransfer/pauseFileTransfer，core/CoreFileTransfer.cpp:439-450——这两个槽需改为同时查 `mp_fileTransfer` 和内联传输表，或把内联实现挂进 `FileTransfer` 类内部统一 peer 列表，**推荐后者**：在 `FileTransfer` 里加 `InlineTransferPeer` 类型，GUI/Core 完全无感）。

---

## F) 设置与配置：持久化机制与新增蓝牙设置项

### F.1 持久化机制（两层）

- **beebeep.rc**（INI，数据目录）：部署级只读配置。`objectRcSettings()`（core/Settings.cpp:562-568）打开 `dataFolder()/beebeep.rc`；`createDefaultRcFile()` 写默认值（core/Settings.cpp:476-559）；`loadCommonSettings()` 读取（core/Settings.cpp:1353 起，如 `UseKeepAliveOptionInSocket` core/Settings.cpp:614、端口 core/Settings.cpp:598-600、`DisableEncryptedConnections` core/Settings.cpp:671）。
- **beebeep.ini**（用户配置）：`objectSettings()`（core/Settings.cpp:1183-1199）——`m_useSettingsFileIni` 时用 `defaultSettingsFilePath()`=`dataFolder()/beebeep.ini`（core/Settings.cpp:2380 附近），否则 Windows 注册表 NativeFormat。`load()`（core/Settings.cpp:1201 起）与 `save()`（core/Settings.cpp:1741 起）按组读写（Version/Chat/User/VCard/Gui/Connection/FileTransfer…；如 `EncPwd` core/Settings.cpp:1803、`FileTransferBufferSize` core/Settings.cpp:1995）。
- **网络密码**：`Settings::setPassword` 把口令 SHA1-hex 成 `m_password`（core/Settings.cpp:1053-1060；空/默认口令映射为 `"*6475*"`），`hash()`=SHA1(name+password+signature)（core/Settings.cpp:1038-1045）用于 HELLO 鉴权（core/Protocol.cpp:619）。若勾选保存，ini 中存 `simpleEncrypt(m_passwordBeforeHash)` 为 `User/EncPwd`（core/Settings.cpp:1800-1804）。

### F.2 新增"蓝牙开关/设备名"设置项应改的文件与位置

建议放 **beebeep.rc（部署级）+ ini 镜像**（开关类配置项目前都在 rc：`DisableFileTransfer` core/Settings.cpp:517 等）：

1. `core/Settings.h`：成员声明区（参照 `m_useKeepAliveOptionInSocket` core/Settings.h:742）加 `bool m_useBluetooth; QString m_bluetoothDeviceName; QBluetoothAddress ...`；inline getter/setter 区（参照 core/Settings.h:1276）。
2. `core/Settings.cpp`：
   - 构造默认值（参照 core/Settings.cpp:153 `m_useKeepAliveOptionInSocket = false;`）；
   - `createDefaultRcFile()` 写默认（参照 core/Settings.cpp:549）；
   - `loadCommonSettings()` 读 rc（参照 core/Settings.cpp:614）；
   - 若需 GUI 可改：`save()` 的 Connection 组（参照 `FileTransferBufferSize` core/Settings.cpp:1995）与 `load()` 对应段。
3. GUI（若极简版需要开关）：设置对话框连接对象照 `mp_actEncryptedConnectionByDefault` 模式（desktop/GuiMain.cpp:1003-1004）。
4. 消费点：`Core::start()` 在广播启动后按开关启动 `BtDiscoverer/BtListener`（插入点在 core/Core.cpp:244-257 之后）；设备名用于 `Settings::createLocalUser`/VCard 展示与 `helloMessage`（core/Protocol.cpp:358 的 localHostName 字段可承载 BT 名）。

---

## G) 回归风险清单

### G.1 改动传输层后 LAN TCP 路径必须回归的行为

1. **发现→连接闭环**：UDP 广播/组播能互相发现并建立 6475（core/Broadcaster.cpp:398→core/CoreConnection.cpp:130-132）；广播回环检测不打自连（core/Broadcaster.cpp:426-432）。
2. **握手与版本协商**：新旧协议版本混连（版本取小逻辑 core/ConnectionSocket.cpp:535-546）、datastream 版本握手（core/ConnectionSocket.cpp:514-523）、加密开/关两两组合（core/ConnectionSocket.cpp:471-501 四象限）。
3. **帧完整性**：半包/粘包（`readBlock` 事务回滚 core/ConnectionSocket.cpp:218-228）、16/32 位块长两种格式（core/ConnectionSocket.cpp:175-203）、`flushAll` 的 40 次循环上限（core/ConnectionSocket.cpp:279，常量 core/Config.h:72）。
4. **保活与超时**：7-tick Ping、21s 无响应断开（core/Connection.cpp:106-110）、16-tick 连接超时（core/ConnectionSocket.cpp:641）、`onBytesWritten` 大块刷新活动时间防误断（core/ConnectionSocket.cpp:337-343）。
5. **重复连接防护**：同 IP 已连拒绝（core/CoreConnection.cpp:108-126）、同名/同 hash 用户双连的"只连其一"仲裁（core/CoreConnection.cpp:334-380）。
6. **6476 文件通道**：上传/下载/续传（startingPosition，core/FileTransferDownload.cpp:95-105）、暂停恢复（core/FileTransferPeer.cpp:222-257）、队列与并发上限（core/FileTransfer.cpp:415-468）。
7. **断线清理**：用户 Offline、FileShare 移除、进行中的传输取消（core/CoreConnection.cpp:194-238）；`stop()/restart()` 全量重连（core/Core.cpp:392-471）。
8. **网络切换**：网卡 down/up 自动重连（core/Core.cpp:537-575）、`updateNetworkConfiguration` 重广播（core/CoreConnection.cpp:532-546）。

### G.2 最容易踩的 5 个坑

1. **`emit disconnected()` 的手工补发**：`checkConnectionTimeout`/`checkTestMessage` 里是**手工 emit** `disconnected()`（core/ConnectionSocket.cpp:652；测试路径 core/ConnectionSocket.cpp:773 emit 的是 abortRequest），因为 Core 靠它触发 `setConnectionClosed`。改成组合后若新流忘发或双发（真实 disconnected 信号 + 手工），要么泄漏连接对象（`m_connections` 不清），要么 double-deleteLater。
2. **`QDataStream( this )` 的设备指针语义**：readBlock 每次调用都现场构造绑定 this 的流（core/ConnectionSocket.cpp:155），BT 适配层若 `ioDevice()` 返回的 QBluetoothSocket 指针在重连/adopt 时更换，旧流缓冲语义（`startTransaction/commitTransaction` 状态在设备上）会错乱；必须保证一次握手周期内设备指针稳定。
3. **16 字节对齐假设无处不在**：加密要求明文 %16==0（core/Protocol.cpp:137-138、core/ConnectionSocket.cpp:358-359、core/Protocol.cpp:2116-2131 都在手工补齐）。新增 FileChunk 消息若块尺寸不是 16 的倍数，`fromMessage` 的补空格逻辑会与"块内二进制数据含 0x2029/U+2029 分隔符"互相污染——**二进制块必须 base64 或专用二进制字段**，否则 `toMessage` 的 `QString::split(PROTOCOL_FIELD_SEPARATOR)`（core/Protocol.cpp:146）会把文件数据切碎。这是 E 方案最大的格式陷阱。
4. **ConnectionSocket 被 6475 和 6476 两处复用**：`FileTransferPeer` 直接 new ConnectionSocket 并依赖其 QTcpSocket 细节（B.6 #29-35：descriptor、peerAddress、errorString）。只改消息路径不改文件路径会漏；反过来给 ConnectionSocket 加"传输类型"分支若破坏 TCP 文件通道的 HELLO 时序（服务端不先发 HELLO 的不对称性，core/ConnectionSocket.cpp:505），6476 上传会卡死在 Request 态。
5. **`User.networkAddress` 承载身份去重**：BT 合成地址若不稳定（如每次 SDP 返回不同伪端口），`hasConnection/isUserConnected`（core/CoreConnection.cpp:52-69, 118-126）会放行重复连接，随后触发 #G.1-5 的重名仲裁路径，产生"已在线拒绝"误报；合成地址必须以 **BT 设备地址（MAC）为键**且在整个会话期不变，`Broadcaster::contactNetworkAddresses` 的直连 emit（core/Broadcaster.cpp:664）也不能把 BT 地址误当 IP 走 `isNetworkAddressForBroadcast`（含 "255"/端口判断，core/Broadcaster.cpp:678-681）。

---

## 附：关键常量速查（core/Config.h）

| 常量 | 值 | 行 |
|---|---|---|
| DEFAULT_BROADCAST_PORT | 36475/UDP | Config.h:53 |
| DEFAULT_LISTENER_PORT | 6475/TCP | Config.h:54 |
| DEFAULT_FILE_TRANSFER_PORT | 6476/TCP | Config.h:55 |
| ENCRYPTED_DATA_BLOCK_SIZE | 16 字节 | Config.h:70 |
| DATA_BLOCK_SIZE_16 / 32 | quint16 / quint32 | Config.h:63,67 |
| TICK_INTERVAL / PING_INTERVAL_TICK / PONG_DEFAULT_TIMEOUT | 1000ms / 7tick / 21000ms | Config.h:104-107 |
| TICK_INTERVAL_CONNECTION_TIMEOUT | 16 tick | Config.h:108 |
| NEW_GENERATION_PROTO_VERSION | 100 | Config.h:95 |
