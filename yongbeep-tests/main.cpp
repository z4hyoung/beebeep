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

int main( int argc, char** argv )
{
  QCoreApplication app( argc, argv );
  Protocol::instance(); // 单例初始化
  testMessageRoundtrip();
  testFileChunkRoundtrip();
  testRijndael();
  testEcdhSymmetry();
  qInfo() << "=== YongBEEP core unit tests:" << g_passed << "passed," << g_failed << "failed ===";
  return g_failed == 0 ? 0 : 1;
}
