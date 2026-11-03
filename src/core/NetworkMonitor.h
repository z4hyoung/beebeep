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
// $Id: NetworkMonitor.h 1597 2026-08-16 05:42:04Z mastroddi $
//
//////////////////////////////////////////////////////////////////////

#ifndef BEEBEEP_NETWORKMONITOR_H
#define BEEBEEP_NETWORKMONITOR_H

#include "Config.h"


class NetworkMonitor : public QObject
{
  Q_OBJECT
public:
  explicit NetworkMonitor( QObject* parent = Q_NULLPTR );
  virtual ~NetworkMonitor();

  // Returns true if at least one active network interface has acquired a valid IP address (IPv4 or IPv6)
  bool isOnline() const;

  // Public method to trigger a manual check of all network status
  void checkNetworkStatus();

signals:
  // Emitted when a global network configuration change occurs
  void networkConfigurationChanged();

  // Emitted when overall online status changes (online <-> offline)
  void onlineStateChanged( bool is_online );

  // Emitted when a network interface connects (acquires IP) or disconnects
  void interfaceStateChanged( bool is_connected, const QHostAddress& address, const QString& interface_name );

private:
  // Internal private method performing the interface scan
  void checkInterfaces();

  QSet<QString> m_activeInterfaces; // Active interface names with valid IP addresses
  bool m_wasOnline;

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
  QNetworkConfigurationManager* mp_netConfManager;
#endif
};


// Inline Function
inline bool NetworkMonitor::isOnline() const { return !m_activeInterfaces.isEmpty(); }

#endif // BEEBEEP_NETWORKMONITOR_H
