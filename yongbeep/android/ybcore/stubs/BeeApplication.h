//------------------------------------------------------------------
// ybcore Android stub for src/desktop/BeeApplication.h
//
// The real BeeApplication (src/desktop/) is a QApplication subclass.
// The Android core library must not link QtWidgets, so this stub is a
// plain QObject exposing exactly the API the core uses
// (Core.cpp / CoreChat.cpp / CoreFileTransfer.cpp / Settings.cpp):
//   addJob / removeJob / setSettingsFilePath / setCheckSettingsFilePath
// Unlike the desktop header it does NOT define a beeApp macro: the
// symbol `beeApp` is a global pointer defined in BeeApplication_stub.cpp
// (pointing at the bee_app_instance global), so `if( beeApp )` checks
// and `beeApp->addJob( obj )` calls in the core compile and link as-is.
// Jobs are accepted but not scheduled (no desktop job thread here).
//------------------------------------------------------------------
#ifndef BEEBEEP_APPLICATION_H
#define BEEBEEP_APPLICATION_H

#include "Config.h"
#include <QObject>

class BeeApplication : public QObject
{
public:
  explicit BeeApplication( QObject* parent = Q_NULLPTR );

  static BeeApplication* instance() { return mp_instance; }

  void addJob( QObject* job );
  void removeJob( QObject* job );

  void setSettingsFilePath( const QString& );
  inline const QString& settingsFilePath() const { return m_settingsFilePath; }

  inline void setCheckSettingsFilePath( bool new_value ) { m_checkSettingsFilePath = new_value; }
  inline bool checkSettingsFilePath() const { return m_checkSettingsFilePath; }

private:
  static BeeApplication* mp_instance;

  QString m_settingsFilePath;
  bool m_checkSettingsFilePath;
};

// Global instance pointer used by the core sources (see BeeApplication_stub.cpp)
extern BeeApplication* beeApp;

#endif // BEEBEEP_APPLICATION_H
