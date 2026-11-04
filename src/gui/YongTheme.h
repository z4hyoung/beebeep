//////////////////////////////////////////////////////////////////////
//
// YongBEEP — WeChat macOS desktop visual theme (S4c)
//
// Single source of truth for the WeChat-style light palette, pixel
// sizes and hand-drawn SVG icon helpers (PRD 4.1 / docs/analysis/
// gui-redesign.md). All WeChat-look code must take its colors from
// here instead of scattering magic numbers. Dark style (UseDarkStyle)
// is untouched: YongTheme is only applied in light mode.
//
//////////////////////////////////////////////////////////////////////

#ifndef BEEBEEP_YONGTHEME_H
#define BEEBEEP_YONGTHEME_H

#include <QColor>
#include <QString>

class QIcon;
class QPalette;


class YongTheme
{
public:
  // ---------- Palette (WeChat macOS light) ----------
  static QColor windowBackground();     // #FFFFFF  conversation list / header / input
  static QColor chatAreaBackground();   // #F5F5F5  message area
  static QColor searchBackground();     // #F5F5F5  search capsule
  static QColor textPrimary();          // #191919  main text
  static QColor textSecondary();        // #B2B2B2  timestamps / summaries
  static QColor separator();            // #E7E7E7  0.5-1px divider lines
  static QColor brandGreen();           // #07C160  buttons / links / online dot
  static QColor bubbleGreen();          // #95EC69  own message bubble
  static QColor bubbleWhite();          // #FFFFFF  peer message bubble
  static QColor unreadRed();            // #FA5151  unread badge
  static QColor rowSelected();          // #E2E2E2  selected conversation row
  static QColor rowHover();             // #EDEDED  hovered conversation row
  static QColor iconGray();             // #7F7F7F  toolbar line icons
  static QColor sendDisabled();         // #E5E5E5  send button when input empty
  static QColor connectingOrange();     // #FA9D3B  connecting status dot
  static QColor linkBlue();             // #576B95  WeChat link color

  // ---------- Pixel sizes ----------
  static int fontBodyPx();              // 14
  static int fontAuxPx();               // 12
  static int fontTimestampPx();         // 11
  static int avatarSizePx();            // 40 (rounded-rect, not a circle!)
  static qreal avatarRadiusPx();        // 6
  static qreal bubbleRadiusPx();        // 4
  static qreal buttonRadiusPx();        // 4
  static int toolbarIconPx();           // 20

  // ---------- Styles ----------
  // Full application palette used to force the light WeChat chrome even
  // when macOS runs in dark mode (only applied when !UseDarkStyle).
  static QPalette lightPalette();

  // QSS for the WeChat main window (applied on GuiMainWindow; cascades
  // to the embedded chat widgets).
  static QString mainWindowQss();

  // ---------- Icons (hand-drawn SVG, :/yongbeep/icons/<name>.svg) ----------
  // Renders icon_name recolored to `color` (the SVGs use the
  // "currentColor" token, replaced before rendering). When active_color
  // is valid it is used for the hovered/active icon mode. Rendered at
  // 2x for retina crispness. QSvgRenderer is used directly because the
  // homebrew Qt build ships no SVG image-format plugin.
  static QIcon svgIcon( const QString& icon_name, const QColor& color, const QColor& active_color = QColor(), int size_px = 24 );

  // Same rendering, cached as a png in the settings cache folder and
  // returned as an <img src='file://...'> fragment for the QTextBrowser
  // chat HTML (bubble avatars, file cards).
  static QString svgHtmlIcon( const QString& icon_name, int size_px, const QString& html_extra = QString() );

  // Picks the generic file-type icon (file-doc / file-image /
  // file-archive) from a file name/extension.
  static QString fileIconNameForFile( const QString& file_name );

};

#endif // BEEBEEP_YONGTHEME_H
