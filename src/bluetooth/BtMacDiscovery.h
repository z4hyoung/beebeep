#ifndef YONGBEEP_BTMACDISCOVERY_H
#define YONGBEEP_BTMACDISCOVERY_H
#include <QObject>
#include <QtBluetooth/QBluetoothAddress>

// Native, asynchronous SDP queries for paired computers. Each Objective-C
// callback owns its lifetime and checks a generation token before reporting.
class BtMacDiscovery : public QObject
{
  Q_OBJECT
public:
  explicit BtMacDiscovery( QObject* parent = nullptr );
  void start();
  void stop();
  void complete( quint64 generation, const QBluetoothAddress&, const QString&, quint16 channel, int error );
signals:
  void peerFound( const QBluetoothAddress&, const QString& );
  void statusChanged( const QString& );
  void finished();
private:
  quint64 m_generation = 0;
  int m_pending = 0;
  int m_found = 0;
  bool m_active = false;
  QString m_lastError;
};
#endif
