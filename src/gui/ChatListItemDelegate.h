//////////////////////////////////////////////////////////////////////
//
// YongBEEP — WeChat-style chat list item delegate (S3-4a, restyled S4c)
//
// Paints one conversation row: avatar (rounded square, 6px radius),
// name (14px/600), single-line 12px gray last-message summary,
// 11px relative time, red unread badge (16px capsule, 99+ cap),
// WeChat-gray selected/hover highlight. Colors come from YongTheme.
//
//////////////////////////////////////////////////////////////////////

#ifndef BEEBEEP_CHATLISTITEMDELEGATE_H
#define BEEBEEP_CHATLISTITEMDELEGATE_H

#include <QStyledItemDelegate>

class ChatListItemDelegate : public QStyledItemDelegate
{
  Q_OBJECT

public:
  explicit ChatListItemDelegate( QObject* parent = Q_NULLPTR );

  void paint( QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index ) const override;
  QSize sizeHint( const QStyleOptionViewItem& option, const QModelIndex& index ) const override;

private:
  int m_avatarSize;
  int m_rowHeight;
  int m_badgeSize;

};

#endif // BEEBEEP_CHATLISTITEMDELEGATE_H
