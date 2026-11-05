//------------------------------------------------------------------
// ybcore Android stub for src/utils/Updater.h
//
// Core.cpp creates an Updater, invokes "checkForNewVersion" by name via
// QMetaObject::invokeMethod and reads versionAvailable()/downloadUrl()/
// news(). On Android the core library never phones home for updates, so
// the stub reports "no new version" (empty strings) and completes the
// job immediately. Q_OBJECT + moc keep connect()/qobject_cast() working.
//------------------------------------------------------------------
#ifndef BEEBEEP_UPDATER_H
#define BEEBEEP_UPDATER_H

#include "Config.h"

class Updater : public QObject
{
  Q_OBJECT

public:
  explicit Updater( QObject *parent = Q_NULLPTR ) : QObject( parent ) {}

  inline const QString& versionAvailable() const { return m_versionAvailable; }
  inline const QString& downloadUrl() const { return m_downloadUrl; }
  inline const QString& news() const { return m_news; }

signals:
  void newVersionAvailable( const QString& );
  void jobCompleted();

public slots:
  void checkForNewVersion() { emit jobCompleted(); }

private:
  QString m_versionAvailable;
  QString m_downloadUrl;
  QString m_news;
};

#endif // BEEBEEP_UPDATER_H
