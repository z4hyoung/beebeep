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
// $Id: GuiChatMessage.cpp 1592 2026-08-11 10:14:22Z mastroddi $
//
//////////////////////////////////////////////////////////////////////

#include "BeeUtils.h"
#include "Chat.h"
#include "GuiChatMessage.h"
#include "ChatMessage.h"
#include "IconManager.h"
#include "Settings.h"
#include "Settings.h"
#include "UserManager.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPixmap>
#include <QUrl>


static QString textImportantPrefix()
{
  return QString( "<font color=red><b>!!</b></font> " );
}

static QString textImportantSuffix()
{
  return QString( "" );
}

// ---------------------------------------------------------------------
// YongBEEP S3-4b: WeChat-style bubble palette (PRD 4.1, ADR-D7: rich text
// has no border-radius, square color blocks are the approved compromise).
// Qt rich text cannot shrink-wrap a bgcolor <td> (it stretches to the full
// cell), while a CSS background-color <span> hugs its text and stays
// continuous across wrapped lines - but only when it sits inside a table
// cell. So every bubble is: outer positioning table (avatar column + empty
// 30% spacer to cap the bubble width) + <span> color block inside the
// content cell. Verified with a standalone QTextBrowser test bench.
// ---------------------------------------------------------------------
static QString ownBubbleStyle()
{
  // WeChat green in light style; darker green with white text in dark style
  return Settings::instance().useDarkStyle() ? QStringLiteral( "background-color:#3eb575;color:#ffffff" )
                                             : QStringLiteral( "background-color:#95ec69" );
}

static QString peerBubbleStyle()
{
  return Settings::instance().useDarkStyle() ? QStringLiteral( "background-color:#3c4048" )
                                             : QStringLiteral( "background-color:#f0f0f0" );
}

static QString centeredInfoLine( const QString& text )
{
  return QString( "<div align=center><font color=%1 size=2>%2</font></div>" ).arg( Settings::instance().chatSystemTextColor(), text );
}

// Caches the user avatar (Bee::avatarForUser: real photo or initials, the
// same generator the conversation list uses) as a png in the data cache
// folder so the HTML stream can reference it with a file:// url.
static QString avatarHtml( const User& u )
{
  const QString avatar_folder = Settings::instance().cacheFolder();
  const QString avatar_path = QString( "%1/chat_avatar_%2.png" ).arg( avatar_folder, QString::number( static_cast<qulonglong>( u.isValid() ? u.id() : ID_INVALID ) ) );
  if( !QFile::exists( avatar_path ) )
  {
    QDir().mkpath( avatar_folder );
    QPixmap avatar_pixmap = Bee::avatarForUser( u, QSize( 96, 96 ), true );
    if( avatar_pixmap.isNull() || !avatar_pixmap.save( avatar_path, "PNG" ) )
      return IconManager::instance().toHtml( "user.png", "*", 24, 24 );
  }
  return QString( "<img src='%1' width=24 height=24 border=0>" ).arg( QUrl::fromLocalFile( avatar_path ).toString() );
}

QString GuiChatMessage::datetimestampToString( const ChatMessage& cm, bool show_timestamp, bool show_datestamp )
{
  QString date_time_stamp_format = "";

  if( show_datestamp || cm.timestamp().date() != QDate::currentDate() )
  {
//: Date format: do not change letters. More info in https://doc.qt.io/qt-5/qdatetime.html#toString
    date_time_stamp_format += QT_TRANSLATE_NOOP( "Date", "yyyy-MM-dd" );
  }

  if( show_timestamp )
  {
    if( !date_time_stamp_format.isEmpty() )
      date_time_stamp_format += QString( " " );
    if( Settings::instance().useMessageTimestampWithAP() )
    {
//: Time format: do not change letters. More info in https://doc.qt.io/qt-5/qdatetime.html#toString
      date_time_stamp_format += QT_TRANSLATE_NOOP( "Date", "h:mm:ss ap" );
    }
    else
    {
//: Time format: do not change letters. More info in https://doc.qt.io/qt-5/qdatetime.html#toString
      date_time_stamp_format += QT_TRANSLATE_NOOP( "Date", "hh:mm:ss" );
    }
  }

  return date_time_stamp_format.isEmpty() ? date_time_stamp_format : (Settings::instance().useMessageTimestampWithAP() ? QLocale("en_US").toString( cm.timestamp(), date_time_stamp_format ) : cm.timestamp().toString( date_time_stamp_format ));
}

QString GuiChatMessage::formatMessage( const User& u, const ChatMessage& cm, VNumber last_user_id, bool show_timestamp, bool show_datestamp,
                                       bool skip_system_message, bool show_message_group_by_user, bool use_your_name, bool use_chat_compact,
                                       const QDateTime& last_message_timestamp )
{
  Q_UNUSED( skip_system_message );
  Q_UNUSED( use_your_name );
  Q_UNUSED( use_chat_compact );

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
    {
      text_formatted.prepend( QString( "<font color=%1>" ).arg( text_color.name() ) );
      text_formatted.append( QLatin1String( "</font>" ) );
    }
  }

  bool own_message = u.isValid() && u.isLocal();
  bool append_message_to_previous = show_message_group_by_user && last_user_id == u.id() && !cm.isImportant();

  // WeChat-like centered gray time separator: first shown message, >= 5 min
  // gap from the previous one or day change (PRD 4.1 "时间戳居中灰字").
  QString html_message = "";
  bool day_changed = last_message_timestamp.isValid() && last_message_timestamp.date() != cm.timestamp().date();
  if( show_timestamp && ( !last_message_timestamp.isValid() || day_changed || last_message_timestamp.secsTo( cm.timestamp() ) >= 300 ) )
  {
    QString date_time_stamp = datetimestampToString( cm, true, show_datestamp || day_changed );
    if( !date_time_stamp.isEmpty() )
      html_message += centeredInfoLine( date_time_stamp );
  }

  // Sender name line (peers only, hidden when grouped with the previous
  // message of the same author; own bubbles carry no name like WeChat).
  QString name_line = "";
  if( !own_message && !append_message_to_previous )
  {
    QString user_name = u.isValid() ? Bee::userNameToShow( u, true ) : QObject::tr( "Unknown" );
    if( cm.isFromAutoresponder() )
      user_name = QString( "%1 (%2)" ).arg( user_name, Settings::instance().autoresponderName() );
    if( Settings::instance().chatUseColoredUserNames() && Bee::isColorVisibleInChat( QColor( u.color() ) ) )
      name_line = QString( "<font color=%1 size=2>%2</font><br>" ).arg( u.color(), user_name );
    else
      name_line = QString( "<font color=%1 size=2>%2</font><br>" ).arg( Settings::instance().chatSystemTextColor(), user_name );
  }

  QString bubble = QString( "<span style=\"%1\">&nbsp;%2%3&nbsp;</span>" )
                     .arg( own_message ? ownBubbleStyle() : peerBubbleStyle(),
                           cm.isImportant() ? textImportantPrefix() : QString(),
                           text_formatted );

  // Outer positioning table: avatar column on the outer edge, empty spacer
  // column (~30%) capping the bubble width, content cell with the span block.
  if( own_message )
  {
    html_message += QString( "<table width=100% cellspacing=0 cellpadding=0 border=0><tr>"
                             "<td width=30%></td>"
                             "<td align=right valign=top>%1</td>"
                             "<td width=8></td>"
                             "<td width=24 valign=top>%2</td>"
                             "</tr></table><br>" ).arg( bubble, avatarHtml( u ) );
  }
  else
  {
    html_message += QString( "<table width=100% cellspacing=0 cellpadding=0 border=0><tr>"
                             "<td width=24 valign=top>%2</td>"
                             "<td width=8></td>"
                             "<td align=left valign=top>%1</td>"
                             "<td width=30%></td>"
                             "</tr></table><br>" ).arg( name_line + bubble, avatarHtml( u ) );
  }

  if( cm.isImportant() )
    html_message.append( textImportantSuffix() );

  return html_message;
}

QString GuiChatMessage::formatSystemMessage( const ChatMessage& cm, VNumber last_user_id, bool show_timestamp, bool show_datestamp, bool use_chat_compact )
{
  Q_UNUSED( last_user_id );
  Q_UNUSED( use_chat_compact );

  if( cm.message().isEmpty() )
    return QString( "" );

  // File transfer / voice lines: keep the clickable links and add a paperclip
  // row inside a light color block (PRD 4.1 file bubble).
  if( cm.type() == ChatMessage::FileTransfer || cm.type() == ChatMessage::Voice )
  {
    return QString( "<div align=center><span style=\"%1\">&nbsp;&#128206;&nbsp;%2%3&nbsp;</span></div><br>" )
             .arg( peerBubbleStyle(), cm.isImportant() ? textImportantPrefix() : QString(), cm.message() );
  }

  // Image previews arrive already wrapped in <div align=center><a><img></a></div>
  if( cm.type() == ChatMessage::ImagePreview )
    return cm.message() + QLatin1String( "<br>" );

  // Plain system messages: centered gray line (PRD 4.1)
  QString date_time_stamp = datetimestampToString( cm, show_timestamp, show_datestamp );
  QString text = QString( "%1%2" ).arg( date_time_stamp.isEmpty() ? QString() : QString( "(%1) " ).arg( date_time_stamp ), cm.message() );
  if( cm.isImportant() )
    text.prepend( textImportantPrefix() );
  return centeredInfoLine( text ) + QLatin1String( "<br>" );
}

QString GuiChatMessage::chatToHtml( const Chat& c, bool skip_file_transfers, bool skip_system_message, bool force_timestamp, bool force_datestamp, bool use_chat_compact, bool skip_cannot_be_saved_messages )
{
  UserList chat_users;
  QString html_text = "";
  VNumber last_message_user_id = skip_system_message ? 0 : ID_SYSTEM_MESSAGE;
  QDateTime last_message_timestamp;

  if( c.isDefault() )
    chat_users = UserManager::instance().userList();
  else
    chat_users = UserManager::instance().userList().fromUsersId( c.usersId() );

  User u;

  foreach( ChatMessage cm, c.messages() )
  {
    if( cm.isFromSystem() )
    {
      if( cm.isFileTransfer() || cm.isImagePreview() )
      {
        if( skip_file_transfers )
          continue;
        if( skip_cannot_be_saved_messages && !cm.canBeSaved() )
          continue;
        html_text += formatSystemMessage( cm, last_message_user_id, force_timestamp || Settings::instance().chatShowMessageTimestamp(), force_datestamp || Settings::instance().chatShowMessageDatestamp(), use_chat_compact );
      }
      else
      {
        if( skip_system_message )
          continue;
        if( skip_cannot_be_saved_messages && !cm.canBeSaved() )
          continue;
        html_text += formatSystemMessage( cm, last_message_user_id, force_timestamp || Settings::instance().chatShowMessageTimestamp(), force_datestamp || Settings::instance().chatShowMessageDatestamp(), use_chat_compact );
      }
    }
    else
    {
      u = chat_users.find( cm.userId() );
      if( !u.isValid() )
      {
        u = UserManager::instance().findUser( cm.userId() );
        if( u.isValid() )
          chat_users.set( u );
      }
      html_text += formatMessage( u, cm, last_message_user_id, force_timestamp || Settings::instance().chatShowMessageTimestamp(), force_datestamp || Settings::instance().chatShowMessageDatestamp(), skip_system_message, false, true, use_chat_compact, last_message_timestamp );
      last_message_timestamp = cm.timestamp();
    }
    last_message_user_id = cm.isImportant() ? ID_IMPORTANT_MESSAGE : cm.userId();
  }

  return html_text;
}
