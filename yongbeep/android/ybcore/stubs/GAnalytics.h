//------------------------------------------------------------------
// ybcore Android stub for src/utils/GAnalytics.h
//
// Core.cpp creates a GAnalytics and invokes "doPost" by name via
// QMetaObject::invokeMethod. On Android the core library never sends
// usage statistics: doPost() finishes the job immediately.
//------------------------------------------------------------------
#ifndef BEEBEEP_GANALYTICS_H
#define BEEBEEP_GANALYTICS_H

#include "Config.h"


class GAnalytics: public QObject
{
  Q_OBJECT

public:
  explicit GAnalytics( QObject* parent = Q_NULLPTR ) : QObject( parent ) {}

public slots:
  void doPost() { emit jobFinished(); }

signals:
  void jobFinished();
};

#endif // BEEBEEP_GANALYTICS_H
