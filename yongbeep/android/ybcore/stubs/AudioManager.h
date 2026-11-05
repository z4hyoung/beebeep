//------------------------------------------------------------------
// ybcore Android stub for src/gui/AudioManager.h
//
// CoreFileTransfer.cpp includes "AudioManager.h" (the real one lives in
// gui/ and depends on QtMultimedia + QSoundEffect). On Android the core
// library has no audio: isAudioDeviceAvailable() is false and every
// playback call is a no-op. Class layout mirrors the real header minus
// the voice-chat members (BEEBEEP_USE_VOICE_CHAT is never defined here)
// so all call sites in the core compile unchanged.
//------------------------------------------------------------------
#ifndef BEEBEEP_AUDIOMANAGER_H
#define BEEBEEP_AUDIOMANAGER_H

#include "Config.h"

class AudioManager
{
// Singleton Object
  static AudioManager* mp_instance;

public:
  bool isAudioDeviceAvailable() { return false; }
  bool loadBeepEffect() { return false; }
  void playBeep( int loops = 1 ) { Q_UNUSED( loops ); }
  void clearBeep() {}
  void playBuzz() {}

  static AudioManager& instance()
  {
    if( !mp_instance )
      mp_instance = new AudioManager();
    return *mp_instance;
  }

  static void close()
  {
    if( mp_instance )
    {
      mp_instance->clearBeep();
      delete mp_instance;
      mp_instance = Q_NULLPTR;
    }
  }

protected:
  AudioManager() {}
};

#endif // BEEBEEP_AUDIOMANAGER_H
