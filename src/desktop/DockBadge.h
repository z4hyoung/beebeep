//////////////////////////////////////////////////////////////////////
//
// YongBEEP — macOS dock unread badge (S3-4b, PRD 3.2 SHOULD item)
//
// macOS: DockBadge_mac.mm sets [[NSApp dockTile] setBadgeLabel:].
// Other platforms: DockBadge.cpp provides an empty implementation so the
// call sites compile unchanged.
//
//////////////////////////////////////////////////////////////////////

#ifndef BEEBEEP_DOCKBADGE_H
#define BEEBEEP_DOCKBADGE_H

#include "Config.h"

namespace DockBadge
{
  /*! Shows the total unread messages count on the macOS dock icon;
   *  0 (or less) clears the badge. No-op on other platforms. */
  void setUnreadCount( int unread_count );
}

#endif // BEEBEEP_DOCKBADGE_H
