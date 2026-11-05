//------------------------------------------------------------------
// ybcore Android stub for src/desktop/GuiChatMessage.h
//
// ChatManager.cpp and SaveChatList.cpp call the static
// GuiChatMessage::chatToHtml() to render a chat as HTML when saving
// chat history. The real implementation lives in desktop/ and drags in
// the whole WeChat-bubble rendering (QPainter/QPainterPath/YongTheme).
// This stub keeps the same signatures and returns an empty document:
// chat saving on Android produces an empty file until the A1.3+ shell
// provides its own renderer. No source file in source/src is touched.
//------------------------------------------------------------------
#ifndef BEEBEEP_GUICHATMESSAGE_H
#define BEEBEEP_GUICHATMESSAGE_H

#include "Config.h"
#include <QDateTime>
class Chat;
class ChatMessage;
class User;


class GuiChatMessage
{
public:
  static QString datetimestampToString( const ChatMessage&, bool show_timestamp, bool show_datestamp )
  {
    Q_UNUSED( show_timestamp );
    Q_UNUSED( show_datestamp );
    return QString();
  }

  static QString chatToHtml( const Chat&, bool skip_file_transfers, bool skip_system_message, bool force_timestamp, bool force_datestamp,
                             bool use_chat_compact, bool skip_cannot_be_saved_messages )
  {
    Q_UNUSED( skip_file_transfers );
    Q_UNUSED( skip_system_message );
    Q_UNUSED( force_timestamp );
    Q_UNUSED( force_datestamp );
    Q_UNUSED( use_chat_compact );
    Q_UNUSED( skip_cannot_be_saved_messages );
    return QString();
  }

  static QString formatMessage( const User&, const ChatMessage&, VNumber, bool, bool, bool, bool, bool, bool,
                                const QDateTime& = QDateTime(), bool = false )
  {
    return QString();
  }

  static QString formatSystemMessage( const ChatMessage&, VNumber, bool, bool, bool )
  {
    return QString();
  }
};

#endif // BEEBEEP_GUICHATMESSAGE_H
