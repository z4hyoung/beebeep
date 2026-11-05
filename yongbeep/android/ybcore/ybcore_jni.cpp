//------------------------------------------------------------------
// ybcore_jni.cpp - JNI bridge between the YongBEEP Android shell
// (com.yongbeep.app.core.CoreBridge) and the BeeBEEP core compiled
// into libybcore.so (A1.2).
//
// Threading model (A1.3):
//   * JNI calls arrive on the Android main (UI) thread. They are all
//     converted to queued slot invocations on YbCoreRunner, which
//     lives on a dedicated QThread together with QCoreApplication,
//     Settings, the singletons and the Core itself. The thread event
//     loop (QThread::exec) drives Listener/Broadcaster/sockets.
//   * All state flows back as JSON strings through the coreEvent
//     signal, forwarded to Java via AttachCurrentThread on the very
//     same core thread:
//       {"type":"started"} / {"type":"stopped"}
//       {"type":"message_received","chat_id":..,"from_user_id":..,...}
//       {"type":"message_sent",...}
//       {"type":"user_online"|"user_offline"|"user_changed",...}
//       {"type":"system_message","text":"..."}
//       {"type":"error","stage":"..."}
//
// Manual peer connection (A1.4 entry point): nativeConnectPeer goes
// through Core::checkNetworkAddress(), the same protected slot the
// automatic discovery path (Broadcaster::newPeerFound) and the mDNS
// path use. It is reached from a tiny Core subclass because it is
// protected - no source/src file is touched for this.
//------------------------------------------------------------------

#include <jni.h>

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QObject>
#include <QRegularExpression>
#include <QThread>
#include <QHostAddress>

#include "Config.h"
#include "Core.h"
#include "Settings.h"
#include "Chat.h"
#include "ChatMessage.h"
#include "NetworkManager.h"
#include "NetworkAddress.h"
#include "Protocol.h"
#include "UserManager.h"
#include "MessageManager.h"
#include "ChatManager.h"
#include "HistoryManager.h"
#include "FileShare.h"
#include "Hive.h"
#include "PluginManager.h"
#include "Random.h"
#include "IconManager.h"
#include "EmoticonManager.h"
#include "ColorManager.h"

// ybcore_version() from ybcore_jni_entry.cpp
extern "C" const char* ybcore_version();

//------------------------------------------------------------------
// JNI globals (set once in nativeInit from the UI thread)
//------------------------------------------------------------------
static JavaVM* g_javaVM = nullptr;
static jobject g_callbackRef = nullptr;      // global ref to CoreBridge singleton
static jmethodID g_onCoreEvent = nullptr;    // void CoreBridge.onCoreEvent(String)
static QThread* g_coreThread = nullptr;
static class YbCoreRunner* g_runner = nullptr;

// QCoreApplication needs argc/argv with static storage duration.
static char ybcore_argv0[] = "ybcore";
static char* ybcore_argv[] = { ybcore_argv0, nullptr };
static int ybcore_argc = 1;

static QString stripHtml( const QString& html_text )
{
  static const QRegularExpression re( "<[^>]*>" );
  QString text = html_text;
  text.replace( re, QString() );
  return text.simplified();
}

// Runs on the core thread (every emitter lives there).
static void forwardEventToJava( const QString& json )
{
  if( !g_javaVM || !g_callbackRef || !g_onCoreEvent )
    return;

  JNIEnv* env = nullptr;
  bool attached = false;
  if( g_javaVM->GetEnv( reinterpret_cast<void**>( &env ), JNI_VERSION_1_6 ) != JNI_OK )
  {
    JavaVMAttachArgs args{ JNI_VERSION_1_6, nullptr, nullptr };
    if( g_javaVM->AttachCurrentThread( &env, &args ) != JNI_OK )
      return;
    attached = true;
  }

  jstring js = env->NewStringUTF( json.toUtf8().constData() );
  if( js )
  {
    env->CallVoidMethod( g_callbackRef, g_onCoreEvent, js );
    env->DeleteLocalRef( js );
  }
  if( env->ExceptionCheck() )
  {
    env->ExceptionDescribe();  // shows up in logcat
    env->ExceptionClear();
  }
  if( attached )
    g_javaVM->DetachCurrentThread();
}

//------------------------------------------------------------------
// Core subclass: lifts the protected manual-connection slot used by
// the automatic discovery path. All duplicate-connection guards in
// Core::newPeerFound apply unchanged (docs/analysis: no transport
// logic is duplicated outside the core).
//------------------------------------------------------------------
class YbCoreBridge : public Core
{
public:
  explicit YbCoreBridge( QObject* parent = nullptr ) : Core( parent ) {}
  void manualConnectToPeer( const QHostAddress& host_address, quint16 port )
  {
    checkNetworkAddress( NetworkAddress( host_address, port ) );
  }
};

//------------------------------------------------------------------
// YbCoreRunner - owns the core stack, lives on the dedicated thread
//------------------------------------------------------------------
class YbCoreRunner : public QObject
{
  Q_OBJECT

public:
  explicit YbCoreRunner( QObject* parent = nullptr )
    : QObject( parent ), mp_core( nullptr ) {}
  ~YbCoreRunner() override
  {
    if( mp_core )
    {
      mp_core->deleteLater();
      mp_core = nullptr;
    }
  }

signals:
  void coreEvent( const QString& json );

public slots:
  // Invoked through QThread::started - runs first on the core thread.
  void setupCoreApplication()
  {
    if( !QCoreApplication::instance() )
      new QCoreApplication( ybcore_argc, ybcore_argv );

    // Same identity as the desktop app (source/src/core/Version.h):
    // isolates the QSettings/config directory from other Qt apps.
    QCoreApplication::setApplicationName( "YongBEEP" );
    QCoreApplication::setOrganizationName( "YongBEEP" );
    QCoreApplication::setOrganizationDomain( "yongbeep.local" );

    qDebug( "ybcore: QCoreApplication ready on core thread %p", QThread::currentThreadId() );
    emitJson( QJsonObject{ { "type", "bridge_ready" },
                           { "version", ybcore_version() } } );
  }

  void startCore( const QString& nickname )
  {
    if( mp_core )
    {
      qDebug( "ybcore: startCore requested but core is already running" );
      emitJson( QJsonObject{ { "type", "started" }, { "already_running", true } } );
      return;
    }

    qDebug( "ybcore: starting core for nickname '%s'", qPrintable( nickname ) );

    // Startup sequence mirrors src/desktop/Main.cpp (no GUI-only steps).
    Random::init();
    Settings::instance().setDefaultFolders();
    Settings::instance().loadRcFile();
    // Nickname is set BEFORE load(): Settings::load() keeps the current
    // local user nickname when no VCard/NickName is stored yet.
    Settings::instance().createLocalUser( nickname );
    Settings::instance().setDataFolder();
    Settings::instance().load();
    Settings::instance().createApplicationUuid();

    // A1 smoke runs on LAN only: no runtime permission prompts.
    Settings::instance().setUseBluetooth( false );

    if( Settings::instance().localUser().name() != nickname )
    {
      User u = Settings::instance().localUser();
      u.setName( nickname );
      Settings::instance().setLocalUser( u );
    }

    NetworkManager::instance().searchLocalHostAddress();
    Settings::instance().setLocalUserHost( NetworkManager::instance().localHostAddress(),
                                           Settings::instance().localUser().networkAddress().hostPort() );

    (void)Hive::instance();
    (void)ColorManager::instance();
    (void)Protocol::instance();
    (void)UserManager::instance();
    MessageManager::instance().loadSavedMessagesAuthCode();
    (void)ChatManager::instance();
    IconManager::instance().setSourcePath( Settings::instance().iconSourcePath() );
    EmoticonManager::instance().setRecentEmoticonsCount( qMax( 8, Settings::instance().emoticonInRecentMenu() ) );
    EmoticonManager::instance().loadRecentEmoticons( Settings::instance().recentEmoticons() );
    EmoticonManager::instance().loadFavoriteEmoticons( Settings::instance().favoriteEmoticons() );
    (void)HistoryManager::instance();
    (void)FileShare::instance();
    PluginManager::instance().loadPlugins( Settings::instance().pluginPath() );

    mp_core = new YbCoreBridge( this );
    connect( mp_core, &Core::connected, this, [this]()
    {
      emitJson( QJsonObject{ { "type", "started" },
                             { "nickname", Settings::instance().localUser().name() } } );
    } );
    connect( mp_core, &Core::disconnected, this, [this]()
    {
      emitJson( QJsonObject{ { "type", "stopped" } } );
    } );
    connect( mp_core, &Core::userChanged, this, &YbCoreRunner::onUserChanged );
    connect( mp_core, &Core::newChatMessage, this, &YbCoreRunner::onNewChatMessage );
    connect( mp_core, &Core::newSystemStatusMessage, this, &YbCoreRunner::onSystemStatusMessage );

    mp_core->init();
    if( !mp_core->start() )
    {
      qWarning( "ybcore: Core::start() failed (see Qt log above)" );
      emitJson( QJsonObject{ { "type", "error" },
                             { "stage", "start" },
                             { "message", "Core::start() failed (network/listener)" } } );
    }
  }

  void stopCore()
  {
    if( !mp_core )
    {
      emitJson( QJsonObject{ { "type", "stopped" }, { "already_stopped", true } } );
      return;
    }
    qDebug( "ybcore: stopping core" );
    mp_core->stop();           // emits disconnected() -> "stopped" event
    mp_core->deleteLater();
    mp_core = nullptr;
  }

  void sendChatMessage( const QString& text, bool markdown )
  {
    if( !mp_core )
    {
      qWarning( "ybcore: cannot send message, core is not running" );
      return;
    }
    int sent = mp_core->sendChatMessage( ID_DEFAULT_CHAT, text, false, false, false, markdown );
    qDebug( "ybcore: sendChatMessage dispatched to %d chat(s)", sent );
  }

  void connectToPeer( const QString& ip, int port )
  {
    emitJson( QJsonObject{ { "type", "connect_attempt" }, { "ip", ip }, { "port", port } } );
    if( !mp_core )
    {
      qWarning( "ybcore: cannot connect to peer, core is not running" );
      emitJson( QJsonObject{ { "type", "error" }, { "stage", "connect" },
                             { "message", "core is not running" } } );
      return;
    }
    QHostAddress host_address( ip );
    if( host_address.isNull() )
    {
      emitJson( QJsonObject{ { "type", "error" }, { "stage", "connect" },
                             { "message", QString( "invalid host address: %1" ).arg( ip ) } } );
      return;
    }
    mp_core->manualConnectToPeer( host_address, static_cast<quint16>( port ) );
  }

private:
  void onUserChanged( const User& user )
  {
    QString status;
    switch( user.status() )
    {
      case User::Offline: status = "offline"; break;
      case User::Online: status = "online"; break;
      case User::Busy: status = "busy"; break;
      case User::Away: status = "away"; break;
      default: status = "unknown"; break;
    }
    QString type = ( user.id() == ID_LOCAL_USER ) ? "local_user_changed"
                 : ( user.isStatusConnected() ? "user_online" : "user_offline" );
    emitJson( QJsonObject{ { "type", type },
                           { "user_id", static_cast<double>( user.id() ) },
                           { "name", user.name() },
                           { "status", status } } );
  }

  void onNewChatMessage( const Chat& chat, const ChatMessage& message )
  {
    if( message.isFromSystem() )
      return;  // system chatter arrives via newSystemStatusMessage / chat dispatch

    QJsonObject o;
    o.insert( "chat_id", static_cast<double>( chat.id() ) );
    o.insert( "chat_name", chat.name() );
    o.insert( "text", message.message() );
    o.insert( "markdown", message.isMarkdown() );
    o.insert( "timestamp", message.timestamp().toString( Qt::ISODate ) );

    if( message.isFromLocalUser() )
    {
      o.insert( "type", "message_sent" );
    }
    else
    {
      User u = UserManager::instance().findUser( message.userId() );
      o.insert( "type", "message_received" );
      o.insert( "from_user_id", static_cast<double>( message.userId() ) );
      o.insert( "from_user_name", u.isValid() ? u.name() : QString( "user #%1" ).arg( message.userId() ) );
    }
    emitJson( o );
  }

  void onSystemStatusMessage( const QString& text, int )
  {
    emitJson( QJsonObject{ { "type", "system_message" },
                           { "text", stripHtml( text ) } } );
  }

  void emitJson( const QJsonObject& object )
  {
    const QString json = QString::fromUtf8( QJsonDocument( object ).toJson( QJsonDocument::Compact ) );
    emit coreEvent( json );
  }

  YbCoreBridge* mp_core;
};

//------------------------------------------------------------------
// JNI entry points (com.yongbeep.app.core.CoreBridge)
//------------------------------------------------------------------
extern "C" JNIEXPORT jstring JNICALL
Java_com_yongbeep_app_core_CoreBridge_nativeVersion( JNIEnv* env, jobject )
{
  return env->NewStringUTF( ybcore_version() );
}

extern "C" JNIEXPORT void JNICALL
Java_com_yongbeep_app_core_CoreBridge_nativeInit( JNIEnv* env, jobject, jobject callback )
{
  if( g_runner )
    return;  // already initialized

  if( env->GetJavaVM( &g_javaVM ) != JNI_OK )
    return;
  g_callbackRef = env->NewGlobalRef( callback );
  jclass callback_class = env->GetObjectClass( callback );
  g_onCoreEvent = env->GetMethodID( callback_class, "onCoreEvent", "(Ljava/lang/String;)V" );
  env->DeleteLocalRef( callback_class );
  if( !g_onCoreEvent )
  {
    qWarning( "ybcore: CoreBridge.onCoreEvent(String) not found - no events will be delivered" );
    env->ExceptionClear();
  }

  // Core thread: QCoreApplication + singletons + Core all live here.
  g_coreThread = new QThread();
  g_coreThread->setObjectName( "ybcore-core" );
  g_runner = new YbCoreRunner();          // no parent: must be movable
  g_runner->moveToThread( g_coreThread );

  QObject::connect( g_coreThread, &QThread::started, g_runner, &YbCoreRunner::setupCoreApplication );
  // Receiver context = runner -> lambda executes on the core thread
  // (direct connection, since the signal is emitted there too).
  QObject::connect( g_runner, &YbCoreRunner::coreEvent, g_runner,
                    []( const QString& json ) { forwardEventToJava( json ); } );
  QObject::connect( g_coreThread, &QThread::finished, g_runner, &QObject::deleteLater );

  g_coreThread->start();
  qDebug( "ybcore: core thread started from JNI (runner %p)", g_runner );
}

static QString jstringToQString( JNIEnv* env, jstring java_string )
{
  if( !java_string )
    return QString();
  const char* chars = env->GetStringUTFChars( java_string, nullptr );
  if( !chars )
    return QString();
  const QString result = QString::fromUtf8( chars );
  env->ReleaseStringUTFChars( java_string, chars );
  return result;
}

extern "C" JNIEXPORT void JNICALL
Java_com_yongbeep_app_core_CoreBridge_nativeStart( JNIEnv* env, jobject, jstring nickname )
{
  if( !g_runner )
    return;
  QMetaObject::invokeMethod( g_runner, "startCore", Qt::QueuedConnection,
                             Q_ARG( QString, jstringToQString( env, nickname ) ) );
}

extern "C" JNIEXPORT void JNICALL
Java_com_yongbeep_app_core_CoreBridge_nativeStop( JNIEnv*, jobject )
{
  if( !g_runner )
    return;
  QMetaObject::invokeMethod( g_runner, "stopCore", Qt::QueuedConnection );
}

extern "C" JNIEXPORT void JNICALL
Java_com_yongbeep_app_core_CoreBridge_nativeSend( JNIEnv* env, jobject, jstring text, jboolean markdown )
{
  if( !g_runner )
    return;
  QMetaObject::invokeMethod( g_runner, "sendChatMessage", Qt::QueuedConnection,
                             Q_ARG( QString, jstringToQString( env, text ) ),
                             Q_ARG( bool, markdown != JNI_FALSE ) );
}

extern "C" JNIEXPORT void JNICALL
Java_com_yongbeep_app_core_CoreBridge_nativeConnectPeer( JNIEnv* env, jobject, jstring ip, jint port )
{
  if( !g_runner )
    return;
  QMetaObject::invokeMethod( g_runner, "connectToPeer", Qt::QueuedConnection,
                             Q_ARG( QString, jstringToQString( env, ip ) ),
                             Q_ARG( int, static_cast<int>( port ) ) );
}

#include "ybcore_jni.moc"
