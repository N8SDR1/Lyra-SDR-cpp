// Lyra — macOS App Nap / idle-sleep control.  See app_activity.h.

#include "platform/app_activity.h"

#import <Foundation/Foundation.h>

namespace lyra::platform {

namespace {

// The token NSProcessInfo returns for the live activity; nil when idle.
id<NSObject> g_activity = nil;

}  // namespace

void setStreamingActivity(bool streaming)
{
    if (streaming == (g_activity != nil)) return;

    if (streaming) {
        g_activity = [[NSProcessInfo processInfo]
            beginActivityWithOptions:(NSActivityUserInitiated
                                      | NSActivityLatencyCritical)
                              reason:@"Streaming from a radio"];
        [g_activity retain];
    } else {
        [[NSProcessInfo processInfo] endActivity:g_activity];
        [g_activity release];
        g_activity = nil;
    }
}

}  // namespace lyra::platform
