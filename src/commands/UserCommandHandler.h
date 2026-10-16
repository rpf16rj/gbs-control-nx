#ifndef USER_COMMAND_HANDLER_H
#define USER_COMMAND_HANDLER_H

#include <Arduino.h>
#include "../core/State.h"

// User command processing (from web UI)
void handleType2Command(char argument);

// Per-mode crop memory: remembers the display window per detected input
// mode and reapplies it after preset changes. cropMemFlush() persists a
// dirty snapshot to LittleFS (call periodically from loop()).
void cropMemApply();
void cropMemFlush();

#endif // USER_COMMAND_HANDLER_H

