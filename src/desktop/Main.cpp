//////////////////////////////////////////////////////////////////////
//
// BeeBEEP Copyright (C) 2010-2026 Marco Mastroddi
//
// BeeBEEP is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published
// by the Free Software Foundation, either version 3 of the License,
// or (at your option) any later version.
//
// BeeBEEP is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with BeeBEEP. If not, see <http://www.gnu.org/licenses/>.
//
// Author: Marco Mastroddi <marco.mastroddi(AT)gmail.com>
//
// $Id: Main.cpp 1596 2026-08-15 06:43:24Z mastroddi $
//
//////////////////////////////////////////////////////////////////////


#include "AudioManager.h"
#include "BeeApplication.h"
#include "Chat.h"
#include "ChatManager.h"
#include "ChatMessage.h"
#include "ColorManager.h"
#include "Core.h"
#include "EmoticonManager.h"
#include "FileShare.h"
#include "GuiIconProvider.h"
#include "GuiMainWindow.h"
#include "HistoryManager.h"
#include "Hive.h"
#include "IconManager.h"
#include "Message.h"
#ifdef BEEBEEP_USE_SHAREDESKTOP
  #include "ImageOptimizer.h"
#endif
#include "Log.h"
#include "MessageManager.h"
#include "NetworkManager.h"
#include "PluginManager.h"
#include "User.h"
#include "UserManager.h"
#include "Protocol.h"
#include "Random.h"
#include "Settings.h"
#include "ShortcutManager.h"
#ifdef BEEBEEP_USE_HUNSPELL
  #include "SpellChecker.h"
#endif
#include <QTimer>


bool SetTranslator( QTranslator* translator, QString language_folder, QString lang )
{
  if( lang.isEmpty() )
  {
    qDebug() << "Language option is empty and default language is installed";
    return false;
  }

  QString language_file_path = Settings::instance().languageFilePath( language_folder, lang );
  if( !translator->load( language_file_path ) )
  {
    qDebug() << qPrintable( language_file_path ) << "file not found. Language" << lang << "will not be installed";
    return false;
  }
  else
    qDebug() << "Language file" << qPrintable( language_file_path ) << "found and" << lang << "installed";
  qApp->installTranslator( translator );
  return true;
}

bool CheckValidArguments( int argc, char *argv[] )
{
  // Qt security issue: https://www.thezdi.com/blog/2019/4/3/loading-up-a-pair-of-qt-bugs-detailing-cve-2019-1636-and-cve-2019-6739
  for( int i = 0; i < argc; i++ )
  {
    if( QString::fromLocal8Bit( argv[i] ).contains( "platformpluginpath", Qt::CaseInsensitive ) )
    {
      qWarning() << "[WARNING] Argument -platformpluginpath has been disabled for security reasons.";
      qDebug() << "Use the qt.conf file if you want to assign a different path for plugins.";
      return false;
    }
  }
  return true;
}

int main( int argc, char *argv[] )
{
  if( !CheckValidArguments( argc, argv ) )
  {
    qDebug() << "Check your arguments and restart the application.";
    return 1;
  }

  // Workaround for ping lag:
  // https://bugreports.qt.io/browse/QTBUG-40332
  qputenv("QT_BEARER_POLL_TIMEOUT", QByteArray::number(-1));

  // Windows with 4k monitors, icons are too big... linux is about to test... MacOSX is ok
#if defined( Q_OS_MACOS )
  QCoreApplication::setAttribute( Qt::AA_EnableHighDpiScaling, true );
#endif


  BeeApplication bee_app( argc, argv );
  (void)Settings::instance();
  bee_app.setApplicationName( Settings::instance().programName() );
  bee_app.setOrganizationName( Settings::instance().organizationName() );
  bee_app.setOrganizationDomain( Settings::instance().organizationDomain() );
  bee_app.setApplicationVersion( Settings::instance().version( false, false, false ) );

  if( bee_app.otherInstanceExists() )
  {
    qDebug() << Settings::instance().programName() << "closed";
    Settings::close();
    return 0;
  }

  /* Enable internal logs */
  Log::installMessageHandler();
  // Next line removes QSslSocket: cannot resolve SSL_* warnings
  QLoggingCategory::setFilterRules( QStringLiteral( "qt.network.ssl.warning=false" ) );

  /* Randomize */
  Random::init();

  /* Load Settings */
  qDebug() << "Starting BeeBEEP" << qPrintable( Settings::instance().version( true, false, true ) )
           << "for" << qPrintable( Settings::instance().operatingSystem( true ) )
           << "and Qt" << QT_VERSION_STR;
  qDebug() << "Qt prefix path:" << qPrintable( QDir::toNativeSeparators( QLibraryInfo::location( QLibraryInfo::PrefixPath ) ) );
  qDebug() << "Qt libraries path:" << qPrintable( QDir::toNativeSeparators( QLibraryInfo::location( QLibraryInfo::LibrariesPath ) ) );
  qDebug() << "Qt binaries path:" << qPrintable( QDir::toNativeSeparators( QLibraryInfo::location( QLibraryInfo::BinariesPath ) ) );
  qDebug() << "Qt plugins path:" << qPrintable( QDir::toNativeSeparators( QLibraryInfo::location( QLibraryInfo::PluginsPath ) ) );
  qDebug() << "Qt data path:" << qPrintable( QDir::toNativeSeparators( QLibraryInfo::location( QLibraryInfo::DataPath ) ) );
  qDebug() << "Qt settings path:" << qPrintable( QDir::toNativeSeparators( QLibraryInfo::location( QLibraryInfo::SettingsPath ) ) );
  qDebug() << "Qt paths are shipped with Qt libraries. Edit qt.conf file to change them.";
  if( bee_app.testAttribute( Qt::AA_EnableHighDpiScaling ) )
    qDebug( "Icons: high DPI scaling enabled" );

  Settings::instance().setDefaultFolders();
  Settings::instance().loadRcFile();
  Settings::instance().createLocalUser( "" );
  Settings::instance().setDataFolder();
  qDebug() << "Settings path:" << qPrintable( Settings::instance().currentSettingsFilePath() );
  Settings::instance().load();
  Settings::instance().createApplicationUuid();
  Log::instance().setMaxLogLines( Settings::instance().maxLogLines() );
  qDebug() << "Log has set max lines to:" << Settings::instance().maxLogLines();
#ifdef BEEBEEP_DEBUG
  QFont chat_font = Settings::instance().chatFont();
  qDebug() << "Font selected for chat:" << chat_font.toString();
  qDebug() << "Font pixel size:" << chat_font.pixelSize();
  qDebug() << "Font point size:" << chat_font.pointSize();
  qDebug() << "Font height:" << static_cast<int>(QFontMetrics( chat_font ).height());
  qDebug() << "Emoticon size in chat:" << Settings::instance().emoticonSizeInChat();
  qDebug() << "Emoticon size in edit:" << Settings::instance().emoticonSizeInEdit();
#endif

  if( !Settings::instance().allowMultipleInstances() )
    bee_app.preventMultipleInstances();

  /* Starting File Logs */
  if( Settings::instance().logToFile() )
    Log::instance().bootFileStream( Settings::instance().logFilePath() );

  /* Apply system language */
  QTranslator translator;
  if( !SetTranslator( &translator, Settings::instance().languagePath(), Settings::instance().language() ) )
  {
    qDebug() << "Looking for language in default folders...";
    SetTranslator( &translator, Settings::instance().defaultLanguageFolderPath(), Settings::instance().language() );
  }

  /* Init Network Manager */
  NetworkManager::instance().searchLocalHostAddress();
  Settings::instance().setLocalUserHost( NetworkManager::instance().localHostAddress(), Settings::instance().localUser().networkAddress().hostPort() );

  /* Init Hive */
  (void)Hive::instance();

  /* Init Color Manager */
  (void)ColorManager::instance();

  /* Init Protocol */
  (void)Protocol::instance();
  qDebug() << "Connection key exchange method selected:" << (Settings::instance().isConnectionKeyExchangeOnlyECDH() ? "ECDH Only" : "auto" );

  /* Init User Manager */
  (void)UserManager::instance();

  /* Init Message Manager */
  MessageManager::instance().loadSavedMessagesAuthCode();

  /* Init Chat Manager */
  (void)ChatManager::instance();

  /* Init Icon Manager */
  IconManager::instance().setSourcePath( Settings::instance().iconSourcePath() );

  /* Init Emoticon Manager */
  EmoticonManager::instance().setRecentEmoticonsCount( qMax( 8, Settings::instance().emoticonInRecentMenu() ) );
  EmoticonManager::instance().loadRecentEmoticons( Settings::instance().recentEmoticons() );
  EmoticonManager::instance().loadFavoriteEmoticons( Settings::instance().favoriteEmoticons() );

  /* Init History Manager */
  (void)HistoryManager::instance();

  /* Init File Sharing */
  (void)FileShare::instance();

  /* Init Icon Provider */
  (void)GuiIconProvider::instance();

#ifdef BEEBEEP_USE_SHAREDESKTOP
  (void)ImageOptimizer::instance();
#endif

  /* Init Audio Manager */
  if( AudioManager::instance().isAudioDeviceAvailable() )
  {
    if( Settings::instance().beepOnNewMessageArrived() )
      AudioManager::instance().loadBeepEffect();
  }
  else
    qWarning() << "Audio device is not available and beep effect cannot be played";

  /* Init Shortcut Manager */
  (void)ShortcutManager::instance();
  if( !Settings::instance().customShortcuts().isEmpty() )
    ShortcutManager::instance().loadFromStringList( Settings::instance().customShortcuts() );

#ifdef BEEBEEP_USE_HUNSPELL
  /* Init SpellChecker */
  (void)SpellChecker::instance();
  if( !Settings::instance().dictionaryPath().isEmpty() )
    SpellChecker::instance().setDictionary( Settings::instance().dictionaryPath() );
#endif

  /* Init Plugins */
  PluginManager::instance().loadPlugins( Settings::instance().pluginPath() );

  /* Init BeeApp */
  bee_app.init();
  bee_app.setAttribute( Qt::AA_DontUseNativeMenuBar );
  if( Settings::instance().autoUserAway() )
    bee_app.setIdleTimeout( Settings::instance().userAwayTimeout() );

  /* Init BeeCore */
  Core bee_core;
  bee_core.init();
  QObject::connect( &bee_app, SIGNAL( networkConfigurationChanged() ), &bee_core, SLOT( updateNetworkConfiguration() ) );

  if( !QSystemTrayIcon::isSystemTrayAvailable() )
    qWarning() << "System tray icon is not available in this OS";

  /* Init Main Window (YongBEEP S3-4a: WeChat-style window replaces GuiMain) */
  QString screenshot_path;
  bool demo_chat = false;
  bool demo_scan = false;
  for( int i = 1; i < argc - 1; i++ )
  {
    if( QString::fromLocal8Bit( argv[i] ) == QLatin1String( "--screenshot" ) )
      screenshot_path = QString::fromLocal8Bit( argv[i+1] );
  }
  for( int i = 1; i < argc; i++ )
  {
    const QString argument = QString::fromLocal8Bit( argv[i] );
    if( argument == QLatin1String( "--demo-chat" ) )
      demo_chat = true;
    else if( argument == QLatin1String( "--demo-scan" ) )
      demo_scan = true;
  }

  GuiMainWindow mw;
  QObject::connect( &bee_app, SIGNAL( showUp() ), &mw, SLOT( raiseOnTop() ) );
  QObject::connect( &bee_app, SIGNAL( tickEvent( int ) ), &mw, SLOT( onTickEvent( int ) ) );
  QObject::connect( &bee_app, SIGNAL( commitDataRequest( QSessionManager& ) ), &mw, SLOT( saveSession( QSessionManager& ) ), Qt::DirectConnection );
  QObject::connect( &bee_app, SIGNAL( shutdownRequest() ), &mw, SLOT( forceShutdown() ), Qt::DirectConnection );
  QObject::connect( &bee_app, SIGNAL( sleepRequest() ), &mw, SLOT( onSleepRequest() ) );
  QObject::connect( &bee_app, SIGNAL( wakeUpRequest() ), &mw, SLOT( onWakeUpRequest() ) );
  mw.loadStyle();
  QMetaObject::invokeMethod( &mw, "checkWindowFlagsAndShow", Qt::QueuedConnection );
  qDebug() << "Loading saved session";
  mw.loadSession();

  if( demo_chat )
  {
    // YongBEEP S3-4b debug helper: fill the default chat with fake messages
    // (2 from a remote peer, 2 from the local user, 1 system) to verify the
    // bubble rendering and take screenshots headlessly (with --screenshot).
    User demo_user;
    demo_user.setId( 990001 );
    demo_user.setName( QStringLiteral( "Windows-PC" ) );
    demo_user.setStatus( User::Online );
    demo_user.setColor( QStringLiteral( "#4f7ecb" ) );
    UserManager::instance().setUser( demo_user );

    Chat demo_chat_chat = ChatManager::instance().chat( ID_DEFAULT_CHAT );
    if( demo_chat_chat.isValid() )
    {
      QList< QPair<VNumber, QString> > demo_messages;
      demo_messages.append( qMakePair( demo_user.id(), QStringLiteral( "那个文件发你" ) ) );
      demo_messages.append( qMakePair( demo_user.id(), QStringLiteral( "好，稍等 😀" ) ) );
      demo_messages.append( qMakePair( ID_LOCAL_USER, QStringLiteral( "收到，谢谢！" ) ) );
      demo_messages.append( qMakePair( ID_LOCAL_USER, QStringLiteral( "报告稍后发你" ) ) );
      for( int demo_i = 0; demo_i < demo_messages.size(); demo_i++ )
      {
        Message m( Message::Chat, demo_messages.at( demo_i ).first, demo_messages.at( demo_i ).second );
        demo_chat_chat.addMessage( ChatMessage( demo_messages.at( demo_i ).first, m, ChatMessage::Chat, false ) );
      }
      // File message through the real path (system FileTransfer message with
      // a clickable file link, rendered as the WeChat file card since S4c).
      Message demo_file_m( Message::System, ID_SYSTEM_MESSAGE,
                           QStringLiteral( "<a href=\"file:///tmp/demo-report.docx\">季度报告.docx</a> 143KB ✓" ) );
      demo_chat_chat.addMessage( ChatMessage( ID_SYSTEM_MESSAGE, demo_file_m, ChatMessage::FileTransfer, false ) );
      Message sys_m( Message::System, ID_SYSTEM_MESSAGE, QObject::tr( "%1 is connected" ).arg( demo_user.name() ) );
      demo_chat_chat.addMessage( ChatMessage( ID_SYSTEM_MESSAGE, sys_m, ChatMessage::System, false ) );
      ChatManager::instance().setChat( demo_chat_chat );

      // S4c: second peer with a private 1:1 chat - the newest chat gets
      // selected, so the screenshot shows the WeChat 1:1 look (no sender
      // names above bubbles) next to the unread badge on the default chat.
      User demo_phone;
      demo_phone.setId( 990002 );
      demo_phone.setName( QStringLiteral( "Yong-iPhone" ) );
      demo_phone.setStatus( User::Online );
      demo_phone.setColor( QStringLiteral( "#07C160" ) );
      UserManager::instance().setUser( demo_phone );

      Group demo_private_group;
      demo_private_group.setId( 990002 );
      demo_private_group.setChatType( Group::PrivateChat );
      demo_private_group.setPrivateId( QStringLiteral( "demo-private-990002" ) );
      demo_private_group.setName( demo_phone.name() );
      demo_private_group.addUser( ID_LOCAL_USER );
      demo_private_group.addUser( demo_phone.id() );

      Chat demo_private_chat;
      demo_private_chat.setGroup( demo_private_group );
      QList< QPair<VNumber, QString> > demo_private_messages;
      demo_private_messages.append( qMakePair( demo_phone.id(), QStringLiteral( "这份是最终版吗？" ) ) );
      demo_private_messages.append( qMakePair( ID_LOCAL_USER, QStringLiteral( "对，发你了，看下" ) ) );
      demo_private_messages.append( qMakePair( demo_phone.id(), QStringLiteral( "好的，收到了 👍" ) ) );
      // S4e: wrapped 3-line peer message - verifies the QPainter rounded
      // bubble and the shrink-to-fit width (right edge stays on one line)
      demo_private_messages.append( qMakePair( demo_phone.id(), QStringLiteral(
        "这条消息有三行，用来验证自绘气泡的圆角与按内容收缩的宽度，第一行先铺开一些文字。"
        "第二行继续补充说明，确保换行以后气泡右缘仍然在同一条直线上面。"
        "第三行收尾，气泡宽度应该明显小于聊天区宽度的百分之六十二。" ) ) );
      // S4e: markdown demo message (**bold**, - list, `code`) from the
      // local user - rendered via QTextDocument::setMarkdown
      const QString demo_markdown_text = QStringLiteral(
        "**本周进展**\n- 消息列表切换为 QListView 自绘\n- 圆角气泡与收缩宽度完成\n构建命令 `make -j8` 通过" );
      demo_private_messages.append( qMakePair( ID_LOCAL_USER, demo_markdown_text ) );
      for( int demo_i = 0; demo_i < demo_private_messages.size(); demo_i++ )
      {
        Message pm( Message::Chat, demo_private_messages.at( demo_i ).first, demo_private_messages.at( demo_i ).second );
        // S4e: backdate the first messages so the WeChat 5-minute separator
        // logic shows a visible centered timestamp above the long message
        if( demo_i <= 2 )
          pm.setTimestamp( QDateTime::currentDateTime().addSecs( -900 ) );
        demo_private_chat.addMessage( ChatMessage( demo_private_messages.at( demo_i ).first, pm, ChatMessage::Chat, false ) );
      }
      // Outgoing file message in the 1:1 chat (WeChat file card, S4c)
      Message demo_own_file_m( Message::System, ID_SYSTEM_MESSAGE,
                               QStringLiteral( "<a href=\"file:///tmp/demo-report.docx\">季度报告.docx</a> 143KB ✓" ) );
      demo_private_chat.addMessage( ChatMessage( ID_SYSTEM_MESSAGE, demo_own_file_m, ChatMessage::FileTransfer, false ) );
      demo_private_chat.setLastMessageTimestamp( QDateTime::currentDateTime() );
      ChatManager::instance().setChat( demo_private_chat );
      // Register the raw markdown text BEFORE any chat is shown, so the
      // demo markdown message is matched and rendered with setMarkdown
      mw.demoChatMarkdownTexts( 990002, QStringList() << demo_markdown_text );

      QMetaObject::invokeMethod( &mw, "loadSession", Qt::QueuedConnection );
      // After loadSession selects a chat: force-select the 1:1 demo chat,
      // seed a draft line (active input + green send button) and put an
      // unread badge on the default chat - deterministic screenshots.
      QTimer::singleShot( 0, &mw, [&mw]()
      {
        mw.demoChatInputDraft( 990002, QStringLiteral( "好，稍后回复你" ), ID_DEFAULT_CHAT, 2 );
      } );
      qDebug() << "Demo chat messages loaded in the default chat";
    }
  }

  if( demo_scan )
    QMetaObject::invokeMethod( &mw, "openScanDevicesDialog", Qt::QueuedConnection );

  if( !screenshot_path.isEmpty() )
  {
    // Debug helper: grab the main window with QWidget::grab() (no screen-recording
    // permission needed) a few seconds after startup, then quit.
    QTimer::singleShot( 3000, &mw, [ &mw, screenshot_path ]() { mw.takeScreenshot( screenshot_path ); } );
  }

  /* Event Loop */
  qDebug() << "Enter in the main event loop";
  int iRet = bee_app.exec();
  qDebug() << "Check and process the remaining events in loop";

  /* Check Icon Provider */
  qDebug() << "IconProvider has load in cache" << GuiIconProvider::instance().cacheSize() << "icons";

  /* Save final session */
  Settings::instance().setRecentEmoticons( EmoticonManager::instance().saveRencentEmoticons() );
  Settings::instance().setFavoriteEmoticons( EmoticonManager::instance().saveFavoriteEmoticons() );
  Settings::instance().loadRcFile();
  Settings::instance().save();

  /* CleanUp */
  bee_app.processEvents( QEventLoop::AllEvents, 2000 );
  qDebug() << "Events remained in loop processed";
  bee_app.cleanUp();
  Settings::instance().clearTemporaryFiles();
  GuiIconProvider::close();
  FileShare::close();
  HistoryManager::close();
  ChatManager::close();
  MessageManager::close();
  UserManager::close();
  Protocol::close();
  PluginManager::close();
  Hive::close();
  NetworkManager::close();
  EmoticonManager::close();
  IconManager::close();
  ColorManager::close();
#ifdef BEEBEEP_USE_SHAREDESKTOP
  ImageOptimizer::close();
#endif
  AudioManager::close();
  ShortcutManager::close();
#ifdef BEEBEEP_USE_HUNSPELL
  SpellChecker::close();
#endif
  qDebug() << "Exit with code:" << iRet;
  Log::instance().closeFileStream();
  Log::instance().close();
  Settings::close();
  /* Exit */
  return iRet;
}
