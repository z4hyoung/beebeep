//------------------------------------------------------------------
// ybcore Android stub implementation for BeeApplication (see
// stubs/BeeApplication.h). One global instance exists inside the
// library so that `beeApp` is never a null pointer in the core.
//------------------------------------------------------------------
#include "BeeApplication.h"
#include "AudioManager.h"

BeeApplication::BeeApplication( QObject* parent )
  : QObject( parent )
{
  m_checkSettingsFilePath = false;
}

void BeeApplication::addJob( QObject* ) { /* no desktop job thread on Android */ }
void BeeApplication::removeJob( QObject* ) {}
void BeeApplication::setSettingsFilePath( const QString& file_path ) { m_settingsFilePath = file_path; }

// AudioManager stub singleton storage (never instantiated, instance() unused in core)
AudioManager* AudioManager::mp_instance = Q_NULLPTR;

// The one and only application object of the core library
BeeApplication bee_app_instance;
BeeApplication* beeApp = &bee_app_instance;
BeeApplication* BeeApplication::mp_instance = &bee_app_instance;
