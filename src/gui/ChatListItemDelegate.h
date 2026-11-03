//////////////////////////////////////////////////////////////////////
//
// YongBEEP — WeChat-style chat list item delegate (S3-4a)
//
// Paints one conversation row: avatar (rounded), name (bold),
// single-line last-message summary, relative time, red unread badge,
// selected/hover highlight. Data comes from ChatListModel roles.
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
  int m_badgeHeight;

};

#endif // BEEBEEP_CHATLISTITEMDELEGATE_H
