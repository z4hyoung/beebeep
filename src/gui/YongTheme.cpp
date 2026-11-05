//////////////////////////////////////////////////////////////////////
//
// YongBEEP — WeChat macOS desktop visual theme (S4c)
//
//////////////////////////////////////////////////////////////////////

#include "YongTheme.h"
#include "Settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QSvgRenderer>
#include <QUrl>
#include <QtDebug>


// ---------- Palette ----------

QColor YongTheme::windowBackground()   { return QColor( 0xFF, 0xFF, 0xFF ); }
QColor YongTheme::chatAreaBackground() { return QColor( 0xF5, 0xF5, 0xF5 ); }
QColor YongTheme::searchBackground()   { return QColor( 0xF5, 0xF5, 0xF5 ); }
QColor YongTheme::textPrimary()        { return QColor( 0x19, 0x19, 0x19 ); }
QColor YongTheme::textSecondary()      { return QColor( 0xB2, 0xB2, 0xB2 ); }
QColor YongTheme::separator()          { return QColor( 0xE7, 0xE7, 0xE7 ); }
QColor YongTheme::brandGreen()         { return QColor( 0x07, 0xC1, 0x60 ); }
QColor YongTheme::bubbleGreen()        { return QColor( 0x95, 0xEC, 0x69 ); }
QColor YongTheme::bubbleWhite()        { return QColor( 0xFF, 0xFF, 0xFF ); }
QColor YongTheme::unreadRed()          { return QColor( 0xFA, 0x51, 0x51 ); }
QColor YongTheme::rowSelected()        { return QColor( 0xE2, 0xE2, 0xE2 ); }
QColor YongTheme::rowHover()           { return QColor( 0xED, 0xED, 0xED ); }
QColor YongTheme::iconGray()           { return QColor( 0x7F, 0x7F, 0x7F ); }
QColor YongTheme::sendDisabled()       { return QColor( 0xE5, 0xE5, 0xE5 ); }
QColor YongTheme::connectingOrange()   { return QColor( 0xFA, 0x9D, 0x3B ); }
QColor YongTheme::linkBlue()           { return QColor( 0x57, 0x6B, 0x95 ); }


// ---------- Pixel sizes ----------

int YongTheme::fontBodyPx()        { return 14; }
int YongTheme::fontAuxPx()         { return 12; }
int YongTheme::fontTimestampPx()   { return 11; }
int YongTheme::avatarSizePx()      { return 40; }
qreal YongTheme::avatarRadiusPx()  { return 6.0; }
qreal YongTheme::bubbleRadiusPx()  { return 8.0; }
qreal YongTheme::buttonRadiusPx()  { return 4.0; }
int YongTheme::toolbarIconPx()     { return 20; }


// ---------- Styles ----------

QPalette YongTheme::lightPalette()
{
  QPalette p;
  p.setColor( QPalette::Window, windowBackground() );
  p.setColor( QPalette::WindowText, textPrimary() );
  p.setColor( QPalette::Base, windowBackground() );
  p.setColor( QPalette::AlternateBase, chatAreaBackground() );
  p.setColor( QPalette::Text, textPrimary() );
  p.setColor( QPalette::PlaceholderText, textSecondary() );
  p.setColor( QPalette::Button, windowBackground() );
  p.setColor( QPalette::ButtonText, textPrimary() );
  p.setColor( QPalette::Highlight, brandGreen() );
  p.setColor( QPalette::HighlightedText, QColor( 0xFF, 0xFF, 0xFF ) );
  p.setColor( QPalette::Link, linkBlue() );
  p.setColor( QPalette::LinkVisited, linkBlue() );
  p.setColor( QPalette::ToolTipBase, windowBackground() );
  p.setColor( QPalette::ToolTipText, textPrimary() );

  // Disabled group: gray text on light-gray controls (send button etc.)
  p.setColor( QPalette::Disabled, QPalette::WindowText, textSecondary() );
  p.setColor( QPalette::Disabled, QPalette::Text, textSecondary() );
  p.setColor( QPalette::Disabled, QPalette::ButtonText, textSecondary() );
  p.setColor( QPalette::Disabled, QPalette::Base, chatAreaBackground() );
  p.setColor( QPalette::Disabled, QPalette::Button, chatAreaBackground() );
  return p;
}

QString YongTheme::mainWindowQss()
{
  return QStringLiteral(
    // ---- global chrome ----
    "#GuiWeChatMainWindow { background: %1; }"
    "#GuiWeChatLeftPanel { background: %1; border-right: 1px solid %2; }"

    // ---- search capsule ----
    "#GuiWeChatSearchBox {"
    "  background: %3; border: none; border-radius: 14px;"
    "  font-size: 13px; color: %4; padding: 0 10px;"
    "  selection-background-color: %5; selection-color: %1;"
    "}"

    // ---- conversation list ----
    "#GuiWeChatChatList { background: %1; border: none; }"

    // ---- scan devices entry (flat green text button, WeChat style) ----
    "#GuiWeChatScanDevicesButton {"
    "  border: none; background: transparent; color: %5;"
    "  font-size: 12px; padding: 6px 12px;"
    "}"
    "#GuiWeChatScanDevicesButton:hover { color: #06AD56; }"
    "#GuiWeChatScanDevicesButton:pressed { color: #069047; }"

    // ---- chat header ----
    "#GuiWeChatHeader { background: %1; }"
    "#GuiWeChatHeaderLine { background: %2; border: none; }"

    // ---- chat area ----
    "#GuiWeChatChatArea { background: %3; }"
    "#GuiWeChatPlaceholder { color: %6; font-size: 14px; background: transparent; }"

    // ---- embedded GuiChat: message viewer + input area ----
    "#GuiChat { background: %3; }"
    "#GuiChatViewer { border: none; background: %3; color: %4; font-size: 14px; }"
    "#GuiChatInputPanel { background: %1; }"
    "#GuiChatInputLine { background: %2; border: none; }"
    "#GuiChatInputToolbar { background: %1; }"
    "#GuiMessageEdit { border: none; background: %1; color: %4; font-size: 14px; }"
    "#GuiChatEmoticonsButton, #GuiChatAttachButton, #GuiChatMarkdownButton { border: none; background: transparent; }"
    "#GuiChatMarkdownButton:checked { background: %3; border-radius: 4px; }"

    // ---- send button: brand green capsule, gray when input is empty ----
    "#GuiChatSendButton {"
    "  background: %5; color: %1; border: none; border-radius: 4px;"
    "  font-size: 12px; padding: 0 8px;"
    "}"
    "#GuiChatSendButton:hover { background: #06AD56; }"
    "#GuiChatSendButton:pressed { background: #069047; }"
    "#GuiChatSendButton:disabled { background: %7; color: #999999; }"

    // ---- splitters: invisible, panels carry their own dividers ----
    // YongBEEP: a real 1px divider between the session list and the chat
    // column, running the FULL height — the pure color boundary "broke"
    // wherever the white input area met the white list panel
    "QSplitter::handle { background: %2; border: none; }"
  ).arg( windowBackground().name(),       // 1 white
         separator().name(),              // 2 #E7E7E7
         chatAreaBackground().name(),     // 3 #F5F5F5
         textPrimary().name(),            // 4 #191919
         brandGreen().name(),             // 5 #07C160
         textSecondary().name(),          // 6 #B2B2B2
         sendDisabled().name() );         // 7 #E5E5E5
}


// ---------- Icons ----------

static QByteArray loadIconSvg( const QString& icon_name )
{
  QFile res_file( QStringLiteral( ":/yongbeep/icons/%1.svg" ).arg( icon_name ) );
  if( !res_file.open( QIODevice::ReadOnly ) )
  {
    qWarning() << "YongTheme: cannot open icon resource" << icon_name;
    return QByteArray();
  }
  return res_file.readAll();
}

static QPixmap renderIconSvg( const QByteArray& svg_data, int size_px )
{
  QPixmap pixmap( size_px * 2, size_px * 2 ); // 2x for retina rendering
  pixmap.fill( Qt::transparent );
  QSvgRenderer renderer( svg_data );
  QPainter painter( &pixmap );
  renderer.render( &painter );
  painter.end();
  return pixmap;
}

QIcon YongTheme::svgIcon( const QString& icon_name, const QColor& color, const QColor& active_color, int size_px )
{
  const QByteArray svg_raw = loadIconSvg( icon_name );
  if( svg_raw.isEmpty() )
    return QIcon();

  QColor base_color = color.isValid() ? color : iconGray();

  QByteArray svg_normal = svg_raw;
  svg_normal.replace( "currentColor", base_color.name().toUtf8() );

  QIcon icon;
  QPixmap pm_normal = renderIconSvg( svg_normal, size_px );
  pm_normal.setDevicePixelRatio( 2.0 );
  icon.addPixmap( pm_normal, QIcon::Normal );

  if( active_color.isValid() )
  {
    QByteArray svg_active = svg_raw;
    svg_active.replace( "currentColor", active_color.name().toUtf8() );
    QPixmap pm_active = renderIconSvg( svg_active, size_px );
    pm_active.setDevicePixelRatio( 2.0 );
    icon.addPixmap( pm_active, QIcon::Active );
  }
  return icon;
}

QString YongTheme::svgHtmlIcon( const QString& icon_name, int size_px, const QString& html_extra )
{
  const QString cache_folder = Settings::instance().cacheFolder();
  const QString png_path = QStringLiteral( "%1/yongbeep_icon_%2_%3.png" ).arg( cache_folder, icon_name ).arg( size_px );
  if( !QFile::exists( png_path ) )
  {
    QDir().mkpath( cache_folder );
    const QByteArray svg_data = loadIconSvg( icon_name );
    if( svg_data.isEmpty() )
      return QString();
    QPixmap pixmap = renderIconSvg( svg_data, size_px );
    if( pixmap.isNull() || !pixmap.save( png_path, "PNG" ) )
    {
      qWarning() << "YongTheme: cannot cache icon png" << png_path;
      return QString();
    }
  }
  return QStringLiteral( "<img src='%1' width=%2 height=%3 border=0 %4>" )
           .arg( QUrl::fromLocalFile( png_path ).toString() )
           .arg( size_px ).arg( size_px ).arg( html_extra );
}

QString YongTheme::fileIconNameForFile( const QString& file_name )
{
  const QString suffix = QFileInfo( file_name ).suffix().toLower();
  if( suffix == QLatin1String( "png" ) || suffix == QLatin1String( "jpg" ) || suffix == QLatin1String( "jpeg" ) ||
      suffix == QLatin1String( "gif" ) || suffix == QLatin1String( "heic" ) || suffix == QLatin1String( "webp" ) ||
      suffix == QLatin1String( "bmp" ) || suffix == QLatin1String( "tiff" ) || suffix == QLatin1String( "svg" ) )
    return QStringLiteral( "file-image" );
  if( suffix == QLatin1String( "zip" ) || suffix == QLatin1String( "rar" ) || suffix == QLatin1String( "7z" ) ||
      suffix == QLatin1String( "tar" ) || suffix == QLatin1String( "gz" ) || suffix == QLatin1String( "bz2" ) ||
      suffix == QLatin1String( "dmg" ) || suffix == QLatin1String( "iso" ) )
    return QStringLiteral( "file-archive" );
  return QStringLiteral( "file-doc" );
}
