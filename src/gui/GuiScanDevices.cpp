//////////////////////////////////////////////////////////////////////
//
// YongBEEP — scan nearby devices dialog (S3-4b)
//
//////////////////////////////////////////////////////////////////////

#include "GuiScanDevices.h"
#include "BtManager.h"
#include "Core.h"
#include "Settings.h"
#include "UserManager.h"
#include <QTimer>
#include <QDesktopServices>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QHBoxLayout>

GuiScanDevices::GuiScanDevices( QWidget* parent )
  : QDialog( parent )
{
  setObjectName( "GuiScanDevices" );
  setWindowTitle( tr( "Connection & devices" ) );
  setMinimumWidth( 440 );
  setMaximumWidth( 540 );
  setStyleSheet( "QDialog { background: #FAFAFA; } QLabel { color: #333333; font-size: 13px; } "
                 "QPushButton { padding: 6px 12px; }" );
  QVBoxLayout* main_layout = new QVBoxLayout( this );
  main_layout->setContentsMargins( 24, 22, 24, 22 );
  main_layout->setSpacing( 16 );
  QLabel* title = new QLabel( tr( "Connection & devices" ), this );
  title->setStyleSheet( "font-size: 18px; font-weight: 600;" );
  main_layout->addWidget( title );
  QLabel* introduction = new QLabel( tr( "Nearby devices connect automatically over LAN. Use this page to check connections or Bluetooth pairing." ), this );
  introduction->setWordWrap( true );
  introduction->setStyleSheet( "color: #808080; font-size: 12px;" );
  main_layout->addWidget( introduction );

  mp_statusLabel = new QLabel( this );
  mp_statusLabel->setObjectName( "GuiConnectionStatus" );
  mp_statusLabel->setTextFormat( Qt::PlainText );
  mp_statusLabel->setWordWrap( true );
  mp_statusLabel->setStyleSheet( "background: white; border: 1px solid #E7E7E7; border-radius: 8px; padding: 14px;" );
  main_layout->addWidget( mp_statusLabel );

  mp_deviceList = new QListWidget( this );
  mp_deviceList->setMinimumHeight( 110 );
  mp_deviceList->setAlternatingRowColors( true );
  mp_pbConnect = new QPushButton( tr( "Connect" ), this );
  mp_pbConnect->setEnabled( false );
  main_layout->addWidget( new QLabel( tr( "Bluetooth devices running YongBEEP" ), this ) );
  mp_searchStatus = new QLabel( tr( "Search for devices, then select one to connect." ), this );
  mp_searchStatus->setObjectName( "GuiBluetoothSearchStatus" );
  mp_searchStatus->setWordWrap( true );
  mp_searchStatus->setStyleSheet( "color: #808080; font-size: 12px;" );
  main_layout->addWidget( mp_searchStatus );
  main_layout->addWidget( mp_deviceList );
  QLabel* hint = new QLabel( this );
  hint->setWordWrap( true );
  hint->setStyleSheet( "color: #808080; font-size: 12px;" );
#if defined( Q_OS_MACOS )
  hint->setText( tr( "Mac can search paired computers and start Bluetooth connections. Pair once in system Bluetooth settings and keep YongBEEP open on both computers." ) );
#else
  hint->setText( tr( "Bluetooth devices are discovered automatically. If a connection fails, pair the computers once in system Bluetooth settings, then refresh the list." ) );
#endif
  main_layout->addWidget( hint );
  QHBoxLayout* buttons = new QHBoxLayout;
  mp_pbOpenSettings = new QPushButton( tr( "Bluetooth settings" ), this );
  mp_pbOpenSettings->setObjectName( "GuiBluetoothSettingsButton" );
  buttons->addWidget( mp_pbOpenSettings );
  buttons->addStretch( 1 );
#if defined( Q_OS_MACOS )
  mp_pbSearch = new QPushButton( tr( "Search paired devices" ), this );
#else
  mp_pbSearch = new QPushButton( tr( "Refresh devices" ), this );
#endif
  mp_pbSearch->setObjectName( "GuiBluetoothSearchButton" );
  connect( mp_pbSearch, &QPushButton::clicked, this, &GuiScanDevices::startSearch );
  buttons->addWidget( mp_pbSearch );
  buttons->addWidget( mp_pbConnect );
  QPushButton* close = new QPushButton( tr( "Close" ), this );
  close->setDefault( true );
  buttons->addWidget( close );
  main_layout->addLayout( buttons );
  connect( mp_pbConnect, &QPushButton::clicked, this, &GuiScanDevices::connectToDevice );
  connect( mp_pbOpenSettings, &QPushButton::clicked, this, &GuiScanDevices::openSystemBluetoothSettings );
  connect( close, &QPushButton::clicked, this, &QDialog::reject );
  if( beeCore->btManager() )
  {
    connect( beeCore->btManager(), &BtManager::btPeerFound, this, &GuiScanDevices::onBtPeerFound );
    connect( beeCore->btManager(), &BtManager::manualSearchStatusChanged, mp_searchStatus, &QLabel::setText );
    connect( beeCore->btManager(), &BtManager::manualSearchFinished, this, [this]() { mp_pbSearch->setEnabled( true ); } );
  }
  connect( beeCore, &Core::bluetoothConnectionFailed, this, [this]( const QString& reason )
  {
    mp_searchStatus->setText( tr( "Bluetooth connection failed: %1. Check YongBEEP on the other device, then retry." ).arg( reason ) );
  } );
  QTimer* refresh_status = new QTimer( this );
  refresh_status->setInterval( 1000 );
  connect( refresh_status, &QTimer::timeout, this, [this]() { if( isVisible() ) updateStatus(); } );
  refresh_status->start();
  updateStatus();
}


void GuiScanDevices::startSearch()
{
  if( !beeCore->isConnected() || !beeCore->btManager() || !beeCore->btManager()->isActive() )
  {
    mp_searchStatus->setText( tr( "Bluetooth is not ready. Turn on Bluetooth and allow YongBEEP to use it, then retry." ) );
    return;
  }
  mp_pbSearch->setEnabled( false );
  mp_searchStatus->setText( tr( "Searching for YongBEEP devices…" ) );
  beeCore->btManager()->triggerManualScan();
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
  QStringList lines;
  lines << (beeCore->isConnected() ? tr( "LAN · automatic discovery enabled" ) : tr( "LAN · not connected" ));
  const bool bluetooth_ready = beeCore->btManager() && beeCore->btManager()->isActive();
#if defined( Q_OS_MACOS )
  lines << (bluetooth_ready && beeCore->btManager()->listenerPort() > 0 ? tr( "Bluetooth · ready" ) : tr( "Bluetooth · not ready" ));
#else
  lines << (bluetooth_ready ? tr( "Bluetooth · automatic discovery enabled" ) : tr( "Bluetooth · not ready" ));
#endif
  lines << QString() << tr( "Connected devices" );
  bool has_peers = false;
  for( const User& peer : UserManager::instance().userList().toList() )
  {
    if( peer.isLocal() ) continue;
    const Core::Transports routes = beeCore->connectedTransports( peer.id() );
    if( !routes ) continue;
    QStringList links;
    if( routes & Core::Lan ) links << tr( "LAN" );
    if( routes & Core::Bluetooth ) links << tr( "Bluetooth" );
    lines << QString( "%1 · %2" ).arg( peer.name(), links.join( " + " ) );
    has_peers = true;
  }
  if( !has_peers ) lines << tr( "No devices connected yet" );
  mp_statusLabel->setText( lines.join( QLatin1Char( '\n' ) ) );
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
  mp_searchStatus->setText( tr( "Connecting to %1…" ).arg( device_name ) );
}

void GuiScanDevices::openSystemBluetoothSettings()
{
#ifdef Q_OS_MACOS
  // The installed Bluetooth.appex declares com.apple.BluetoothSettings.
  // Ask Launch Services directly: spawning `open` only reports whether that
  // process started, so a rejected settings URL used to fail silently.
  bool opened = QDesktopServices::openUrl( QUrl( QStringLiteral( "x-apple.systempreferences:com.apple.BluetoothSettings" ) ) );
  if( !opened )
    opened = QDesktopServices::openUrl( QUrl( QStringLiteral( "x-apple.systempreferences:" ) ) );
#elif defined( Q_OS_WIN )
  const bool opened = QDesktopServices::openUrl( QUrl( QStringLiteral( "ms-settings:bluetooth" ) ) );
#else
  const bool opened = false;
#endif
  if( !opened )
  {
    qWarning() << "Unable to open system Bluetooth settings";
    mp_searchStatus->setText( tr( "Unable to open Bluetooth settings. Open your system settings and select Bluetooth." ) );
  }
}
