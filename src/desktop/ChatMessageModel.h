//////////////////////////////////////////////////////////////////////
//
// YongBEEP — chat message list model (S4e chatview-v2)
//
// Wraps a Chat's ChatMessage list for the QListView + delegate message
// rendering that replaced the QTextBrowser+HTML pipeline. Each item owns
// a cached QTextDocument (built once, laid out shrink-to-fit against the
// current viewport width) so paint() never re-parses rich text.
//
// Ordinary chat messages render as Markdown by default. Their original
// payload is retained independently of the document used for display,
// including when messages are restored from saved HTML archives.
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
  bool searchHighlight;

  bool showTimestamp;
  QString timestampText;
  QDateTime timestamp;
  bool showName;
  QString nameText;

  bool markdown;              // render with QTextDocument::setMarkdown
  QString markdownRaw;        // original raw markdown text
  QString sourceText;         // exact message payload for copying
  QString contentHtml;        // classic-pipeline inner HTML (no bubble table)
  QString plainText;          // for copy / quote / find

  QString fileUrl;            // FileCard: beefile:/file: link target
  QString fileFolderUrl;
  bool fileAvailable;
  QString fileName;
  QString fileStatus;
  FileSizeType fileProgressBytes;  // live "15M / 100M" progress (0 = not transferring)
  FileSizeType fileProgressTotal;
  qint64 fileProgressElapsed;      // ms since the transfer started (for the speed text)

  // YongBEEP partial text selection: text positions inside the laid out
  // QTextDocument, -1 = nothing selected (anchor == position = collapsed)
  int selectionAnchor;
  int selectionPosition;

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
         FileCardWidth = 300, FileCardHeight = 116 };

  explicit ChatMessageModel( QObject* parent = Q_NULLPTR );
  ~ChatMessageModel();

  int rowCount( const QModelIndex& parent = QModelIndex() ) const override;
  QVariant data( const QModelIndex& index, int role ) const override;

  void resetFromChat( const Chat& c, bool all_history = false );
  static QString savedHtmlForChat( const Chat& );
  void updateUserProfile( const User& );
  bool appendMessage( const Chat& c, const ChatMessage& cm );

  /*! YongBEEP: live "15M / 100M" progress on file cards. Matches the card
   * by file name + total size (the card carries no protocol file id). */
  void updateFileProgress( const QString& file_name, FileSizeType bytes, FileSizeType total, qint64 elapsed );

  /*! Morph a live file card into its final state (sent/received/failed):
   * same newest-first name+size match as updateFileProgress. */
  void updateFileStatus( const QString& file_name, FileSizeType total, const QString& status, const QString& file_path = QString(), bool completed = true );

  // --- YongBEEP partial text selection (delegate draws the highlight) ---
  void setItemSelection( int row, int anchor, int position );
  void clearAllSelections();
  void highlightSearchResult( int row, const QString& query );
  void selectWordAt( int row, int position );
  bool hasSelection( int row ) const;
  /*! Selected plain text of a row (empty when no selection). */
  QString selectedText( int row ) const;
  /*! Whole messages use their original source; selections retain Markdown formatting. */
  QString textToCopy( int row, bool selection_only = false ) const;

  void setChatFont( const QFont& );
  void setViewportWidth( int );
  inline int viewportWidth() const;

  // GUI-side Markdown registry: raw texts sent with the MD toggle on
  void addMarkdownText( const QString& raw_text );
  void registerMarkdownTexts( const QStringList& raw_texts );

  ChatMessageItem* itemAt( int row ) const;
  ChatMessageItem* itemForIndex( const QModelIndex& ) const;

  void ensureLayout( ChatMessageItem* ) const;
  static QString fileSizeText( FileSizeType );
  qreal maxBubbleWidth() const;       // 62% of the viewport (S4e spec)
  QColor bubbleTextColor() const;

  int findPlainText( const QString& txt, int from_row, bool search_forward ) const;

private:
  ChatMessageItem* createItem( const Chat& c, const ChatMessage& cm );
  void appendBuiltItem( ChatMessageItem* item );
  QList<ChatMessageItem*> itemsFromSavedHtml( const Chat&, const QString& );
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
