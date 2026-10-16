#ifndef FIRMWARE_VERSION_H
#define FIRMWARE_VERSION_H

// Firmware version for OTA updates - must match the current GitHub release tag.
// Can be overridden at build time: -D GBS_FW_VERSION="\"vX.Y.Z\""
#ifndef GBS_FW_VERSION
#define GBS_FW_VERSION "v1.4.0"
#endif

// Single definition in FirmwareVersion.cpp. Note: some TUs declare this as
// `extern const char* FIRMWARE_VERSION;`, so the pointer itself stays non-const.
extern const char* FIRMWARE_VERSION;

#endif // FIRMWARE_VERSION_H
