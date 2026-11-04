//////////////////////////////////////////////////////////////////////
//
// YongBEEP — chat message delegate (S4e chatview-v2)
//
// QPainter-drawn WeChat bubbles for the QListView message list:
// - true rounded corners (r=8, own #95EC69 right / peer #FFFFFF + 1px
//   #E7E7E7 border left) - fixes the ragged right edge of HTML bubbles
// - shrink-to-fit width via QTextDocument::idealWidth(), capped at 62%
//   of the chat area - fixes the right-side empty space
// - WeChat file card, timestamp separators, avatars (40px r6), markdown
//
//////////////////////////////////////////////////////////////////////

#ifndef BEEBEEP_CHATMESSAGEDELEGATE_H
#define BEEBEEP_CHATMESSAGEDELEGATE_H

#include <QStyledItemDelegate>
#include <QUrl>

class ChatMessageItem;
class ChatMessageModel;


class ChatMessageDelegate : public QStyledItemDelegate
{
  Q_OBJECT

public:
  explicit ChatMessageDelegate( QObject* parent = Q_NULLPTR );

  void paint( QPainter*, const QStyleOptionViewItem&, const QModelIndex& ) const override;
  QSize sizeHint( const QStyleOptionViewItem&, const QModelIndex& ) const override;
  bool editorEvent( QEvent*, QAbstractItemModel*, const QStyleOptionViewItem&, const QModelIndex& ) override;

  // anchor (link) under a viewport position, for the context menu
  QUrl anchorAt( const QModelIndex& index, const QRect& row_rect, const QPoint& viewport_pos ) const;

signals:
  void urlActivated( const QUrl& url );

private:
  ChatMessageItem* itemForIndex( const QModelIndex& ) const;
  const ChatMessageModel* messageModel( const QModelIndex& ) const;
  QRect avatarRect( const ChatMessageItem*, const QRect& row_rect, qreal content_top ) const;
  QRectF bubbleRect( const ChatMessageItem*, const QRect& row_rect, qreal content_top, int row_width ) const;
  QRectF documentRect( const ChatMessageItem*, const QRect& row_rect, qreal content_top, int row_width ) const;
  QRectF fileCardRect( const ChatMessageItem*, const QRect& row_rect, qreal content_top, int row_width ) const;
  QUrl anchorInItem( const ChatMessageItem*, const QRect& row_rect, int row_width, const QPointF& row_pos ) const;
  void drawStatusLine( QPainter*, const QString& status_text, const QRectF& line_rect, const QColor& base_color ) const;
};

#endif // BEEBEEP_CHATMESSAGEDELEGATE_H
