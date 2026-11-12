// YongBEEP 核心单元测试（host 运行，链接真实应用对象，非复制品）
// 覆盖：协议消息往返 / FileChunk 分块(base64) / Rijndael 加解密 / ECDH 对称
// 运行：./run-tests.sh（在 source/yongbeep-tests/ 下）
#include <QCoreApplication>
#include <QDebug>
#include <QUuid>
#include <QtGlobal>
#include <cstring>

#include "Message.h"
#include "Protocol.h"
#include "Rijndael.h"
#include "ECDH.h"
#include "Config.h"
#include "ChatListModel.h"
#include "ChatManager.h"
#include "Core.h"
#include "Connection.h"
#include "Settings.h"
#include "StreamDeviceTcp.h"
#include "UserManager.h"

static int g_failed = 0, g_passed = 0;
#define CHECK(cond, name) do { \
  if( cond ) { g_passed++; } \
  else { g_failed++; qWarning() << "FAIL:" << name; } \
} while(0)

static void testMessageRoundtrip()
{
  Protocol& p = Protocol::instance();
  Message m( Message::Chat, ID_DEFAULT_CHAT, QString::fromUtf8("你好 hello <b>&</b> 'quote' \"dq\"") );
  m.addFlag( Message::Markdown );
  m.addFlag( Message::SourceCode );
  QByteArray wire = p.fromMessage( m, 96 );
  Message out = p.toMessage( wire, 96 );
  CHECK( out.type() == Message::Chat, "toMessage ok" );
  CHECK( out.type() == Message::Chat, "type" );
  CHECK( out.id() == m.id(), "id" );
  CHECK( out.text() == m.text(), "text" );
  CHECK( out.isMarkdown() && out.isSourceCode(), "flags(markdown+sourcecode)" );
  // 前向兼容：未知标志不应导致解析失败（模拟旧端收新标志）
  Message m2( Message::Chat, ID_DEFAULT_CHAT, "fwd" );
  m2.addFlag( Message::Markdown );
  Message out2 = p.toMessage( p.fromMessage( m2, 96 ), 95 );
  CHECK( out2.type() == Message::Chat, "old proto parses new flags" );
}

static void testFileChunkRoundtrip()
{
  Protocol& p = Protocol::instance();
  const FileSizeType sizes[] = { 1, 65535, 65536, 65537, 300000 };
  int case_no = 0;
  for( FileSizeType n : sizes )
  {
    ++case_no;
    QByteArray payload( int(n), Qt::Uninitialized );
    for( FileSizeType i = 0; i < n; ++i )
      payload[int(i)] = char( (i * 31 + 7) % 256 ); // 含 0x00/0x2029 高位字节的伪随机
    // 生产路径：构造分块消息 → 线上序列化 → 反序列化 → 逐字段+字节级比对
    Message mc = p.fileChunkDataToMessage( 42, 3, 10, n * 3, payload );
    CHECK( mc.type() == Message::FileChunk, "chunk type" );
    QByteArray wire = p.fromMessage( mc, 96 );
    Message wire_back = p.toMessage( wire, 96 );
    CHECK( wire_back.type() == Message::FileChunk, "chunk wire parse" );
    VNumber fid = 0; quint32 ci = 0, tc = 0; FileSizeType off = 0, csz = 0; QByteArray back;
    CHECK( p.fileChunkFromMessage( wire_back, &fid, &ci, &tc, &off, &csz, &back ), "chunk fields parse" );
    CHECK( fid == 42 && ci == 3 && tc == 10 && off == n * 3, "chunk fields values" );
    CHECK( back == payload, QString("chunk bytes roundtrip #%1 (%2B)").arg(case_no).arg(int(n)).toUtf8() );
  }
}

static void testRijndael()
{
  // 走生产代码路径：Protocol::encryptByteArray/decryptByteArray（含块对齐与密钥派生）
  Protocol& p = Protocol::instance();
  QByteArray cipher_key = QCryptographicHash::hash( QByteArrayLiteral("yb-unit-test-key"), QCryptographicHash::Sha1 );
  QByteArray plain;
  for( int i = 0; i < 1000; ++i )
    plain.append( QString::number( i ).toLatin1() + ',' );
  QByteArray cipher = p.encryptByteArray( plain, cipher_key, 96 );
  CHECK( !cipher.isEmpty(), "encrypt produces output" );
  CHECK( cipher != plain, "ciphertext differs" );
  QByteArray restored = p.decryptByteArray( cipher, cipher_key, 96 );
  CHECK( restored.startsWith( QByteArrayLiteral("0,1,2,3,") ), "decrypt content head" );
  CHECK( restored.size() >= plain.size(), "decrypt padded size >= plain" );
}

static void testEcdhSymmetry()
{
  ECDH::Keys alice, bob;
  alice.create();
  bob.create();
  CHECK( !alice.publicKey().isEmpty() && !bob.publicKey().isEmpty(), "ecdh pubkeys" );
  CHECK( alice.publicKey() != bob.publicKey(), "pubkeys differ" );
  bool ok_a = alice.generateSharedKey( bob.publicKey() );
  bool ok_b = bob.generateSharedKey( alice.publicKey() );
  CHECK( ok_a && ok_b, "shared key derived" );
  CHECK( !alice.sharedKey().isEmpty(), "shared key non-empty" );
  CHECK( alice.sharedKey() == bob.sharedKey(), "ecdh symmetry" );
}

// Only the transport I/O is simulated. HELLO recognition, link selection,
// contact presence and the avatar's model role all run the desktop code.
class TestStream : public StreamDeviceTcp
{
public:
  explicit TestStream( bool bluetooth ) : m_bluetooth( bluetooth ) {}
  bool isBluetoothTransport() const override { return m_bluetooth; }
  bool isOpen() const override { return true; }
  bool isConnectedState() const override { return true; }
  QHostAddress peerAddress() const override
  {
    return QHostAddress( m_bluetooth ? QStringLiteral( "fe80::1234" ) : QStringLiteral( "192.0.2.7" ) );
  }
  qint64 write( const QByteArray& bytes ) override { return bytes.size(); }
private:
  bool m_bluetooth;
};

class TestCore : public Core
{
public:
  using Core::connection;
  using Core::preferredConnections;
  using Core::closeConnection;

  Connection* openLink( bool bluetooth )
  {
    Connection* c = createConnection();
    TestStream* stream = new TestStream( bluetooth );
    c->setStreamDevice( stream );
    setupNewConnection( c );
    emit stream->connected();
    return c;
  }
};

static QByteArray peerHello( const QString& device_hash )
{
  QStringList fields = { "6475", "BadgePeer", QString::number( User::Online ), "", "badge-test", "",
                         "1", device_hash };
  Message hello( Message::Hello, 96, fields.join( QChar::LineSeparator ) );
  hello.setData( QString::fromLatin1( Settings::instance().hash( "BadgePeer" ) ) );
  return Protocol::instance().fromMessage( hello, 1 );
}

static void testConcurrentLinks( bool bluetooth_first )
{
  TestCore core;
  const QByteArray hello = peerHello( "stable-device-hash" );
  Connection* first = core.openLink( bluetooth_first );
  emit first->authenticationRequested( hello );
  const VNumber id = first->userId();
  CHECK( id != ID_INVALID, "first HELLO accepted" );
  if( id == ID_INVALID )
    return;
  ChatListModel model;
  model.reload();
  const VNumber chat_id = ChatManager::instance().privateChatForUser( id ).id();
  QModelIndex index = model.index( model.rowOfChat( chat_id ), 0 );
  auto badges = [&]() { return index.data( ChatListModel::TransportsRole ).toInt(); };
  const Core::Transport first_type = bluetooth_first ? Core::Bluetooth : Core::Lan;
  const Core::Transport second_type = bluetooth_first ? Core::Lan : Core::Bluetooth;
  CHECK( badges() == first_type, "single transport badge" );

  Connection* impostor = core.openLink( !bluetooth_first );
  emit impostor->authenticationRequested( peerHello( "other-device-same-nickname" ) );
  CHECK( impostor->userId() == ID_INVALID && badges() == first_type, "different device cannot attach a link" );
  Connection* second = core.openLink( !bluetooth_first );
  CHECK( badges() == first_type, "unverified transport has no badge" );
  emit second->authenticationRequested( hello );
  CHECK( second->userId() == id && core.connectedUsers() == 1, "two links, one contact" );
  CHECK( badges() == (Core::Lan | Core::Bluetooth), "both authenticated badges" );
  CHECK( core.connection( id ) == (bluetooth_first ? second : first), "prefer LAN in either connection order" );
  CHECK( core.preferredConnections().size() == 1, "broadcast has one recipient" );
  Connection* duplicate = core.openLink( !bluetooth_first );
  emit duplicate->authenticationRequested( hello );
  CHECK( duplicate->userId() == ID_INVALID && core.connectedUsers() == 1, "duplicate transport refused" );
  core.closeConnection( first );
  CHECK( badges() == second_type && core.connection( id ) == second, "remove only the lost link" );
  CHECK( UserManager::instance().findUser( id ).isStatusConnected(), "contact remains online" );
  CHECK( ChatManager::instance().privateChatForUser( id ).id() == chat_id, "conversation survives link loss" );
  core.closeConnection( second );
  CHECK( badges() == 0 && core.connectedUsers() == 0, "last link clears badges and count" );
  CHECK( !UserManager::instance().findUser( id ).isStatusConnected(), "last link closed goes offline" );
}

int main( int argc, char** argv )
{
  qputenv( "QT_QPA_PLATFORM", "offscreen" );
  QApplication app( argc, argv );
  QStandardPaths::setTestModeEnabled( true );
  Protocol::instance(); // 单例初始化
  testMessageRoundtrip();
  testFileChunkRoundtrip();
  testRijndael();
  testEcdhSymmetry();
  testConcurrentLinks( false );
  testConcurrentLinks( true );
  qInfo() << "=== YongBEEP core unit tests:" << g_passed << "passed," << g_failed << "failed ===";
  return g_failed == 0 ? 0 : 1;
}
