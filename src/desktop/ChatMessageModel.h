//////////////////////////////////////////////////////////////////////
//
// YongBEEP — chat message list model (S4e chatview-v2)
//
// Wraps a Chat's ChatMessage list for the QListView + delegate message
// rendering that replaced the QTextBrowser+HTML pipeline. Each item owns
// a cached QTextDocument (built once, laid out shrink-to-fit against the
// current viewport width) so paint() never re-parses rich text.
//
// Markdown (S4e): Message::Markdown is the wire flag; the Core dispatch
// path is frozen in this round, so the model keeps a GUI-side registry of
// the raw texts sent with the input-bar MD toggle on (they render with
// QTextDocument::setMarkdown, both on the local echo and on same-session
// chat reloads). Old messages without a registry hit keep the classic
// HTML pipeline.
//
//////////////////////////////////////////////////////////////////////

#ifndef BEEBEEP_CHATMESSAGEMODEL_H
#define BEEBEEP_CHATMESSAGEMODEL_H

#include "Config.h"
#include "ChatMessage.h"
#include "User.h"
#include <QAbstractListModel>
#include <QDateTime>
#include <QIcon>
#include <QPixmap>
#include <QSizeF>

class Chat;
class QTextDocument;


class ChatMessageItem
{
public:
  enum Kind { UserMessage, SystemLine, FileCard, FreeFormDoc };

  ChatMessageItem();
  ~ChatMessageItem();

  int kind;
  ChatMessage message;
  User user;
  bool ownMessage;

  bool showTimestamp;
  QString timestampText;
  bool showName;
  QString nameText;

  bool markdown;              // render with QTextDocument::setMarkdown
  QString markdownRaw;        // original raw markdown text
  QString contentHtml;        // classic-pipeline inner HTML (no bubble table)
  QString plainText;          // for copy / quote / find

  QString fileUrl;            // FileCard: beefile:/file: link target
  QString fileName;
  QString fileStatus;
  FileSizeType fileProgressBytes;  // live "15M / 100M" progress (0 = not transferring)
  FileSizeType fileProgressTotal;

  QPixmap avatar;             // rounded, 2x for retina
  QIcon fileIcon;

  // --- layout cache (built lazily by ChatMessageModel::ensureLayout) ---
  QTextDocument* doc;
  qreal laidOutForWidth;      // max content width the doc was laid out for
  QSizeF docSize;

private:
  ChatMessageItem( const ChatMessageItem& );
  ChatMessageItem& operator=( const ChatMessageItem& );
};


class ChatMessageModel : public QAbstractListModel
{
  Q_OBJECT

public:
  // Shared geometry constants (delegate paints with the same values)
  // YongBEEP audit pass: roomier timestamp separators and bubble padding
  enum { HorizontalMargin = 12, AvatarGap = 10, BubbleHPad = 12, BubbleVPad = 10,
         RowBottomSpacing = 10, TimestampRowHeight = 36, NameRowHeight = 17,
         FileCardWidth = 240, FileCardHeight = 60 };

  explicit ChatMessageModel( QObject* parent = Q_NULLPTR );
  ~ChatMessageModel();

  int rowCount( const QModelIndex& parent = QModelIndex() ) const override;
  QVariant data( const QModelIndex& index, int role ) const override;

  void resetFromChat( const Chat& c );
  bool appendMessage( const Chat& c, const ChatMessage& cm );

  /*! YongBEEP: live "15M / 100M" progress on file cards. Matches the card
   * by file name + total size (the card carries no protocol file id). */
  void updateFileProgress( const QString& file_name, FileSizeType bytes, FileSizeType total );

  void setChatFont( const QFont& );
  void setViewportWidth( int );
  inline int viewportWidth() const;

  // GUI-side Markdown registry: raw texts sent with the MD toggle on
  void addMarkdownText( const QString& raw_text );
  void registerMarkdownTexts( const QStringList& raw_texts );

  ChatMessageItem* itemAt( int row ) const;
  ChatMessageItem* itemForIndex( const QModelIndex& ) const;

  void ensureLayout( ChatMessageItem* ) const;
  qreal maxBubbleWidth() const;       // 62% of the viewport (S4e spec)
  QColor bubbleTextColor() const;

  int findPlainText( const QString& txt, int from_row, bool search_forward ) const;

private:
  ChatMessageItem* createItem( const Chat& c, const ChatMessage& cm );
  void appendBuiltItem( ChatMessageItem* item );
  void clearItems();
  void buildDocument( ChatMessageItem* item ) const;
  bool matchMarkdownRegistry( const ChatMessage& cm, QString* raw_out ) const;
  bool messageCanBeShowed( const ChatMessage& cm ) const;
  QString timestampTextFor( const ChatMessage& cm ) const;
  QString plainTextFromHtml( const QString& html ) const;
  QPixmap roundedAvatar( const User& u ) const;

  QList<ChatMessageItem*> m_items;
  QFont m_chatFont;
  int m_viewportWidth;

  QDateTime m_lastMessageTimestamp;
  VNumber m_lastMessageUserId;
  bool m_chatIsPrivate;
  bool m_isDefaultChat;

  QList< QPair<QString, QString> > m_markdownTexts; // raw -> formatHtmlText(raw)
  static const int MaxMarkdownTexts = 200;
};

inline int ChatMessageModel::viewportWidth() const { return m_viewportWidth; }

#endif // BEEBEEP_CHATMESSAGEMODEL_H
