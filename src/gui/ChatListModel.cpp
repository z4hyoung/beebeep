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
#include "Core.h"
#include "IconManager.h"
#include "Settings.h"
#include "User.h"
#include "UserManager.h"
#include "YongTheme.h"

#include <algorithm>


static QString LastMessageSummary( const Chat& c )
{
  if( c.messages().isEmpty() )
    return "";
  // WeChat shows the last real chat message, not the trailing system line
  // (S4c); fall back to the very last message for chats with system only.
  const ChatMessage* cm = Q_NULLPTR;
  for( int i = c.messages().size() - 1; i >= 0; i-- )
  {
    if( !c.messages().at( i ).isFromSystem() )
    {
      cm = &c.messages().at( i );
      break;
    }
  }
  if( !cm )
    cm = &c.messages().last();
  QString txt = Bee::removeHtmlTags( cm->message() );
  txt.replace( QLatin1Char( '\n' ), QLatin1Char( ' ' ) );
  txt = txt.simplified();
  // WeChat conversation list prefixes own messages with "You: " (S4c)
  if( cm->isFromLocalUser() )
    txt.prepend( ChatListModel::tr( "You: " ) );
  return txt;
}

static QDateTime ChatActivityTime( const Chat& c )
{
  QDateTime dt = c.lastMessageTimestamp();
  if( !c.messages().isEmpty() )
  {
    const QDateTime& last_msg_ts = c.messages().last().timestamp();
    if( last_msg_ts.isValid() && ( !dt.isValid() || last_msg_ts > dt ) )
      dt = last_msg_ts;
  }
  return dt;
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
        QIcon avatar = Bee::avatarForUser( peer, Settings::instance().avatarIconSize(), Settings::instance().showUserPhoto() );
        if( !Settings::instance().localUser().isStatusConnected() || !peer.isStatusConnected() )
          avatar = Bee::convertToGrayScale( avatar, Settings::instance().avatarIconSize() );
        return avatar;
      }
      return IconManager::instance().icon( "chat.png" );
    }
  case SummaryRole:
    return LastMessageSummary( c );
  case TimeTextRole:
    return FormatChatTime( ChatActivityTime( c ) );
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
  foreach( Chat c, ChatManager::instance().constChatList() )
  {
    if( !c.isValid() || c.isGroup() ) // group chats are out of product scope (PRD 3.4)
      continue;
    if( !m_filter.isEmpty() && !chatDisplayName( c ).contains( m_filter, Qt::CaseInsensitive ) )
      continue;
    new_ids.append( c.id() );
  }

  std::sort( new_ids.begin(), new_ids.end(), []( VNumber id_a, VNumber id_b )
  {
    QDateTime ts_a = ChatActivityTime( ChatManager::instance().chat( id_a ) );
    QDateTime ts_b = ChatActivityTime( ChatManager::instance().chat( id_b ) );
    if( ts_a != ts_b )
      return ts_a > ts_b; // most recent first
    return id_a < id_b;
  } );

  beginResetModel();
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
