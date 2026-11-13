//////////////////////////////////////////////////////////////////////
//
// YongBEEP — WeChat-style chat list model (S3-4a)
//
//////////////////////////////////////////////////////////////////////

#include "ChatListModel.h"
#include "BeeUtils.h"
#include "Chat.h"
#include "ChatManager.h"
#include "ChatMessage.h"
#include "ChatMessageModel.h"
#include "Core.h"
#include "IconManager.h"
#include "Settings.h"
#include "User.h"
#include "UserManager.h"
#include "YongTheme.h"

#include <algorithm>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextDocument>
#include <QTextDocumentFragment>


static QString MessagePlainText( const ChatMessage& cm )
{
  if( cm.isSourceCode() ) return cm.message().simplified();
  QTextDocument doc;
  if( cm.isMarkdown() ) doc.setMarkdown( cm.message() );
  else doc.setHtml( cm.message() );
  return doc.toPlainText().simplified();
}

static QString PreviewWithSender( const QString& text, bool own )
{
  return own ? ChatListModel::tr( "You: " ) + text : text;
}

static QString LastLiveMessageSummary( const Chat& c, QDateTime* timestamp )
{
  *timestamp = QDateTime();
  for( int i = c.messages().size() - 1; i >= 0; --i )
  {
    const ChatMessage& cm = c.messages().at( i );
    if( cm.isHeader() ) continue;
    QString text;
    if( cm.isFileTransfer() )
    {
      const auto link = QRegularExpression( "<a\\b[^>]*href=['\"](?:file|beeshowfileinfolder):[^'\"]*['\"][^>]*>(.*?)</a>",
          QRegularExpression::DotMatchesEverythingOption ).match( cm.message() );
      if( !link.hasMatch() ) continue; // offers and diagnostic lines are not messages
      text = ChatListModel::tr( "[File] %1" ).arg( QTextDocumentFragment::fromHtml( link.captured( 1 ) ).toPlainText() );
      const bool own = cm.message().contains( "<!--yb-file:out:" );
      *timestamp = cm.timestamp();
      return PreviewWithSender( text, own );
    }
    if( cm.isImagePreview() ) text = ChatListModel::tr( "[Image]" );
    else if( cm.isVoice() ) text = ChatListModel::tr( "[Voice]" );
    else if( !cm.isFromSystem() ) text = MessagePlainText( cm );
    else continue;
    if( text.isEmpty() ) continue;
    *timestamp = cm.timestamp();
    return PreviewWithSender( text, cm.isFromLocalUser() );
  }
  return QString();
}

static QDateTime LegacyHistoryTime( const QString& text, const QDateTime& previous )
{
  QDateTime timestamp = QDateTime::fromString( text, "yyyy-MM-dd hh:mm" );
  if( timestamp.isValid() ) return timestamp;
  const QTime time = QTime::fromString( text, "hh:mm" );
  if( time.isValid() )
  {
    // A clock value alone cannot establish a date. Carry forward a known
    // message date, including midnight rollovers between saved batches.
    if( !previous.isValid() ) return QDateTime();
    timestamp = QDateTime( previous.date(), time );
    return time < previous.time() ? timestamp.addDays( 1 ) : timestamp;
  }
  // Date separators without a year use the history file's year.
  QDate reference_date = QFileInfo( Settings::instance().savedChatsFilePath() ).lastModified().date();
  if( !reference_date.isValid() ) reference_date = QDate::currentDate();
  timestamp = QDateTime::fromString( QString( "%1-%2" ).arg( reference_date.year() ).arg( text ), "yyyy-MM-dd hh:mm" );
  if( timestamp.isValid() )
    return timestamp.date() > reference_date ? timestamp.addYears( -1 ) : timestamp;
  return QDateTime();
}

static QString LastHistoryMessageSummary( const Chat& c, QDateTime* timestamp )
{
  *timestamp = QDateTime();
  ChatMessageModel history;
  history.resetFromChat( c, true );
  QString summary;
  QDateTime message_time;
  for( int row = 0; row < history.rowCount(); ++row )
  {
    const ChatMessageItem* item = history.itemAt( row );
    if( item->timestamp.isValid() )
      message_time = item->timestamp;
    else if( !item->timestampText.isEmpty() )
      message_time = LegacyHistoryTime( item->timestampText, message_time );
    if( item->kind != ChatMessageItem::FileCard && item->kind != ChatMessageItem::UserMessage ) continue;
    *timestamp = message_time;
    if( item->kind == ChatMessageItem::FileCard )
      summary = PreviewWithSender( ChatListModel::tr( "[File] %1" ).arg( item->fileName ), item->ownMessage );
    else
      summary = PreviewWithSender( item->plainText.simplified(), item->ownMessage );
  }
  return summary;
}

static QString FormatChatTime( const QDateTime& dt )
{
  if( !dt.isValid() )
    return "";
  QDateTime now = QDateTime::currentDateTime();
  if( dt.date() == now.date() )
    return dt.time().toString( QStringLiteral( "hh:mm" ) );
  if( dt.date() == now.date().addDays( -1 ) )
    return ChatListModel::tr( "Yesterday" );
  if( dt.date().year() == now.date().year() )
    return dt.date().toString( QStringLiteral( "MM-dd" ) );
  return dt.date().toString( QStringLiteral( "yyyy-MM-dd" ) );
}


ChatListModel::ChatListModel( QObject* parent )
  : QAbstractListModel( parent )
{
}

int ChatListModel::rowCount( const QModelIndex& parent ) const
{
  if( parent.isValid() )
    return 0;
  return m_chatIds.size();
}

QVariant ChatListModel::data( const QModelIndex& index, int role ) const
{
  if( !index.isValid() || index.row() < 0 || index.row() >= m_chatIds.size() )
    return QVariant();

  Chat c = ChatManager::instance().chat( m_chatIds.at( index.row() ) );
  if( !c.isValid() )
    return QVariant();

  const SearchResult* hit = !m_filter.isEmpty() && index.row() < m_results.size()
      ? &m_results.at( index.row() ) : Q_NULLPTR;
  if( role == SearchRowRole ) return hit ? hit->messageRow : -1;
  if( role == SearchQueryRole ) return m_filter;
  if( role == SearchKindRole ) return hit ? hit->kind : ContactResult;
  if( hit )
  {
    if( role == Qt::DisplayRole || role == NameRole ) return hit->name;
    if( role == SummaryRole || role == Qt::ToolTipRole ) return hit->summary;
    if( role == TimeTextRole ) return hit->time;
    if( role == UnreadRole ) return 0;
    if( role == TransportsRole && hit->messageRow >= 0 ) return 0;
    if( role == AvatarRole && hit->kind == FileResult ) return hit->icon;
  }

  switch( role )
  {
  case ChatIdRole:
    return QVariant::fromValue<VNumber>( c.id() );
  case Qt::DisplayRole:
  case NameRole:
    return chatDisplayName( c );
  case AvatarRole:
    {
      if( c.isDefault() )
      {
        // S4c: flat WeChat-style bubble tile replaces the glossy legacy icon
        return YongTheme::svgIcon( "chat-bubble", YongTheme::brandGreen(), QColor(), YongTheme::avatarSizePx() );
      }
      User peer = UserManager::instance().findUser( c.privateUserId() );
      if( peer.isValid() )
      {
        return QIcon( Bee::avatarForUser( peer, Settings::instance().avatarIconSize(), Settings::instance().showUserPhoto() ) );
      }
      return IconManager::instance().icon( "chat.png" );
    }
  case SummaryRole:
    return m_summaries.value( c.id() );
  case TimeTextRole:
    return FormatChatTime( m_messageTimes.value( c.id() ) );
  case UnreadRole:
    return c.unreadMessages();
  case TransportsRole:
    return c.isPrivate() && beeCore ? int( beeCore->connectedTransports( c.privateUserId() ) ) : 0;
  default:
    return QVariant();
  }
}

void ChatListModel::reload()
{
  QList<VNumber> new_ids;
  QMap<VNumber, QString> summaries;
  QMap<VNumber, QDateTime> message_times;
  foreach( Chat c, ChatManager::instance().constChatList() )
  {
    if( !c.isValid() || c.isGroup() ) // group chats are out of product scope (PRD 3.4)
      continue;
    new_ids.append( c.id() );
    QDateTime timestamp;
    QString summary = LastLiveMessageSummary( c, &timestamp );
    if( summary.isEmpty() )
    {
      const QString saved = ChatMessageModel::savedHtmlForChat( c );
      if( !m_historySummaries.contains( c.id() ) || m_historySummaries.value( c.id() ).html != saved )
      {
        HistorySummary history;
        history.html = saved;
        if( !saved.isEmpty() ) history.text = LastHistoryMessageSummary( c, &history.timestamp );
        m_historySummaries.insert( c.id(), history );
      }
      const HistorySummary history = m_historySummaries.value( c.id() );
      summary = history.text;
      timestamp = history.timestamp.isValid() ? history.timestamp : c.lastMessageTimestamp();
    }
    summaries.insert( c.id(), summary );
    message_times.insert( c.id(), timestamp );
  }

  std::sort( new_ids.begin(), new_ids.end(), [&message_times]( VNumber id_a, VNumber id_b )
  {
    const QDateTime ts_a = message_times.value( id_a );
    const QDateTime ts_b = message_times.value( id_b );
    if( ts_a != ts_b )
      return ts_a > ts_b; // most recent first
    return id_a < id_b;
  } );

  QList<SearchResult> results;
  if( !m_filter.isEmpty() )
  {
    QList<VNumber> matching_ids;
    for( VNumber id : new_ids )
    {
      const Chat c = ChatManager::instance().chat( id );
      const QString name = chatDisplayName( c );
      if( name.contains( m_filter, Qt::CaseInsensitive ) )
      {
        results.append( { id, -1, ContactResult, name, tr( "Contact" ), QString(), QIcon() } );
        matching_ids.append( id );
      }
      ChatMessageModel messages;
      messages.resetFromChat( c, true );
      QString time;
      for( int row = 0; row < messages.rowCount(); ++row )
      {
        const ChatMessageItem* item = messages.itemAt( row );
        if( item->showTimestamp ) time = item->timestampText;
        if( item->kind != ChatMessageItem::UserMessage && item->kind != ChatMessageItem::FileCard ) continue;
        const QString text = item->kind == ChatMessageItem::FileCard ? item->fileName : item->plainText;
        const int match = text.indexOf( m_filter, 0, Qt::CaseInsensitive );
        if( match < 0 ) continue;
        const bool file = item->kind == ChatMessageItem::FileCard;
        const int start = qMax( 0, match - 18 );
        QString snippet = (start > 0 ? QStringLiteral( "…" ) : QString()) + text.mid( start, 100 );
        results.append( { id, row, file ? FileResult : MessageResult, file ? item->fileName : name,
            file ? tr( "File · %1 · %2" ).arg( name, item->fileStatus ) : tr( "Message · %1" ).arg( snippet ),
            time, item->fileIcon } );
        matching_ids.append( id );
      }
    }
    new_ids = matching_ids;
  }

  beginResetModel();
  m_summaries = summaries;
  m_messageTimes = message_times;
  m_results = results;
  m_chatIds = new_ids;
  endResetModel();
}

void ChatListModel::setFilter( const QString& filter_text )
{
  QString new_filter = filter_text.trimmed();
  if( new_filter == m_filter )
    return;
  m_filter = new_filter;
  reload();
}

VNumber ChatListModel::chatIdAt( int row ) const
{
  if( row < 0 || row >= m_chatIds.size() )
    return ID_INVALID;
  return m_chatIds.at( row );
}

int ChatListModel::rowOfChat( VNumber chat_id ) const
{
  return m_chatIds.indexOf( chat_id );
}

QString ChatListModel::chatDisplayName( const Chat& c ) const
{
  if( c.isDefault() )
    return tr( "All users" );
  if( c.isPrivate() )
  {
    User peer = UserManager::instance().findUser( c.privateUserId() );
    if( peer.isValid() )
      return Bee::userNameToShow( peer, false );
  }
  return c.name();
}
