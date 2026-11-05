//////////////////////////////////////////////////////////////////////
//
// YongBEEP — WeChat-style main window (S3-4a)
//
//////////////////////////////////////////////////////////////////////

#include "GuiMainWindow.h"
#include "AudioManager.h"
#include "BeeApplication.h"
#include "BeeUtils.h"
#include "BtManager.h"
#include "Chat.h"
#include "ChatListModel.h"
#include "ChatListItemDelegate.h"
#include "ChatManager.h"
#include "ChatMessage.h"
#include "Core.h"
#include "DockBadge.h"
#include "FileDialog.h"
#include "FileInfo.h"
#include "GuiAskPassword.h"
#include "GuiChat.h"
#include "GuiSavedChat.h"
#include "GuiScanDevices.h"
#include "GuiSystemTray.h"
#include "GuiWizard.h"
#include "IconManager.h"
#include "Settings.h"
#include "User.h"
#include "UserManager.h"
#include "YongTheme.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QScreen>
#include <QSessionManager>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyleFactory>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>


GuiMainWindow::GuiMainWindow( QWidget* parent )
  : QMainWindow( parent ), m_currentChatId( ID_INVALID )
{
  setObjectName( "GuiWeChatMainWindow" );
  setWindowIcon( IconManager::instance().icon( "beebeep.png" ) );
  m_forceShutdown = false;
  m_coreIsConnecting = false;
  mp_scanDevices = Q_NULLPTR;

  createGui();
  setupCoreConnections();
  updateWindowTitle();
}


void GuiMainWindow::createGui()
{
  // ---------- Left panel: search + conversation list (WeChat ~280px) ----------
  mp_searchBox = new QLineEdit( this );
  mp_searchBox->setObjectName( "GuiWeChatSearchBox" );
  mp_searchBox->setPlaceholderText( tr( "Search" ) );
  mp_searchBox->setClearButtonEnabled( true );
  mp_searchBox->setFixedHeight( 28 );
  // Leading magnifier icon inside the capsule (light gray, dark on focus)
  mp_searchBox->addAction( YongTheme::svgIcon( "search", YongTheme::textSecondary(), YongTheme::iconGray(), 16 ),
                           QLineEdit::LeadingPosition );

  mp_chatListModel = new ChatListModel( this );
  mp_chatListView = new QListView( this );
  mp_chatListView->setObjectName( "GuiWeChatChatList" );
  mp_chatListView->setModel( mp_chatListModel );
  mp_chatListView->setItemDelegate( new ChatListItemDelegate( mp_chatListView ) );
  mp_chatListView->setEditTriggers( QAbstractItemView::NoEditTriggers );
  mp_chatListView->setSelectionMode( QAbstractItemView::SingleSelection );
  mp_chatListView->setVerticalScrollMode( QAbstractItemView::ScrollPerPixel );
  mp_chatListView->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
  mp_chatListView->setUniformItemSizes( true );
  mp_chatListView->setMouseTracking( true );
  mp_chatListView->setFrameShape( QFrame::NoFrame );

  QWidget* left_panel = new QWidget( this );
  left_panel->setObjectName( "GuiWeChatLeftPanel" );
  left_panel->setMinimumWidth( 240 );
  left_panel->setMaximumWidth( 380 );
  QVBoxLayout* left_layout = new QVBoxLayout( left_panel );
  left_layout->setContentsMargins( 0, 0, 0, 0 );
  left_layout->setSpacing( 0 );

  // Search capsule sits inset at the top of the list (WeChat macOS)
  QWidget* search_container = new QWidget( left_panel );
  search_container->setObjectName( "GuiWeChatSearchContainer" );
  QHBoxLayout* search_layout = new QHBoxLayout( search_container );
  search_layout->setContentsMargins( 12, 10, 12, 8 );
  search_layout->setSpacing( 0 );
  search_layout->addWidget( mp_searchBox );
  left_layout->addWidget( search_container );
  left_layout->addWidget( mp_chatListView, 1 );

  // Bottom of the left panel: bluetooth scan entry (PRD 4.1 "[＋ 扫描设备]"),
  // WeChat-style flat green text button with a small bluetooth glyph
  QWidget* scan_container = new QWidget( left_panel );
  scan_container->setObjectName( "GuiWeChatScanContainer" );
  QHBoxLayout* scan_layout = new QHBoxLayout( scan_container );
  scan_layout->setContentsMargins( 12, 8, 12, 10 );
  scan_layout->setSpacing( 0 );
  mp_scanDevicesButton = new QPushButton( tr( "Scan devices" ), scan_container );
  mp_scanDevicesButton->setObjectName( "GuiWeChatScanDevicesButton" );
  mp_scanDevicesButton->setIcon( YongTheme::svgIcon( "bluetooth", YongTheme::brandGreen(), QColor(), 14 ) );
  mp_scanDevicesButton->setIconSize( QSize( 14, 14 ) );
  mp_scanDevicesButton->setCursor( Qt::PointingHandCursor );
  scan_layout->addWidget( mp_scanDevicesButton, 0, Qt::AlignHCenter );
  left_layout->addWidget( scan_container );

  // ---------- Right panel: chat header + embedded GuiChat ----------
  mp_headerName = new QLabel( this );
  mp_headerName->setObjectName( "GuiWeChatHeaderName" );
  QFont header_font = mp_headerName->font();
  header_font.setPixelSize( YongTheme::fontBodyPx() );
  header_font.setWeight( QFont::DemiBold );
  mp_headerName->setFont( header_font );

  // Connection status: 8px dot + 12px text (green/orange/gray, S4c spec)
  mp_headerStatusDot = new QLabel( this );
  mp_headerStatusDot->setObjectName( "GuiWeChatHeaderDot" );
  mp_headerStatusDot->setFixedSize( 8, 8 );
  mp_headerStatusDot->setStyleSheet( QString( "border-radius: 4px; background: %1;" ).arg( YongTheme::textSecondary().name() ) );

  mp_headerStatus = new QLabel( this );
  mp_headerStatus->setObjectName( "GuiWeChatHeaderStatus" );
  QFont status_font = mp_headerStatus->font();
  status_font.setPixelSize( YongTheme::fontAuxPx() );
  mp_headerStatus->setFont( status_font );

  QWidget* header_panel = new QWidget( this );
  header_panel->setObjectName( "GuiWeChatHeader" );
  QHBoxLayout* header_layout = new QHBoxLayout( header_panel );
  header_layout->setContentsMargins( 16, 10, 16, 10 );
  header_layout->setSpacing( 6 );
  // YongBEEP: dot + status text grouped in their own centered box so every
  // element shares one centerline regardless of per-label height differences
  QWidget* status_group = new QWidget( header_panel );
  QHBoxLayout* status_layout = new QHBoxLayout( status_group );
  status_layout->setContentsMargins( 0, 0, 0, 0 );
  status_layout->setSpacing( 5 );
  status_layout->addWidget( mp_headerStatusDot, 0, Qt::AlignVCenter );
  status_layout->addWidget( mp_headerStatus, 0, Qt::AlignVCenter );
  mp_headerName->setAlignment( Qt::AlignVCenter );
  header_layout->addWidget( mp_headerName, 1 );
  header_layout->addWidget( status_group, 0, Qt::AlignVCenter );

  mp_chatPlaceholder = new QLabel( tr( "Select a chat to start messaging" ), this );
  mp_chatPlaceholder->setObjectName( "GuiWeChatPlaceholder" );
  mp_chatPlaceholder->setAlignment( Qt::AlignCenter );

  mp_chatStack = new QStackedWidget( this );
  mp_chatStack->addWidget( mp_chatPlaceholder );

  QWidget* chat_area = new QWidget( this );
  chat_area->setObjectName( "GuiWeChatChatArea" );
  QVBoxLayout* chat_area_layout = new QVBoxLayout( chat_area );
  chat_area_layout->setContentsMargins( 0, 0, 0, 0 );
  chat_area_layout->setSpacing( 0 );
  chat_area_layout->addWidget( mp_chatStack, 1 );

  QWidget* right_panel = new QWidget( this );
  QVBoxLayout* right_layout = new QVBoxLayout( right_panel );
  right_layout->setContentsMargins( 0, 0, 0, 0 );
  right_layout->setSpacing( 0 );
  right_layout->addWidget( header_panel );
  // YongBEEP: header/input divider lines removed on user feedback — the
  // white-header vs gray-chat vs white-input background contrast separates
  // the zones cleanly (WeChat does the same without rules).
  right_layout->addWidget( chat_area, 1 );

  // ---------- Splitter ----------
  QSplitter* splitter = new QSplitter( Qt::Horizontal, this );
  splitter->setObjectName( "GuiWeChatSplitter" );
  splitter->setChildrenCollapsible( false );
  splitter->setHandleWidth( 1 );
  splitter->addWidget( left_panel );
  splitter->addWidget( right_panel );
  splitter->setStretchFactor( 0, 0 );
  splitter->setStretchFactor( 1, 1 );
  int left_width = QSettings( QLatin1String( "YongBEEP" ), QLatin1String( "main" ) )
                      .value( QLatin1String( "leftPanelWidth" ), 280 ).toInt();
  left_width = qBound( 240, left_width, 380 );
  splitter->setSizes( QList<int>() << left_width << qMax( 640, width() - left_width ) );
  setCentralWidget( splitter );

  // ---------- Tray icon: show/hide + quit only (PRD 3.4 cut list) ----------
  mp_trayIcon = new GuiSystemTray( this );
  mp_menuTrayIcon = new QMenu( this );
  mp_menuTrayIcon->addAction( IconManager::instance().icon( "chat.png" ), tr( "Show/Hide" ), this, SLOT( toggleVisibleFromTray() ) );
  mp_menuTrayIcon->addSeparator();
  mp_menuTrayIcon->addAction( IconManager::instance().icon( "quit.png" ), tr( "Quit" ), this, SLOT( quitFromTray() ) );
  mp_trayIcon->setContextMenu( mp_menuTrayIcon );

  connect( mp_searchBox, SIGNAL( textChanged( const QString& ) ), this, SLOT( filterTextChanged( const QString& ) ) );
  connect( mp_chatListView, SIGNAL( clicked( const QModelIndex& ) ), this, SLOT( chatActivated( const QModelIndex& ) ) );
  connect( mp_chatListView, SIGNAL( activated( const QModelIndex& ) ), this, SLOT( chatActivated( const QModelIndex& ) ) );
  connect( mp_scanDevicesButton, SIGNAL( clicked() ), this, SLOT( openScanDevicesDialog() ) );
  connect( mp_trayIcon, SIGNAL( activated( QSystemTrayIcon::ActivationReason ) ), this, SLOT( trayIconActivated( QSystemTrayIcon::ActivationReason ) ) );
  connect( qApp, SIGNAL( focusChanged( QWidget*, QWidget* ) ), this, SLOT( onApplicationFocusChanged( QWidget*, QWidget* ) ) );

  statusBar()->hide();
  setMinimumSize( 680, 480 );
}


void GuiMainWindow::setupCoreConnections()
{
  connect( beeCore, SIGNAL( connected() ), this, SLOT( onCoreConnected() ) );
  connect( beeCore, SIGNAL( disconnected() ), this, SLOT( onCoreDisconnected() ) );
  connect( beeCore, SIGNAL( newChatMessage( const Chat&, const ChatMessage& ) ), this, SLOT( onNewChatMessage( const Chat&, const ChatMessage& ) ) );
  connect( beeCore, SIGNAL( chatChanged( const Chat& ) ), this, SLOT( onChatChanged( const Chat& ) ) );
  connect( beeCore, SIGNAL( chatRemoved( const Chat& ) ), this, SLOT( onChatRemoved( const Chat& ) ) );
  connect( beeCore, SIGNAL( userChanged( const User& ) ), this, SLOT( onUserChanged( const User& ) ) );
  connect( beeCore, SIGNAL( userRemoved( const User& ) ), this, SLOT( onUserChanged( const User& ) ) );
  connect( beeCore, SIGNAL( userConnectionStatusChanged( const User& ) ), this, SLOT( onUserConnectionStatusChanged( const User& ) ) );
  connect( beeCore, SIGNAL( savedChatListAvailable() ), this, SLOT( onSavedChatListAvailable() ) );
  connect( beeCore, SIGNAL( fileDownloadRequest( const User&, const FileInfo& ) ), this, SLOT( onFileDownloadRequest( const User&, const FileInfo& ) ) );
  connect( beeCore, SIGNAL( folderDownloadRequest( const User&, const QString&, const QList<FileInfo>& ) ), this, SLOT( onFolderDownloadRequest( const User&, const QString&, const QList<FileInfo>& ) ) );
  connect( beeCore, SIGNAL( newSystemStatusMessage( const QString&, int ) ), this, SLOT( showMessage( const QString&, int ) ) );

  // Bluetooth manager (read-only use of its existing signals, S3-4b)
  if( beeCore->btManager() )
    connect( beeCore->btManager(), SIGNAL( bluetoothPermissionDenied() ), this, SLOT( onBtPermissionDenied() ) );
}


void GuiMainWindow::setupChatConnections( GuiChat* gui_chat )
{
  connect( gui_chat, SIGNAL( newMessage( VNumber, const QString&, bool, bool ) ), this, SLOT( sendMessage( VNumber, const QString&, bool, bool ) ) );
  connect( gui_chat, SIGNAL( writing( VNumber ) ), beeCore, SLOT( sendWritingMessage( VNumber ) ) );
  connect( gui_chat, SIGNAL( openUrl( const QUrl&, VNumber ) ), this, SLOT( openUrlFromChat( const QUrl&, VNumber ) ) );
  connect( gui_chat, SIGNAL( sendFileFromChatRequest( VNumber, const QString& ) ), this, SLOT( sendFileFromChat( VNumber, const QString& ) ) );
  connect( gui_chat, SIGNAL( sendFilesFromChatRequest( VNumber, const QStringList& ) ), this, SLOT( sendFilesFromChat( VNumber, const QStringList& ) ) );
  connect( gui_chat, SIGNAL( chatToClear( VNumber ) ), this, SLOT( clearChat( VNumber ) ) );
  connect( gui_chat, SIGNAL( showStatusMessageRequest( const QString&, int ) ), this, SLOT( showMessage( const QString&, int ) ) );
}


GuiChat* GuiMainWindow::chatWidget( VNumber chat_id, bool create_if_missing ){
  GuiChat* gui_chat = m_chatWidgets.value( chat_id, Q_NULLPTR );
  if( gui_chat || !create_if_missing )
    return gui_chat;

  gui_chat = new GuiChat( this );
  setupChatConnections( gui_chat );
  gui_chat->setCompactInputMode(); // hide floating-window leftovers (send&close checkbox, record button)
  // GuiChat::updateActions() dereferences the toolbar actions built by setupToolBar():
  // fill them into a hidden bar so the widget can be embedded without the old toolbar.
  QToolBar* hidden_chat_bar = new QToolBar( gui_chat );
  hidden_chat_bar->hide();
  gui_chat->setupToolBar( hidden_chat_bar );
  mp_chatStack->addWidget( gui_chat );
  m_chatWidgets.insert( chat_id, gui_chat );
  return gui_chat;
}

void GuiMainWindow::demoChatInputDraft( VNumber chat_id, const QString& draft, VNumber unread_chat_id, int unread_count )
{
  // Deterministic screenshot setup (S4c --demo-chat): select the given chat,
  // seed the input draft, then add an unread badge to another chat.
  showChat( chat_id );

  GuiChat* gui_chat = chatWidget( chat_id, true );
  if( gui_chat )
    gui_chat->setDemoInputDraft( draft );

  if( unread_chat_id != ID_INVALID && unread_count > 0 )
  {
    Chat c = ChatManager::instance().chat( unread_chat_id );
    if( c.isValid() )
    {
      for( int i = 0; i < unread_count; i++ )
        c.addUnreadMessage();
      ChatManager::instance().setChat( c );
    }
  }

  refreshChatList( true );
}

void GuiMainWindow::demoChatMarkdownTexts( VNumber chat_id, const QStringList& raw_texts )
{
  // YongBEEP S4e (--demo-chat): create the chat widget up front and
  // register the raw markdown texts so the matching local-user demo
  // messages render with setMarkdown when the chat is shown later.
  GuiChat* gui_chat = chatWidget( chat_id, true );
  if( gui_chat )
    gui_chat->registerDemoMarkdownTexts( raw_texts );
}


void GuiMainWindow::showChat( VNumber chat_id )
{
  Chat c = ChatManager::instance().chat( chat_id );
  if( !c.isValid() )
  {
    qWarning() << "Invalid chat" << chat_id << "found in GuiMainWindow::showChat(...)";
    return;
  }

  GuiChat* gui_chat = chatWidget( chat_id, true );
  if( !gui_chat )
    return;

  m_currentChatId = chat_id;
  gui_chat->setChat( c );
  gui_chat->updateActions( c, beeCore->isConnected(), beeCore->connectedUsers(), beeCore->isFileTransferActive() );
  mp_chatStack->setCurrentWidget( gui_chat );
  updateChatHeader();

  if( isActiveWindow() )
  {
    beeCore->readAllMessagesInChat( chat_id );
    gui_chat->ensureFocusInChat();
  }
}


void GuiMainWindow::chatActivated( const QModelIndex& index )
{
  if( !index.isValid() )
    return;
  VNumber chat_id = Bee::qVariantToVNumber( index.data( ChatListModel::ChatIdRole ) );
  if( chat_id != ID_INVALID && chat_id != m_currentChatId )
    showChat( chat_id );
}


void GuiMainWindow::filterTextChanged( const QString& txt )
{
  mp_chatListModel->setFilter( txt );
  if( mp_chatListModel->rowCount() > 0 && mp_chatListModel->rowOfChat( m_currentChatId ) < 0 )
    selectFirstChat();
}


void GuiMainWindow::refreshChatList( bool restore_selection )
{
  VNumber chat_id_to_restore = restore_selection ? m_currentChatId : ID_INVALID;
  mp_chatListModel->reload();

  int row = mp_chatListModel->rowOfChat( chat_id_to_restore );
  if( row >= 0 )
  {
    QModelIndex index = mp_chatListModel->index( row, 0 );
    mp_chatListView->setCurrentIndex( index );
  }

  updateDockBadge();
}


void GuiMainWindow::selectFirstChat()
{
  if( mp_chatListModel->rowCount() == 0 )
  {
    mp_chatStack->setCurrentWidget( mp_chatPlaceholder );
    m_currentChatId = ID_INVALID;
    updateChatHeader();
    return;
  }

  QModelIndex index = mp_chatListModel->index( 0, 0 );
  mp_chatListView->setCurrentIndex( index );
  showChat( mp_chatListModel->chatIdAt( 0 ) );
}


void GuiMainWindow::updateChatHeader()
{
  Chat c = ChatManager::instance().chat( m_currentChatId );
  if( !c.isValid() )
  {
    mp_headerName->setText( Settings::instance().programName() );
    mp_headerStatus->setText( QString() );
    return;
  }

  QString chat_name = c.isDefault() ? tr( "All users" ) : c.name();
  if( c.isPrivate() )
  {
    User peer = UserManager::instance().findUser( c.privateUserId() );
    if( peer.isValid() )
      chat_name = Bee::userNameToShow( peer, false );
  }
  mp_headerName->setText( chat_name );

  // Connection indicator driven by the Core user signals (S3-4b):
  // green when the peer is connected, yellow while connecting, red offline.
  bool peer_online = false;
  if( c.isDefault() )
    peer_online = beeCore->isConnected() && beeCore->connectedUsers() > 0;
  else
  {
    foreach( User u, UserManager::instance().userList().fromUsersId( c.usersId() ).toList() )
    {
      if( !u.isLocal() && u.isValid() && u.isStatusConnected() )
        peer_online = true;
    }
  }

  // Connection indicator (S4c WeChat spec): 8px status dot + plain 12px text,
  // green online / orange connecting / gray offline - no emoji glyphs.
  QColor status_color = YongTheme::textSecondary();
  QString status_text = tr( "Disconnected" );
  if( peer_online )
  {
    status_color = YongTheme::brandGreen();
    status_text = tr( "Connected" );
  }
  else if( m_coreIsConnecting || (beeCore->btManager() && beeCore->btManager()->isActive()) )
  {
    status_color = YongTheme::connectingOrange();
    status_text = tr( "Connecting" );
  }

  mp_headerStatusDot->setStyleSheet( QString( "border-radius: 4px; background: %1;" ).arg( status_color.name() ) );
  mp_headerStatus->setText( status_text );
  mp_headerStatus->setStyleSheet( QString( "color: %1; background: transparent;" ).arg( status_color.name() ) );
}


void GuiMainWindow::updateWindowTitle()
{
  // S4c WeChat spec: keep the title bar native and clean - the connection
  // state lives in the chat header status dot, no emoji glyph in the title.
  setWindowTitle( Settings::instance().programName() );
}


void GuiMainWindow::updateDockBadge()
{
  // Sum of every chat unread counter (docs/analysis/gui-redesign.md section H.2);
  // macOS shows it on the dock icon, other platforms no-op.
  DockBadge::setUnreadCount( ChatManager::instance().unreadMessages() );
}


void GuiMainWindow::openScanDevicesDialog()
{
  if( !mp_scanDevices )
  {
    mp_scanDevices = new GuiScanDevices( this );
    connect( mp_scanDevices, SIGNAL( finished() ), this, SLOT( updateWindowTitle() ) );
  }

  if( !mp_scanDevices->isVisible() )
  {
    mp_scanDevices->adjustSize();
    mp_scanDevices->move( geometry().center() - QPoint( mp_scanDevices->width() / 2, mp_scanDevices->height() / 2 ) );
    mp_scanDevices->show();
  }
  else
  {
    mp_scanDevices->raise();
    mp_scanDevices->activateWindow();
  }
}


void GuiMainWindow::onBtPermissionDenied()
{
  QMessageBox::warning( this, Settings::instance().programName(),
                        tr( "Bluetooth permission was denied. Enable it for %1 in the system privacy settings to chat over bluetooth." )
                          .arg( Settings::instance().programName() ) );
  updateWindowTitle();
  updateChatHeader();
}


void GuiMainWindow::showMessage( const QString& status_msg, int time_out )
{
  Q_UNUSED( status_msg );
  Q_UNUSED( time_out );
  // Status bar is hidden in the WeChat-style window; system status messages
  // are visible in the default chat / chat header (kept as slot for GuiChat).
}


void GuiMainWindow::onNewChatMessage( const Chat& c, const ChatMessage& cm )
{
  if( !c.isValid() )
    return;

  GuiChat* gui_chat = m_chatWidgets.value( c.id(), Q_NULLPTR );
  if( gui_chat && gui_chat->chatId() == c.id() )
    gui_chat->appendChatMessage( c, cm );

  if( c.id() == m_currentChatId && isActiveWindow() )
  {
    beeCore->readAllMessagesInChat( c.id() );
    refreshChatList( true );
  }
  else
  {
    if( !cm.isFromLocalUser() )
      showAlertForMessage( c, cm );
    refreshChatList( true );
  }
}


void GuiMainWindow::showAlertForMessage( const Chat& c, const ChatMessage& cm )
{
  Q_UNUSED( cm );

  if( Settings::instance().beepOnNewMessageArrived() &&
      !(Settings::instance().localUser().status() == User::Busy && Settings::instance().disableBeepInUserStatusBusy()) )
  {
    if( AudioManager::instance().isAudioDeviceAvailable() )
      AudioManager::instance().playBeep();
  }

  User u = UserManager::instance().findUser( cm.userId() );
  QString msg = u.isValid() ? tr( "New message from %1" ).arg( u.name() ) : tr( "New message arrived" );
  mp_trayIcon->showNewMessageArrived( c.id(), msg, false );
}


void GuiMainWindow::onChatChanged( const Chat& c )
{
  if( !c.isValid() )
    return;

  GuiChat* gui_chat = m_chatWidgets.value( c.id(), Q_NULLPTR );
  if( gui_chat && gui_chat->chatId() == c.id() )
    gui_chat->updateActions( c, beeCore->isConnected(), beeCore->connectedUsers(), beeCore->isFileTransferActive() );

  refreshChatList( true );
  if( c.id() == m_currentChatId )
    updateChatHeader();
}


void GuiMainWindow::onChatRemoved( const Chat& c )
{
  GuiChat* gui_chat = m_chatWidgets.value( c.id(), Q_NULLPTR );
  if( gui_chat )
  {
    mp_chatStack->removeWidget( gui_chat );
    gui_chat->deleteLater();
    m_chatWidgets.remove( c.id() );
  }
  if( mp_trayIcon->chatId() == c.id() )
    mp_trayIcon->setUnreadMessages( c.id(), 0 );

  refreshChatList( false );
  if( m_currentChatId == c.id() )
    selectFirstChat();
}


void GuiMainWindow::onSavedChatListAvailable()
{
  refreshChatList( true );
}


void GuiMainWindow::onUserChanged( const User& u )
{
  refreshChatList( true );
  if( u.isLocal() )
    updateWindowTitle();
  updateChatHeader();
}


void GuiMainWindow::onUserConnectionStatusChanged( const User& u )
{
  refreshChatList( true );
  updateChatHeader();
  // Any user going online/offline changes the connection status dot (S3-4b)
  Q_UNUSED( u );
  updateWindowTitle();
}


void GuiMainWindow::onCoreConnected()
{
  m_coreIsConnecting = false;
  updateWindowTitle();
  updateChatHeader();
  refreshChatList( true );
}


void GuiMainWindow::onCoreDisconnected()
{
  m_coreIsConnecting = false;
  updateWindowTitle();
  updateChatHeader();
  refreshChatList( true );
}


void GuiMainWindow::onApplicationFocusChanged( QWidget*, QWidget* now )
{
  if( now && isAncestorOf( now ) && m_currentChatId != ID_INVALID )
    beeCore->readAllMessagesInChat( m_currentChatId );
}


void GuiMainWindow::sendMessage( VNumber chat_id, const QString& msg, bool is_source_code, bool is_markdown )
{
  beeCore->sendChatMessage( chat_id, msg, false, false, is_source_code, is_markdown );
  refreshChatList( true );
}


void GuiMainWindow::sendFileFromChat( VNumber chat_id, const QString& file_path )
{
  Chat c = ChatManager::instance().chat( chat_id );
  if( !c.isValid() )
    return;

  QStringList files_path_selected;
  if( file_path.isEmpty() || !QFile::exists( file_path ) )
  {
    files_path_selected = FileDialog::getOpenFileNames( true, this, tr( "%1 - Select a file" ).arg( Settings::instance().programName() ) + QString( " %1" ).arg( tr( "or more" ) ),
                                                       Settings::instance().lastDirectorySelected() );
    if( files_path_selected.isEmpty() )
      return;
    Settings::instance().setLastDirectorySelectedFromFile( files_path_selected.last() );
  }
  else
    files_path_selected.append( file_path );

  beeCore->sendFilesFromChat( c.id(), files_path_selected );
}


void GuiMainWindow::sendFilesFromChat( VNumber chat_id, const QStringList& file_path_list )
{
  Chat c = ChatManager::instance().chat( chat_id );
  if( !c.isValid() || file_path_list.isEmpty() )
    return;
  beeCore->sendFilesFromChat( c.id(), file_path_list );
}


void GuiMainWindow::clearChat( VNumber chat_id )
{
  Chat c = ChatManager::instance().chat( chat_id );
  if( !c.isValid() )
    return;

  QString chat_name = c.isDefault() ? tr( "All users" ).toLower() : c.name();
  if( QMessageBox::question( this, Settings::instance().programName(),
                             tr( "Do you want to clear messages with %1?" ).arg( chat_name ),
                             tr( "Yes" ), tr( "No" ), QString(), 1, 1 ) != 0 )
    return;

  if( beeCore->clearMessagesInChat( chat_id, false ) )
  {
    Chat c_updated = ChatManager::instance().chat( chat_id );
    GuiChat* gui_chat = m_chatWidgets.value( chat_id, Q_NULLPTR );
    if( c_updated.isValid() && gui_chat )
      gui_chat->setChat( c_updated );
    refreshChatList( true );
  }
}


void GuiMainWindow::openUrlFromChat( const QUrl& url, VNumber chat_id )
{
  if( url.scheme() == QLatin1String( "beeshowsavedchat" ) )
  {
    QStringList sl_chat_id = url.toString().split( QStringLiteral( "#" ) );
    if( !sl_chat_id.isEmpty() )
    {
      bool chat_id_is_integer = false;
      VNumber saved_chat_id = sl_chat_id.last().toULongLong( &chat_id_is_integer );
      if( chat_id_is_integer && saved_chat_id > 0 )
      {
        QString chat_name = ChatManager::instance().chatName( saved_chat_id );
        if( ChatManager::instance().chatHasSavedText( chat_name ) )
        {
          GuiSavedChat* saved_chat = new GuiSavedChat( this );
          saved_chat->setAttribute( Qt::WA_DeleteOnClose, true );
          saved_chat->resize( 680, 420 );
          saved_chat->showSavedChat( chat_name );
          saved_chat->show();
          return;
        }
      }
    }
    QMessageBox::information( this, Settings::instance().programName(),
                              tr( "There are no messages saved in the chat." ), tr( "Ok" ) );
    return;
  }

  if( url.scheme() == FileInfo::urlSchemeShowFileInFolder() )
  {
    QUrl adj_file_url = url;
    adj_file_url.setScheme( QLatin1String( "file" ) );
    if( !Bee::showFileInGraphicalShell( Bee::convertToNativeFolderSeparator( adj_file_url.toLocalFile() ) ) )
    {
      QFileInfo file_info_url( adj_file_url.toLocalFile() );
      adj_file_url = QUrl::fromLocalFile( Bee::convertToNativeFolderSeparator( file_info_url.absoluteDir().absolutePath() ) );
      openUrlFromChat( adj_file_url, chat_id );
    }
    return;
  }

  if( Bee::isLocalFile( url ) )
  {
    QString file_path = Bee::convertToNativeFolderSeparator( url.toLocalFile() );
    QFileInfo fi( file_path );
    if( !fi.exists() )
    {
      QMessageBox::information( this, Settings::instance().programName(),
                                tr( "%1: no such file or directory." ).arg( file_path.isEmpty() ? url.toString() : file_path ), tr( "Ok" ) );
      return;
    }
    QDesktopServices::openUrl( QUrl::fromLocalFile( file_path ) );
    return;
  }

  QDesktopServices::openUrl( url );
}


void GuiMainWindow::onFileDownloadRequest( const User& u, const FileInfo& fi )
{
  if( !Settings::instance().enableFileTransfer() )
  {
    beeCore->refuseToDownloadFile( u.id(), fi );
    return;
  }

  // YongBEEP default: auto-receive like WeChat (PRD F5); ask only if enabled in settings
  int msg_result = 1;
  if( Settings::instance().confirmOnDownloadFile() )
  {
    msg_result = QMessageBox::question( this, Settings::instance().programName(),
                                        tr( "Do you want to download %1 from %2?" ).arg( fi.name(), u.name() ),
                                        tr( "No" ), tr( "Yes" ), tr( "Yes, and don't ask anymore" ), 0, 0 );
    if( msg_result == 2 )
      Settings::instance().setConfirmOnDownloadFile( false );
  }

  if( msg_result == 0 )
  {
    beeCore->refuseToDownloadFile( u.id(), fi );
    return;
  }

  QFileInfo qfile_info( Bee::convertToNativeFolderSeparator( Settings::instance().downloadDirectoryForUser( u ) ), fi.name() );
  if( qfile_info.exists() && Settings::instance().onExistingFileAction() == Settings::GenerateNewFileName )
    qfile_info = QFileInfo( Bee::uniqueFilePath( qfile_info.absoluteFilePath(), true ) );

  FileInfo file_info = fi;
  file_info.setPath( qfile_info.absoluteFilePath() );
  beeCore->downloadFile( u.id(), file_info, false );
}


void GuiMainWindow::onFolderDownloadRequest( const User& u, const QString& folder_name, const QList<FileInfo>& file_info_list )
{
  if( !Settings::instance().enableFileTransfer() || file_info_list.isEmpty() )
    return;

  foreach( FileInfo fi, file_info_list )
  {
    QString download_folder = Bee::convertToNativeFolderSeparator(
          QString( "%1/%2" ).arg( Settings::instance().downloadDirectoryForUser( u ), fi.shareFolder() ) );
    QFileInfo qfile_info( download_folder, fi.name() );
    if( qfile_info.exists() && Settings::instance().onExistingFileAction() == Settings::GenerateNewFileName )
      qfile_info = QFileInfo( Bee::uniqueFilePath( qfile_info.absoluteFilePath(), true ) );
    FileInfo file_info = fi;
    file_info.setPath( qfile_info.absoluteFilePath() );
    beeCore->downloadFile( u.id(), file_info, false );
  }
  qDebug() << "Auto-downloaded folder" << folder_name << "from" << u.path();
}


void GuiMainWindow::loadSession()
{
  refreshChatList( false );
  selectFirstChat();
  QTimer::singleShot( 100, beeCore, SLOT( buildSavedChatList() ) );
  QTimer::singleShot( Settings::instance().delayConnectionAtStartup(), this, SLOT( startCore() ) );
}


void GuiMainWindow::startCore()
{
  if( beeCore->isConnected() || m_coreIsConnecting )
    return;

  m_coreIsConnecting = true;

  if( Settings::instance().askChangeUserAtStartup() )
  {
    if( !showWizard() )
    {
      m_coreIsConnecting = false;
      return;
    }
  }

  if( Settings::instance().firstTime() )
  {
    Settings::instance().setFirstTime( false );
    Settings::instance().setAskChangeUserAtStartup( false );
  }

  if( Settings::instance().askPassword() )
  {
    GuiAskPassword gap( this );
    gap.setModal( true );
    gap.loadData();
    gap.show();
    gap.setFixedSize( gap.size() );
    if( gap.exec() == QDialog::Rejected )
    {
      m_coreIsConnecting = false;
      return;
    }
  }

  if( beeCore->start() )
    updateWindowTitle();
  else
  {
    m_coreIsConnecting = false;
    QMetaObject::invokeMethod( beeCore, "checkNetworkInterface", Qt::QueuedConnection );
  }
}


bool GuiMainWindow::showWizard()
{
  GuiWizard gw( this );
  gw.setModal( true );
  gw.loadSettings();
  gw.show();
  gw.setFixedSize( gw.size() );
  return gw.exec() == QDialog::Accepted;
}


void GuiMainWindow::onTickEvent( int ticks )
{
  mp_trayIcon->onTickEvent( ticks );

  if( beeApp->isInSleepMode() )
    return;

  if( !m_coreIsConnecting )
    beeCore->onTickEvent( ticks );
}


void GuiMainWindow::onSleepRequest()
{
  if( beeCore->isConnected() )
    beeCore->stop();
}


void GuiMainWindow::onWakeUpRequest()
{
  refreshChatList( true );
}


void GuiMainWindow::showUp()
{
  Bee::showUp( this );
}


void GuiMainWindow::raiseOnTop()
{
  Bee::raiseOnTop( this );
}


void GuiMainWindow::toggleVisibleFromTray()
{
  if( isVisible() && !isMinimized() )
  {
    Chat c = ChatManager::instance().firstChatWithUnreadMessages();
    if( c.isValid() )
      mp_trayIcon->setUnreadMessages( c.id(), c.unreadMessages() );
    else
      mp_trayIcon->setUnreadMessages( ID_INVALID, 0 );
    hide();
  }
  else
  {
    Bee::raiseOnTop( this );
  }
}


void GuiMainWindow::quitFromTray()
{
  forceShutdown();
}


void GuiMainWindow::trayIconActivated( QSystemTrayIcon::ActivationReason )
{
  // macOS tray clicks always open the context menu (Show/Hide, Quit);
  // other platforms toggle the window on left click.
#ifndef Q_OS_MACOS
  toggleVisibleFromTray();
#endif
}


void GuiMainWindow::hideToTrayIcon()
{
  toggleVisibleFromTray();
}


void GuiMainWindow::forceShutdown()
{
  qDebug() << "Shutdown...";
  m_forceShutdown = true;
  if( beeCore->isConnected() )
    beeCore->stop();
  close();
}


void GuiMainWindow::saveSession( QSessionManager& )
{
  qDebug() << "Session manager ask to save and close session";
  forceShutdown();
}


void GuiMainWindow::checkWindowFlagsAndShow()
{
  if( !mp_trayIcon->isVisible() )
    mp_trayIcon->show();

  if( Settings::instance().guiGeometry().isEmpty() )
  {
    resize( 920, 620 );
    QScreen* scr = beeApp->primaryScreen();
    if( scr )
      move( scr->availableGeometry().center() - QPoint( width() / 2, height() / 2 ) );
  }
  else
  {
    restoreGeometry( Settings::instance().guiGeometry() );
  }

  show();
}


void GuiMainWindow::takeScreenshot( const QString& file_path )
{
  const QPixmap screenshot = grab();
  const bool saved = screenshot.save( file_path );
  qDebug() << "Screenshot" << ( saved ? "saved to" : "FAILED for" ) << file_path << "size" << screenshot.size() << "window title:" << windowTitle();

  // If the scan-devices dialog is open (debug --demo-scan), grab it too as
  // <path>-scan.<ext> so the dialog can be verified headlessly.
  if( mp_scanDevices && mp_scanDevices->isVisible() )
  {
    QString scan_path = file_path;
    int dot_pos = scan_path.lastIndexOf( QLatin1Char( '.' ) );
    if( dot_pos > 0 )
      scan_path.insert( dot_pos, QStringLiteral( "-scan" ) );
    else
      scan_path += QStringLiteral( "-scan.png" );
    const QPixmap scan_screenshot = mp_scanDevices->grab();
    const bool scan_saved = scan_screenshot.save( scan_path );
    qDebug() << "Scan dialog screenshot" << ( scan_saved ? "saved to" : "FAILED for" ) << scan_path << "size" << scan_screenshot.size();
  }

  forceShutdown();
}


void GuiMainWindow::loadStyle()
{
  QStyle* p_style = QStyleFactory::create( QStringLiteral( "Fusion" ) );
  if( p_style )
    beeApp->setStyle( p_style );

  if( Settings::instance().useDarkStyle() )
  {
    beeApp->setPalette( Bee::darkPalette() );
    setStyleSheet( QString() ); // dark style keeps the pre-S4c look
  }
  else
  {
    // S4c: force the light WeChat chrome (macOS dark mode would otherwise
    // leak the dark system palette into the Fusion style).
    beeApp->setPalette( YongTheme::lightPalette() );
    setStyleSheet( YongTheme::mainWindowQss() );
  }
}


void GuiMainWindow::closeEvent( QCloseEvent* e )
{
  if( !m_forceShutdown )
  {
    if( Settings::instance().closeMinimizeInTray() && QSystemTrayIcon::isSystemTrayAvailable() )
    {
      QTimer::singleShot( 0, this, SLOT( hideToTrayIcon() ) );
      e->ignore();
      return;
    }
  }

  if( beeCore->isConnected() )
    beeCore->stop();

  if( Settings::instance().saveGeometryOnExit() && isVisible() )
  {
    Settings::instance().setGuiGeometry( saveGeometry() );
    Settings::instance().save();
  }

  // YongBEEP: persist the session-list splitter width so a restart keeps the
  // user's preferred layout (clamped to the WeChat-like 240-380 range).
  if( QSplitter* main_splitter = findChild<QSplitter*>( QLatin1String( "GuiWeChatSplitter" ) ) )
  {
    QList<int> sizes = main_splitter->sizes();
    if( !sizes.isEmpty() && sizes.first() > 0 )
      QSettings( QLatin1String( "YongBEEP" ), QLatin1String( "main" ) ).setValue( QLatin1String( "leftPanelWidth" ), sizes.first() );
  }

  mp_trayIcon->hide();
  qApp->setQuitOnLastWindowClosed( true );
  e->accept();
}
