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
// $Id: GuiChat.cpp 1592 2026-08-11 10:14:22Z mastroddi $
//
//////////////////////////////////////////////////////////////////////

#include "BeeUtils.h"
#include "ChatManager.h"
#include "ChatMessage.h"
#include "ChatMessageDelegate.h"
#include "ChatMessageModel.h"
#include "Core.h"
#include "FileDialog.h"
#include "GuiChat.h"
#include "GuiChatMessage.h"
#include "GuiCreateTextFile.h"
#include "GuiEmoticons.h"
#ifdef BEEBEEP_USE_VOICE_CHAT
#include "GuiVoicePlayer.h"
#endif
#include "IconManager.h"
#include "Settings.h"
#include "ShortcutManager.h"
#include "YongTheme.h"
#ifdef BEEBEEP_USE_HUNSPELL
  #include "SpellChecker.h"
#endif
#include "UserManager.h"
#include <QListView>
#include <QScrollBar>


GuiChat::GuiChat( QWidget *parent )
 : QWidget( parent )
{
  setupUi( this );
  setObjectName( "GuiChat" );
  setAcceptDrops( true );
  // YongBEEP: balanced message grid margins — icons stay near the field but
  // the text keeps breathing room on top (2px left it pinned to the top)
  if( QLayout* msg_layout = mp_frameMessage->layout() )
    msg_layout->setContentsMargins( 12, 6, 12, 8 );

  mp_inputToolbar = Q_NULLPTR;
  mp_tbEmoticons = Q_NULLPTR;
  mp_tbAttach = Q_NULLPTR;
  mp_tbMarkdown = Q_NULLPTR;
  mp_emoticonsPopup = Q_NULLPTR;
  mp_emoticonsWidget = Q_NULLPTR;

  // YongBEEP S4e (chatview-v2): QListView + delegate replaces the
  // QTextBrowser+HTML rendering (true rounded bubbles, shrink-to-fit
  // width, markdown)
  setupMessageList();

#ifdef BEEBEEP_USE_VOICE_CHAT
  mp_guiVoicePlayer = new GuiVoicePlayer( mp_frameVoicePlayer );
  QGridLayout* voice_grid_layout = new QGridLayout( mp_frameVoicePlayer );
  voice_grid_layout->setSpacing( 0 );
  voice_grid_layout->setObjectName( QString::fromUtf8( "voice_grid_layout" ) );
  voice_grid_layout->setContentsMargins( 4, 4, 4, 4 );
  voice_grid_layout->addWidget( mp_guiVoicePlayer );
#endif
  mp_frameVoicePlayer->hide();

  QGridLayout* grid_layout = new QGridLayout( this );
  grid_layout->setSpacing( 0 );
  grid_layout->setObjectName( QString::fromUtf8( "grid_layout" ) );
  grid_layout->setContentsMargins( 0, 0, 0, 0 );

  mp_splitter = new QSplitter( this );
  mp_splitter->setOrientation( Qt::Vertical );
  mp_splitter->setChildrenCollapsible( false );
  mp_splitter->setHandleWidth( 1 );
  mp_splitter->addWidget( mp_frameChat );
  setupInputToolbar();
  QWidget* input_panel = new QWidget( this );
  input_panel->setObjectName( "GuiChatInputPanel" );
  // Required for the QSS background + border-top divider to actually paint
  input_panel->setAttribute( Qt::WA_StyledBackground, true );
  QVBoxLayout* input_panel_layout = new QVBoxLayout( input_panel );
  input_panel_layout->setContentsMargins( 0, 0, 0, 0 );
  input_panel_layout->setSpacing( 0 );
  // YongBEEP audit: full-width line above the input area, matching the
  // header underline and the full-height vertical divider
  QFrame* input_line = new QFrame( input_panel );
  input_line->setObjectName( "GuiChatInputLine" );
  input_line->setFrameShape( QFrame::NoFrame );
  input_line->setFixedHeight( 1 );
  input_line->setAttribute( Qt::WA_StyledBackground, true );
  input_panel_layout->addWidget( input_line );
  input_panel_layout->addWidget( mp_inputToolbar );
  input_panel_layout->addWidget( mp_frameMessage );
  mp_splitter->addWidget( input_panel );

  grid_layout->addWidget( mp_splitter, 1, 0, 1, 1 );
  QList<int> widget_sizes;
  widget_sizes.append( 300 );
  widget_sizes.append( 130 );
  mp_splitter->setSizes( widget_sizes );

  mp_teMessage->setFocusPolicy( Qt::StrongFocus );
  mp_teMessage->setAcceptRichText( false );
#ifdef BEEBEEP_USE_HUNSPELL
  mp_teMessage->setCompleter( SpellChecker::instance().completer() );
#endif
  mp_teMessage->setObjectName( "GuiMessageEdit" );

#ifdef BEEBEEP_USE_VOICE_CHAT
  connect( mp_pbRecordVoiceMessage, SIGNAL( clicked() ), this, SIGNAL( showVoiceMessageDialogRequest() ) );
#else
  mp_pbRecordVoiceMessage->setDisabled( true );
  mp_pbRecordVoiceMessage->setToolTip( tr( "Voice messages are not available with this version of BeeBEEP.\nDownload for free and install the version with this feature from the %1 website." ).arg( Settings::instance().organizationDomain() ) );
#endif

  m_chatId = ID_DEFAULT_CHAT;
  m_lastMessageUserId = ID_SYSTEM_MESSAGE;
  m_lastMessageTimestamp = QDateTime();
  m_lastTextFound = "";
  m_lastFindRow = -1;

  mp_menuContext = new QMenu( this );
  mp_menuFilters = new QMenu( this );
  mp_actFilterMessages = Q_NULLPTR;

  mp_scFocusInChat = new QShortcut( this );
  mp_scFocusInChat->setContext( Qt::WindowShortcut );
  connect( mp_scFocusInChat, SIGNAL( activated() ), this, SLOT( ensureFocusInChat() ) );

  mp_scPrintChat = new QShortcut( this );
  mp_scPrintChat->setKey( QKeySequence::Print );
  mp_scPrintChat->setContext( Qt::WindowShortcut );
  connect( mp_scPrintChat, SIGNAL( activated() ), this, SLOT( printChat() ) );

  mp_scFindTextInChat = new QShortcut( this );
  mp_scFindTextInChat->setKey( QKeySequence::Find );
  mp_scFindTextInChat->setContext( Qt::WindowShortcut );
  connect( mp_scFindTextInChat, SIGNAL( activated() ), this, SLOT( showFindTextInChatDialog() ) );

  mp_scFindNextTextInChat = new QShortcut( this );
  mp_scFindNextTextInChat->setKey( QKeySequence::FindNext );
  mp_scFindNextTextInChat->setContext( Qt::WindowShortcut );
  connect( mp_scFindNextTextInChat, SIGNAL( activated() ), this, SLOT( findNextTextInChat() ) );

  mp_scFindPreviousTextInChat = new QShortcut( this );
  mp_scFindPreviousTextInChat->setKey( QKeySequence::FindPrevious );
  mp_scFindPreviousTextInChat->setContext( Qt::WindowShortcut );
  connect( mp_scFindPreviousTextInChat, SIGNAL( activated() ), this, SLOT( findPreviousTextInChat() ) );

  mp_scViewEmoticons = new QShortcut( this );
  mp_scViewEmoticons->setContext( Qt::WindowShortcut );
  connect( mp_scViewEmoticons, SIGNAL( activated() ), this, SIGNAL( toggleVisibilityEmoticonsPanelRequest() ) );

  mp_actSelectBackgroundColor = new QAction( IconManager::instance().icon( "background-color.png" ), tr( "Change background color" ), this );
  connect( mp_actSelectBackgroundColor, SIGNAL( triggered() ), this, SLOT( selectBackgroundColor() ) );

  mp_actSelectForegroundColor = new QAction( IconManager::instance().icon( "font-color.png" ), tr( "Select chat default text color" ), this );
  connect( mp_actSelectForegroundColor, SIGNAL( triggered() ), this, SLOT( selectForegroundColor() ) );

  mp_actSelectSystemColor = new QAction( IconManager::instance().icon( "log.png" ), tr( "Select chat system text color" ), this );
  connect( mp_actSelectSystemColor, SIGNAL( triggered() ), this, SLOT( selectSystemColor() ) );

  mp_actSaveAs = new QAction( IconManager::instance().icon( "save-as.png" ), tr( "Save chat" ), this );
  connect( mp_actSaveAs, SIGNAL( triggered() ), this, SLOT( saveChat() ) );

  mp_actPrint = new QAction( IconManager::instance().icon( "printer.png" ), tr( "Print..." ), this );
  mp_actPrint->setShortcut( QKeySequence::Print );
  connect( mp_actPrint, SIGNAL( triggered() ), this, SLOT( printChat() ) );

  mp_actClear = new QAction( IconManager::instance().icon( "clear.png" ), tr( "Clear messages" ), this );
  connect( mp_actClear, SIGNAL( triggered() ), this, SLOT( clearChat() ) );

  mp_actClearSystemMessages = new QAction( IconManager::instance().icon( "log.png" ), tr( "Clear system messages" ), this );
  connect( mp_actClearSystemMessages, SIGNAL( triggered() ), this, SLOT( clearSystemMessages() ) );

  mp_actFindTextInChat = new QAction( IconManager::instance().icon( "search.png" ), tr( "Find text in chat" ), this );
  connect( mp_actFindTextInChat, SIGNAL( triggered() ), this, SLOT( showFindTextInChatDialog() ) );

  mp_actRestoreDefaultFont = new QAction( IconManager::instance().icon( "font.png" ), tr( "Restore the default font" ), this );
  connect( mp_actRestoreDefaultFont, SIGNAL( triggered() ), this, SLOT( resetChatFontToDefault() ) );

  // S4c WeChat body text is 14px in the light theme (dark style untouched)
  QFont chat_font = Settings::instance().chatFont();
  if( !Settings::instance().useDarkStyle() )
    chat_font.setPixelSize( YongTheme::fontBodyPx() );
  setChatFont( chat_font );
  updateChatColors();
  updateOnSendingMessage();

  // S4c WeChat send button: small brand-green capsule with white "Send"
  // text (36x26 minimum), gray and disabled while the input is empty.
  m_sendAllowedByState = true;
  mp_pbSend->setObjectName( "GuiChatSendButton" );
  mp_pbSend->setText( tr( "Send" ) );
  mp_pbSend->setIcon( QIcon() );
  mp_pbSend->setFlat( true );
  mp_pbSend->setFixedHeight( 26 );
  mp_pbSend->setMinimumWidth( 36 );
  mp_pbSend->setCursor( Qt::PointingHandCursor );
  connect( mp_teMessage, SIGNAL( textChanged() ), this, SLOT( updateSendButton() ) );
  updateSendButton();

#ifdef BEEBEEP_USE_SHAREDESKTOP
  mp_actShareDesktop = new QAction( IconManager::instance().icon( "desktop-share.png" ), tr( "Share your desktop" ), this );
  mp_actShareDesktop->setCheckable( true );
  mp_actShareDesktop->setEnabled( Settings::instance().enableShareDesktop() );
  connect( mp_actShareDesktop, SIGNAL( triggered() ), this, SLOT( shareDesktopToChat() ) );
  mp_actScreenshot = new QAction( IconManager::instance().icon( "screenshot.png" ), tr( "Send a screenshot" ), this );
  connect( mp_actScreenshot, SIGNAL( triggered() ), this, SLOT( sendScreenshotToChat() ) );
#endif

  connect( mp_lvChat, SIGNAL( customContextMenuRequested( const QPoint& ) ), this, SLOT( customContextMenu( const QPoint& ) ) );
  connect( mp_messageDelegate, SIGNAL( urlActivated( const QUrl& ) ), this, SLOT( checkAnchorClicked( const QUrl& ) ) );
  connect( mp_teMessage, SIGNAL( returnPressed() ), this, SLOT( sendMessage() ) );
  connect( mp_teMessage, SIGNAL( writing() ), this, SLOT( checkWriting() ) );
  connect( mp_teMessage, SIGNAL( urlsToCheck( const QMimeData* ) ), this, SLOT( checkAndSendUrls( const QMimeData* ) ) );
  connect( mp_teMessage, SIGNAL( imageToCheck( const QMimeData* ) ), this, SLOT( checkAndSendImage( const QMimeData* ) ) );
  connect( mp_pbSend, SIGNAL( clicked() ), this, SLOT( sendMessage() ) );
}

// YongBEEP S3-4a: hide floating-window leftovers when embedded in the main window
void GuiChat::setCompactInputMode()
{
  mp_cbSendAndClose->setChecked( false );
  mp_cbSendAndClose->hide();
  mp_pbRecordVoiceMessage->hide();
}

// ---------------------------------------------------------------------
// YongBEEP S4e (chatview-v2): QListView + custom delegate message list.
// Replaces the QTextBrowser+HTML chain: the delegate paints true rounded
// bubbles (r=8), shrink-to-fit documents (max 62% of the area) and the
// WeChat file card; the model caches one QTextDocument per message.
// ---------------------------------------------------------------------
void GuiChat::setupMessageList()
{
  mp_messageModel = new ChatMessageModel( this );
  mp_messageDelegate = new ChatMessageDelegate( this );

  // keep the #GuiChatViewer objectName so the YongTheme QSS still paints
  // the #F5F5F5 chat background from the main window cascade
  mp_lvChat->setObjectName( "GuiChatViewer" );
  mp_lvChat->setModel( mp_messageModel );
  mp_lvChat->setItemDelegate( mp_messageDelegate );
  mp_lvChat->setSelectionMode( QAbstractItemView::NoSelection );
  mp_lvChat->setEditTriggers( QAbstractItemView::NoEditTriggers );
  mp_lvChat->setVerticalScrollMode( QAbstractItemView::ScrollPerPixel ); // smooth scrolling
  mp_lvChat->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
  mp_lvChat->setResizeMode( QListView::Adjust );
  mp_lvChat->setUniformItemSizes( false );
  mp_lvChat->setSpacing( 0 );
  mp_lvChat->setFocusPolicy( Qt::StrongFocus ); // keyboard events like CTRL+c
  mp_lvChat->setContextMenuPolicy( Qt::CustomContextMenu );
  mp_lvChat->viewport()->installEventFilter( this ); // relayout on resize
  mp_messageModel->setViewportWidth( qMax( 200, mp_lvChat->viewport()->width() ) );
}

bool GuiChat::eventFilter( QObject* obj, QEvent* event )
{
  if( obj == mp_lvChat->viewport() && ( event->type() == QEvent::Resize || event->type() == QEvent::Show ) )
  {
    int new_width = qMax( 200, mp_lvChat->viewport()->width() );
    if( new_width != mp_messageModel->viewportWidth() )
    {
      mp_messageModel->setViewportWidth( new_width );
      mp_lvChat->doItemsLayout(); // rebuild the cached item heights
    }
  }
  return QWidget::eventFilter( obj, event );
}

void GuiChat::registerDemoMarkdownTexts( const QStringList& raw_texts )
{
  mp_messageModel->registerMarkdownTexts( raw_texts );
}

// YongBEEP S4c debug helper (--demo-chat + --screenshot only): the demo core
// is never connected, which would leave the input disabled and the send
// button gray in every screenshot. Force-enable the input and seed a draft
// line so the shots show the real WeChat active state (green send button,
// per-chat draft restore).
void GuiChat::setDemoInputDraft( const QString& draft )
{
  m_sendAllowedByState = true;
  mp_teMessage->setEnabled( true );
  mp_teMessage->setToolTip( QString() );
  mp_teMessage->setPlaceholderText( tr( "Write a message" ) );
  if( !draft.isEmpty() )
    mp_teMessage->setPlainText( draft );
  updateSendButton();
}

// YongBEEP S3-4b / S4c: WeChat-style input toolbar row [smile][paperclip]
// above the message edit (PRD 4.1). 20px hand-drawn SVG line icons, gray
// #7F7F7F with a #191919 hover state (YongTheme). The smile button opens the
// existing GuiEmoticons picker embedded in a lightweight popup frame; the
// paperclip reuses the GuiChat::sendFile() chain (file dialog ->
// sendFileFromChatRequest -> Core::sendFilesFromChat, inline over bluetooth
// when needed).
void GuiChat::setupInputToolbar()
{
  mp_inputToolbar = new QWidget( this );
  mp_inputToolbar->setObjectName( "GuiChatInputToolbar" );
  QHBoxLayout* toolbar_layout = new QHBoxLayout( mp_inputToolbar );
  // YongBEEP: tight icon row (the old 6+9px gap left the icons floating
  // ~20px above the input field — read as misalignment)
  toolbar_layout->setContentsMargins( 12, 4, 12, 0 );
  toolbar_layout->setSpacing( 8 );

  mp_tbEmoticons = new QToolButton( mp_inputToolbar );
  mp_tbEmoticons->setObjectName( "GuiChatEmoticonsButton" );
  mp_tbEmoticons->setIcon( YongTheme::svgIcon( "smile", YongTheme::iconGray(), YongTheme::textPrimary(), YongTheme::toolbarIconPx() ) );
  mp_tbEmoticons->setIconSize( QSize( YongTheme::toolbarIconPx(), YongTheme::toolbarIconPx() ) );
  mp_tbEmoticons->setToolTip( tr( "Insert an emoticon" ) );
  mp_tbEmoticons->setAutoRaise( true );
  mp_tbEmoticons->setCursor( Qt::PointingHandCursor );

  mp_tbAttach = new QToolButton( mp_inputToolbar );
  mp_tbAttach->setObjectName( "GuiChatAttachButton" );
  mp_tbAttach->setIcon( YongTheme::svgIcon( "paperclip", YongTheme::iconGray(), YongTheme::textPrimary(), YongTheme::toolbarIconPx() ) );
  mp_tbAttach->setIconSize( QSize( YongTheme::toolbarIconPx(), YongTheme::toolbarIconPx() ) );
  mp_tbAttach->setToolTip( tr( "Send a file" ) );
  mp_tbAttach->setAutoRaise( true );
  mp_tbAttach->setCursor( Qt::PointingHandCursor );

  // S4e: checkable Markdown toggle - messages sent while on carry the
  // Message::Markdown intent and render with QTextDocument::setMarkdown
  mp_tbMarkdown = new QToolButton( mp_inputToolbar );
  mp_tbMarkdown->setObjectName( "GuiChatMarkdownButton" );
  mp_tbMarkdown->setIcon( YongTheme::svgIcon( "markdown", YongTheme::iconGray(), YongTheme::textPrimary(), YongTheme::toolbarIconPx() ) );
  mp_tbMarkdown->setIconSize( QSize( YongTheme::toolbarIconPx(), YongTheme::toolbarIconPx() ) );
  mp_tbMarkdown->setText( tr( "MD" ) );
  mp_tbMarkdown->setToolTip( tr( "Send messages as Markdown" ) );
  mp_tbMarkdown->setCheckable( true );
  mp_tbMarkdown->setAutoRaise( true );
  mp_tbMarkdown->setCursor( Qt::PointingHandCursor );

  toolbar_layout->addWidget( mp_tbEmoticons );
  toolbar_layout->addWidget( mp_tbAttach );
  toolbar_layout->addWidget( mp_tbMarkdown );
  toolbar_layout->addStretch( 1 );

  connect( mp_tbEmoticons, SIGNAL( clicked() ), this, SLOT( showEmoticonsPopup() ) );
  connect( mp_tbAttach, SIGNAL( clicked() ), this, SLOT( sendFile() ) );
  connect( mp_tbMarkdown, SIGNAL( toggled( bool ) ), this, SLOT( onMarkdownToggled( bool ) ) );
}

void GuiChat::onMarkdownToggled( bool checked )
{
  if( checked )
  {
    mp_tbMarkdown->setIcon( YongTheme::svgIcon( "markdown", YongTheme::brandGreen(), YongTheme::textPrimary(), YongTheme::toolbarIconPx() ) );
    mp_tbMarkdown->setToolTip( tr( "Markdown is enabled" ) );
  }
  else
  {
    mp_tbMarkdown->setIcon( YongTheme::svgIcon( "markdown", YongTheme::iconGray(), YongTheme::textPrimary(), YongTheme::toolbarIconPx() ) );
    mp_tbMarkdown->setToolTip( tr( "Send messages as Markdown" ) );
  }
  ensureFocusInChat();
}

void GuiChat::showEmoticonsPopup()
{
  if( !mp_emoticonsPopup )
  {
    mp_emoticonsPopup = new QFrame( this, Qt::Popup );
    mp_emoticonsPopup->setObjectName( "GuiChatEmoticonsPopup" );
    mp_emoticonsPopup->setFrameShape( QFrame::StyledPanel );
    mp_emoticonsPopup->setAttribute( Qt::WA_DeleteOnClose, false );
    QVBoxLayout* popup_layout = new QVBoxLayout( mp_emoticonsPopup );
    popup_layout->setContentsMargins( 2, 2, 2, 2 );
    mp_emoticonsWidget = new GuiEmoticons( mp_emoticonsPopup );
    QMetaObject::invokeMethod( mp_emoticonsWidget, "updateEmoticons", Qt::QueuedConnection );
    connect( mp_emoticonsWidget, SIGNAL( emoticonSelected( const Emoticon& ) ), this, SLOT( emoticonSelectedInPopup( const Emoticon& ) ) );
    popup_layout->addWidget( mp_emoticonsWidget );
    mp_emoticonsPopup->setLayout( popup_layout );
    mp_emoticonsPopup->setFixedSize( 336, 264 );
  }

  mp_emoticonsPopup->move( mp_tbEmoticons->mapToGlobal( QPoint( 0, mp_tbEmoticons->height() + 2 ) ) );
  mp_emoticonsPopup->show();
}

void GuiChat::emoticonSelectedInPopup( const Emoticon& e )
{
  addEmoticon( e );
  if( mp_emoticonsPopup && mp_emoticonsPopup->isVisible() )
    mp_emoticonsPopup->hide();
}

void GuiChat::setupToolBar( QToolBar* chat_bar )
{
  // Chat toolbar
  chat_bar->addAction( IconManager::instance().icon( "font.png" ), tr( "Change font style" ), this, SLOT( selectFont() ) );
  chat_bar->addAction( IconManager::instance().icon( "font-color.png" ), tr( "Change font color" ), this, SLOT( selectFontColor() ) );
  mp_actFilterMessages = chat_bar->addAction( IconManager::instance().icon( "filter.png" ), tr( "Filter message" ), this, SLOT( showChatMessageFilterMenu() ) );
  mp_actFilterMessages->setCheckable( true );
  updateFilterMessagesButton();
  chat_bar->addAction( IconManager::instance().icon( "settings.png" ), tr( "Chat settings" ), this, SIGNAL( showChatMenuRequest() ) );
  mp_actSpellChecker = chat_bar->addAction( IconManager::instance().icon( "spellchecker.png" ), tr( "Spell checking" ), this, SLOT( onSpellCheckerActionClicked() ) );
  mp_actSpellChecker->setCheckable( true );
  mp_actCompleter = chat_bar->addAction( IconManager::instance().icon( "dictionary.png" ), tr( "Word completer" ), this, SLOT( onCompleterActionClicked() ) );
  mp_actCompleter->setCheckable( true );
  mp_actUseReturnToSendMessage = chat_bar->addAction( IconManager::instance().icon( "key-return.png" ), tr( "Use Return key to send message" ), this, SLOT( onUseReturnToSendMessageClicked() ) );
  mp_actUseReturnToSendMessage->setCheckable( true );
  updateActionsOnFocusChanged();
  mp_actSendFile = chat_bar->addAction( IconManager::instance().icon( "send-file.png" ), tr( "Send file" ), this, SLOT( sendFile() ) );
  mp_actSendFolder = chat_bar->addAction( IconManager::instance().icon( "send-folder.png" ), tr( "Send folder" ), this, SLOT( sendFolder() ) );
  mp_actSendTextCode = chat_bar->addAction( IconManager::instance().icon( "send-code.png" ), tr( "Send code" ), this, SLOT( sendTextCode() ) );
#ifdef BEEBEEP_USE_SHAREDESKTOP
  chat_bar->addAction( mp_actScreenshot );
  chat_bar->addAction( mp_actShareDesktop );
#endif
  chat_bar->addAction( mp_actSaveAs );

  mp_teMessage->addActionToContextMenu( mp_actSendFile );
  mp_teMessage->addActionToContextMenu( mp_actSendFolder );
  mp_teMessage->addActionToContextMenu( mp_actSendTextCode );
  QAction* act = new QAction( mp_teMessage );
  act->setSeparator( true );
  mp_teMessage->addActionToContextMenu( act );
  mp_teMessage->addActionToContextMenu( mp_actRestoreDefaultFont );
}

void GuiChat::updateOnSendingMessage()
{
  if( Settings::instance().chatOnSendingMessage() == Settings::CloseChatOnSendingMessage )
  {
    mp_cbSendAndClose->setText( tr( "Close" ) );
    mp_cbSendAndClose->setToolTip( tr( "Close chat window on sending message" ) );
  }
  else
  {
    mp_cbSendAndClose->setText( tr( "Minimize" ) );
    mp_cbSendAndClose->setToolTip( tr( "Minimize chat window on sending message" ) );
  }
  mp_cbSendAndClose->setChecked( Settings::instance().chatOnSendingMessage() > Settings::SkipOnSendingMessage );
}

void GuiChat::updateActions( const Chat& c, bool is_connected, int connected_users, bool file_transfer_is_active )
{
  if( c.id() != m_chatId )
    return;

  mp_teMessage->setEnabled( c.isValid() );
  mp_lvChat->setEnabled( c.isValid() );

  bool local_user_is_member = c.hasUser( Settings::instance().localUser().id() );
  bool chat_is_empty = ChatManager::instance().isChatEmpty( c, true );
  bool can_send_files = false;
  UserList chat_members;
#ifdef BEEBEEP_USE_SHAREDESKTOP
  bool desktop_is_shared = false;
  QStringList share_desktop_users;
#endif
  if( connected_users > 0 )
  {
    chat_members = UserManager::instance().userList().fromUsersId( c.usersId() );
    foreach( User u, chat_members.toList() )
    {
      if( u.isLocal() )
        continue;
      if( u.isStatusConnected() )
        can_send_files = true;
#ifdef BEEBEEP_USE_SHAREDESKTOP
      if( Settings::instance().enableShareDesktop() )
      {
        if( beeCore->shareDesktopIsActive( u.id() ) )
        {
          desktop_is_shared = true;
          share_desktop_users.append( u.name() );
        }
      }
#endif
    }
  }

  mp_actClear->setDisabled( chat_is_empty );
  mp_actClearSystemMessages->setEnabled( c.hasSystemMessages() );
  mp_actSendFile->setEnabled( file_transfer_is_active && local_user_is_member && is_connected && can_send_files );
  if( Settings::instance().disableSendFolder() )
    mp_actSendFolder->setEnabled( false );
  else
    mp_actSendFolder->setEnabled( file_transfer_is_active && local_user_is_member && is_connected && can_send_files );

  if( !is_connected )
  {
    mp_teMessage->setEnabled( false );
    m_sendAllowedByState = false;
    mp_actSendTextCode->setEnabled( false );
    mp_teMessage->setToolTip( tr( "You are not connected" ) );
  }
  else
  {
    if( Settings::instance().disableSendMessage() )
    {
      mp_teMessage->setEnabled( false );
      m_sendAllowedByState = false;
      mp_actSendTextCode->setEnabled( false );
      mp_teMessage->setToolTip( tr( "Send messages is disabled" ) );
    }
    else if( c.isDefault() && !Settings::instance().chatWithAllUsersIsEnabled() )
    {
      mp_teMessage->setEnabled( false );
      m_sendAllowedByState = false;
      mp_actSendTextCode->setEnabled( false );
      mp_teMessage->setToolTip( tr( "Chat with all users is disabled" ) );
    }
    else if( c.isPrivate() && Settings::instance().disablePrivateChats() )
    {
      mp_teMessage->setEnabled( false );
      m_sendAllowedByState = false;
      mp_actSendTextCode->setEnabled( false );
      mp_teMessage->setToolTip( tr( "Private chat is disabled" ) );
    }
    else
    {
      mp_teMessage->setEnabled( local_user_is_member );
      m_sendAllowedByState = local_user_is_member;
      mp_actSendTextCode->setEnabled( local_user_is_member );
      if( local_user_is_member )
        mp_teMessage->setToolTip( "" );
      else
        mp_teMessage->setToolTip( tr( "You have left this chat" ) );
    }
  }
  updateSendButton();

  if( mp_teMessage->isEnabled() )
  {
    if( c.isDefault() )
      mp_teMessage->setPlaceholderText( tr( "Write a message to all user" ) );
    else
      mp_teMessage->setPlaceholderText( tr( "Write a message to %1" ).arg( c.name() ) );
  }
  else
    mp_teMessage->setPlaceholderText( mp_teMessage->toolTip() );

#ifdef BEEBEEP_USE_SHAREDESKTOP
  mp_actShareDesktop->setEnabled( Settings::instance().enableShareDesktop() && m_chatId != ID_DEFAULT_CHAT && local_user_is_member && is_connected && can_send_files );
  if( Settings::instance().enableShareDesktop() )
    mp_actShareDesktop->setChecked( desktop_is_shared );
  else
    mp_actShareDesktop->setChecked( false );

  if( mp_actShareDesktop->isChecked() )
  {
    mp_actShareDesktop->setToolTip( tr( "Your desktop is shared with %1" ).arg( Bee::stringListToTextString( share_desktop_users, true, 5 ) ) );
  }
  else
  {
    mp_actShareDesktop->setToolTip( tr( "Share your desktop" ) );
    mp_actShareDesktop->setIcon( IconManager::instance().icon( "desktop-share.png" ) );
  }

  mp_actScreenshot->setEnabled( m_chatId != ID_DEFAULT_CHAT && local_user_is_member && is_connected && can_send_files );
#endif

#ifdef BEEBEEP_USE_VOICE_CHAT
  mp_pbRecordVoiceMessage->setEnabled( mp_pbSend->isEnabled() && file_transfer_is_active && !Settings::instance().disableVoiceMessages() );
  if( !file_transfer_is_active )
    mp_pbRecordVoiceMessage->setToolTip( tr( "File transfer must be enabled to send voice messages" ) );
  else
    mp_pbRecordVoiceMessage->setToolTip( tr( "Record voice message" ) );
#endif
}

// S4c WeChat send button state: enabled only when the chat allows sending
// (connection/membership, set by updateActions) AND the input is not empty.
// The QSS turns the button gray (#E5E5E5) while disabled.
void GuiChat::updateSendButton()
{
  bool has_text = mp_teMessage->document() && !mp_teMessage->document()->isEmpty();
  mp_pbSend->setEnabled( m_sendAllowedByState && has_text );
}

// YongBEEP S4e: context menu rebuilt on the QListView - simplified per
// message (copy full text + quote) plus the chat-level actions.
void GuiChat::customContextMenu( const QPoint& p )
{
  mp_menuContext->clear();
  m_contextIndex = mp_lvChat->indexAt( p );

  if( m_contextIndex.isValid() )
  {
    QAction* act = mp_menuContext->addAction( IconManager::instance().icon( "copy.png" ), tr( "Copy to clipboard" ), this, SLOT( copyMessageToClipboard() ), QKeySequence::Copy );
    act->setEnabled( !m_contextIndex.data().toString().isEmpty() );
    mp_menuContext->addAction( IconManager::instance().icon( "quote.png" ), tr( "Quote message" ), this, SLOT( quoteMessageToInput() ) );

    QUrl anchor = mp_messageDelegate->anchorAt( m_contextIndex, mp_lvChat->visualRect( m_contextIndex ), p );
    if( anchor.isValid() && !anchor.toString().isEmpty() )
    {
      act = mp_menuContext->addAction( IconManager::instance().icon( "anchor.png" ), tr( "Copy URL to clipboard" ), this, SLOT( copyUrlToClipboard() ) );
      act->setData( anchor.toString() );
    }
    mp_menuContext->addSeparator();
  }

  mp_menuContext->addAction( mp_actSelectBackgroundColor );
  mp_menuContext->addAction( mp_actSelectForegroundColor );
  mp_menuContext->addAction( mp_actSelectSystemColor );
  mp_menuContext->addSeparator();
  mp_menuContext->addAction( mp_actFindTextInChat );
  mp_menuContext->addSeparator();
  mp_menuContext->addAction( mp_actClear );
  mp_menuContext->addAction( mp_actClearSystemMessages );
  mp_menuContext->addSeparator();
  mp_menuContext->addAction( mp_actPrint );
  mp_menuContext->addSeparator();
  mp_menuContext->addAction( mp_actRestoreDefaultFont );
  mp_menuContext->exec( QCursor::pos() );
  m_contextIndex = QPersistentModelIndex();
}

void GuiChat::copyMessageToClipboard()
{
  if( !m_contextIndex.isValid() )
    return;
  QString text = m_contextIndex.data().toString();
  if( text.isEmpty() )
    return;
  QClipboard* cb = qApp->clipboard();
  if( cb )
    cb->setText( text );
}

void GuiChat::quoteMessageToInput()
{
  if( !m_contextIndex.isValid() )
    return;
  QString text = m_contextIndex.data().toString();
  if( text.isEmpty() )
    return;
  text.replace( QChar( '\n' ), QLatin1String( " " ) );
  QString text_to_add = QString( " [quote]%1 [/quote]<br>" ).arg( text.trimmed() );
  mp_teMessage->addText( text_to_add );
  mp_teMessage->setFocus();
}

bool GuiChat::messageCanBeShowed( const ChatMessage& cm )
{
  if( m_chatId == ID_DEFAULT_CHAT && Settings::instance().showOnlyMessagesInDefaultChat() )
    return cm.isChatActivity() && !Settings::instance().chatMessageFilter().testBit( static_cast<int>( cm.type() ) );
  else
    return !Settings::instance().chatMessageFilter().testBit( static_cast<int>( cm.type() ) );
}

bool GuiChat::historyCanBeShowed()
{
  return !Settings::instance().chatMessageFilter().testBit( static_cast<int>( ChatMessage::History ) );
}

void GuiChat::updateFilterMessagesButton()
{
  if( !mp_actFilterMessages )
    return;
  QBitArray filter_array = Settings::instance().chatMessageFilter();
  bool filters_active = false;
  for( int i = 0; i < filter_array.size(); i++ )
  {
    if( filter_array.testBit( i ) )
    {
      filters_active = true;
      break;
    }
  }
  mp_actFilterMessages->setChecked( filters_active );
}

void GuiChat::showChatMessageFilterMenu()
{
  mp_menuFilters->clear();
  QAction* act;

  act = mp_menuFilters->addAction( tr( "Show only messages in default chat" ), this, SLOT( changeChatMessageFilter() ) );
  act->setCheckable( true );
  act->setChecked( Settings::instance().showOnlyMessagesInDefaultChat() );
  act->setData( static_cast<int>( ChatMessage::NumTypes ) );
  mp_menuFilters->addSeparator();

  for( int i = ChatMessage::System; i < ChatMessage::NumTypes; i++ )
  {
    act = mp_menuFilters->addAction( Bee::chatMessageTypeToString( i ), this, SLOT( changeChatMessageFilter() ) );
    act->setCheckable( true );
    act->setChecked( !Settings::instance().chatMessageFilter().testBit( i ) );
    act->setData( i );
  }
  mp_menuFilters->exec( QCursor::pos() );
  updateFilterMessagesButton();
}

void GuiChat::changeChatMessageFilter()
{
  QAction* act = qobject_cast<QAction*>( sender() );
  if( !act )
    return;

  if( act->data().toInt() == static_cast<int>( ChatMessage::NumTypes ) )
  {
    Settings::instance().setShowOnlyMessagesInDefaultChat( act->isChecked() );
    if( m_chatId != ID_DEFAULT_CHAT )
      return;
  }
  else
  {
    QBitArray filter_array = Settings::instance().chatMessageFilter();
    filter_array.setBit( act->data().toInt(), !act->isChecked() );
    Settings::instance().setChatMessageFilter( filter_array );
  }

  updateFilterMessagesButton();
  updateChat();
}

void GuiChat::setLastMessageTimestamp( const QDateTime& dt )
{
  if( dt.isValid() && !Settings::instance().chatShowMessageTimestamp() )
  {
    QString last_msg_timestamp = tr( "Last message %1" );
    if( dt.date() == QDate::currentDate() )
      last_msg_timestamp = last_msg_timestamp.arg( dt.toString( "hh:mm" ) );
    else
      last_msg_timestamp = last_msg_timestamp.arg( QLocale::system().toString( dt, QLocale::LongFormat ) );
    mp_lvChat->setToolTip( last_msg_timestamp );
  }
  else
    mp_lvChat->setToolTip( "" );
}

void GuiChat::sendMessage()
{
  if( Settings::instance().disableSendMessage() )
    return;
  QString msg = mp_teMessage->message();
  if( msg.isEmpty() )
    return;
  mp_teMessage->addMessageToHistory();
  // S4e: the MD toggle now travels on the wire as Message::Markdown
  // (sendChatMessage -> m.setMarkdown()); the model-side registry is kept
  // only as a fallback for messages built before the flag existed.
  const bool is_markdown = mp_tbMarkdown && mp_tbMarkdown->isChecked();
  if( is_markdown )
    mp_messageModel->addMarkdownText( msg );
  emit newMessage( m_chatId, msg, false, is_markdown );
  mp_teMessage->clearMessage();
  if( mp_cbSendAndClose->isChecked() )
  {
    if( Settings::instance().chatOnSendingMessage() == Settings::CloseChatOnSendingMessage )
      QMetaObject::invokeMethod( this, "closeRequest", Qt::QueuedConnection );
    else
      QMetaObject::invokeMethod( this, "hideRequest", Qt::QueuedConnection );
  }
  else
    ensureFocusInChat();
}

void GuiChat::checkWriting()
{
  emit writing( m_chatId );
}

void GuiChat::checkAnchorClicked( const QUrl& url )
{
  if( url.scheme() == FileInfo::urlSchemeVoiceMessage() )
    emit showStatusMessageRequest( tr( "Opening voice message" ) + QString( "..." ), 3000 );

  emit openUrl( url, m_chatId );
}

void GuiChat::updateChat()
{
  Chat c = ChatManager::instance().chat( m_chatId );
  if( !c.isValid() )
  {
    qWarning() << "Invalid chat" << m_chatId << "found in GuiChat::updateChat(...)";
    return;
  }
  setChat( c );
}

void GuiChat::loadSavedMessages()
{
  emit showStatusMessageRequest( tr( "Loading of saved messages..." ), 3000 );
  QApplication::processEvents();

  if( !ChatManager::instance().isLoadHistoryCompleted() )
  {
    emit showStatusMessageRequest( tr( "The loading of saved messages has not yet been completed."), 2000 );
    QTimer::singleShot( 2000, this, SLOT( operationCompleted() ) );
    return;
  }

  if( !historyCanBeShowed() )
  {
    QTimer::singleShot( 0, this, SLOT( operationCompleted() ) );
    return;
  }

  Chat c = ChatManager::instance().chat( m_chatId );
  if( !c.isValid() )
  {
    QTimer::singleShot( 0, this, SLOT( operationCompleted() ) );
    return;
  }

  setChat( c );
}

bool GuiChat::setChat( const Chat& c )
{
  if( !c.isValid() )
    return false;

  // Keep the scroll position when the SAME chat is reloaded (saved-messages
  // load); jump to the bottom when a different chat is opened.
  bool same_chat = ( m_chatId == c.id() );
  int keep_row = -1;
  if( same_chat && mp_messageModel->rowCount() > 0 )
  {
    QModelIndex top_index = mp_lvChat->indexAt( QPoint( 0, 0 ) );
    if( top_index.isValid() && !mp_lvChat->visualRect( top_index ).isNull() )
      keep_row = top_index.row();
  }

  m_chatId = c.id();
  m_lastMessageUserId = ID_SYSTEM_MESSAGE;
  m_lastMessageTimestamp = QDateTime();

  mp_messageModel->resetFromChat( c );

  setLastMessageTimestamp( c.lastMessageTimestamp() );
  if( keep_row >= 0 && keep_row < mp_messageModel->rowCount() )
    mp_lvChat->scrollTo( mp_messageModel->index( keep_row, 0 ), QAbstractItemView::PositionAtTop );
  else
    ensureLastMessageVisible();
  updateChat( c );
  QTimer::singleShot( 0, this, SLOT( operationCompleted() ) );
  return true;
}

bool GuiChat::updateChat( const Chat& c )
{
  if( m_chatId != c.id() )
    return false;
  updateChatColors();
  return true;
}

void GuiChat::ensureFocusInChat()
{
  if( mp_teMessage->isEnabled() )
    mp_teMessage->setFocus();

  if( !mp_teMessage->hasFocus() )
    mp_teMessage->setCursor( Qt::WaitCursor );
  else
    mp_teMessage->setCursor( Qt::ArrowCursor );
}

void GuiChat::ensureLastMessageVisible()
{
  if( mp_messageModel->rowCount() > 0 )
    mp_lvChat->scrollTo( mp_messageModel->index( mp_messageModel->rowCount() - 1, 0 ), QAbstractItemView::PositionAtBottom );
}

// S4e: auto-scroll to the bottom only when the user is already there -
// reading older messages upward is never yanked down by new arrivals.
bool GuiChat::isScrolledToBottom() const
{
  QScrollBar* bar = mp_lvChat->verticalScrollBar();
  if( !bar )
    return true;
  return bar->value() >= bar->maximum() - 48;
}

bool GuiChat::appendChatMessage( const Chat& c, const ChatMessage& cm )
{
  if( m_chatId != c.id() )
  {
    qWarning() << "Trying to append chat message of chat id" << c.id() << "in chat shown with id" << m_chatId << "... skip it";
    return false;
  }

  if( !c.isValid() )
  {
    qWarning() << "Invalid chat" << m_chatId << "found in GuiChat::appendChatMessage(...)";
    return false;
  }

  bool show_timestamp_last_message = !cm.isFromLocalUser() && !cm.isFromSystem();
  mp_actClear->setDisabled( c.isEmpty() && !ChatManager::instance().chatHasSavedText( c.name() ) );
  mp_actClearSystemMessages->setEnabled( c.hasSystemMessages() );

  bool was_at_bottom = isScrolledToBottom();
  if( mp_messageModel->appendMessage( c, cm ) )
  {
    if( was_at_bottom )
      ensureLastMessageVisible();
  }

  if( show_timestamp_last_message )
    setLastMessageTimestamp( cm.timestamp() );

  return true;
}

void GuiChat::setChatFont( const QFont& f )
{
  mp_lvChat->setFont( f );
  mp_messageModel->setChatFont( f );
  mp_teMessage->setFont( f );
  mp_teMessage->update();
  mp_actRestoreDefaultFont->setEnabled( f != QApplication::font() );
}

void GuiChat::selectFont()
{
  bool ok = false;
  QFont f = QFontDialog::getFont( &ok, Settings::instance().chatFont(), this );
  if( ok )
  {
    Settings::instance().setChatFont( f, false );
    Settings::instance().save();
    setChatFont( f );
    ensureLastMessageVisible();
    emit updateChatFontRequest();
  }
}

void GuiChat::updateChatColors()
{
  if( Settings::instance().useDarkStyle() )
  {
    // Dark style keeps the pre-S4c settings-driven behavior.
    QString background_color = m_chatId == ID_DEFAULT_CHAT ? Settings::instance().defaultChatBackgroundColor() : Settings::instance().chatBackgroundColor();
    QString text_color = Settings::instance().chatFontColor() != Settings::instance().chatDefaultTextColor() ? Settings::instance().chatFontColor(): Settings::instance().chatDefaultTextColor();
    mp_teMessage->setTextColor( text_color );
    mp_teMessage->setStyleSheet( QString( "#GuiMessageEdit { background-color: %1; color: %2; }" ).arg( background_color, text_color ) );
    mp_lvChat->setStyleSheet( QString( "#GuiChatViewer { background-color: %1; color: %2; }" ).arg( background_color, Settings::instance().chatDefaultTextColor() ) );
    return;
  }

  // S4c WeChat light theme: the chat area is #F5F5F5 with white input area,
  // both styled by the YongTheme QSS on the main window. Clear the
  // per-widget overrides so the QSS rules (#GuiChatViewer/#GuiMessageEdit)
  // win, and keep the placeholder gray.
  mp_teMessage->setStyleSheet( QString() );
  mp_lvChat->setStyleSheet( QString() );
  mp_teMessage->setTextColor( YongTheme::textPrimary() );
  QPalette message_palette = mp_teMessage->palette();
  message_palette.setColor( QPalette::PlaceholderText, YongTheme::textSecondary() );
  mp_teMessage->setPalette( message_palette );
}

void GuiChat::selectFontColor()
{
  QColor c = QColorDialog::getColor( QColor( Settings::instance().chatFontColor() ), this );
  if( c.isValid() && c.name() != Settings::instance().chatFontColor() )
  {
    Settings::instance().setChatFontColor( c.name() );
    Settings::instance().save();
    emit updateChatColorsRequest();
  }
}

void GuiChat::selectBackgroundColor()
{
  QColor c = QColorDialog::getColor( QColor( (m_chatId == ID_DEFAULT_CHAT ? Settings::instance().defaultChatBackgroundColor() : Settings::instance().chatBackgroundColor() ) ), this );
  if( c.isValid() )
  {
    if( m_chatId == ID_DEFAULT_CHAT )
    {
      Settings::instance().setDefaultChatBackgroundColor( c.name() );
      updateChatColors();
    }
    else
    {
      Settings::instance().setChatBackgroundColor( c.name() );
      emit updateChatColorsRequest();
    }
  }
}

void GuiChat::selectForegroundColor()
{
  QColor c = QColorDialog::getColor( QColor( Settings::instance().chatDefaultTextColor() ), this );
  if( c.isValid() && c.name() != Settings::instance().chatDefaultTextColor() )
  {
    Settings::instance().setChatDefaultTextColor( c.name() );
    emit updateChatColorsRequest();
  }
}

void GuiChat::selectSystemColor()
{
  QColor c = QColorDialog::getColor( QColor( Settings::instance().chatSystemTextColor() ), this );
  if( c.isValid() && c.name() != Settings::instance().chatSystemTextColor() )
  {
    Settings::instance().setChatSystemTextColor( c.name() );
    QMessageBox::information( this, Settings::instance().programName(), tr( "You must close and reopen this window to see the changes applied." ), tr( "Ok" ) );
    emit updateChatColorsRequest();
  }
}

void GuiChat::addEmoticon( const Emoticon& e )
{
  mp_teMessage->addEmoticon( e );
  ensureFocusInChat();
}

void GuiChat::addText( const QString& txt )
{
  mp_teMessage->addText( txt );
  ensureFocusInChat();
}

void GuiChat::saveChat()
{
  Chat c = ChatManager::instance().chat( m_chatId );
  if( !c.isValid() )
  {
    qWarning() << "Invalid chat id" << m_chatId << "found in GuiChat::saveChat()";
    return;
  }

  QString file_name = FileDialog::getSaveFileName( this,
                        tr( "Please select a file to save all the messages of the chat." ),
                        Settings::instance().dataFolder(), "PDF Chat Files (*.pdf)" );
  if( file_name.isEmpty() )
    return;

  if( !file_name.endsWith( QLatin1String( ".pdf" ), Qt::CaseInsensitive ) )
    file_name.append( QLatin1String( ".pdf" ) );

  QPrinter printer;
  printer.setOutputFormat( QPrinter::PdfFormat );
  printer.setOutputFileName( file_name );
  QTextDocument *doc = new QTextDocument( this );
  QString html_text = "";
  if( ChatManager::instance().chatHasSavedText( c.name() ) )
    html_text.append( ChatManager::instance().chatSavedText( c.name() ) );
  html_text.append( GuiChatMessage::chatToHtml( c, !Settings::instance().chatSaveFileTransfers(),
                                                  !Settings::instance().chatSaveSystemMessages(), true, true, Settings::instance().chatCompact(), Settings::instance().useCompactDataSaving() ) );
  html_text.replace( FileInfo::urlSchemeVoiceMessage() + QLatin1String(":"), "file://" );
  html_text.replace( FileInfo::urlSchemeShowFileInFolder() + QLatin1String(":"), "file://" );
  doc->setHtml( html_text );
  doc->print( &printer );
  QMessageBox::information( this, Settings::instance().programName(), tr( "%1: save completed." ).arg( file_name ), tr( "Ok" ) );
  doc->deleteLater();
}

void GuiChat::clearChat()
{
  emit chatToClear( m_chatId );
}

void GuiChat::sendFile()
{
  emit sendFileFromChatRequest( m_chatId, QString( "" ) );
}

void GuiChat::sendFolder()
{
  QString folder_selected = FileDialog::getExistingDirectory( this, Settings::instance().programName(),
                                                              Settings::instance().lastDirectorySelected() );
  if( folder_selected.isEmpty() )
    return;

  emit sendFileFromChatRequest( m_chatId, folder_selected );
}

void GuiChat::dragEnterEvent( QDragEnterEvent *event )
{
  if( event->mimeData()->hasUrls() )
    event->acceptProposedAction();
}

void GuiChat::checkAndSendImage( const QMimeData* source )
{
  if( !source->hasImage() )
    return;

  QString image_format = "png";
  QString image_initial_path = Settings::instance().cacheFolder() +
                                    QString( "/beeimgtmp-%1." ).arg( Bee::dateTimeStringSuffix( QDateTime::currentDateTime() ) )
                                    + image_format;
  QString file_path = Bee::uniqueFilePath( image_initial_path, false );
  qDebug() << "Sending the dropped image as a file:" << file_path;

  QImage image = qvariant_cast<QImage>( source->imageData() );

  if( !image.save( file_path, image_format.toLatin1() ) )
  {
    QMessageBox::warning( this, Settings::instance().programName(),
      tr( "Unable to save temporary file: %1" ).arg( file_path ) );
    return;
  }

  Settings::instance().addTemporaryFilePath( file_path );

  emit sendFileFromChatRequest( m_chatId, file_path );
}


void GuiChat::checkAndSendUrls( const QMimeData* source )
{
  if( !source->hasUrls() )
    return;

  QStringList file_path_list;
  QString file_path;
  int num_files = 0;

  foreach( QUrl url, source->urls() )
  {
#ifdef BEEBEEP_DEBUG
    qDebug() << "Checking pasted url:" << qPrintable( url.toString() );
#endif
    if( Bee::isLocalFile( url ) )
    {
      file_path = url.toLocalFile();
      file_path_list.append( Bee::convertToNativeFolderSeparator( file_path ) );
      num_files++;
      if( num_files > Settings::instance().maxQueuedDownloads() )
        break;
    }
  }

  if( num_files <= 0 )
  {
    mp_teMessage->addPasted( source );
    return;
  }

  if( file_path_list.isEmpty() )
  {
    if( num_files > 0 )
    {
      QMessageBox::warning( this, Settings::instance().programName(), tr( "You are trying to send %1 files simultaneously but the maximum allowed is %2." )
                                                                      .arg( num_files ).arg( Settings::instance().maxQueuedDownloads() ), tr( "Ok" ) );
    }
    return;
  }

  num_files = qMin( num_files, Settings::instance().maxQueuedDownloads() );

  if( QMessageBox::question( this, Settings::instance().programName(),
                             tr( "Do you want to send %1 %2 to the members of this chat?" ).arg( num_files )
                             .arg( num_files == 1 ? tr( "file" ) : tr( "files" ) ),
                             tr( "Yes" ), tr( "No" ), QString(), 0, 1 ) == 1 )
  {
    return;
  }

  emit sendFilesFromChatRequest( m_chatId, file_path_list );
}

void GuiChat::dropEvent( QDropEvent *event )
{
  if( event->mimeData()->hasUrls() )
    checkAndSendUrls( event->mimeData() );
}

void GuiChat::editChatMembers()
{
  emit editGroupRequest( m_chatId );
}

void GuiChat::updateShortcuts()
{
  QKeySequence ks = ShortcutManager::instance().shortcut( ShortcutManager::SetFocusInMessageBox );
  if( !ks.isEmpty() )
  {
    mp_scFocusInChat->setKey( ks );
    mp_scFocusInChat->setEnabled( Settings::instance().useCustomShortcuts() );
  }
  else
    mp_scFocusInChat->setEnabled( false );

  ks = ShortcutManager::instance().shortcut( ShortcutManager::FindTextInChat );
  if( Settings::instance().useCustomShortcuts() )
  {
    if( ks.isEmpty() )
    {
      mp_actFindTextInChat->setShortcut( QKeySequence() );
      mp_scFindTextInChat->setKey( QKeySequence::Find );
      mp_scFindTextInChat->setEnabled( false );
    }
    else
    {
      mp_actFindTextInChat->setShortcut( ks );
      mp_scFindTextInChat->setKey( ks );
      mp_scFindTextInChat->setEnabled( true );
    }
  }
  else
  {
    mp_actFindTextInChat->setShortcut( QKeySequence::Find );
    mp_scFindTextInChat->setKey( QKeySequence::Find );
    mp_scFindTextInChat->setEnabled( true );
  }

  ks = ShortcutManager::instance().shortcut( ShortcutManager::FindNextTextInChat );
  if( Settings::instance().useCustomShortcuts() )
  {
    if( ks.isEmpty() )
    {
      mp_scFindNextTextInChat->setKey( QKeySequence::FindNext );
      mp_scFindNextTextInChat->setEnabled( false );
    }
    else
    {
      mp_scFindNextTextInChat->setKey( ks );
      mp_scFindNextTextInChat->setEnabled( true );
    }
  }
  else
  {
    mp_scFindNextTextInChat->setKey( QKeySequence::FindNext );
    mp_scFindNextTextInChat->setEnabled( true );
  }

  ks = ShortcutManager::instance().shortcut( ShortcutManager::FindPreviousTextInChat );
  if( Settings::instance().useCustomShortcuts() )
  {
    if( ks.isEmpty() )
    {
      mp_scFindPreviousTextInChat->setKey( QKeySequence::FindPrevious );
      mp_scFindPreviousTextInChat->setEnabled( false );
    }
    else
    {
      mp_scFindPreviousTextInChat->setKey( ks );
      mp_scFindPreviousTextInChat->setEnabled( true );
    }
  }
  else
  {
    mp_scFindPreviousTextInChat->setKey( QKeySequence::FindPrevious );
    mp_scFindPreviousTextInChat->setEnabled( true );
  }

  ks = ShortcutManager::instance().shortcut( ShortcutManager::SendFile );
  if( !ks.isEmpty() && Settings::instance().useCustomShortcuts() )
    mp_actSendFile->setShortcut( ks );
  else
    mp_actSendFile->setShortcut( QKeySequence() );

  ks = ShortcutManager::instance().shortcut( ShortcutManager::SendFolder );
  if( !ks.isEmpty() && Settings::instance().useCustomShortcuts() )
    mp_actSendFolder->setShortcut( ks );
  else
    mp_actSendFolder->setShortcut( QKeySequence() );

  ks = ShortcutManager::instance().shortcut( ShortcutManager::SendChatMessage );
  if( !ks.isEmpty() && Settings::instance().useCustomShortcuts() )
    mp_pbSend->setShortcut( ks );
  else
    mp_pbSend->setShortcut( QKeySequence() );

  ks = ShortcutManager::instance().shortcut( ShortcutManager::Print );
  if( !ks.isEmpty() && Settings::instance().useCustomShortcuts() )
    mp_actPrint->setShortcut( ks );
  else
    mp_actPrint->setShortcut( QKeySequence() );

  ks = ShortcutManager::instance().shortcut( ShortcutManager::Print );
  if( Settings::instance().useCustomShortcuts() )
  {
    if( ks.isEmpty() )
    {
      mp_actPrint->setShortcut( QKeySequence() );
      mp_scPrintChat->setKey( QKeySequence::Print );
      mp_scPrintChat->setEnabled( false );
    }
    else
    {
      mp_actPrint->setShortcut( ks );
      mp_scPrintChat->setKey( ks );
      mp_scPrintChat->setEnabled( true );
    }
  }
  else
  {
    mp_actPrint->setShortcut( QKeySequence::Print );
    mp_scPrintChat->setKey( QKeySequence::Print );
    mp_scPrintChat->setEnabled( true );
  }

  ks = ShortcutManager::instance().shortcut( ShortcutManager::ShowEmoticons );
  if( !ks.isEmpty() )
  {
    mp_scViewEmoticons->setKey( ks );
    mp_scViewEmoticons->setEnabled( Settings::instance().useCustomShortcuts() );
  }
  else
    mp_scViewEmoticons->setEnabled( false );

}

void GuiChat::updateUseReturnKeyToSendMessageToolTip()
{
  if( Settings::instance().useReturnToSendMessage() )
    mp_actUseReturnToSendMessage->setToolTip( tr( "Use key Return to send message" ) );
  else
    mp_actUseReturnToSendMessage->setToolTip( tr( "Use key Return to make a carriage return" ) );
}

void GuiChat::onUseReturnToSendMessageClicked()
{
  Settings::instance().setUseReturnToSendMessage( mp_actUseReturnToSendMessage->isChecked() );
  updateUseReturnKeyToSendMessageToolTip();
  ensureFocusInChat();
}

void GuiChat::updateSpellCheckerToolTip()
{
  QString tool_tip = "";
  if( Settings::instance().useSpellChecker() )
    tool_tip = tr( "Spell checking is enabled" );
  else
    tool_tip = tr( "Spell checking is disabled" );

#ifdef BEEBEEP_USE_HUNSPELL
  if( !SpellChecker::instance().isValid() )
  {
    tool_tip.append( QString( " (%1)" ).arg( tr( "There is not a valid dictionary" ) ) );
    mp_actSpellChecker->setEnabled( false );
  }
  else
    mp_actSpellChecker->setEnabled( true );
#else
  mp_actSpellChecker->setEnabled( false );
#endif

  mp_actSpellChecker->setToolTip( tool_tip );
}

void GuiChat::updateCompleterToolTip()
{
  QString tool_tip = "";

  if( Settings::instance().useWordCompleter() )
    tool_tip = tr( "Word completer is enabled" );
  else
    tool_tip = tr( "Word completer is disabled" );

#ifdef BEEBEEP_USE_HUNSPELL
  if( !SpellChecker::instance().isValid() )
  {
    tool_tip.append( QString( " (%1)" ).arg( tr( "There is not a valid dictionary" ) ) );
    mp_actCompleter->setEnabled( false );
  }
  else
    mp_actCompleter->setEnabled( true );
#else
  mp_actCompleter->setEnabled( false );
#endif

  mp_actCompleter->setToolTip( tool_tip );
}

void GuiChat::onSpellCheckerActionClicked()
{
#ifdef BEEBEEP_USE_HUNSPELL
  if( SpellChecker::instance().isValid() )
    Settings::instance().setUseSpellChecker( mp_actSpellChecker->isChecked() );
  else
    Settings::instance().setUseSpellChecker( false );
#else
  Settings::instance().setUseSpellChecker( mp_actSpellChecker->isChecked() );
#endif
  updateSpellCheckerToolTip();
  mp_teMessage->rehighlightMessage();
  ensureFocusInChat();
}

void GuiChat::onCompleterActionClicked()
{
#ifdef BEEBEEP_USE_HUNSPELL
  if( SpellChecker::instance().isValid() )
    Settings::instance().setUseWordCompleter( mp_actCompleter->isChecked() );
  else
    Settings::instance().setUseWordCompleter( false );
#else
  Settings::instance().setUseWordCompleter( mp_actCompleter->isChecked() );
#endif
  updateCompleterToolTip();
  ensureFocusInChat();
}

void GuiChat::updateActionsOnFocusChanged()
{
  mp_actUseReturnToSendMessage->setChecked( Settings::instance().useReturnToSendMessage() );
  updateUseReturnKeyToSendMessageToolTip();
  mp_actSpellChecker->setChecked( Settings::instance().useSpellChecker() );
  updateSpellCheckerToolTip();
  mp_actCompleter->setChecked( Settings::instance().useWordCompleter() );
  updateCompleterToolTip();
}

void GuiChat::printChat()
{
  QPrinter printer( QPrinter::HighResolution );
  printer.setFullPage( true );
  QPrintDialog *dlg = new QPrintDialog( &printer, this );
  dlg->setOptions( QAbstractPrintDialog::PrintSelection | QAbstractPrintDialog::PrintPageRange |
                   QAbstractPrintDialog::PrintShowPageSize | QAbstractPrintDialog::PrintCollateCopies |
                   QAbstractPrintDialog::PrintCurrentPage |
                   QAbstractPrintDialog::PrintToFile );

  if( dlg->exec() == QDialog::Accepted )
  {
    // S4e: the message list is no longer a QTextBrowser - compose the
    // printable document from the classic chatToHtml pipeline
    Chat c = ChatManager::instance().chat( m_chatId );
    if( c.isValid() )
    {
      QString html_text;
      if( ChatManager::instance().chatHasSavedText( c.name() ) )
        html_text.append( ChatManager::instance().chatSavedText( c.name() ) );
      html_text.append( GuiChatMessage::chatToHtml( c, !Settings::instance().chatSaveFileTransfers(),
                                                    !Settings::instance().chatSaveSystemMessages(), true, true,
                                                    Settings::instance().chatCompact(), Settings::instance().useCompactDataSaving() ) );
      html_text.replace( QStringLiteral( "beefile:" ), QStringLiteral( "file://" ) );
      QTextDocument *doc = new QTextDocument( this );
      doc->setHtml( html_text );
      doc->print( dlg->printer() );
      doc->deleteLater();
    }
  }

  dlg->deleteLater();
}

void GuiChat::showFindTextInChatDialog()
{
  QString label = tr( "Find text in chat" );
  bool ok = false;
  QString text_to_search = QInputDialog::getText( this, Settings::instance().programName(), label,
                                                  QLineEdit::Normal, m_lastTextFound, &ok );
  if( ok && !text_to_search.isEmpty() )
  {
    if( !text_to_search.trimmed().isEmpty() )
      findTextInChat( text_to_search.trimmed() );
    else
      findTextInChat( text_to_search );
  }
}

void GuiChat::findNextTextInChat()
{
  findTextInChat( m_lastTextFound );
}

void GuiChat::findPreviousTextInChat()
{
  findTextInChat( m_lastTextFound, false );
}


void GuiChat::findTextInChat( const QString& txt, bool search_forward )
{
  if( txt.isEmpty() )
    return;

  // S4e: search runs on the model's plain texts and scrolls the list to
  // the matching message
  bool new_search = ( txt != m_lastTextFound );
  int from_row = new_search ? -1 : ( m_lastFindRow >= 0 ? m_lastFindRow + ( search_forward ? 1 : -1 ) : -1 );
  int found_row = mp_messageModel->findPlainText( txt, from_row, search_forward );
  if( found_row < 0 )
  {
    if( !new_search )
    {
      found_row = mp_messageModel->findPlainText( txt, -1, search_forward );
      if( found_row >= 0 )
      {
        m_lastTextFound = txt;
        m_lastFindRow = found_row;
        mp_lvChat->scrollTo( mp_messageModel->index( found_row, 0 ), QAbstractItemView::PositionAtCenter );
        return;
      }
    }
    QMessageBox::information( this, Settings::instance().programName(), tr( "%1 not found in chat." ).arg( QString( "\"%1\"" ).arg( txt ) ) );
  }
  else
  {
    m_lastTextFound = txt;
    m_lastFindRow = found_row;
    mp_lvChat->scrollTo( mp_messageModel->index( found_row, 0 ), QAbstractItemView::PositionAtCenter );
  }
}

void GuiChat::resetChatFontToDefault()
{
  if( QMessageBox::question( this, Settings::instance().programName(), tr( "Do you want to restore the default font?" ), tr( "Yes" ), tr( "No" ), QString(), 0, 1 ) == 1 )
    return;
  Settings::instance().setChatFont( QApplication::font(), false );
  setChatFont( Settings::instance().chatFont() );
}

void GuiChat::operationCompleted()
{
  emit showStatusMessageRequest( tr( "Ready." ), -1 );
}

void GuiChat::clearSystemMessages()
{
  emit clearSystemMessagesRequestFromChat( m_chatId );
}

#ifdef BEEBEEP_USE_SHAREDESKTOP
void GuiChat::shareDesktopToChat()
{
  emit shareDesktopToChatRequest( m_chatId, mp_actShareDesktop->isChecked() );
}

void GuiChat::sendScreenshotToChat()
{
  int answer_id = QMessageBox::question( this, Settings::instance().programName(), tr( "Do you want to send a screenshot?" ), tr( "Yes and hide this chat" ), tr( "Yes" ), tr( "No" ), 0, 2 );
  if( answer_id == 2 )
    return;

  mp_actScreenshot->setEnabled( false );
  if( answer_id == 0 )
    emit hideRequest();
  QTimer::singleShot( 1000, this, SLOT( sendScreenshotToChat_Private() ) );
}

void GuiChat::sendScreenshotToChat_Private()
{
  // To avoid chat window captured
  QTimer::singleShot( 3000, this, SLOT( enableScreenshotAction() ) );
  emit screenshotToChatRequest( m_chatId );
}

void GuiChat::enableScreenshotAction()
{
  mp_actScreenshot->setEnabled( beeCore->isConnected() );
}

void GuiChat::onTickEvent( int ticks )
{
  if( mp_actShareDesktop->isChecked() )
  {
    if( ticks % 2 == 0 )
      mp_actShareDesktop->setIcon( IconManager::instance().icon( "desktop-share.png" ) );
    else
      mp_actShareDesktop->setIcon( IconManager::instance().icon( "desktop-share-alternate.png" ) );
  }
}
#else
void GuiChat::onTickEvent( int )
{}
#endif

void GuiChat::copyUrlToClipboard()
{
  QAction* act = qobject_cast<QAction*>( sender() );
  if( act )
  {
    QString s_url = act->data().toString();
    if( !s_url.isEmpty() )
    {
      QClipboard* cb = qApp->clipboard();
      if( cb )
      {
        cb->setText( s_url);
        emit showStatusMessageRequest( tr( "Url copied to clipboard" ), 3000 );
      }
    }
  }
}

void GuiChat::sendTextCode()
{
  if( Settings::instance().disableSendMessage() )
    return;

  GuiCreateTextFile gct( this );
  gct.show();
  gct.setModal( true );
  gct.setSizeGripEnabled( true );
  if( gct.exec() == QDialog::Accepted )
  {
    QString text_code = gct.text().trimmed();
    if( text_code.isEmpty() )
      return;
    text_code.append( '\n' );
    if( gct.sendAsFile() )
    {
      QString folder_path = Settings::instance().createTextCodeAsTemporaryFile() ? Settings::instance().cacheFolder() : Settings::instance().downloadDirectory();
      QString file_initial_path = folder_path +
                                  QString( "/beecode-%1." ).arg( Bee::dateTimeStringSuffix( QDateTime::currentDateTime() ) ) +
                                  Settings::instance().createTextCodeFileSuffix();
      QString file_path = Bee::uniqueFilePath( file_initial_path, false );
      QFile f( file_path );
      if( f.open( QIODevice::WriteOnly ) )
      {
        QTextStream ts( &f );
        ts << text_code << Qt::endl;
        ts.flush();
        f.close();
        if( Settings::instance().createTextCodeAsTemporaryFile() )
          Settings::instance().addTemporaryFilePath( file_path );
        ensureFocusInChat();
        emit sendFileFromChatRequest( m_chatId, file_path );
      }
      else
      {
        QMessageBox::warning( this, Settings::instance().programName(), tr( "Unable to save temporary file: %1" ).arg( file_path ) );
        ensureFocusInChat();
        return;
      }
    }
    else
    {
      ensureFocusInChat();
      emit newMessage( m_chatId, text_code, true );
    }
  }
  else
  {
    ensureFocusInChat();
  }
}
