//////////////////////////////////////////////////////////////////////
//
// YongBEEP — macOS dock unread badge (S3-4b, PRD 3.2 SHOULD item)
//
// Hook point per docs/analysis/gui-redesign.md section H.2: the total
// unread count (sum of every chat unread counter, already available as
// ChatManager::unreadMessages()) is set on the NSApplication dock tile.
//
//////////////////////////////////////////////////////////////////////

// Q_OS_MACOS is defined by the Qt headers pulled in through DockBadge.h,
// so the platform include must come first.
#include "DockBadge.h"

#ifdef Q_OS_MACOS

#import <Cocoa/Cocoa.h>

void DockBadge::setUnreadCount( int unread_count )
{
  NSString* badge_label = nil;
  if( unread_count > 0 )
    badge_label = [NSString stringWithFormat:@"%d", unread_count];
  [[NSApp dockTile] setBadgeLabel:badge_label];
}

#endif // Q_OS_MACOS
