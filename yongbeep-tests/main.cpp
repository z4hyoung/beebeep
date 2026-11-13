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
#include "ChatMessageModel.h"
#include "ChatMessageDelegate.h"
#include "GuiChatMessage.h"
#include "GuiScanDevices.h"
#include "GuiChat.h"
#include "GuiMainWindow.h"
#include <QLabel>
#include <QListWidget>
#include <QListView>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTextEdit>
#include <QPushButton>
#include <QTimer>
#include "YongTheme.h"
#include "BeeUtils.h"
#include <QTemporaryDir>
#include <QFile>
#include <QPainter>
#include <QTextDocument>
#include <QMouseEvent>
#include <QTranslator>
#include "ChatManager.h"
#include "Core.h"
#include "Connection.h"
#include "Settings.h"
#include "StreamDeviceTcp.h"
#include "StreamDeviceBt.h"
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
  using Core::parseFileMessage;

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
  Settings::instance().setAvatarIconSize( QSize( 40, 40 ) );
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
  User colored_peer = UserManager::instance().findUser( id );
  colored_peer.setColor( "#E87543" ); UserManager::instance().setUser( colored_peer );
  const QImage identity_avatar = index.data( ChatListModel::AvatarRole ).value<QIcon>().pixmap( QSize( 40, 40 ) ).toImage();
  CHECK( !identity_avatar.isNull() && identity_avatar.pixelColor( 0, 0 ).red() != identity_avatar.pixelColor( 0, 0 ).blue(),
         "conversation avatar stays colored even while local service is offline" );


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

  // A contact can retain an earlier Bluetooth/IP address while its live LAN
  // socket uses another address. A TCP file offer must use the live socket.
  User stale_peer = UserManager::instance().findUser( id );
  stale_peer.setNetworkAddress( NetworkAddress( QHostAddress( "fe80::dead" ), 6475 ) );
  UserManager::instance().setUser( stale_peer );
  FileInfo offered( Protocol::instance().newId(), FileInfo::Upload );
  offered.setNameAndSuffix( "YongBEEP-file-test-256KiB.txt" );
  offered.setSize( 256 * 1024 );
  offered.setHostPort( 16476 ); // file port comes from the offer, not chat
  offered.setLastModified( QDateTime::currentDateTime() );
  FileInfo received;
  const auto request = QObject::connect( &core, &Core::fileDownloadRequest,
      [&received]( const User&, const FileInfo& fi ) { received = fi; } );
  core.parseFileMessage( stale_peer, Protocol::instance().fileInfoToMessage( offered, 96 ) );
  CHECK( received.isValid(), "LAN file offer accepted" );
  CHECK( received.networkAddress().hostAddress() == QHostAddress( "192.0.2.7" ),
         "TCP download uses live LAN address instead of stale contact address" );
  CHECK( received.networkAddress().hostPort() == 16476, "TCP download keeps offered file port" );
  QObject::disconnect( request );

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
  CHECK( index.data( ChatListModel::AvatarRole ).value<QIcon>().pixmap( QSize( 40, 40 ) ).toImage() == identity_avatar,
         "peer disconnect only removes badges and leaves its avatar unchanged" );

}

static void testChatPresentation()
{
  Settings::instance().setChatMessagesToShow( -1 );
  User local;
  local.setId( ID_LOCAL_USER ); local.setName( "Yong" ); local.setColor( "#999999" );
  UserManager::instance().setUser( local );
  User remote;
  remote.setId( 880001 ); remote.setName( "zykj" ); remote.setColor( "#5476A3" );
  UserManager::instance().setUser( remote );
  Group group;
  group.setId( 880001 ); group.setChatType( Group::PrivateChat );
  group.setPrivateId( "ui-test-chat" ); group.setName( "ui-test-chat" );
  group.addUser( ID_LOCAL_USER ); group.addUser( remote.id() );
  Chat chat; chat.setGroup( group );
  auto message = [&]( VNumber sender, const QString& text, ChatMessage::Type kind )
  {
    Message m( sender == ID_SYSTEM_MESSAGE ? Message::System : Message::Chat, Protocol::instance().newId(), text );
    chat.addMessage( ChatMessage( sender, m, kind, true ) );
  };
  message( ID_SYSTEM_MESSAGE, "Chat with zykj.", ChatMessage::Header );
  message( remote.id(), QString::fromUtf8( "收到，文件发给你。" ), ChatMessage::Chat );
  message( ID_LOCAL_USER, QString::fromUtf8( "好的，谢谢！\n这条多行消息应当放在一个完整的圆角气泡里。" ), ChatMessage::Chat );
  message( remote.id(), QString( 180, QLatin1Char( 'X' ) ), ChatMessage::Chat );

  QTemporaryDir folder;
  QFile file( folder.filePath( QString::fromUtf8( "YongBEEP-file-test-256KiB-文件测试.txt" ) ) );
  CHECK( file.open( QIODevice::WriteOnly ), "UI test file created" );
  file.write( QByteArray( 262144, 'x' ) ); file.close();
  const QString file_html = QString( "Received <a href=\"%1\">%2</a> from zykj. Open folder.<!--yb-file:in:complete:262144-->" )
      .arg( QUrl::fromLocalFile( file.fileName() ).toString(), QFileInfo( file ).fileName() );
  message( ID_SYSTEM_MESSAGE, file_html, ChatMessage::FileTransfer );
  ChatMessageModel model;
  QFont body = QApplication::font(); body.setPixelSize( 14 ); model.setChatFont( body );
  model.setViewportWidth( 760 ); model.resetFromChat( chat );
  CHECK( model.rowCount() == 4, "redundant chat header hidden" );
  ChatMessageItem* card = model.itemAt( 3 );
  if( !card ) return;
  CHECK( card && card->kind == ChatMessageItem::FileCard && card->fileAvailable, "received file is one available card" );
  CHECK( card && !card->ownMessage && card->fileProgressBytes == card->fileProgressTotal, "received card direction and final progress" );
  ChatMessageDelegate delegate;
  QModelIndex file_index = model.index( 3, 0 );
  const QRect file_row( 0, 500, 760, 170 );
  const int x = ChatMessageModel::HorizontalMargin + YongTheme::avatarSizePx() + ChatMessageModel::AvatarGap;
  const int y = file_row.top() + (card->showTimestamp ? ChatMessageModel::TimestampRowHeight : 0);
  CHECK( delegate.anchorAt( file_index, file_row, QPoint( x+20, y+30 ) ).toLocalFile() == file.fileName(), "scrolled card opens file without a QTextDocument" );
  CHECK( delegate.anchorAt( file_index, file_row, QPoint( x+260, y+100 ) ).scheme() == FileInfo::urlSchemeShowFileInFolder(), "folder action has separate target" );
  CHECK( delegate.anchorAt( file_index, file_row, QPoint( 740, y+10 ) ).isEmpty(), "outside card cannot open file" );
  int opened = 0;
  QObject::connect( &delegate, &ChatMessageDelegate::urlActivated, [&opened]( const QUrl& ) { ++opened; } );
  QStyleOptionViewItem option; option.font = body; option.rect = file_row;
  QMouseEvent click( QEvent::MouseButtonRelease, QPointF( x+20, y+30 ), QPointF( x+20, y+30 ), Qt::LeftButton, Qt::NoButton, Qt::NoModifier );
  delegate.editorEvent( &click, &model, option, file_index );
  CHECK( opened == 1, "left click activates file card" );
  QMouseEvent right( QEvent::MouseButtonRelease, QPointF( x+20, y+30 ), QPointF( x+20, y+30 ), Qt::RightButton, Qt::NoButton, Qt::NoModifier );
  delegate.editorEvent( &right, &model, option, file_index );
  CHECK( opened == 1, "right click does not open file" );

  for( int width : { 320, 760 } )
  {
    model.setViewportWidth( width );
    model.ensureLayout( model.itemAt( 2 ) );
    CHECK( model.itemAt( 2 )->docSize.width() <= model.maxBubbleWidth(), "long unbroken text wraps in narrow and wide windows" );
  }
  model.setViewportWidth( 760 );
  const QRect text_row( 0, 240, 760, 200 );
  auto text_item = model.itemAt( 0 ); model.ensureLayout( text_item );
  const int text_y = 240 + (text_item->showTimestamp ? ChatMessageModel::TimestampRowHeight : 0) + ChatMessageModel::BubbleVPad + 5;
  CHECK( delegate.textPositionAt( model.index( 0, 0 ), text_row, QPoint( x+16, text_y ) ) >= 0,
         "text selection uses local row coordinates after scrolling" );
  model.setItemSelection( 0, 0, 2 );
  CHECK( !model.selectedText( 0 ).isEmpty(), "partial text selection retained" );
  model.clearAllSelections();

  // Use the real text serializer plus a legacy file-card fixture. The old
  // file serializer writes a PNG into the application cache; keep tests local.
  const QString saved = GuiChatMessage::chatToHtml( chat, true, true, true, true, false, true )
      + QString( "<table width=320><tr><td>icon</td><td>%1</td></tr></table><br>" ).arg(
          QString( file_html ).replace( "\">", "\" style=\"text-decoration:none\">" ) );
  ChatManager::instance().addSavedChats( { { chat.name(), saved } } );
  Chat history; history.setGroup( group );
  ChatMessageModel restored; restored.setChatFont( body ); restored.setViewportWidth( 760 ); restored.resetFromChat( history );
  int bubbles=0, cards=0;
  for( int n=0; n<restored.rowCount(); ++n )
  {
    auto item=restored.itemAt( n );
    if( item->kind == ChatMessageItem::UserMessage )
    {
      ++bubbles;
      CHECK( !item->contentHtml.contains( "background-color" ), "legacy per-line backgrounds removed" );
    }
    if( item->kind == ChatMessageItem::FileCard )
    {
      ++cards;
      CHECK( item->fileAvailable && !item->ownMessage, "file direction and open target survive history reload" );
    }
  }
  CHECK( bubbles == 3 && cards == 1, "legacy history becomes individual bubbles and file card" );

  // Search must include archived messages hidden by the normal display limit.
  ChatManager::instance().setChat( history );
  Settings::instance().setChatMessagesToShow( 1 );
  ChatListModel search;
  search.reload();
  // An attached view may read the old rows while model reset begins.
  QObject::connect( &search, &QAbstractItemModel::modelAboutToBeReset, [&search]()
  {
    for( int row=0; row<search.rowCount(); ++row )
      (void)search.index( row, 0 ).data( ChatListModel::NameRole );
  } );
  search.setFilter( QString::fromUtf8( "文件发给你" ) );
  CHECK( search.rowCount() == 1, "global search finds old message outside visible history limit" );
  const QModelIndex message_hit = search.index( 0, 0 );
  CHECK( message_hit.data( ChatListModel::SearchKindRole ).toInt() == ChatListModel::MessageResult,
         "message search result is distinguished from a contact" );
  const int target_row = message_hit.data( ChatListModel::SearchRowRole ).toInt();
  ChatMessageModel target;
  target.resetFromChat( history, true );
  CHECK( target.itemAt( target_row ) && target.itemAt( target_row )->plainText.contains( QString::fromUtf8( "文件发给你" ) ),
         "search result points to exact restored message" );
  target.highlightSearchResult( target_row, QString::fromUtf8( "文件发给你" ) );
  CHECK( target.selectedText( target_row ) == QString::fromUtf8( "文件发给你" ), "matched message text is highlighted" );
  search.setFilter( "FILE-TEST-256kib" );
  CHECK( search.rowCount() == 1, "file-name search is case insensitive and searches archive" );
  CHECK( search.index( 0, 0 ).data( ChatListModel::SearchKindRole ).toInt() == ChatListModel::FileResult,
         "file-name search produces a file result" );
  target.highlightSearchResult( search.index( 0, 0 ).data( ChatListModel::SearchRowRole ).toInt(), "file-test" );
  CHECK( target.itemAt( search.index( 0, 0 ).data( ChatListModel::SearchRowRole ).toInt() )->searchHighlight,
         "file search highlights the exact card" );
  target.clearAllSelections();
  CHECK( !target.itemAt( search.index( 0, 0 ).data( ChatListModel::SearchRowRole ).toInt() )->searchHighlight,
         "normal click clears search highlight" );
  search.setFilter( "zykj" );
  CHECK( search.rowCount() == 1 && search.index( 0, 0 ).data( ChatListModel::SearchRowRole ).toInt() == -1,
         "contact search still works" );
  search.setFilter( "no-such-message-172934" );
  CHECK( search.rowCount() == 0, "unmatched search has no stale rows" );
  search.setFilter( "" );
  CHECK( search.rowOfChat( history.id() ) >= 0, "clearing search restores the conversation list" );
  Settings::instance().setChatMessagesToShow( -1 );

  model.updateFileStatus( QFileInfo( file ).fileName(), 262144, "Failed", file.fileName(), false );
  CHECK( !card->fileAvailable && card->fileStatus == "Failed", "failure never becomes sent because a local file exists" );
  CHECK( delegate.anchorAt( file_index, file_row, QPoint( x+20, y+30 ) ).isEmpty(), "failed card does not activate unavailable file" );

  Chat pending; pending.setGroup( group );
  pending.addMessage( ChatMessage( ID_SYSTEM_MESSAGE, Message( Message::System, ID_SYSTEM_MESSAGE,
      "<a href=\"file:///tmp/transfer.txt\">transfer.txt</a><!--live-->" ), ChatMessage::FileTransfer, false ) );
  CHECK( pending.rewriteLiveFileCard( "transfer.txt", "Received transfer.txt" ), "live card changes in place" );
  CHECK( pending.messages().last().canBeSaved(), "completed received card survives compact history saving" );

  const QString preview = qEnvironmentVariable( "YONGBEEP_UI_PREVIEW" );
  if( !preview.isEmpty() )
  {
    QImage image( 760, 640, QImage::Format_ARGB32_Premultiplied );
    image.fill( YongTheme::chatAreaBackground() );
    QPainter painter( &image );
    int top=16;
    for( int row=0; row<restored.rowCount(); ++row )
    {
      QStyleOptionViewItem opt; opt.font=body;
      const QModelIndex index=restored.index( row, 0 );
      const int height=delegate.sizeHint( opt, index ).height();
      opt.rect=QRect( 0, top, 760, height );
      delegate.paint( &painter, opt, index );
      top+=height;
    }
    painter.end();
    CHECK( image.save( preview ), "UI preview exported" );
  }
}

static void testLastMessageSummary()
{
  const auto previous_history = ChatManager::instance().constHistoryMap();
  User peer; peer.setId( 880020 ); peer.setName( "Summary Peer" ); peer.setColor( "#457AC0" );
  UserManager::instance().setUser( peer );
  Group group; group.setId( 880020 ); group.setChatType( Group::PrivateChat );
  group.setPrivateId( "summary-test" ); group.setName( "summary-test" );
  group.addUser( ID_LOCAL_USER ); group.addUser( peer.id() );
  Chat chat; chat.setGroup( group );
  ChatListModel list;
  auto summary = [&]()
  {
    ChatManager::instance().setChat( chat ); list.reload();
    return list.index( list.rowOfChat( chat.id() ), 0 ).data( ChatListModel::SummaryRole ).toString();
  };
  const QString file_html = "<table><tr><td>icon</td><td><a href=\"file:///tmp/report.txt\">report &amp; final.txt</a><!--yb-file:in:complete:123--></td></tr></table><br>";
  ChatManager::instance().addSavedChats( { { chat.name(), file_html } } );
  chat.addMessage( ChatMessage( ID_SYSTEM_MESSAGE, Message( Message::System, ID_SYSTEM_MESSAGE, "Chat with Summary Peer." ), ChatMessage::Header, false ) );
  CHECK( summary() == ChatListModel::tr( "[File] %1" ).arg( "report & final.txt" ), "restart restores last received file name as conversation preview" );
  chat.addMessage( ChatMessage( peer.id(), Message( Message::Chat, peer.id(), "Hello\nworld & team" ), ChatMessage::Chat, true ) );
  chat.addMessage( ChatMessage( ID_SYSTEM_MESSAGE, Message( Message::System, ID_SYSTEM_MESSAGE, "Peer connected" ), ChatMessage::Connection, false ) );
  CHECK( summary() == "Hello world & team", "last real message wins over connection notices and older archive" );
  chat.addMessage( ChatMessage( ID_LOCAL_USER, Message( Message::Chat, ID_LOCAL_USER, "Last message" ), ChatMessage::Chat, true ) );
  CHECK( summary() == ChatListModel::tr( "You: " ) + "Last message", "outgoing preview identifies sender" );
  chat.addMessage( ChatMessage( ID_SYSTEM_MESSAGE, Message( Message::System, ID_SYSTEM_MESSAGE,
      "Sent <a href=\"file:///tmp/sent.txt\">sent.txt</a> to peer.<!--yb-file:out:complete:123-->" ), ChatMessage::FileTransfer, true ) );
  CHECK( summary() == ChatListModel::tr( "You: " ) + ChatListModel::tr( "[File] %1" ).arg( "sent.txt" ), "last file replaces text preview without transfer boilerplate" );
  const QString saved_text = GuiChatMessage::chatToHtml( chat, true, true, true, true, false, true );
  ChatManager::instance().addSavedChats( { { chat.name(), saved_text } } );
  chat = Chat(); chat.setGroup( group );
  CHECK( summary() == ChatListModel::tr( "You: " ) + "Last message", "changed archive invalidates cached preview and restores last outgoing text" );
  ChatManager::instance().addSavedChats( previous_history );
}

static void testConversationOrder()
{
  const auto previous_history = ChatManager::instance().constHistoryMap();
  const Chat previous_default = ChatManager::instance().defaultChat();
  auto createChat = []( VNumber id, const QString& name, User::Status status )
  {
    User peer;
    peer.setId( id ); peer.setName( name ); peer.setStatus( status ); peer.setColor( "#518ACE" );
    UserManager::instance().setUser( peer );
    return Protocol::instance().createPrivateChat( peer );
  };
  Chat older = createChat( 880101, "Order online", User::Online );
  Chat newer = createChat( 880102, "Order offline", User::Offline );
  Chat empty = createChat( 880103, "Order empty", User::Online );
  Chat tied = createChat( 880104, "Order tied", User::Online );
  const QDate day = QDate::currentDate();
  auto append = [&]( Chat& chat, VNumber sender, ChatMessage::Type type, const QTime& time, const QString& text )
  {
    Message message( sender == ID_SYSTEM_MESSAGE ? Message::System : Message::Chat, chat.id(), text );
    message.setTimestamp( QDateTime( day, time ) );
    chat.addMessage( ChatMessage( sender, message, type, true ) );
  };
  append( older, older.privateUserId(), ChatMessage::Chat, QTime( 9, 30 ), "Older real message" );
  append( newer, newer.privateUserId(), ChatMessage::Chat, QTime( 10, 30 ), "Newer real message" );
  append( older, ID_SYSTEM_MESSAGE, ChatMessage::Connection, QTime( 23, 50 ), "Peer connected" );
  append( empty, ID_SYSTEM_MESSAGE, ChatMessage::Header, QTime( 23, 59 ), "Chat header" );
  append( tied, tied.privateUserId(), ChatMessage::Chat, QTime( 10, 30 ), "Same time" );
  ChatListModel list;
  auto reload = [&]()
  {
    for( const Chat& chat : { older, newer, empty, tied } ) ChatManager::instance().setChat( chat );
    list.reload();
  };
  auto timeText = [&]( const Chat& chat )
  {
    return list.index( list.rowOfChat( chat.id() ), 0 ).data( ChatListModel::TimeTextRole ).toString();
  };
  reload();
  CHECK( list.rowOfChat( newer.id() ) < list.rowOfChat( older.id() ), "newer offline conversation sorts before older online conversation" );
  CHECK( list.rowOfChat( empty.id() ) > list.rowOfChat( older.id() ) && timeText( empty ).isEmpty(), "empty conversation has no system-event timestamp and sorts last" );
  CHECK( timeText( older ) == "09:30", "conversation time matches real message instead of later connection notice" );
  CHECK( (list.rowOfChat( newer.id() ) < list.rowOfChat( tied.id() )) == (newer.id() < tied.id()), "equal message timestamps have deterministic order" );
  User peer = UserManager::instance().findUser( newer.privateUserId() );
  peer.setStatus( User::Online ); UserManager::instance().setUser( peer );
  older.addUnreadMessage();
  reload();
  CHECK( list.rowOfChat( newer.id() ) < list.rowOfChat( older.id() ), "presence and unread counts do not override message time" );

  const QString file = "Sent <a href=\"file:///tmp/order.txt\">order.txt</a><!--yb-file:out:complete:10-->";
  append( older, ID_SYSTEM_MESSAGE, ChatMessage::FileTransfer, QTime( 11, 0 ), file );
  reload();
  CHECK( list.rowOfChat( older.id() ) < list.rowOfChat( newer.id() ) && timeText( older ) == "11:00", "outgoing file message moves conversation to its message-time position" );
  append( older, ID_SYSTEM_MESSAGE, ChatMessage::Connection, QTime( 23, 58 ), "Peer disconnected" );
  reload();
  CHECK( timeText( older ) == "11:00", "later disconnection leaves last message time intact" );

  append( older, ID_SYSTEM_MESSAGE, ChatMessage::FileTransfer, QTime( 13, 10, 25 ), file );
  append( newer, newer.privateUserId(), ChatMessage::Chat, QTime( 13, 10, 20 ), "Archived first message" );
  append( newer, newer.privateUserId(), ChatMessage::Chat, QTime( 13, 10, 30 ), "Archived last message" );
  const QString saved = GuiChatMessage::chatToHtml( newer, true, true, true, true, false, true );
  CHECK( saved.contains( QDateTime( day, QTime( 13, 10, 30 ) ).toString( Qt::ISODateWithMs ) ), "saved message preserves precise timestamp even within a separator group" );
  const Group group = newer.group();
  newer = Chat(); newer.setGroup( group );
  append( newer, ID_SYSTEM_MESSAGE, ChatMessage::Header, QTime( 23, 59 ), "Restarted chat header" );
  auto history = previous_history;
  history.insert( newer.name(), saved );
  ChatManager::instance().addSavedChats( history );
  reload();
  CHECK( list.rowOfChat( newer.id() ) < list.rowOfChat( older.id() ) && timeText( newer ) == "13:10", "restart restores exact last-message ordering from history" );
  reload();
  CHECK( list.rowOfChat( newer.id() ) < list.rowOfChat( older.id() ), "cached history time retains ordering on list refresh" );

  QString legacy = saved;
  legacy.remove( QRegularExpression( " data-yb-timestamp=\"[^\"]*\"" ) );
  legacy.replace( ">13:10<", QString( ">%1 13:10<" ).arg( day.toString( "yyyy-MM-dd" ) ) );
  history.insert( newer.name(), legacy );
  ChatManager::instance().addSavedChats( history );
  reload();
  CHECK( timeText( newer ) == "13:10" && list.rowOfChat( newer.id() ) < list.rowOfChat( tied.id() ), "legacy history uses its visible date and time for ordering" );

  auto legacyMessage = [&]( const Chat& conversation, const QTime& time )
  {
    Chat single; single.setGroup( conversation.group() );
    append( single, peer.id(), ChatMessage::Chat, time, "Legacy ordering fixture" );
    QString html = GuiChatMessage::chatToHtml( single, true, true, true, true, false, true );
    html.remove( QRegularExpression( " data-yb-timestamp=\"[^\"]*\"" ) );
    return html;
  };
  Chat all_users = Protocol::instance().createDefaultChat();
  QString dated_history = legacyMessage( newer, QTime( 10, 13 ) );
  dated_history.replace( ">10:13<", QString( ">%1 10:13<" ).arg( day.addDays( -2 ).toString( "yyyy-MM-dd" ) ) );
  dated_history += legacyMessage( newer, QTime( 9, 14 ) );
  dated_history += legacyMessage( newer, QTime( 21, 31 ) );
  dated_history += legacyMessage( newer, QTime( 18, 5 ) );
  history.insert( newer.name(), dated_history );
  history.insert( all_users.name(), legacyMessage( all_users, QTime( 20, 30 ) ) );
  ChatManager::instance().addSavedChats( history );
  ChatManager::instance().setChat( all_users );
  reload();
  CHECK( timeText( newer ) == "18:05", "dated legacy history carries dates across midnight between saved batches" );
  CHECK( timeText( all_users ).isEmpty(), "clock-only archive never fabricates today's date from the history file" );
  CHECK( list.rowOfChat( newer.id() ) < list.rowOfChat( all_users.id() ), "recent dated contact sorts ahead of undated All users history" );
  append( all_users, ID_LOCAL_USER, ChatMessage::Chat, QTime( 18, 6 ), "New broadcast message" );
  ChatManager::instance().setChat( all_users );
  reload();
  CHECK( list.rowOfChat( all_users.id() ) < list.rowOfChat( newer.id() ) && timeText( all_users ) == "18:06",
      "All users sorts ahead when its actual last message is newer" );
  if( previous_default.isValid() ) ChatManager::instance().setChat( previous_default );
  else ChatManager::instance().removeChat( all_users );
  for( const Chat& chat : { older, newer, empty, tied } ) ChatManager::instance().removeChat( chat );
  ChatManager::instance().addSavedChats( previous_history );
}

static void testPresenceNeutralAvatars()
{
  User peer;
  peer.setId( 880009 ); peer.setName( "Color Peer" ); peer.setColor( "#E87543" );
  peer.setStatus( User::Online );
  const QSize size( 40, 40 );
  const QImage initials_online = Bee::avatarForUser( peer, size, true ).toImage();
  peer.setStatus( User::Offline );
  CHECK( Bee::avatarForUser( peer, size, true ).toImage() == initials_online,
         "initials avatar keeps its identity color while offline" );
  CHECK( initials_online.pixelColor( 0, 0 ) == QColor( "#E87543" ), "initials avatar has no status-color border" );
  QPixmap photo( 80, 80 ); photo.fill( QColor( "#2476CC" ) );
  VCard vcard = peer.vCard(); vcard.setPhoto( photo ); peer.setVCard( vcard );
  const QImage photo_offline = Bee::avatarForUser( peer, size, true ).toImage();
  peer.setStatus( User::Online );
  CHECK( Bee::avatarForUser( peer, size, true ).toImage() == photo_offline,
         "photo stays in original color across presence changes" );
  CHECK( photo_offline.pixelColor( 0, 0 ) == QColor( "#2476CC" ), "photo has no presence border or gray filter" );
}

static void testConversationCache()
{
  const int previous_limit = Settings::instance().chatMessagesToShow();
  const QBitArray previous_filter = Settings::instance().chatMessageFilter();
  const auto previous_history = ChatManager::instance().constHistoryMap();
  Settings::instance().setChatMessagesToShow( -1 );
  Settings::instance().setChatMessageFilter( QBitArray( ChatMessage::NumTypes, false ) );
  TestCore core;
  User peer;
  peer.setId( 880002 ); peer.setName( "CachePeer" ); peer.setColor( "#518ACE" );
  UserManager::instance().setUser( peer );
  Chat chat = Protocol::instance().createPrivateChat( peer );
  for( int i = 0; i < 60; ++i )
    chat.addMessage( ChatMessage( peer.id(), Message( Message::Chat, chat.id(), QString( "Cached message %1" ).arg( i ) ), ChatMessage::Chat, true ) );
  ChatManager::instance().setChat( chat );

  GuiMainWindow window;
  window.resize( 920, 620 );
  window.show();
  auto activate = [&]( VNumber id )
  {
    return QMetaObject::invokeMethod( &window, "showChat", Qt::DirectConnection, Q_ARG( VNumber, id ) );
  };
  CHECK( activate( chat.id() ), "normal entry opens cached conversation" );
  QApplication::processEvents();
  QStackedWidget* stack = window.findChild<QStackedWidget*>();
  GuiChat* view = stack ? qobject_cast<GuiChat*>( stack->currentWidget() ) : nullptr;
  ChatMessageModel* model = view ? view->findChild<ChatMessageModel*>() : nullptr;
  QListView* messages = view ? view->findChild<QListView*>( "GuiChatViewer" ) : nullptr;
  QTextEdit* input = view ? view->findChild<QTextEdit*>( "GuiMessageEdit" ) : nullptr;
  CHECK( model && messages && input && model->rowCount() == 60, "first activation loads all live messages" );
  if( !model || !messages || !input || model->rowCount() != 60 )
  {
    Settings::instance().setChatMessagesToShow( previous_limit );
    Settings::instance().setChatMessageFilter( previous_filter );
    return;
  }
  view->setDemoInputDraft( "Unsent cache draft" );
  model->ensureLayout( model->itemAt( 0 ) );
  ChatMessageItem* first = model->itemAt( 0 );
  QTextDocument* document = first->doc;
  messages->verticalScrollBar()->setValue( messages->verticalScrollBar()->maximum() / 2 + 3 );
  const int scroll_position = messages->verticalScrollBar()->value();
  int resets = 0;
  QObject::connect( model, &QAbstractItemModel::modelReset, [&resets]() { ++resets; } );
  for( int i = 0; i < 5; ++i )
  {
    activate( ID_DEFAULT_CHAT );
    activate( chat.id() );
    QApplication::processEvents();
  }
  CHECK( resets == 0, "repeated normal conversation switches never reset unchanged message models" );
  CHECK( model->itemAt( 0 ) == first && first->doc == document, "switching retains parsed message and document caches" );
  CHECK( messages->verticalScrollBar()->value() == scroll_position, "switching preserves exact scroll position" );
  CHECK( input->toPlainText() == "Unsent cache draft", "switching preserves input draft" );

  ChatMessage incoming( peer.id(), Message( Message::Chat, chat.id(), "Arrived while hidden" ), ChatMessage::Chat, true );
  activate( ID_DEFAULT_CHAT );
  chat.addMessage( incoming );
  ChatManager::instance().setChat( chat );
  view->appendChatMessage( chat, incoming );
  CHECK( resets == 0 && model->rowCount() == 61, "hidden conversation appends new messages incrementally" );
  activate( chat.id() );
  CHECK( model->rowCount() == 61 && model->itemAt( 60 )->plainText == "Arrived while hidden", "changed conversation reloads without losing or duplicating new messages" );
  const int after_message = resets;
  activate( ID_DEFAULT_CHAT ); activate( chat.id() );
  CHECK( resets == after_message, "changed conversation becomes reusable after refresh" );

  QTemporaryDir directory;
  QFile file( directory.filePath( "cached-transfer.txt" ) );
  CHECK( file.open( QIODevice::WriteOnly ), "cache transfer fixture created" );
  file.write( "cache test" ); file.close();
  const QString link = QString( "<a href=\"%1\">cached-transfer.txt</a>" ).arg( QUrl::fromLocalFile( file.fileName() ).toString() );
  chat.addMessage( ChatMessage( ID_SYSTEM_MESSAGE, Message( Message::System, chat.id(),
      "Sending " + link + "<!--live--><!--yb-file:out:pending:10-->" ), ChatMessage::FileTransfer, true ) );
  ChatManager::instance().setChat( chat ); activate( chat.id() );
  CHECK( !model->itemAt( model->rowCount() - 1 )->fileAvailable, "pending file card loaded" );
  const auto count = chat.messages().size();
  CHECK( chat.rewriteLiveFileCard( "cached-transfer.txt", "Sent " + link + "<!--yb-file:out:complete:10-->" ), "file card rewritten in place" );
  ChatManager::instance().setChat( chat );
  const int before_rewrite = resets;
  activate( ID_DEFAULT_CHAT ); activate( chat.id() );
  CHECK( chat.messages().size() == count && resets == before_rewrite + 1 && model->itemAt( model->rowCount() - 1 )->fileAvailable,
      "same-count file card rewrite invalidates cached content" );

  Chat archive; archive.setGroup( chat.group() );
  archive.addMessage( ChatMessage( peer.id(), Message( Message::Chat, chat.id(), "Archived cache needle" ), ChatMessage::Chat, true ) );
  auto history = previous_history;
  history.insert( chat.name(), GuiChatMessage::chatToHtml( archive, true, true, true, true, false, true ) );
  ChatManager::instance().addSavedChats( history );
  emit core.savedChatListAvailable();
  CHECK( model->findPlainText( "Archived cache needle", -1, true ) >= 0, "asynchronous history completion refreshes visible conversation" );
  const int after_history = resets;
  activate( ID_DEFAULT_CHAT ); activate( chat.id() );
  CHECK( resets == after_history, "loaded history is reused on subsequent switches" );

  Settings::instance().setChatMessagesToShow( 1 );
  activate( chat.id() );
  CHECK( model->rowCount() == 2 && model->itemAt( 1 )->kind == ChatMessageItem::FileCard, "display limit change invalidates cache" );
  QBitArray filter = Settings::instance().chatMessageFilter();
  filter.setBit( ChatMessage::FileTransfer, true );
  Settings::instance().setChatMessageFilter( filter );
  activate( chat.id() );
  CHECK( model->rowCount() == 1, "message filter change invalidates cache" );
  Settings::instance().setChatMessageFilter( QBitArray( ChatMessage::NumTypes, false ) );
  activate( chat.id() );
  view->revealSearchResult( 0, "Archived cache needle" );
  const int result_row = model->findPlainText( "Archived cache needle", -1, true );
  const int after_search = resets;
  activate( ID_DEFAULT_CHAT ); activate( chat.id() );
  CHECK( resets == after_search && result_row >= 0 && model->selectedText( result_row ) == "Archived cache needle",
      "switching retains full-history search result and highlight" );
  view->setChat( chat );
  CHECK( resets == after_search + 1 && model->rowCount() == 2, "explicit reload retains existing forced-refresh behavior" );
  Settings::instance().setChatMessagesToShow( previous_limit );
  Settings::instance().setChatMessageFilter( previous_filter );
  ChatManager::instance().addSavedChats( previous_history );
}

static void testConnectionPage()
{
  TestCore core;
  Connection* link = core.openLink( false );
  emit link->authenticationRequested( peerHello( "stable-device-hash" ) );
  GuiScanDevices page;
  QLabel* status = page.findChild<QLabel*>( "GuiConnectionStatus" );
  CHECK( status && status->text().contains( "BadgePeer", Qt::CaseInsensitive ), "connection page lists authenticated LAN peer" );
  CHECK( status && !status->text().contains( "Scanning", Qt::CaseInsensitive ), "connection page never claims a stopped scan is active" );
#if defined( Q_OS_MACOS )
  QListWidget* devices = page.findChild<QListWidget*>();
  CHECK( devices && !devices->isHidden(), "macOS offers native paired-device search and connection list" );
  CHECK( page.findChild<QPushButton*>( "GuiBluetoothSearchButton" ) != nullptr, "manual Bluetooth search button is available" );
#endif
  Connection* failed_bt = core.openLink( true );
  QString failure_reason;
  QObject::connect( &core, &Core::bluetoothConnectionFailed, [&failure_reason]( const QString& reason ) { failure_reason = reason; } );
  emit failed_bt->error( QAbstractSocket::UnknownSocketError );
  QLabel* search_status = page.findChild<QLabel*>( "GuiBluetoothSearchStatus" );
  CHECK( !failure_reason.isEmpty(), "Bluetooth connection failure has a readable reason" );
  CHECK( search_status && search_status->text() == GuiScanDevices::tr( "Bluetooth connection failed: %1. Check YongBEEP on the other device, then retry." ).arg( failure_reason ),
         "Bluetooth connection failure appears in the devices page" );
  core.closeConnection( link );
  page.updateStatus();
  CHECK( status && !status->text().contains( "BadgePeer", Qt::CaseInsensitive ), "connection page removes disconnected peers" );
}

static void testBluetoothCallbackLifetime()
{
#if defined( Q_OS_MACOS )
  StreamDeviceBt stream;
  QBluetoothSocket* socket = qobject_cast<QBluetoothSocket*>( stream.ioDevice() );
  bool native_callback_active = true;
  bool handled = false, aborted_inside_callback = false;
  QObject::connect( &stream, &StreamDevice::errorOccurred, [&]()
  {
    handled = true;
    aborted_inside_callback = native_callback_active;
    stream.abort(); // Core performs this cleanup after a failed connection.
  } );
  emit socket->errorOccurred( QBluetoothSocket::SocketError::UnknownSocketError );
  native_callback_active = false;
  QCoreApplication::processEvents();
  CHECK( handled && !aborted_inside_callback, "Bluetooth error cleanup waits until native callback returns" );
#endif
}

static void testConnectionRecovery()
{
  TestCore core;
  GuiMainWindow window;
  QTimer* retry = window.findChild<QTimer*>( "GuiConnectionRecoveryTimer" );
  CHECK( retry && !retry->isActive(), "connection recovery starts idle" );
  if( !retry ) return;
  emit core.networkInterfaceIsUp();
  QCoreApplication::processEvents();
  CHECK( retry->isActive() && retry->isSingleShot() && retry->interval() >= 1000,
         "network recovery schedules a bounded reconnect instead of a busy loop" );
  window.onSleepRequest();
  CHECK( !retry->isActive(), "sleep cancels pending reconnects" );
  emit core.networkInterfaceIsUp();
  QCoreApplication::processEvents();
  CHECK( !retry->isActive(), "late network notifications cannot reconnect while suspended" );
  window.onWakeUpRequest();
  CHECK( retry->isActive(), "wake automatically schedules connection service recovery" );
  emit core.connected();
  CHECK( !retry->isActive(), "successful connection cancels retry timer" );
  emit core.disconnected();
  CHECK( retry->isActive(), "unexpected service stop schedules automatic recovery" );
  window.onSleepRequest();
}

int main( int argc, char** argv )
{
  qputenv( "QT_QPA_PLATFORM", "offscreen" );
  QApplication app( argc, argv );
  QStandardPaths::setTestModeEnabled( true );
  QTranslator ui_translation;
  if( ui_translation.load( qEnvironmentVariable( "YONGBEEP_UI_TRANSLATION" ) ) )
    app.installTranslator( &ui_translation );
  Protocol::instance(); // 单例初始化
  testMessageRoundtrip();
  testFileChunkRoundtrip();
  testRijndael();
  testEcdhSymmetry();
  testConcurrentLinks( false );
  testConcurrentLinks( true );
  testChatPresentation();
  testConversationCache();
  testConnectionPage();
  testBluetoothCallbackLifetime();
  testConnectionRecovery();
  testPresenceNeutralAvatars();
  testLastMessageSummary();
  testConversationOrder();
  qInfo() << "=== YongBEEP core unit tests:" << g_passed << "passed," << g_failed << "failed ===";
  return g_failed == 0 ? 0 : 1;
}
