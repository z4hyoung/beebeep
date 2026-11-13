#include "BtMacDiscovery.h"
#include "StreamDeviceBt.h"
#include <QPointer>
#include <QTimer>
#include <QDebug>
#include <QSet>
#import <IOBluetooth/IOBluetooth.h>

@interface YBServiceQuery : NSObject
{
@public
  QPointer<BtMacDiscovery> owner;
  quint64 generation;
  IOBluetoothSDPUUID* serviceUuid;
  IOBluetoothDevice* queriedDevice;
}
- (void)sdpQueryComplete:(IOBluetoothDevice*)device status:(IOReturn)status;
@end

@implementation YBServiceQuery
- (void)sdpQueryComplete:(IOBluetoothDevice*)device status:(IOReturn)status
{
  qDebug() << "Native Mac SDP callback" << QString::fromNSString( device.name ) << "status" << status;
  BluetoothRFCOMMChannelID channel = 0;
  if( status == kIOReturnSuccess )
  {
    IOBluetoothSDPServiceRecord* record = [device getServiceRecordForUUID:serviceUuid];
    if( record ) [record getRFCOMMChannelID:&channel];
  }
  QString address = QString::fromNSString( device.addressString );
  address.replace( '-', ':' );
  const QString name = QString::fromNSString( device.name ?: device.addressString );
  if( owner ) owner->complete( generation, QBluetoothAddress( address ), name, channel, status );
  [self release]; // kept alive from the start of this asynchronous query
}
- (void)dealloc
{
  [serviceUuid release];
  [queriedDevice release];
  [super dealloc];
}
@end

BtMacDiscovery::BtMacDiscovery( QObject* parent ) : QObject( parent ) {}

void BtMacDiscovery::start()
{
  if( m_active ) return;
  m_active = true;
  const quint64 generation = ++m_generation;
  m_found = 0;
  m_lastError.clear();
  const QByteArray bytes = StreamDeviceBt::serviceUuid().toRfc4122();
  IOBluetoothSDPUUID* uuid = [IOBluetoothSDPUUID uuidWithBytes:bytes.constData() length:bytes.size()];
  NSMutableArray* computers = [NSMutableArray array];
  QSet<QString> seenAddresses;
  for( IOBluetoothDevice* device in [IOBluetoothDevice pairedDevices] )
  {
    if( device.deviceClassMajor != kBluetoothDeviceClassMajorComputer &&
        device.deviceClassMajor != kBluetoothDeviceClassMajorPhone ) continue;
    const QString address = QString::fromNSString( device.addressString ).toUpper();
    if( seenAddresses.contains( address ) ) continue;
    seenAddresses.insert( address );
    // pairedDevices can contain duplicate entries; query each physical device once.
    [computers addObject:[IOBluetoothDevice deviceWithAddressString:device.addressString]];
  }
  m_pending = int( computers.count );
  emit statusChanged( tr( "Searching paired computers for YongBEEP…" ) );
  qDebug() << "Native Mac Bluetooth search checks" << m_pending << "paired computers/phones";
  if( !m_pending )
  {
    m_active = false;
    emit statusChanged( tr( "No paired computers found. Pair the Windows computer in Bluetooth settings first." ) );
    emit finished();
    return;
  }
  for( IOBluetoothDevice* device in computers )
  {
    QString address = QString::fromNSString( device.addressString ); address.replace( '-', ':' );
    const QString name = QString::fromNSString( device.name ?: device.addressString );
    // Paired computers can already have a populated service table. Opening
    // RFCOMM also establishes the baseband link, so do not require it first.
    // The normal encrypted handshake verifies the peer before it goes online.
    BluetoothRFCOMMChannelID channel = 0;
    IOBluetoothSDPServiceRecord* record = [device getServiceRecordForUUID:uuid];
    if( record && [record getRFCOMMChannelID:&channel] == kIOReturnSuccess && channel >= 1 && channel <= 30 )
    {
      qDebug() << "Native Mac uses known service channel" << name << channel;
      complete( generation, QBluetoothAddress( address ), name, channel, 0 );
      continue;
    }
    YBServiceQuery* query = [[YBServiceQuery alloc] init];
    query->owner = this;
    query->generation = generation;
    query->serviceUuid = [uuid retain];
    // IOBluetooth does not keep the caller's device object alive. Qt drains
    // its autorelease pool between events, so own it until the callback.
    query->queriedDevice = [device retain];
    qDebug() << "Native Mac SDP query" << name << address;
    // Query the full table, then match only our service UUID in the callback.
    // The filtered API can return success without delivering a callback for
    // a paired Windows computer; the full query returns its current channel.
    const IOReturn error = [device performSDPQuery:query];
    if( error != kIOReturnSuccess )
    {
      complete( generation, QBluetoothAddress( address ), name, 0, error );
      [query release];
    }
  }
  QTimer::singleShot( 15000, this, [this, generation]()
  {
    if( !m_active || generation != m_generation ) return;
    qWarning() << "Native Mac Bluetooth search timed out with" << m_pending << "queries pending";
    m_active = false;
    ++m_generation; // late native callbacks remain alive but cannot touch a new search
    emit statusChanged( m_found ? tr( "Found %1 YongBEEP device(s)." ).arg( m_found )
        : tr( "Bluetooth search timed out. Keep YongBEEP open on Windows and try again." ) );
    emit finished();
  } );
}

void BtMacDiscovery::stop()
{
  const bool was_active = m_active;
  m_active = false;
  ++m_generation;
  if( was_active ) emit finished();
}

void BtMacDiscovery::complete( quint64 generation, const QBluetoothAddress& address, const QString& name, quint16 channel, int error )
{
  if( !m_active || generation != m_generation ) return;
  qDebug() << "Native Mac SDP result" << name << address.toString() << "channel" << channel << "error" << error;
  if( !address.isNull() && channel >= 1 && channel <= 30 )
  {
    ++m_found;
    StreamDeviceBt::rememberRfcommChannel( address, channel );
    emit peerFound( address, name );
  }
  else
    m_lastError = error ? tr( "%1: Bluetooth service query failed (%2)." ).arg( name ).arg( error )
        : tr( "%1 is paired, but its YongBEEP service is not available." ).arg( name );
  if( --m_pending == 0 )
  {
    m_active = false;
    emit statusChanged( m_found ? tr( "Found %1 YongBEEP device(s)." ).arg( m_found ) : m_lastError );
    emit finished();
  }
}
