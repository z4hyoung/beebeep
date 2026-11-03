//////////////////////////////////////////////////////////////////////
//
// BeeBEEP Copyright (C) 2010-2026 Marco Mastroddi
//
// BeeBEEP is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published
// by the Free Software Foundation, either version 3 of the License,
// or (at your option) any later version.
//
// BeeBEEP is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with BeeBEEP. If not, see <http://www.gnu.org/licenses/>.
//
// Author: Marco Mastroddi <marco.mastroddi(AT)gmail.com>
//
// $Id: NetworkMonitor.cpp 1602 2026-09-24 08:46:55Z mastroddi $
//
//////////////////////////////////////////////////////////////////////

#include "NetworkMonitor.h"

NetworkMonitor::NetworkMonitor( QObject* parent )
  : QObject( parent ), m_wasOnline( false )
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
  , mp_netConfManager( Q_NULLPTR )
#endif
{
#if defined( BEEBEEP_DEBUG )
  qDebug() << "[NetworkMonitor] Initializing NetworkMonitor...";
#endif

#if QT_VERSION >= QT_VERSION_CHECK(6, 1, 0)
  // Qt 6: Register backend notification callbacks
  if( QNetworkInformation::loadDefaultBackend() )
  {
    auto net_info = QNetworkInformation::instance();
    if( net_info )
    {
#if defined( BEEBEEP_DEBUG )
      qDebug() << "[NetworkMonitor] QNetworkInformation backend loaded successfully:" << net_info->backendName();
#endif
      connect( net_info, &QNetworkInformation::reachabilityChanged,
               this, &NetworkMonitor::checkNetworkStatus );
      connect( net_info, &QNetworkInformation::isOnlineChanged,
               this, &NetworkMonitor::checkNetworkStatus );
    }
  }
  else
  {
    qWarning() << "Failed to load QNetworkInformation default backend.";
  }
#elif QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
  // Qt 5: Register network configuration manager callbacks
  mp_netConfManager = new QNetworkConfigurationManager;
#if defined( BEEBEEP_DEBUG )
  qDebug() << "[NetworkMonitor] QNetworkConfigurationManager initialized for Qt 5.";
#endif

  connect( mp_netConfManager, &QNetworkConfigurationManager::configurationChanged,
           this, &NetworkMonitor::checkNetworkStatus );
  connect( mp_netConfManager, &QNetworkConfigurationManager::onlineStateChanged,
           this, &NetworkMonitor::checkNetworkStatus );
#endif
}

NetworkMonitor::~NetworkMonitor()
{
#if defined( BEEBEEP_DEBUG )
  qDebug() << "[NetworkMonitor] Destroying NetworkMonitor instance.";
#endif
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
  delete mp_netConfManager;
#endif
}

void NetworkMonitor::checkNetworkStatus()
{
#if defined( BEEBEEP_DEBUG )
  qDebug() << "[NetworkMonitor] Public checkNetworkStatus() called. Starting scan...";
#endif
  checkInterfaces();
}

void NetworkMonitor::checkInterfaces()
{
  qDebug() << "NetworkMonitor scan all system network interfaces...";
  QSet<QString> current_interfaces;
  const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();

  qDebug() << "NetworkMonitor has found" << interfaces.size() << "raw interfaces";

  for( const QNetworkInterface& net_interface : interfaces )
  {
    const QString iface_name = net_interface.name();
    QNetworkInterface::InterfaceFlags flags = net_interface.flags();
    qDebug() << "NetworkMonitor check interface:" << iface_name
             << "| Flags:" << flags
             << "| Human Readable:" << net_interface.humanReadableName();

    // Filter out inactive or loopback interfaces
    if( flags.testFlag( QNetworkInterface::IsLoopBack ) )
    {
      qDebug() << "[NetworkMonitor skip loopback interface:" << iface_name;
      continue;
    }

    if( !flags.testFlag( QNetworkInterface::IsUp ) || !flags.testFlag( QNetworkInterface::IsRunning ) )
    {
      qDebug() << "NetworkMonitor skip down/inactive interface:" << iface_name;
      continue;
    }

#if QT_VERSION >= QT_VERSION_CHECK( 5, 11, 0 )
    if( net_interface.type() == QNetworkInterface::Virtual )
    {
      qDebug() << "NetworkMonitor skip virtual interface type:" << iface_name;
      continue;
    }
#endif

    // Blacklist VPN
    const QString lower_name = iface_name.toLower();
    if( lower_name.startsWith( "docker" )  ||
        lower_name.startsWith( "veth" )    ||
        lower_name.startsWith( "virbr" )   ||
        lower_name.startsWith( "br-" )     ||
        lower_name.startsWith( "vboxnet" ) ||
        lower_name.startsWith( "vmnet" )   ||
        lower_name.startsWith( "tun" ) )
    {
      qDebug() << "NetworkMonitor skip virtual/container interface by name:" << iface_name;
      continue;
    }

    // Inspect assigned IP addresses
    QHostAddress valid_address;
    const QList<QNetworkAddressEntry> entries = net_interface.addressEntries();

    qDebug() << "NetworkMonitor found interface" << iface_name << "has" << entries.size() << "address entry/entries.";

    for( const QNetworkAddressEntry& entry : entries )
    {
      QHostAddress addr = entry.ip();

      if( addr.isLoopback() || addr.isLinkLocal() )
      {
        qDebug() << "NetworkMonitor ignore loopback or link-local IP:" << addr.toString() << "on" << iface_name;
        continue;
      }

      if( addr.protocol() == QAbstractSocket::IPv4Protocol )
      {
        qDebug() << "NetworkMonitor found valid IPv4 address:" << addr.toString() << "on" << iface_name;
        valid_address = addr;
        break;
      }
      else if( addr.protocol() == QAbstractSocket::IPv6Protocol )
      {
        qDebug() << "NetworkMonitor found valid IPv6 address:" << addr.toString() << "on" << iface_name;
        valid_address = addr;
        break;
      }
    }

    // Ignore interfaces without a valid IPv4/IPv6 address
    if( valid_address.isNull() )
    {
      qDebug() << "NetworkMonitor found interface" << iface_name << "has no valid IP address assigned yet.";
      continue;
    }

    current_interfaces.insert( iface_name );

    // Check if this interface was previously inactive
    if( !m_activeInterfaces.contains( iface_name ) )
    {
      qDebug() << "NetworkMonitor detected new active interface:" << iface_name << "with IP" << valid_address.toString();
      emit interfaceStateChanged( true, valid_address, iface_name );
      emit networkConfigurationChanged();
    }
  }

  // Detect interfaces that were active previously but are now disconnected
  for( const QString& old_iface : qAsConst( m_activeInterfaces ) )
  {
    if( !current_interfaces.contains( old_iface ) )
    {
      qDebug() << "NetworkMonitor found interface disconnected:" << old_iface;
      emit interfaceStateChanged( false, QHostAddress(), old_iface );
      emit networkConfigurationChanged();
    }
  }

  // Update stored active interfaces
  m_activeInterfaces = current_interfaces;

  // Evaluate overall online status
  const bool currently_online = isOnline();

  qDebug() << "NetworkMonitor scan finished. Active interfaces count:" << m_activeInterfaces.size();

  if( currently_online != m_wasOnline )
  {
    m_wasOnline = currently_online;
    qDebug() << "NetworkMonitor changed state to" << ( currently_online ? "ONLINE" : "OFFLINE" );
    emit onlineStateChanged( currently_online );
  }
  else
  {
    qDebug() << "NetworkMonitor overall state remains unchanged (" << ( currently_online ? "ONLINE" : "OFFLINE" ) << ").";
  }
}
