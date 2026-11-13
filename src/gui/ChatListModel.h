//////////////////////////////////////////////////////////////////////
//
// YongBEEP — WeChat-style chat list model (S3-4a)
//
// Wraps ChatManager's chat list into a QAbstractListModel for the
// QListView + delegate rendering in GuiMainWindow. Group chats are
// not exposed (PRD 3.4 removal list); the default chat and 1:1
// private chats are shown, sorted by their last message time.
//
//////////////////////////////////////////////////////////////////////

#ifndef BEEBEEP_CHATLISTMODEL_H
#define BEEBEEP_CHATLISTMODEL_H

#include "Config.h"
#include <QAbstractListModel>
#include <QIcon>
#include <QMap>

class Chat;


class ChatListModel : public QAbstractListModel
{
  Q_OBJECT

public:
  enum DataRole
  {
    ChatIdRole = Qt::UserRole + 1,
    AvatarRole,      // QIcon (grayscaled when peer is offline)
    NameRole,        // QString display name
    SummaryRole,     // QString plain-text last message (single line)
    TimeTextRole,    // QString relative time (hh:mm / Yesterday / MM-dd)
    UnreadRole,      // int unread badge count
    SearchRowRole,   // full-history message row, -1 for a contact
    SearchQueryRole,
    SearchKindRole,
    TransportsRole   // Core::Transports, authenticated links for a private chat
  };

  enum SearchKind { ContactResult, MessageResult, FileResult };

  explicit ChatListModel( QObject* parent = Q_NULLPTR );

  int rowCount( const QModelIndex& parent = QModelIndex() ) const override;
  QVariant data( const QModelIndex& index, int role ) const override;

  void reload();
  void setFilter( const QString& filter_text );

  VNumber chatIdAt( int row ) const;
  int rowOfChat( VNumber chat_id ) const;

private:
  QString chatDisplayName( const Chat& c ) const;

  struct SearchResult
  {
    VNumber chatId;
    int messageRow;
    int kind;
    QString name;
    QString summary;
    QString time;
    QIcon icon;
  };
  QMap<VNumber, QString> m_summaries;
  QMap<VNumber, QDateTime> m_messageTimes;
  struct HistorySummary
  {
    QString html;
    QString text;
    QDateTime timestamp;
  };
  QMap<VNumber, HistorySummary> m_historySummaries;
  QList<SearchResult> m_results;
  QList<VNumber> m_chatIds;
  QString m_filter;

};

#endif // BEEBEEP_CHATLISTMODEL_H
