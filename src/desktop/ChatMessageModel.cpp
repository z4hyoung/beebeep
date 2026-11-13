//////////////////////////////////////////////////////////////////////
//
// YongBEEP — chat message list model (S4e chatview-v2)
//
//////////////////////////////////////////////////////////////////////

#include "ChatMessageModel.h"
#include "BeeUtils.h"
#include "Chat.h"
#include "ChatManager.h"
#include "GuiChatMessage.h"
#include "FileInfo.h"
#include "Protocol.h"
#include "Settings.h"
#include "UserManager.h"
#include "YongTheme.h"

#include <QApplication>
#include <QFileInfo>
#include <QLocale>
#include <QTextDocumentFragment>
#include <QTextOption>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QTextBoundaryFinder>
#include <QTextCursor>
#include <QTextDocument>
#include <QtDebug>


ChatMessageItem::ChatMessageItem()
  : kind( ChatMessageItem::SystemLine ), ownMessage( false ), searchHighlight( false ), showTimestamp( false ), showName( false ),
    markdown( false ), fileAvailable( false ), fileProgressBytes( 0 ), fileProgressTotal( 0 ), fileProgressElapsed( 0 ),
    selectionAnchor( -1 ), selectionPosition( -1 ),
    doc( Q_NULLPTR ), laidOutForWidth( -1.0 )
{
}

ChatMessageItem::~ChatMessageItem()
{
  if( doc )
    delete doc;
}


ChatMessageModel::ChatMessageModel( QObject* parent )
  : QAbstractListModel( parent ), m_chatFont( QApplication::font() ), m_viewportWidth( 480 ),
    m_lastMessageUserId( ID_SYSTEM_MESSAGE ), m_chatIsPrivate( false ), m_isDefaultChat( false )
{
}

ChatMessageModel::~ChatMessageModel()
{
  clearItems();
}

int ChatMessageModel::rowCount( const QModelIndex& parent ) const
{
  if( parent.isValid() )
    return 0;
  return m_items.size();
}

QVariant ChatMessageModel::data( const QModelIndex& index, int role ) const
{
  ChatMessageItem* item = itemForIndex( index );
  if( !item )
    return QVariant();
  if( role == Qt::DisplayRole )
    return item->plainText;
  if( role == Qt::ToolTipRole && item->kind == ChatMessageItem::FileCard )
    return item->fileName + QLatin1Char( '\n' ) + item->fileStatus;
  return QVariant();
}

ChatMessageItem* ChatMessageModel::itemAt( int row ) const
{
  return ( row >= 0 && row < m_items.size() ) ? m_items.at( row ) : Q_NULLPTR;
}

ChatMessageItem* ChatMessageModel::itemForIndex( const QModelIndex& index ) const
{
  return index.isValid() ? itemAt( index.row() ) : Q_NULLPTR;
}

void ChatMessageModel::clearItems()
{
  qDeleteAll( m_items );
  m_items.clear();
}

bool ChatMessageModel::messageCanBeShowed( const ChatMessage& cm ) const
{
  // The contact is already named in the chat header.
  if( cm.isHeader() )
    return false;
  // same filter as the old GuiChat::messageCanBeShowed
  if( m_isDefaultChat && Settings::instance().showOnlyMessagesInDefaultChat() )
    return cm.isChatActivity() && !Settings::instance().chatMessageFilter().testBit( static_cast<int>( cm.type() ) );
  return !Settings::instance().chatMessageFilter().testBit( static_cast<int>( cm.type() ) );
}

void ChatMessageModel::setChatFont( const QFont& f )
{
  m_chatFont = f;
  // invalidate cached documents so they are rebuilt with the new font
  foreach( ChatMessageItem* item, m_items )
  {
    if( item->doc )
    {
      delete item->doc;
      item->doc = Q_NULLPTR;
    }
    item->laidOutForWidth = -1.0;
  }
}

void ChatMessageModel::setViewportWidth( int new_width )
{
  if( new_width <= 0 || new_width == m_viewportWidth )
    return;
  m_viewportWidth = new_width;
  foreach( ChatMessageItem* item, m_items )
    item->laidOutForWidth = -1.0; // relayout shrink-to-fit against the new width
  // both signals: QListView re-queries the size hints (rows shrink/grow)
  // and repaints every row
  emit layoutChanged();
  if( !m_items.isEmpty() )
    emit dataChanged( index( 0, 0 ), index( m_items.size() - 1, 0 ) );
}

QString ChatMessageModel::fileSizeText( FileSizeType bytes )
{
  return QLocale::system().formattedDataSize( bytes, 0, QLocale::DataSizeIecFormat );
}

qreal ChatMessageModel::maxBubbleWidth() const
{
  // S4e spec: bubbles shrink-wrap their content, capped at 62% of the chat area
  return qMax<qreal>( 60.0, qMin<qreal>( 560.0, qMin<qreal>( m_viewportWidth * 0.62,
      m_viewportWidth - 2 * HorizontalMargin - YongTheme::avatarSizePx() - AvatarGap ) ) );
}

QColor ChatMessageModel::bubbleTextColor() const
{
  if( Settings::instance().useDarkStyle() )
    return QColor( Settings::instance().chatDefaultTextColor() );
  return YongTheme::textPrimary();
}

// ---------------------------------------------------------------------
// Markdown registry (GUI-side flag plumbing; see file header)
// ---------------------------------------------------------------------

void ChatMessageModel::addMarkdownText( const QString& raw_text )
{
  if( raw_text.isEmpty() )
    return;
  QString html_form = Protocol::instance().formatHtmlText( raw_text );
  if( html_form.endsWith( QLatin1String( "<br>" ) ) )
    html_form.chop( 4 );
  m_markdownTexts.prepend( qMakePair( raw_text, html_form ) );
  while( m_markdownTexts.size() > MaxMarkdownTexts )
    m_markdownTexts.removeLast();
}

void ChatMessageModel::registerMarkdownTexts( const QStringList& raw_texts )
{
  foreach( const QString& raw_text, raw_texts )
    addMarkdownText( raw_text );
}

bool ChatMessageModel::matchMarkdownRegistry( const ChatMessage& cm, QString* raw_out ) const
{
  if( m_markdownTexts.isEmpty() || cm.isFromSystem() || cm.isSourceCode() || !cm.isFromLocalUser() )
    return false;
  for( int i = 0; i < m_markdownTexts.size(); i++ )
  {
    if( m_markdownTexts.at( i ).second == cm.message() )
    {
      if( raw_out )
        *raw_out = m_markdownTexts.at( i ).first;
      return true;
    }
  }
  return false;
}

// ---------------------------------------------------------------------
// Item building (reuses the classic rich-text content pipeline)
// ---------------------------------------------------------------------

QString ChatMessageModel::plainTextFromHtml( const QString& html ) const
{
  QString plain_text = Bee::removeHtmlTags( html );
  plain_text.replace( QLatin1String( "&nbsp;" ), QChar( ' ' ) );
  plain_text.replace( QLatin1String( "&lt;" ), QLatin1String( "<" ) );
  plain_text.replace( QLatin1String( "&gt;" ), QLatin1String( ">" ) );
  plain_text.replace( QLatin1String( "&amp;" ), QLatin1String( "&" ) );
  plain_text.replace( QLatin1String( "&quot;" ), QLatin1String( "\"" ) );
  plain_text.replace( QChar( '\n' ), QLatin1String( " " ) );
  return plain_text.simplified();
}

QPixmap ChatMessageModel::roundedAvatar( const User& u ) const
{
  const int render_size = YongTheme::avatarSizePx() * 2; // 2x for retina
  QPixmap avatar_pixmap = Bee::avatarForUser( u, QSize( render_size, render_size ), true );
  if( avatar_pixmap.isNull() )
    return QPixmap();
  QPixmap rounded_pixmap( render_size, render_size );
  rounded_pixmap.fill( Qt::transparent );
  QPainter painter( &rounded_pixmap );
  QPainterPath clip_path;
  clip_path.addRoundedRect( 0, 0, render_size, render_size,
                            YongTheme::avatarRadiusPx() * 2.0, YongTheme::avatarRadiusPx() * 2.0 );
  painter.setClipPath( clip_path );
  painter.drawPixmap( 0, 0, avatar_pixmap );
  painter.end();
  rounded_pixmap.setDevicePixelRatio( 2.0 );
  return rounded_pixmap;
}

QString ChatMessageModel::timestampTextFor( const ChatMessage& cm ) const
{
  // WeChat-like separator text: hh:mm on the same day, date + hh:mm before
  // (same rules as the old GuiChatMessage::formatMessage S4c logic)
  QDate msg_date = cm.timestamp().date();
  QString date_time_stamp = cm.timestamp().time().toString( QStringLiteral( "hh:mm" ) );
  if( msg_date != QDate::currentDate() )
  {
    QString date_part = msg_date.year() == QDate::currentDate().year() ? msg_date.toString( QStringLiteral( "MM-dd" ) )
                                                                       : msg_date.toString( QStringLiteral( "yyyy-MM-dd" ) );
    date_time_stamp.prepend( date_part + QLatin1Char( ' ' ) );
  }
  return date_time_stamp;
}

static QString senderName( const User& u, const ChatMessage& message )
{
  QString name = Bee::userNameToShow( u, false );
  if( message.isFromAutoresponder() )
    name = QString( "%1 (%2)" ).arg( name, Settings::instance().autoresponderName() );
  return name;
}

void ChatMessageModel::updateUserProfile( const User& u )
{
  int first = -1;
  int last = -1;
  QPixmap avatar;
  for( int row = 0; row < m_items.size(); ++row )
  {
    ChatMessageItem* item = m_items.at( row );
    if( (item->kind != ChatMessageItem::UserMessage && item->kind != ChatMessageItem::FileCard) || item->user.id() != u.id() )
      continue;
    if( item->user.vCard() == u.vCard() && item->user.color() == u.color() )
      continue;
    if( first < 0 )
    {
      first = row;
      avatar = roundedAvatar( u );
    }
    item->user = u;
    item->avatar = avatar;
    if( item->showName )
      item->nameText = senderName( u, item->message );
    last = row;
  }
  if( first >= 0 )
    emit dataChanged( index( first, 0 ), index( last, 0 ) );
}

ChatMessageItem* ChatMessageModel::createItem( const Chat& c, const ChatMessage& cm )
{
  ChatMessageItem* item = new ChatMessageItem();
  item->message = cm;

  if( cm.isFromSystem() )
  {
    item->ownMessage = false;

    if( cm.isFileTransfer() )
    {
      // WeChat file card: parse the <a href> link from the system message
      // (same parsing as the old GuiChatMessage fileTransferCardHtml)
      static const QRegularExpression link_rx( QStringLiteral( "<a\\b[^>]*href=['\"]([^'\"]+)['\"][^>]*>(.*?)</a>" ), QRegularExpression::DotMatchesEverythingOption );
      QRegularExpressionMatch match = link_rx.match( cm.message() );
      if( match.hasMatch() && QUrl( match.captured( 1 ) ).isLocalFile() )
      {
        item->kind = ChatMessageItem::FileCard;
        item->fileUrl = QTextDocumentFragment::fromHtml( match.captured( 1 ) ).toPlainText();
        item->fileName = plainTextFromHtml( match.captured( 2 ) );
        const QFileInfo local_file( QUrl( item->fileUrl ).toLocalFile() );
        item->fileAvailable = local_file.isFile() && !cm.message().contains( "<!--live-->" );
        QUrl folder_url( item->fileUrl );
        folder_url.setScheme( FileInfo::urlSchemeShowFileInFolder() );
        item->fileFolderUrl = folder_url.toString();
        const auto metadata = QRegularExpression( "<!--yb-file:(in|out):(pending|complete|failed):(\\d+)-->" ).match( cm.message() );
        item->ownMessage = metadata.hasMatch() ? metadata.captured( 1 ) == "out"
            : cm.message().contains( QRegularExpression( "Sending |Sent |Upload |发送|已发送" ) );
        item->user = UserManager::instance().findUser( item->ownMessage ? ID_LOCAL_USER : c.privateUserId() );
        item->avatar = roundedAvatar( item->user );
        if( metadata.hasMatch() )
        {
          item->fileProgressTotal = metadata.captured( 3 ).toLongLong();
          item->fileAvailable = metadata.captured( 2 ) == "complete" && local_file.isFile();
          if( metadata.captured( 2 ) != "pending" )
            item->fileProgressBytes = item->fileProgressTotal;
        }
        QString rest_text = cm.message();
        rest_text.remove( match.capturedStart(), match.capturedLength() );
        rest_text.remove( QRegularExpression( QStringLiteral( "<img[^>]*>" ) ) );
        rest_text.remove( QRegularExpression( QStringLiteral( "<!--.*?-->" ) ) ); // YongBEEP live-card rewrite marker
        // The full transfer sentence belongs in the tooltip, not the card.
        item->fileStatus = plainTextFromHtml( rest_text );
        if( item->fileAvailable )
        {
          item->fileProgressBytes = item->fileProgressTotal;
          item->fileStatus = tr( "%1 · %2" ).arg( fileSizeText( local_file.size() ), item->ownMessage ? tr( "Sent" ) : tr( "Received" ) );
        }
        else if( cm.message().contains( "<!--live-->" ) )
          item->fileStatus = item->ownMessage ? tr( "Sending…" ) : tr( "Receiving…" );
        item->fileIcon = YongTheme::svgIcon( YongTheme::fileIconNameForFile( item->fileName ), YongTheme::iconGray() );
        item->plainText = item->fileName + QLatin1Char( ' ' ) + item->fileStatus;
        item->showTimestamp = !m_lastMessageTimestamp.isValid() ||
                              m_lastMessageTimestamp.secsTo( cm.timestamp() ) >= 300;
        if( item->showTimestamp )
          item->timestampText = timestampTextFor( cm );
        m_lastMessageUserId = ID_SYSTEM_MESSAGE;
        m_lastMessageTimestamp = cm.timestamp();
        return item;
      }
    }

    if( cm.isImagePreview() )
    {
      // arrives already wrapped in <div align=center><a><img></a></div>
      item->kind = ChatMessageItem::FreeFormDoc;
      item->contentHtml = cm.message();
      item->plainText = plainTextFromHtml( cm.message() );
      m_lastMessageUserId = ID_SYSTEM_MESSAGE;
      m_lastMessageTimestamp = cm.timestamp();
      return item;
    }

    item->kind = ChatMessageItem::SystemLine;
    if( cm.type() == ChatMessage::FileTransfer || cm.type() == ChatMessage::Voice )
    {
      // voice lines and unparsed file transfers: centered clickable block
      item->contentHtml = QString( "<div align=center>&nbsp;&#128206;&nbsp;%1%2&nbsp;</div>" )
                            .arg( cm.isImportant() ? QString( "<font color=red><b>!!</b></font> " ) : QString(),
                                  cm.message() );
    }
    else
    {
      QString date_time_stamp = GuiChatMessage::datetimestampToString( cm, Settings::instance().chatShowMessageTimestamp(),
                                                                        Settings::instance().chatShowMessageDatestamp() );
      QString text = QString( "%1%2" ).arg( date_time_stamp.isEmpty() ? QString() : QString( "(%1) " ).arg( date_time_stamp ),
                                            cm.message() );
      if( cm.isImportant() )
        text.prepend( QString( "<font color=red><b>!!</b></font> " ) );
      item->contentHtml = QString( "<div align=center>%1</div>" ).arg( text );
    }
    item->plainText = plainTextFromHtml( item->contentHtml );
    m_lastMessageUserId = ID_SYSTEM_MESSAGE;
    m_lastMessageTimestamp = cm.timestamp();
    return item;
  }

  User u = UserManager::instance().findUser( cm.userId() );
  if( !u.isValid() )
  {
    qWarning() << "User" << cm.userId() << "not found for message:" << qPrintable( cm.message() );
    delete item;
    return Q_NULLPTR;
  }

  item->kind = ChatMessageItem::UserMessage;
  item->user = u;
  item->ownMessage = u.isValid() && u.isLocal();
  item->avatar = roundedAvatar( u );

  // Markdown: the wire flag is authoritative (works for remote peers, history
  // reloads and cross-session); the GUI-side registry stays only as a fallback
  // for messages sent before the flag existed.
  QString md_raw;
  if( cm.isMarkdown() )
  {
    item->markdown = true;
    item->markdownRaw = cm.message(); // raw text preserved by createFromMessage()
    item->plainText = cm.message();
  }
  else if( matchMarkdownRegistry( cm, &md_raw ) )
  {
    item->markdown = true;
    item->markdownRaw = md_raw;
    item->plainText = md_raw;
  }

  // Centered gray time separator: first shown message, >=5 min gap or day
  // change (same rules as the old S4c HTML pipeline)
  bool day_changed = m_lastMessageTimestamp.isValid() && m_lastMessageTimestamp.date() != cm.timestamp().date();
  item->showTimestamp = Settings::instance().chatShowMessageTimestamp() &&
                        ( !m_lastMessageTimestamp.isValid() || day_changed || m_lastMessageTimestamp.secsTo( cm.timestamp() ) >= 300 );
  if( item->showTimestamp )
    item->timestampText = timestampTextFor( cm );

  // Sender name: peers only, never in 1:1 chats, never on grouped messages
  bool append_to_previous = Settings::instance().showMessagesGroupByUser() && m_lastMessageUserId == u.id() && !cm.isImportant();
  item->showName = !item->ownMessage && !m_chatIsPrivate && !append_to_previous;
  if( item->showName )
    item->nameText = senderName( u, cm );

  // Classic rich-text content pipeline (emoticons/links/file html are
  // already inside cm.message(); SourceCode keeps its <code> block)
  if( !item->markdown )
  {
    QString text_formatted = cm.message();
    if( cm.isSourceCode() )
    {
      text_formatted.replace( QChar( '&' ), QLatin1String( "&amp;" ) );
      text_formatted.replace( QChar( '<' ), QLatin1String( "&lt;" ) );
      text_formatted.replace( QChar( '>' ), QLatin1String( "&gt;" ) );
      text_formatted.prepend( "\n<code>" );
      text_formatted.append( "\n</code>\n" );
    }
    text_formatted.replace( QString( "  " ), QLatin1String( "&nbsp;&nbsp;" ) );
    text_formatted.replace( QChar( '\t' ), QLatin1String( "&nbsp;&nbsp;&nbsp;&nbsp;" ) );
    text_formatted.replace( QChar( '\r' ), QLatin1String( "" ) );
    text_formatted.replace( QChar( '\n' ), QLatin1String( "<br>" ) );
    if( !cm.isSourceCode() )
    {
      QColor text_color = (cm.textColor().isValid() && cm.textColor() != QColor( 0, 0, 0 ) && cm.textColor() != QColor( 255, 255, 255 ) ) ? cm.textColor() : QColor();
      if( text_color.isValid() && Bee::isColorVisibleInChat( text_color ) )
        text_formatted = QString( "<font color=%1>%2</font>" ).arg( text_color.name(), text_formatted );
    }
    if( cm.isImportant() )
      text_formatted.prepend( QString( "<font color=red><b>!!</b></font> " ) );
    item->contentHtml = text_formatted;
    item->plainText = plainTextFromHtml( text_formatted );
  }

  m_lastMessageUserId = cm.isImportant() ? ID_IMPORTANT_MESSAGE : cm.userId();
  m_lastMessageTimestamp = cm.timestamp();
  return item;
}

void ChatMessageModel::appendBuiltItem( ChatMessageItem* item )
{
  beginInsertRows( QModelIndex(), m_items.size(), m_items.size() );
  m_items.append( item );
  endInsertRows();
}

bool ChatMessageModel::appendMessage( const Chat& c, const ChatMessage& cm )
{
  if( !messageCanBeShowed( cm ) )
    return false;
  m_chatIsPrivate = c.isPrivate();
  m_isDefaultChat = c.isDefault();
  ChatMessageItem* item = createItem( c, cm );
  if( !item )
    return false;
  appendBuiltItem( item );
  return true;
}

void ChatMessageModel::updateFileProgress( const QString& file_name, FileSizeType bytes, FileSizeType total, qint64 elapsed )
{
  // YongBEEP: match the NEWEST card (scan backwards) by name plus total size:
  // the size pins the match to this transfer, so two files sharing a name
  // (or a retransfer of the same name with a different size) never cross-wire
  // their progress. Cards still waiting for the first progress callback carry
  // total == 0 and accept any total once.
  for( int i = m_items.size() - 1; i >= 0; --i )
  {
    ChatMessageItem* item = m_items.at( i );
    if( item->kind != ChatMessageItem::FileCard || item->fileName != file_name )
      continue;
    if( item->fileProgressTotal > 0 && item->fileProgressTotal != total )
      continue; // same name, different transfer
    if( item->fileProgressBytes == bytes && item->fileProgressTotal == total )
      return;
    item->fileProgressBytes = bytes;
    item->fileProgressTotal = total;
    item->fileProgressElapsed = elapsed;
    emit dataChanged( index( i, 0 ), index( i, 0 ) );
    return;
  }
}

void ChatMessageModel::updateFileStatus( const QString& file_name, FileSizeType total, const QString& status, const QString& file_path, bool completed )
{
  // YongBEEP: same matching rules of updateFileProgress - newest card first,
  // name plus total size pins the match to this transfer.
  for( int i = m_items.size() - 1; i >= 0; --i )
  {
    ChatMessageItem* item = m_items.at( i );
    if( item->kind != ChatMessageItem::FileCard || item->fileName != file_name )
      continue;
    if( item->fileProgressTotal > 0 && item->fileProgressTotal != total )
      continue; // same name, different transfer
    item->fileStatus = status;
    if( !file_path.isEmpty() )
    {
      item->fileUrl = QUrl::fromLocalFile( file_path ).toString();
      QUrl folder_url( item->fileUrl );
      folder_url.setScheme( FileInfo::urlSchemeShowFileInFolder() );
      item->fileFolderUrl = folder_url.toString();
    }
    item->fileAvailable = completed && QFileInfo( QUrl( item->fileUrl ).toLocalFile() ).isFile();
    if( item->fileAvailable )
      item->fileStatus = tr( "%1 · %2" ).arg( fileSizeText( total ), item->ownMessage ? tr( "Sent" ) : tr( "Received" ) );
    item->fileProgressBytes = total; // finalize: the delegate stops the live bar
    emit dataChanged( index( i, 0 ), index( i, 0 ) );
    return;
  }
}

void ChatMessageModel::highlightSearchResult( int row, const QString& query )
{
  clearAllSelections();
  ChatMessageItem* item = itemAt( row );
  if( !item ) return;
  item->searchHighlight = true;
  if( item->kind == ChatMessageItem::UserMessage )
  {
    ensureLayout( item );
    const int start = item->doc->toPlainText().indexOf( query, 0, Qt::CaseInsensitive );
    if( start >= 0 ) setItemSelection( row, start, start + query.size() );
  }
  emit dataChanged( index( row, 0 ), index( row, 0 ) );
}

void ChatMessageModel::setItemSelection( int row, int anchor, int position )
{
  ChatMessageItem* item = itemAt( row );
  if( !item )
    return;
  item->selectionAnchor = anchor;
  item->selectionPosition = position;
  emit dataChanged( index( row, 0 ), index( row, 0 ) );
}

void ChatMessageModel::clearAllSelections()
{
  for( int i = 0; i < m_items.size(); ++i )
  {
    ChatMessageItem* item = m_items.at( i );
    if( item->selectionAnchor >= 0 || item->searchHighlight )
    {
      item->searchHighlight = false;
      item->selectionAnchor = -1;
      item->selectionPosition = -1;
      emit dataChanged( index( i, 0 ), index( i, 0 ) );
    }
  }
}

void ChatMessageModel::selectWordAt( int row, int position )
{
  ChatMessageItem* item = itemAt( row );
  if( !item )
    return;
  ensureLayout( item );
  if( !item->doc )
    return;
  const QString plain = item->doc->toPlainText();
  if( position < 0 || position >= plain.size() )
    return;
  QTextBoundaryFinder finder( QTextBoundaryFinder::Word, plain );
  finder.setPosition( position );
  int start = finder.toPreviousBoundary();
  if( start < 0 )
    start = 0;
  int end = finder.toNextBoundary();
  if( end < 0 )
    end = plain.size();
  if( end > start )
    setItemSelection( row, start, end );
}

bool ChatMessageModel::hasSelection( int row ) const
{
  ChatMessageItem* item = itemAt( row );
  return item && item->selectionAnchor >= 0 && item->selectionPosition != item->selectionAnchor;
}

QString ChatMessageModel::selectedText( int row ) const
{
  ChatMessageItem* item = itemAt( row );
  if( !item || !hasSelection( row ) )
    return QString();
  ensureLayout( item );
  if( !item->doc )
    return QString();
  const QString plain = item->doc->toPlainText();
  int start = qMin( item->selectionAnchor, item->selectionPosition );
  int end = qMax( item->selectionAnchor, item->selectionPosition );
  start = qBound( 0, start, plain.size() );
  end = qBound( 0, end, plain.size() );
  return plain.mid( start, end - start );
}

QString ChatMessageModel::savedHtmlForChat( const Chat& c )
{
  if( ChatManager::instance().chatHasSavedText( c.name() ) )
    return ChatManager::instance().chatSavedText( c.name() );
  if( c.isPrivate() && c.name().contains( "@" ) )
    return ChatManager::instance().chatSavedText( User::nameFromPath( c.name() ) );
  return QString();
}

void ChatMessageModel::resetFromChat( const Chat& c, bool all_history )
{
  beginResetModel();
  clearItems();
  m_chatIsPrivate = c.isPrivate();
  m_isDefaultChat = c.isDefault();
  m_lastMessageTimestamp = QDateTime();
  m_lastMessageUserId = ID_SYSTEM_MESSAGE;

  int missed_lines = -1;
  int num_messages = c.messages().size();
  int msg_lines = !all_history && Settings::instance().chatMessagesToShow() >= 0 ? qMin( num_messages, Settings::instance().chatMessagesToShow() ) : num_messages;
  int history_lines = !all_history && Settings::instance().chatMessagesToShow() >= 0 ? qMax( 0, Settings::instance().chatMessagesToShow() - msg_lines ) : -1;

  bool history_shown = ChatManager::instance().isLoadHistoryCompleted() &&
                       (all_history || !Settings::instance().chatMessageFilter().testBit( static_cast<int>( ChatMessage::History ) )) &&
                       history_lines != 0;

  QList<ChatMessageItem*> new_items;
  QList<ChatMessageItem*> blob_items;

  // Recover individual message items from the legacy saved HTML.
  if( history_shown )
  {
    const QString saved_html = savedHtmlForChat( c );
    if( !saved_html.isEmpty() )
    {
      blob_items = itemsFromSavedHtml( c, saved_html );
      if( history_lines >= 0 && blob_items.size() > history_lines )
      {
        missed_lines = blob_items.size() - history_lines;
        while( blob_items.size() > history_lines )
          delete blob_items.takeFirst();
      }
    }
  }

  if( missed_lines < 0 )
    missed_lines = qMax( 0, num_messages - msg_lines );

  // Limit-reached notice ("only the last N messages are shown") - keeps the
  // beeshowsavedchat link, which the delegate routes to openUrl
  if( !all_history && Settings::instance().chatMessagesToShow() >= 0 && (missed_lines > 0 || num_messages > msg_lines) )
  {
    QUrl saved_chat_url;
    saved_chat_url.setUrl( QString( "#%1" ).arg( c.id() ) );
    saved_chat_url.setScheme( "beeshowsavedchat" );
    ChatMessageItem* limit_item = new ChatMessageItem();
    limit_item->kind = ChatMessageItem::SystemLine;
    limit_item->contentHtml = QString( "<div align=center><i>... %1 ...<br>... %2, %3 ...</i></div>" )
                                .arg( tr( "only the last %1 messages are shown" ).arg( Settings::instance().chatMessagesToShow() ),
                                      tr( "maximum number of messages to show reached" ),
                                      tr( "open %1 to read them all" ).arg( QString( "<a href='%1'>%2</a>" ).arg( saved_chat_url.toString(), tr( "the saved chat" ) ) ) );
    limit_item->plainText = plainTextFromHtml( limit_item->contentHtml );
    new_items.append( limit_item );
  }

  new_items.append( blob_items );

  foreach( ChatMessage cm, c.messages() )
  {
    if( num_messages > msg_lines )
    {
      num_messages--;
      continue;
    }
    if( cm.isHeader() || (!all_history && !messageCanBeShowed( cm )) )
      continue;
    ChatMessageItem* item = createItem( c, cm );
    if( item )
      new_items.append( item );
  }

  m_items = new_items;
  endResetModel();
}

// Old history stores positioning tables rather than message objects. Recover
// each bubble/card so history uses the same layout and hit testing as live chat.
QList<ChatMessageItem*> ChatMessageModel::itemsFromSavedHtml( const Chat& c, const QString& html )
{
  QList<ChatMessageItem*> result;
  QString pending_timestamp;
  const QRegularExpression table_rx( "<table\\b[^>]*>.*?</table>", QRegularExpression::DotMatchesEverythingOption );
  const QRegularExpression cell_rx( "<td\\b[^>]*>(.*?)</td>", QRegularExpression::DotMatchesEverythingOption );
  const QRegularExpression bubble_rx( "<span style=\"background-color:[^\"]*\">(.*)</span>", QRegularExpression::DotMatchesEverythingOption );
  auto append_other = [&]( const QString& fragment )
  {
    QString text = plainTextFromHtml( fragment );
    if( text.isEmpty() )
      return;
    if( QRegularExpression( "^(?:(?:\\d{4}-)?\\d{2}-\\d{2} )?\\d{2}:\\d{2}$" ).match( text ).hasMatch() )
    {
      pending_timestamp = text;
      return;
    }
    ChatMessageItem* line = new ChatMessageItem();
    line->kind = ChatMessageItem::FreeFormDoc;
    line->contentHtml = fragment;
    line->plainText = text;
    result.append( line );
  };
  int end = 0;
  auto tables = table_rx.globalMatch( html );
  while( tables.hasNext() )
  {
    const auto table = tables.next();
    append_other( html.mid( end, table.capturedStart() - end ) );
    end = table.capturedEnd();
    QStringList cells;
    auto matches = cell_rx.globalMatch( table.captured() );
    while( matches.hasNext() ) cells.append( matches.next().captured( 1 ) );
    ChatMessageItem* item = Q_NULLPTR;
    if( cells.size() == 4 )
    {
      const bool own = cells.first().trimmed().isEmpty();
      const QString content = cells.at( own ? 1 : 2 );
      const auto bubble = bubble_rx.match( content );
      if( bubble.hasMatch() )
      {
        item = new ChatMessageItem();
        item->kind = ChatMessageItem::UserMessage;
        item->ownMessage = own;
        item->user = UserManager::instance().findUser( own ? ID_LOCAL_USER : c.privateUserId() );
        item->avatar = roundedAvatar( item->user );
        const auto avatar = QRegularExpression( "src=['\"]([^'\"]+)" ).match( cells.at( own ? 3 : 0 ) );
        if( !item->user.isValid() && avatar.hasMatch() )
        {
          QPixmap saved_avatar( QUrl( avatar.captured( 1 ) ).toLocalFile() );
          if( !saved_avatar.isNull() )
          {
            saved_avatar.setDevicePixelRatio( qreal( saved_avatar.width() ) / YongTheme::avatarSizePx() );
            item->avatar = saved_avatar;
          }
        }
        item->contentHtml = bubble.captured( 1 ).trimmed();
        // Strip only the padding inserted by the old serializer.
        if( item->contentHtml.startsWith( "&nbsp;" ) ) item->contentHtml.remove( 0, 6 );
        if( item->contentHtml.endsWith( "&nbsp;" ) ) item->contentHtml.chop( 6 );
        item->plainText = plainTextFromHtml( item->contentHtml );
        item->nameText = plainTextFromHtml( content.left( bubble.capturedStart() ) );
        item->showName = !m_chatIsPrivate && !own && !item->nameText.isEmpty();
      }
    }
    else if( cells.size() == 2 )
    {
      Message m( Message::System, ID_SYSTEM_MESSAGE, table.captured() );
      item = createItem( c, ChatMessage( ID_SYSTEM_MESSAGE, m, ChatMessage::FileTransfer, false ) );
    }
    if( !item )
      append_other( table.captured() );
    else
    {
      item->showTimestamp = !pending_timestamp.isEmpty();
      item->timestampText = pending_timestamp;
      pending_timestamp.clear();
      result.append( item );
    }
  }
  append_other( html.mid( end ) );
  return result;
}

// ---------------------------------------------------------------------
// Document cache + shrink-to-fit layout
// ---------------------------------------------------------------------

void ChatMessageModel::buildDocument( ChatMessageItem* item ) const
{
  QTextDocument* doc = new QTextDocument();
  doc->setDefaultFont( m_chatFont );
  doc->setDocumentMargin( 0 );
  QTextOption text_option = doc->defaultTextOption();
  text_option.setWrapMode( QTextOption::WrapAtWordBoundaryOrAnywhere );
  doc->setDefaultTextOption( text_option );

  if( item->kind == ChatMessageItem::SystemLine )
  {
    QFont aux_font = m_chatFont;
    aux_font.setPixelSize( Settings::instance().useDarkStyle() ? YongTheme::fontAuxPx() : YongTheme::fontTimestampPx() + 1 );
    doc->setDefaultFont( aux_font );
    QColor line_color = Settings::instance().useDarkStyle() ? QColor( Settings::instance().chatSystemTextColor() ) : YongTheme::textSecondary();
    doc->setDefaultStyleSheet( QStringLiteral( "body { color: %1; } a { color: %2; } img { vertical-align: middle; }" )
                                 .arg( line_color.name(), YongTheme::linkBlue().name() ) );
    doc->setHtml( item->contentHtml );
  }
  else if( item->markdown )
  {
    doc->setMarkdown( item->markdownRaw );
    // defaultStyleSheet does not apply to setMarkdown: recolor via a
    // document-wide merge so bold/lists/code keep their formats
    QTextCursor cursor( doc );
    cursor.select( QTextCursor::Document );
    QTextCharFormat format;
    format.setForeground( bubbleTextColor() );
    cursor.mergeCharFormat( format );
  }
  else
  {
    QColor text_color = bubbleTextColor();
    doc->setDefaultStyleSheet( QStringLiteral( "body { color: %1; } a { color: %2; } img { vertical-align: middle; }" )
                                 .arg( text_color.name(), YongTheme::linkBlue().name() ) );
    doc->setHtml( item->contentHtml );
  }

  item->doc = doc;
  item->laidOutForWidth = -1.0;
}

void ChatMessageModel::ensureLayout( ChatMessageItem* item ) const
{
  if( !item )
    return;
  if( !item->doc )
    buildDocument( item );

  qreal max_content_width = 0.0;
  if( item->kind == ChatMessageItem::UserMessage )
    max_content_width = maxBubbleWidth() - 2.0 * BubbleHPad;
  else if( item->kind == ChatMessageItem::FileCard )
    max_content_width = 0.0; // painted by hand, no document
  else
    max_content_width = qMax<qreal>( 40.0, m_viewportWidth - 2.0 * HorizontalMargin );

  if( item->laidOutForWidth == max_content_width )
    return;

  item->doc->setTextWidth( -1.0 );
  qreal ideal_width = item->doc->idealWidth();
  qreal text_width = item->kind == ChatMessageItem::UserMessage
      ? qBound<qreal>( 1.0, ideal_width, max_content_width ) : max_content_width;
  item->doc->setTextWidth( text_width );
  item->docSize = item->doc->size();
  item->laidOutForWidth = max_content_width;
}

int ChatMessageModel::findPlainText( const QString& txt, int from_row, bool search_forward ) const
{
  if( txt.isEmpty() )
    return -1;
  if( m_items.isEmpty() )
    return -1;
  if( from_row < 0 )
    from_row = search_forward ? 0 : m_items.size() - 1;
  int count = m_items.size();
  for( int i = 0; i < count; i++ )
  {
    int step = search_forward ? i : -i;
    int row = ( ( from_row + step ) % count + count ) % count;
    ChatMessageItem* item = m_items.at( row );
    if( item && item->plainText.contains( txt, Qt::CaseInsensitive ) )
      return row;
  }
  return -1;
}
