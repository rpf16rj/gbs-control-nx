#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// Web server initialization
void startWebserver();
void initUpdateOTA();

// Telemetry snapshot for /api/v1/status. Must be called from loop() context
// (owns the I2C bus); the async HTTP handler only serializes the cache.
void updateApiTelemetry();

#endif // WEB_SERVER_H

