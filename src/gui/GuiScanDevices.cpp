//////////////////////////////////////////////////////////////////////
//
// YongBEEP — scan nearby devices dialog (S3-4b)
//
//////////////////////////////////////////////////////////////////////

#include "GuiScanDevices.h"
#include "BtManager.h"
#include "Core.h"
#include "Settings.h"
#include <QDesktopServices>
#include <QLabel>
#include <QListWidget>
#include <QProcess>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QHBoxLayout>

GuiScanDevices::GuiScanDevices( QWidget* parent )
  : QDialog( parent )
{
  setObjectName( "GuiScanDevices" );
  setWindowTitle( tr( "%1 - Scan devices" ).arg( Settings::instance().programName() ) );
  setMinimumWidth( 420 );

  mp_statusLabel = new QLabel( this );
  mp_statusLabel->setWordWrap( true );

  mp_deviceList = new QListWidget( this );
  mp_deviceList->setMinimumHeight( 110 );
  mp_deviceList->setAlternatingRowColors( true );

  QLabel* hint_label = new QLabel( this );
  hint_label->setWordWrap( true );
  hint_label->setText( tr( "If a device appears in the list but the connection does not start, "
                           "pair it first in the system bluetooth settings (one time only)." ) );
  QFont hint_font = hint_label->font();
  hint_font.setPointSizeF( hint_font.pointSizeF() * 0.9 );
  hint_label->setFont( hint_font );

  QLabel* hint_macos = new QLabel( this );
  hint_macos->setWordWrap( true );
  hint_macos->setText( tr( "macOS: open System Settings > Bluetooth, select the device and press Pair, "
                           "then confirm the pairing code." ) );
  hint_macos->setFont( hint_font );

  QLabel* hint_windows = new QLabel( this );
  hint_windows->setWordWrap( true );
  hint_windows->setText( tr( "Windows: open Settings > Devices > Bluetooth, click Add device, "
                             "choose the device and accept the pairing code." ) );
  hint_windows->setFont( hint_font );

  mp_pbOpenSettings = new QPushButton( tr( "Open system bluetooth settings" ), this );
  mp_pbConnect = new QPushButton( tr( "Connect" ), this );
  mp_pbConnect->setDefault( true );
  mp_pbConnect->setEnabled( false ); // enabled as soon as a device is found
  QPushButton* pb_close = new QPushButton( tr( "Close" ), this );

  QHBoxLayout* buttons_layout = new QHBoxLayout;
  buttons_layout->addWidget( mp_pbOpenSettings, 1 );
  buttons_layout->addStretch( 1 );
  buttons_layout->addWidget( mp_pbConnect, 0 );
  buttons_layout->addWidget( pb_close, 0 );

  QVBoxLayout* main_layout = new QVBoxLayout( this );
  main_layout->addWidget( mp_statusLabel );
  main_layout->addWidget( mp_deviceList, 1 );
  main_layout->addWidget( hint_label );
  main_layout->addWidget( hint_macos );
  main_layout->addWidget( hint_windows );
  main_layout->addLayout( buttons_layout );

  connect( mp_pbConnect, SIGNAL( clicked() ), this, SLOT( connectToDevice() ) );
  connect( mp_pbOpenSettings, SIGNAL( clicked() ), this, SLOT( openSystemBluetoothSettings() ) );
  connect( pb_close, SIGNAL( clicked() ), this, SLOT( reject() ) );

  // Read-only reuse of the existing discovery signal (no bluetooth logic here)
  if( beeCore->btManager() )
    connect( beeCore->btManager(), SIGNAL( btPeerFound( const QBluetoothAddress&, const QString& ) ),
             this, SLOT( onBtPeerFound( const QBluetoothAddress&, const QString& ) ) );

  updateStatus();
}

void GuiScanDevices::onBtPeerFound( const QBluetoothAddress& bt_address, const QString& device_name )
{
  if( bt_address.isNull() )
    return;
  m_devices.insert( bt_address.toString(), device_name.isEmpty() ? tr( "Unknown device" ) : device_name );
  rebuildDeviceList();
}

void GuiScanDevices::rebuildDeviceList()
{
  int current_row = mp_deviceList->currentRow();
  mp_deviceList->clear();
  for( QMap<QString, QString>::const_iterator it = m_devices.constBegin(); it != m_devices.constEnd(); ++it )
  {
    QListWidgetItem* item = new QListWidgetItem( tr( "%1  (%2)" ).arg( it.value(), it.key() ), mp_deviceList );
    item->setData( Qt::UserRole, it.key() );
    item->setData( Qt::UserRole + 1, it.value() );
  }
  if( current_row >= 0 && current_row < mp_deviceList->count() )
    mp_deviceList->setCurrentRow( current_row );
  else if( mp_deviceList->count() > 0 )
    mp_deviceList->setCurrentRow( 0 );

  mp_pbConnect->setEnabled( mp_deviceList->count() > 0 );
  updateStatus();
}

void GuiScanDevices::updateStatus()
{
  if( beeCore->btManager() && beeCore->btManager()->isActive() )
    mp_statusLabel->setText( tr( "Scanning for nearby %1 devices..." ).arg( Settings::instance().programName() ) );
  else
    mp_statusLabel->setText( tr( "Bluetooth discovery is not active. Start the connection or enable bluetooth in the system settings." ) );
}

void GuiScanDevices::connectToDevice()
{
  QListWidgetItem* item = mp_deviceList->currentItem();
  if( !item )
    return;

  QString bt_address = item->data( Qt::UserRole ).toString();
  QString device_name = item->data( Qt::UserRole + 1 ).toString();
  if( bt_address.isEmpty() )
    return;

  // Reuses the automatic discovery path (Core::btPeerFound slot) through the
  // minimal Core forwarder: same guards, same StreamDeviceBt handshake.
  beeCore->connectToBluetoothPeer( QBluetoothAddress( bt_address ), device_name );
  accept();
}

void GuiScanDevices::openSystemBluetoothSettings()
{
#ifdef Q_OS_MACOS
  // Deep link to the bluetooth pane; fall back to the generic System Settings.
  if( !QProcess::startDetached( QStringLiteral( "open" ), QStringList() << QStringLiteral( "x-apple.systempreferences:com.apple.Bluetooth-Settings.extension" ) ) )
    QProcess::startDetached( QStringLiteral( "open" ), QStringList() << QStringLiteral( "-b" ) << QStringLiteral( "com.apple.systempreferences" ) );
#elif defined( Q_OS_WIN )
  QDesktopServices::openUrl( QUrl( QStringLiteral( "ms-settings:bluetooth" ) ) );
#else
  QDesktopServices::openUrl( QUrl( QStringLiteral( "https://www.qt.io" ) ) );
#endif
}
