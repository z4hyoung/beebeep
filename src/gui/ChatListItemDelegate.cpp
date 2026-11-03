//////////////////////////////////////////////////////////////////////
//
// YongBEEP — WeChat-style chat list item delegate (S3-4a)
//
//////////////////////////////////////////////////////////////////////

#include "ChatListItemDelegate.h"
#include "ChatListModel.h"

#include <QPainter>
#include <QPainterPath>


ChatListItemDelegate::ChatListItemDelegate( QObject* parent )
  : QStyledItemDelegate( parent ), m_avatarSize( 42 ), m_rowHeight( 66 ), m_badgeHeight( 18 )
{
}

QSize ChatListItemDelegate::sizeHint( const QStyleOptionViewItem&, const QModelIndex& ) const
{
  return QSize( 240, m_rowHeight );
}

void ChatListItemDelegate::paint( QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index ) const
{
  painter->save();
  painter->setRenderHint( QPainter::Antialiasing, true );

  // Row background: selected > hovered > none
  if( option.state & QStyle::State_Selected )
    painter->fillRect( option.rect, QColor( 0x2a, 0x6f, 0xdb, 0x33 ) );
  else if( option.state & QStyle::State_MouseOver )
    painter->fillRect( option.rect, QColor( 0, 0, 0, 12 ) );

  const int h_pad = 10;
  QRect row_rect = option.rect.adjusted( h_pad, 0, -h_pad, 0 );

  // Avatar (rounded square, vertically centered)
  QIcon avatar = index.data( ChatListModel::AvatarRole ).value<QIcon>();
  QRect avatar_rect( row_rect.left(), option.rect.center().y() - m_avatarSize / 2, m_avatarSize, m_avatarSize );
  if( !avatar.isNull() )
  {
    QPainterPath avatar_clip;
    avatar_clip.addRoundedRect( avatar_rect, 7.0, 7.0 );
    painter->save();
    painter->setClipPath( avatar_clip );
    painter->drawPixmap( avatar_rect, avatar.pixmap( QSize( m_avatarSize, m_avatarSize ) ) );
    painter->restore();
  }

  const int text_left = avatar_rect.right() + 10;
  const QColor grey_color = QColor( 0x99, 0x99, 0x99 );

  // Fonts
  QFont f_name = option.font;
  f_name.setBold( true );
  QFont f_small = option.font;
  f_small.setPointSizeF( qMax( 7.0, f_small.pointSizeF() * 0.86 ) );

  QFontMetrics fm_name( f_name );
  QFontMetrics fm_small( f_small );

  // Relative time (top right)
  QString time_text = index.data( ChatListModel::TimeTextRole ).toString();
  int time_w = fm_small.horizontalAdvance( time_text );
  QRect time_rect( row_rect.right() - time_w, row_rect.top() + 8, time_w, fm_small.height() );
  painter->setFont( f_small );
  painter->setPen( grey_color );
  painter->drawText( time_rect, Qt::AlignRight | Qt::AlignVCenter, time_text );

  // Unread badge (right, below time)
  int unread = index.data( ChatListModel::UnreadRole ).toInt();
  int badge_left = row_rect.right() + 1;
  if( unread > 0 )
  {
    QFont f_badge = f_small;
    f_badge.setBold( true );
    QFontMetrics fm_badge( f_badge );
    QString badge_text = unread > 99 ? QStringLiteral( "99+" ) : QString::number( unread );
    int badge_w = qMax( m_badgeHeight, fm_badge.horizontalAdvance( badge_text ) + 10 );
    QRect badge_rect( row_rect.right() - badge_w, time_rect.bottom() + 3, badge_w, m_badgeHeight );
    painter->setPen( Qt::NoPen );
    painter->setBrush( QColor( 0xfa, 0x51, 0x51 ) );
    painter->drawRoundedRect( badge_rect, m_badgeHeight / 2.0, m_badgeHeight / 2.0 );
    painter->setPen( Qt::white );
    painter->setFont( f_badge );
    painter->drawText( badge_rect, Qt::AlignCenter, badge_text );
    badge_left = badge_rect.left();
  }

  // Name (bold, top left, elided before time)
  int name_right = qMin( time_rect.left() - 8, badge_left - 8 );
  QString name_text = index.data( ChatListModel::NameRole ).toString();
  painter->setFont( f_name );
  painter->setPen( option.palette.color( QPalette::Text ) );
  QRect name_rect( text_left, row_rect.top() + 6, qMax( 20, name_right - text_left ), fm_name.height() );
  painter->drawText( name_rect, Qt::AlignLeft | Qt::AlignVCenter,
                     fm_name.elidedText( name_text, Qt::ElideRight, name_rect.width() ) );

  // Last message summary (single line, grey, elided)
  QString summary_text = index.data( ChatListModel::SummaryRole ).toString();
  int summary_right = unread > 0 ? badge_left - 8 : row_rect.right();
  QRect summary_rect( text_left, name_rect.bottom(), qMax( 20, summary_right - text_left ), fm_small.height() + 4 );
  painter->setFont( f_small );
  painter->setPen( grey_color );
  painter->drawText( summary_rect, Qt::AlignLeft | Qt::AlignVCenter,
                     fm_small.elidedText( summary_text, Qt::ElideRight, summary_rect.width() ) );

  painter->restore();
}
