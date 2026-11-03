//////////////////////////////////////////////////////////////////////
//
// YongBEEP — WeChat-style main window (S3-4a)
//
// New lean main window (route B of docs/analysis/gui-redesign.md §H.1):
// QSplitter with a ~260px conversation list (QListView + custom
// delegate) on the left and an embedded GuiChat on the right, one
// instance per chat id. The legacy GuiMain (5400 lines of menus,
// tabs and floating chats) stays in the tree for reference but is no
// longer instantiated by Main.cpp.
//
//////////////////////////////////////////////////////////////////////

#ifndef BEEBEEP_GUIMAINWINDOW_H
#define BEEBEEP_GUIMAINWINDOW_H

#include "Config.h"
#include "FileInfo.h"
#include <QMainWindow>

class Chat;
class ChatListModel;
class ChatMessage;
class GuiChat;
class GuiSystemTray;
class QLabel;
class QLineEdit;
class QListView;
class QMenu;
class QSessionManager;
class QStackedWidget;
class User;


class GuiMainWindow : public QMainWindow
{
  Q_OBJECT

public:
  GuiMainWindow( QWidget* parent = Q_NULLPTR );

  void takeScreenshot( const QString& file_path );

public slots:
  void loadSession();
  void checkWindowFlagsAndShow();
  void startCore();
  void forceShutdown();
  void onTickEvent( int );
  void onSleepRequest();
  void onWakeUpRequest();
  void showUp();
  void raiseOnTop();
  void saveSession( QSessionManager& );
  void loadStyle();

protected:
  void closeEvent( QCloseEvent* );

private slots:
  void onCoreConnected();
  void onCoreDisconnected();
  void onUserChanged( const User& );
  void onUserConnectionStatusChanged( const User& );
  void onNewChatMessage( const Chat&, const ChatMessage& );
  void onChatChanged( const Chat& );
  void onChatRemoved( const Chat& );
  void onSavedChatListAvailable();
  void onFileDownloadRequest( const User&, const FileInfo& );
  void onFolderDownloadRequest( const User&, const QString&, const QList<FileInfo>& );
  void showChat( VNumber chat_id );
  void chatActivated( const QModelIndex& );
  void filterTextChanged( const QString& );
  void sendMessage( VNumber, const QString&, bool );
  void sendFileFromChat( VNumber, const QString& );
  void sendFilesFromChat( VNumber, const QStringList& );
  void clearChat( VNumber );
  void openUrlFromChat( const QUrl&, VNumber );
  void trayIconActivated( QSystemTrayIcon::ActivationReason );
  void toggleVisibleFromTray();
  void quitFromTray();
  void hideToTrayIcon();
  void showMessage( const QString&, int );
  void onApplicationFocusChanged( QWidget*, QWidget* );

private:
  void createGui();
  void setupCoreConnections();
  void setupChatConnections( GuiChat* );
  GuiChat* chatWidget( VNumber chat_id, bool create_if_missing );
  void selectFirstChat();
  void refreshChatList( bool restore_selection );
  void updateChatHeader();
  void updateWindowTitle();
  void showAlertForMessage( const Chat&, const ChatMessage& );
  bool showWizard();

private:
  QLineEdit* mp_searchBox;
  QListView* mp_chatListView;
  ChatListModel* mp_chatListModel;
  QLabel* mp_headerName;
  QLabel* mp_headerStatus;
  QStackedWidget* mp_chatStack;
  QLabel* mp_chatPlaceholder;

  QMap<VNumber, GuiChat*> m_chatWidgets;
  VNumber m_currentChatId;

  GuiSystemTray* mp_trayIcon;
  QMenu* mp_menuTrayIcon;

  bool m_forceShutdown;
  bool m_coreIsConnecting;

};

#endif // BEEBEEP_GUIMAINWINDOW_H
