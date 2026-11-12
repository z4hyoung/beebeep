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
#include <QTextDocumentFragment>
#include "ChatMessage.h"
#include "IconManager.h"
#include "Settings.h"
#include "UserManager.h"
#include "YongTheme.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QRegularExpression>
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
// YongBEEP S3-4b / S4c: WeChat-style bubble palette (PRD 4.1, ADR-D7: rich
// text has no border-radius, square color blocks are the approved
// compromise). Qt rich text cannot shrink-wrap a bgcolor <td> (it stretches
// to the full cell), while a CSS background-color <span> hugs its text and
// stays continuous across wrapped lines - but only when it sits inside a
// table cell. So every bubble is: outer positioning table (avatar column +
// empty 40% spacer capping the bubble at ~60% of the chat width) + <span>
// color block inside the content cell. S4c colors: own #95EC69, peer white
// #FFFFFF on the #F5F5F5 chat background, text #191919 (light style only;
// dark style keeps the S3-4b colors).
// ---------------------------------------------------------------------
static QString ownBubbleStyle()
{
  // WeChat green with dark text in light style; darker green with white text in dark style
  return Settings::instance().useDarkStyle() ? QStringLiteral( "background-color:#3eb575;color:#ffffff" )
                                             : QStringLiteral( "background-color:#95ec69;color:#191919" );
}

static QString peerBubbleStyle()
{
  return Settings::instance().useDarkStyle() ? QStringLiteral( "background-color:#3c4048;color:#ffffff" )
                                             : QStringLiteral( "background-color:#ffffff;color:#191919" );
}

static QString centeredInfoLine( const QString& text )
{
  if( Settings::instance().useDarkStyle() )
    return QString( "<div align=center><font color=%1 size=2>%2</font></div>" ).arg( Settings::instance().chatSystemTextColor(), text );
  // S4c WeChat spec: centered 11px #B2B2B2 timestamp
  return QString( "<div align=center><span style=\"font-size:%1px;color:%2\">%3</span></div>" )
           .arg( YongTheme::fontTimestampPx() ).arg( YongTheme::textSecondary().name(), text );
}

// Caches the user avatar (Bee::avatarForUser: real photo or initials, the
// same generator the conversation list uses) as a png in the data cache
// folder so the HTML stream can reference it with a file:// url.
// S4c: rendered at 2x (80px) with the corners rounded by 12px (2x of the
// 6px WeChat avatar radius) so the 40x40 html image shows a rounded square.
static QString avatarHtml( const User& u )
{
  const int html_avatar_size = YongTheme::avatarSizePx(); // 40
  const int render_avatar_size = html_avatar_size * 2;
  const QString avatar_folder = Settings::instance().cacheFolder();
  const QString avatar_path = QString( "%1/chat_avatar_%2_r.png" ).arg( avatar_folder, QString::number( static_cast<qulonglong>( u.isValid() ? u.id() : ID_INVALID ) ) );
  if( !QFile::exists( avatar_path ) )
  {
    QDir().mkpath( avatar_folder );
    QPixmap avatar_pixmap = Bee::avatarForUser( u, QSize( render_avatar_size, render_avatar_size ), true );
    if( !avatar_pixmap.isNull() )
    {
      QPixmap rounded_pixmap( render_avatar_size, render_avatar_size );
      rounded_pixmap.fill( Qt::transparent );
      QPainter painter( &rounded_pixmap );
      QPainterPath clip_path;
      clip_path.addRoundedRect( 0, 0, render_avatar_size, render_avatar_size,
                                YongTheme::avatarRadiusPx() * 2.0, YongTheme::avatarRadiusPx() * 2.0 );
      painter.setClipPath( clip_path );
      painter.drawPixmap( 0, 0, avatar_pixmap );
      painter.end();
      if( !rounded_pixmap.save( avatar_path, "PNG" ) )
        return IconManager::instance().toHtml( "user.png", "*", html_avatar_size, html_avatar_size );
    }
    else
      return IconManager::instance().toHtml( "user.png", "*", html_avatar_size, html_avatar_size );
  }
  return QString( "<img src='%1' width=%2 height=%2 border=0>" ).arg( QUrl::fromLocalFile( avatar_path ).toString() ).arg( html_avatar_size );
}

// ---------------------------------------------------------------------
// S4c: WeChat file message card - white block, 32px file-type icon on the
// left, 13px file name (link stays clickable) and an 11px gray status line.
// The incoming system message text is free-form ("name 143KB ..." or the
// full transfer sentence); the <a href> link provides the file name, the
// remaining text becomes the status line with ✓ green / ✗ red.
// ---------------------------------------------------------------------
static QString fileTransferCardHtml( const ChatMessage& cm )
{
  static const QRegularExpression link_rx( QStringLiteral( "<a href=\\\"([^\\\"]+)\\\">([^<]+)</a>" ) );
  QRegularExpressionMatch match = link_rx.match( cm.message() );
  if( !match.hasMatch() )
    return QString();

  const QString file_url = match.captured( 1 );
  const QString file_name = QTextDocumentFragment::fromHtml( match.captured( 2 ) ).toPlainText();

  QString rest_text = cm.message();
  rest_text.remove( match.capturedStart(), match.capturedLength() );
  rest_text.remove( QRegularExpression( QStringLiteral( "<img[^>]*>" ) ) );
  rest_text = Bee::removeHtmlTags( rest_text ).simplified();

  QString status_line = rest_text.toHtmlEscaped();
  const auto marker = QRegularExpression( "<!--yb-file:[^>]+-->" ).match( cm.message() );
  if( marker.hasMatch() )
    status_line += marker.captured();
  status_line.replace( QString::fromUtf8( "\xE2\x9C\x93" ), QString( "<font color=%1>\xE2\x9C\x93</font>" ).arg( YongTheme::brandGreen().name() ) ); // check mark
  status_line.replace( QString::fromUtf8( "\xE2\x9C\x97" ), QString( "<font color=%1>\xE2\x9C\x97</font>" ).arg( YongTheme::unreadRed().name() ) ); // ballot x

  const QString icon_html = YongTheme::svgHtmlIcon( YongTheme::fileIconNameForFile( file_name ), 32 );
  if( icon_html.isEmpty() )
    return QString();

  const bool dark = Settings::instance().useDarkStyle();
  const QString card_bgcolor = dark ? QStringLiteral( "#3c4048" ) : QStringLiteral( "#ffffff" );
  const QString name_color = dark ? QStringLiteral( "#ffffff" ) : YongTheme::textPrimary().name();
  const QString status_color = dark ? QStringLiteral( "#aaaaaa" ) : YongTheme::textSecondary().name();

  return QString(
           "<div align=center>"
           "<table width=320 cellspacing=0 cellpadding=8 border=0 align=center><tr>"
           "<td bgcolor=%1 width=48 align=center valign=middle>%2</td>"
           "<td bgcolor=%1 valign=middle>"
           "<span style=\"font-size:13px;color:%3\"><a href=\"%4\" style=\"text-decoration:none;color:%3\">%5</a></span><br>"
           "<span style=\"font-size:11px;color:%6\">%7</span>"
           "</td>"
           "</tr></table>"
           "</div><br>" )
           .arg( card_bgcolor, icon_html, name_color, file_url, file_name.toHtmlEscaped(), status_color, status_line );
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
                                       const QDateTime& last_message_timestamp, bool chat_is_private )
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
  // S4c: hh:mm on the same day, date + hh:mm on older days (no seconds).
  QString html_message = "";
  bool day_changed = last_message_timestamp.isValid() && last_message_timestamp.date() != cm.timestamp().date();
  if( show_timestamp && ( !last_message_timestamp.isValid() || day_changed || last_message_timestamp.secsTo( cm.timestamp() ) >= 300 ) )
  {
    QDate msg_date = cm.timestamp().date();
    QString date_time_stamp = cm.timestamp().time().toString( QStringLiteral( "hh:mm" ) );
    if( msg_date != QDate::currentDate() )
    {
      QString date_part = msg_date.year() == QDate::currentDate().year() ? msg_date.toString( QStringLiteral( "MM-dd" ) )
                                                                         : msg_date.toString( QStringLiteral( "yyyy-MM-dd" ) );
      date_time_stamp.prepend( date_part + QLatin1Char( ' ' ) );
    }
    if( !date_time_stamp.isEmpty() )
      html_message += centeredInfoLine( date_time_stamp );
  }

  // Sender name line: peers only, and never in 1:1 chats - WeChat private
  // chats show no names above the bubbles (S4c), the avatar is enough.
  // Own bubbles never carry a name; grouped consecutive messages omit it too.
  QString name_line = "";
  if( !own_message && !chat_is_private && !append_message_to_previous )
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
  // column (~40%) capping the bubble width at ~60% of the chat area (S4c),
  // content cell with the span block. Avatars are 40x40 rounded squares.
  if( own_message )
  {
    html_message += QString( "<table width=100% cellspacing=0 cellpadding=0 border=0><tr>"
                             "<td width=40%></td>"
                             "<td align=right valign=top>%1</td>"
                             "<td width=12></td>"
                             "<td width=%3 valign=top>%2</td>"
                             "</tr></table><br>" ).arg( bubble, avatarHtml( u ) ).arg( YongTheme::avatarSizePx() );
  }
  else
  {
    html_message += QString( "<table width=100% cellspacing=0 cellpadding=0 border=0><tr>"
                             "<td width=%3 valign=top>%2</td>"
                             "<td width=12></td>"
                             "<td align=left valign=top>%1</td>"
                             "<td width=40%></td>"
                             "</tr></table><br>" ).arg( name_line + bubble, avatarHtml( u ) ).arg( YongTheme::avatarSizePx() );
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

  // File transfer lines: WeChat file card (S4c) - white block with a 32px
  // file-type icon, 13px name and 11px gray status. Falls back to the
  // centered paperclip block when no file link can be parsed.
  if( cm.type() == ChatMessage::FileTransfer )
  {
    QString card_html = fileTransferCardHtml( cm );
    if( !card_html.isEmpty() )
      return card_html;
  }

  // Voice lines and unparsed file transfers: centered block with the
  // clickable links (paperclip row, S3-4b).
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
      html_text += formatMessage( u, cm, last_message_user_id, force_timestamp || Settings::instance().chatShowMessageTimestamp(), force_datestamp || Settings::instance().chatShowMessageDatestamp(), skip_system_message, false, true, use_chat_compact, last_message_timestamp, c.isPrivate() );
      last_message_timestamp = cm.timestamp();
    }
    last_message_user_id = cm.isImportant() ? ID_IMPORTANT_MESSAGE : cm.userId();
  }

  return html_text;
}
