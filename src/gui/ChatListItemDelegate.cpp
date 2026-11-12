//////////////////////////////////////////////////////////////////////
//
// YongBEEP — WeChat-style chat list item delegate (S3-4a, restyled S4c)
//
// Paints one conversation row following the WeChat macOS spec:
// 64px row, 40x40 avatar with 6px rounded corners (not a circle),
// 14px/600 name, 12px gray single-line summary, 11px timestamp,
// 16px red unread capsule (99+ cap), gray selected/hover highlight
// (#E2E2E2 / #EDEDED - never blue). Colors come from YongTheme.
//
//////////////////////////////////////////////////////////////////////

#include "ChatListItemDelegate.h"
#include "ChatListModel.h"
#include "Core.h"
#include "YongTheme.h"

#include <QPainter>
#include <QPainterPath>


ChatListItemDelegate::ChatListItemDelegate( QObject* parent )
  : QStyledItemDelegate( parent ), m_avatarSize( YongTheme::avatarSizePx() ), m_rowHeight( 64 ), m_badgeSize( 16 )
{
}

QSize ChatListItemDelegate::sizeHint( const QStyleOptionViewItem&, const QModelIndex& ) const
{
  return QSize( 280, m_rowHeight );
}

void ChatListItemDelegate::paint( QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index ) const
{
  painter->save();
  painter->setRenderHint( QPainter::Antialiasing, true );

  // Row background: WeChat grays - selected > hovered > plain white.
  if( option.state & QStyle::State_Selected )
    painter->fillRect( option.rect, YongTheme::rowSelected() );
  else if( option.state & QStyle::State_MouseOver )
    painter->fillRect( option.rect, YongTheme::rowHover() );

  // 12px padding left/right, 40x40 avatar vertically centered
  const int h_pad = 12;
  QRect row_rect = option.rect.adjusted( h_pad, 0, -h_pad, 0 );

  QIcon avatar = index.data( ChatListModel::AvatarRole ).value<QIcon>();
  QRect avatar_rect( row_rect.left(), option.rect.center().y() - m_avatarSize / 2, m_avatarSize, m_avatarSize );
  if( !avatar.isNull() )
  {
    QPainterPath avatar_clip;
    avatar_clip.addRoundedRect( avatar_rect, YongTheme::avatarRadiusPx(), YongTheme::avatarRadiusPx() );
    painter->save();
    painter->setClipPath( avatar_clip );
    painter->drawPixmap( avatar_rect, avatar.pixmap( QSize( m_avatarSize, m_avatarSize ) ) );
    painter->restore();
  }

  // Anchor the badges to the avatar. The unread count stays on the far right.
  const int transports = index.data( ChatListModel::TransportsRole ).toInt();
  QRect badge( avatar_rect.right() - 11, avatar_rect.bottom() - 11, 16, 16 );
  auto paint_link = [&]( const QIcon& glyph, const QColor& fill )
  {
    painter->setPen( QPen( Qt::white, 1.5 ) );
    painter->setBrush( fill );
    painter->drawEllipse( QRectF( badge ).adjusted( 0.75, 0.75, -0.75, -0.75 ) );
    glyph.paint( painter, badge.adjusted( 2, 2, -2, -2 ) );
    badge.translate( -16, 0 );
  };
  if( transports & Core::Bluetooth )
  {
    static const QIcon glyph = YongTheme::svgIcon( "link-bluetooth", Qt::white, QColor(), 12 );
    paint_link( glyph, QColor( "#2563B9" ) );
  }
  if( transports & Core::Lan )
  {
    static const QIcon glyph = YongTheme::svgIcon( "link-lan", Qt::white, QColor(), 12 );
    paint_link( glyph, QColor( "#147653" ) );
  }

  const int text_left = avatar_rect.right() + 10;

  // Fonts: name 14px/600, summary 12px, time 11px
  QFont f_name = option.font;
  f_name.setPixelSize( YongTheme::fontBodyPx() );
  f_name.setWeight( QFont::DemiBold );
  QFont f_aux = option.font;
  f_aux.setPixelSize( YongTheme::fontAuxPx() );
  QFont f_time = option.font;
  f_time.setPixelSize( YongTheme::fontTimestampPx() );
  QFont f_badge = option.font;
  f_badge.setPixelSize( YongTheme::fontTimestampPx() );
  f_badge.setWeight( QFont::DemiBold );

  QFontMetrics fm_name( f_name );
  QFontMetrics fm_aux( f_aux );
  QFontMetrics fm_time( f_time );
  QFontMetrics fm_badge( f_badge );

  // YongBEEP: the name+summary text block is vertically CENTERED in the row
  // (it used to hug the top, misaligned with the centered avatar)
  const int text_gap = 2;
  const int text_block_h = fm_name.height() + text_gap + fm_aux.height();
  const int text_top = option.rect.top() + ( m_rowHeight - text_block_h ) / 2;

  // Timestamp (11px gray, top right, aligned with the name line)
  QString time_text = index.data( ChatListModel::TimeTextRole ).toString();
  int time_w = fm_time.horizontalAdvance( time_text );
  // YongBEEP audit: 12px inset from the right edge for time (and badge below)
  QRect time_rect( row_rect.right() - 12 - time_w, option.rect.top() + 12, time_w, fm_time.height() );
  painter->setFont( f_time );
  painter->setPen( YongTheme::textSecondary() );
  painter->drawText( time_rect, Qt::AlignRight | Qt::AlignVCenter, time_text );

  // Unread badge (red capsule, bottom right on the summary line — WeChat
  // layout; the old mid-row position overlapped the top-right timestamp)
  int unread = index.data( ChatListModel::UnreadRole ).toInt();
  int badge_left = row_rect.right() + 1;
  if( unread > 0 )
  {
    QString badge_text = unread > 99 ? QStringLiteral( "99+" ) : QString::number( unread );
    int badge_w = qMax( m_badgeSize, fm_badge.horizontalAdvance( badge_text ) + 10 );
    int summary_center_y = text_top + fm_name.height() + text_gap + fm_aux.height() / 2;
    QRect badge_rect( row_rect.right() - 12 - badge_w, summary_center_y - m_badgeSize / 2, badge_w, m_badgeSize );
    painter->setPen( Qt::NoPen );
    painter->setBrush( YongTheme::unreadRed() );
    painter->drawRoundedRect( badge_rect, m_badgeSize / 2.0, m_badgeSize / 2.0 );
    painter->setPen( Qt::white );
    painter->setFont( f_badge );
    painter->drawText( badge_rect, Qt::AlignCenter, badge_text );
    badge_left = badge_rect.left();
  }

  // Name (14px/600, top left, elided before time/badge)
  int name_right = qMin( time_rect.left() - 8, badge_left - 8 );
  QString name_text = index.data( ChatListModel::NameRole ).toString();
  painter->setFont( f_name );
  painter->setPen( YongTheme::textPrimary() );
  QRect name_rect( text_left, text_top, qMax( 20, name_right - text_left ), fm_name.height() );
  painter->drawText( name_rect, Qt::AlignLeft | Qt::AlignVCenter,
                     fm_name.elidedText( name_text, Qt::ElideRight, name_rect.width() ) );

  // Last message summary (12px gray, single line, elided)
  QString summary_text = index.data( ChatListModel::SummaryRole ).toString();
  int summary_right = unread > 0 ? badge_left - 8 : row_rect.right();
  painter->setFont( f_aux );
  painter->setPen( YongTheme::textSecondary() );
  QRect summary_rect( text_left, name_rect.bottom() + text_gap - 1, qMax( 20, summary_right - text_left ), fm_aux.height() );
  painter->drawText( summary_rect, Qt::AlignLeft | Qt::AlignVCenter,
                     fm_aux.elidedText( summary_text, Qt::ElideRight, summary_rect.width() ) );

  painter->restore();
}
