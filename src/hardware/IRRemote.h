#ifndef IR_REMOTE_H
#define IR_REMOTE_H

#include <Arduino.h>

// Optional IR remote receiver support (fork issue #17).
// Enable with build flag -DHAVE_IR_REMOTE and add the IRremoteESP8266
// library dependency (the gbsc_*_ir PlatformIO envs do this for you).
//
// Decoded NEC commands are pushed onto the existing command queues, so the
// video loop stays the single consumer of commands (no async mutation).
//
// Default code table targets the common 21-key "Car MP3" NEC remote
// (address 0x00). Adjust the map in IRRemote.cpp or via serial log:
// unknown codes are printed, so any remote can be learned.

void initIRRemote();
void pollIRRemote();

#endif // IR_REMOTE_H
