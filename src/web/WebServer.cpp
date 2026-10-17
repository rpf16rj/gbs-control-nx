#include "WebServer.h"
#include "../core/Globals.h"
#include "../commands/CommandQueue.h"
#include "../config/FirmwareVersion.h"
#include "../web/SerialMirror.h"
#include "../config/WiFiConfig.h"
#include "../storage/PreferencesManager.h"
#include "../video/Scanlines.h"
#include "../../webui/build/webui_html.h"
#include "../core/slot.h"
#include "../gbs/tv5725.h"
#include "../gbs/GBSController.h"
#include <LittleFS.h>
#include <ArduinoOTA.h>
#ifdef ESP8266
#include <Updater.h>
#else
#include <Update.h>
#endif
#ifdef ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#else
#include <WiFi.h>
#include <esp_wifi.h>
#include <ESPmDNS.h>
#endif

// Empty string for WiFi.begin() when no password
static const String emptyWiFiPassword = "";

// Optional HTTP basic auth for destructive/sensitive endpoints.
// Create a LittleFS file "/web_auth.txt" containing "user:password" to enable.
// When the file is absent or empty, endpoints stay open (previous behavior).
static bool webAuthConfigured = false;
static bool webAuthChecked = false;
static String webAuthUser;
static String webAuthPass;

static bool isWebAuthConfigured()
{
    if (!webAuthChecked) {
        webAuthChecked = true;
        if (File f = LittleFS.open("/web_auth.txt", "r")) {
            String line = f.readStringUntil('\n');
            f.close();
            line.trim();
            int sep = line.indexOf(':');
            if (sep > 0) {
                webAuthUser = line.substring(0, sep);
                webAuthPass = line.substring(sep + 1);
                webAuthConfigured = webAuthPass.length() > 0;
            }
        }
    }
    return webAuthConfigured;
}

// Call at the top of sensitive handlers. Returns false after sending 401.
static bool checkWebAuth(AsyncWebServerRequest *request)
{
    if (!isWebAuthConfigured()) {
        return true;
    }
    if (!request->authenticate(webAuthUser.c_str(), webAuthPass.c_str())) {
        request->requestAuthentication();
        return false;
    }
    return true;
}

// Reject path traversal / absolute-outside-root file requests.
static bool isPathSafe(const String &path)
{
    if (path.indexOf("..") >= 0 || path.indexOf('\\') >= 0) {
        return false;
    }
    return true;
}

// --- API telemetry cache -------------------------------------------------
// HTTP handlers run in the AsyncTCP task, so they must not touch the I2C bus
// (the loop owns it). loop() calls updateApiTelemetry() periodically and the
// /api/v1/* handlers only serialize this snapshot.
static struct {
    uint16_t inHtotal;   // STATUS_SYNC_PROC_HTOTAL (pixel clock units)
    uint16_t inVtotal;   // STATUS_SYNC_PROC_VTOTAL (lines)
    uint16_t pllDiv;     // PLLAD_MD
    uint8_t pllLock;
    uint8_t hsActive;
    uint8_t vsActive;
    uint8_t sync2En;
    uint8_t svmGain;
    uint8_t intByps;
    uint8_t ifFmt0;    // STATUS_00: IF autodetect (NTSC/PAL/VGA, int/prg, SD)
    uint8_t ifFmt4;    // STATUS_04: 1080i/HD/INT/PRG flags
    uint8_t chipId;    // CHIP_ID_PRODUCT - which die is actually on the board
    uint8_t chipRev;
    uint8_t pullDown;  // STATUS_DEINT_PULLDN - 3:2 pulldown detected
    uint8_t fifoFlags; // STATUS_MEM_FF_* FIFO health byte
    uint16_t outVtotal; // STATUS_VDS_VERT_COUNT - live output lines
    uint16_t glbNoise;  // VDS_GLB_NOISE - global noise estimate
    uint8_t stepGain;   // VDS_STEP_GAIN
    uint8_t pkCore;     // VDS_PK_LB_CORE (coring level)
    uint8_t dfieldEn;   // VDS_DFIELD_EN - interlaced output experiment
    uint8_t vdsField;   // STATUS_VDS_FIELD - which field is being output
    float srcHz;
} apiTel;

void updateApiTelemetry()
{
    typedef TV5725<GBS_ADDR> GBS;
    if (!rto->boardHasPower) {
        return;
    }
    apiTel.inHtotal = GBS::STATUS_SYNC_PROC_HTOTAL::read();
    apiTel.inVtotal = GBS::STATUS_SYNC_PROC_VTOTAL::read();
    apiTel.pllDiv = GBS::PLLAD_MD::read();
    apiTel.pllLock = GBS::STATUS_MISC_PLLAD_LOCK::read();
    apiTel.hsActive = GBS::STATUS_SYNC_PROC_HSACT::read();
    apiTel.vsActive = GBS::STATUS_SYNC_PROC_VSACT::read();
    apiTel.sync2En = GBS::DAC_RGBS_S1EN::read();
    apiTel.svmGain = GBS::VDS_SVM_GAIN::read();
    apiTel.intByps = GBS::VDS_TAP6_BYPS::read();
    apiTel.ifFmt0 = GBS::STATUS_00::read();
    apiTel.ifFmt4 = GBS::STATUS_04::read();
    apiTel.chipId = GBS::CHIP_ID_PRODUCT::read();
    apiTel.chipRev = GBS::CHIP_ID_REVISION::read();
    apiTel.pullDown = GBS::STATUS_DEINT_PULLDN::read();
    apiTel.fifoFlags = GBS::STATUS_13::read();
    apiTel.outVtotal = GBS::STATUS_VDS_VERT_COUNT::read();
    apiTel.glbNoise = GBS::VDS_GLB_NOISE::read();
    apiTel.stepGain = GBS::VDS_STEP_GAIN::read();
    apiTel.pkCore = GBS::VDS_PK_LB_CORE::read();
    apiTel.dfieldEn = GBS::VDS_DFIELD_EN::read();
    apiTel.vdsField = GBS::STATUS_VDS_FIELD::read();

    // getSourceFieldRate() drives the debug pin / test bus, so keep it slow
    if (rto->sourceDisconnected || !apiTel.vsActive) {
        apiTel.srcHz = 0.0f;
    } else {
        static uint32_t lastHzMeasure = 0;
        if (millis() - lastHzMeasure > 4000) {
            lastHzMeasure = millis();
            apiTel.srcHz = getSourceFieldRate(0);
        }
    }
}

void startWebserver()
{
    persWM.setApCredentials(ap_ssid, ap_password);
    persWM.onConnect([]() {
        SerialM.print(F("(WiFi): STA mode connected; IP: "));
        SerialM.println(WiFi.localIP().toString());
#ifdef ESP8266
        if (MDNS.begin(device_hostname_partial, WiFi.localIP())) { // MDNS request for gbscontrol.local
            //Serial.println("MDNS started");
            MDNS.addService("http", "tcp", 80); // Add service to MDNS-SD
            MDNS.announce();
        }
#else
        if (MDNS.begin(device_hostname_partial)) { // MDNS request for gbscontrol.local
            //Serial.println("MDNS started");
            MDNS.addService("http", "tcp", 80); // Add service to MDNS-SD
            // MDNS.announce(); // Not needed on ESP32
        }
#endif
        SerialM.println(FPSTR(st_info_string));
    });
    persWM.onAp([]() {
        SerialM.println(FPSTR(ap_info_string));
        // add mdns announce here as well?
    });

#ifdef ESP8266
    disconnectedEventHandler = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected &event) {
        Serial.print("Station disconnected, reason: ");
        Serial.println(event.reason);
    });
#else
    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        Serial.print("Station disconnected, reason: ");
        Serial.println(info.wifi_sta_disconnected.reason);
    }, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
#endif

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        //Serial.println("sending web page");
        if (ESP.getFreeHeap() > 10000) {
            AsyncWebServerResponse *response = request->beginResponse(200, "text/html", webui_html, webui_html_len);
            response->addHeader("Content-Encoding", "gzip");
            request->send(response);
        }
    });

    server.on("/sc", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (ESP.getFreeHeap() > 10000) {
            int params = request->params();
            //Serial.print("got serial request params: ");
            //Serial.println(params);
            if (params > 0) {
                const AsyncWebParameter *p = request->getParam(0);
                //Serial.println(p->name());
                char cmd = p->name().charAt(0);

                // hack, problem with '+' command received via url param
                if (cmd == ' ') {
                    cmd = '+';
                }
                serialCommandQueue.push(cmd); // consumed by loop()
            }
            request->send(200); // reply
        }
    });

    server.on("/uc", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (ESP.getFreeHeap() > 10000) {
            int params = request->params();
            //Serial.print("got user request params: ");
            //Serial.println(params);
            if (params > 0) {
                const AsyncWebParameter *p = request->getParam(0);
                //Serial.println(p->name());
                userCommandQueue.push(p->name().charAt(0)); // consumed by loop()
            }
            request->send(200); // reply
        }
    });

    server.on("/wifi/connect", HTTP_POST, [](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response =
            request->beginResponse(200, "application/json", "true");
        request->send(response);

        // Temporarily enable persistence to save credentials (ESP8266 only)
        // Temporarily enable persistence to save credentials
#ifdef ESP8266
        WiFi.persistent(true);
#endif
        
        if (request->arg("n").length()) {     // n holds ssid
            String ssid = request->arg("n");
            String pass = request->arg("p");
#ifdef ESP32
            // On ESP32, WiFi.begin(..., false) does not reliably save to NVS.
            // Use direct ESP-IDF calls to force saving to flash.
            wifi_config_t conf;
            if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK) {
                memset(&conf, 0, sizeof(conf));
            }

            memset(conf.sta.ssid, 0, sizeof(conf.sta.ssid));
            strncpy((char*)conf.sta.ssid, ssid.c_str(), sizeof(conf.sta.ssid) - 1);
            
            if (pass.length()) {
                memset(conf.sta.password, 0, sizeof(conf.sta.password));
                strncpy((char*)conf.sta.password, pass.c_str(), sizeof(conf.sta.password) - 1);
                conf.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
            } else {
                 memset(conf.sta.password, 0, sizeof(conf.sta.password));
                 conf.sta.threshold.authmode = WIFI_AUTH_OPEN;
            }
            
            esp_wifi_set_storage(WIFI_STORAGE_FLASH);
            esp_wifi_set_config(WIFI_IF_STA, &conf);
            
            SerialM.println("Saved WiFi creds to NVS (ESP32) - Safe Method");
#else
            // ESP8266 logic
            if (pass.length()) { // p holds password
                // false = only save credentials, don't connect
                WiFi.begin(ssid.c_str(), pass.c_str(), 0, 0, false);
            } else {
                WiFi.begin(ssid.c_str(), emptyWiFiPassword.c_str(), 0, 0, false);
            }
#endif
        } else {
            WiFi.begin();
        }
        
        // Disable persistence again to avoid Core 3.1.x bug (ESP8266 only)
#ifdef ESP8266
        WiFi.persistent(false);
#endif

        userCommandQueue.push('u'); // next loop, set wifi station mode and restart device
    });

    server.on("/bin/slots.bin", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (ESP.getFreeHeap() > 10000) {
            SlotMetaArray slotsObject;
            File slotsBinaryFileRead = LittleFS.open(SLOTS_FILE, "r");

            if (!slotsBinaryFileRead) {
                File slotsBinaryFileWrite = LittleFS.open(SLOTS_FILE, "w");
                for (int i = 0; i < SLOTS_TOTAL; i++) {
                    slotsObject.slot[i].slot = i;
                    slotsObject.slot[i].presetID = 0;
                    slotsObject.slot[i].scanlines = 0;
                    slotsObject.slot[i].scanlinesStrength = 0;
                    slotsObject.slot[i].wantVdsLineFilter = false;
                    slotsObject.slot[i].wantStepResponse = true;
                    slotsObject.slot[i].wantPeaking = true;
                    char emptySlotName[25] = "Empty                   ";
                    strncpy(slotsObject.slot[i].name, emptySlotName, 25);
                }
                slotsBinaryFileWrite.write((byte *)&slotsObject, sizeof(slotsObject));
                slotsBinaryFileWrite.close();
            } else {
                slotsBinaryFileRead.close();
            }

            request->send(LittleFS, "/slots.bin", "application/octet-stream");
        }
    });

    server.on("/slot/set", HTTP_GET, [](AsyncWebServerRequest *request) {
        bool result = false;

        if (ESP.getFreeHeap() > 10000) {
            int params = request->params();

            if (params > 0) {
                const AsyncWebParameter *slotParam = request->getParam(0);
                String slotParamValue = slotParam->value();
                char slotValue[2];
                slotParamValue.toCharArray(slotValue, sizeof(slotValue));
                uopt->presetSlot = (uint8_t)slotValue[0];
                uopt->presetPreference = OutputCustomized;
                saveUserPrefs();
                result = true;
            }
        }

        request->send(200, "application/json", result ? "true" : "false");
    });

    server.on("/slot/save", HTTP_GET, [](AsyncWebServerRequest *request) {
        bool result = false;

        if (ESP.getFreeHeap() > 10000) {
            int params = request->params();

            if (params > 0) {
                SlotMetaArray slotsObject;
                File slotsBinaryFileRead = LittleFS.open(SLOTS_FILE, "r");

                if (slotsBinaryFileRead) {
                    slotsBinaryFileRead.read((byte *)&slotsObject, sizeof(slotsObject));
                    slotsBinaryFileRead.close();
                } else {
                    File slotsBinaryFileWrite = LittleFS.open(SLOTS_FILE, "w");

                    for (int i = 0; i < SLOTS_TOTAL; i++) {
                        slotsObject.slot[i].slot = i;
                        slotsObject.slot[i].presetID = 0;
                        slotsObject.slot[i].scanlines = 0;
                        slotsObject.slot[i].scanlinesStrength = 0;
                        slotsObject.slot[i].wantVdsLineFilter = false;
                        slotsObject.slot[i].wantStepResponse = true;
                        slotsObject.slot[i].wantPeaking = true;
                        char emptySlotName[25] = "Empty                   ";
                        strncpy(slotsObject.slot[i].name, emptySlotName, 25);
                    }

                    slotsBinaryFileWrite.write((byte *)&slotsObject, sizeof(slotsObject));
                    slotsBinaryFileWrite.close();
                }

                // index param
                const AsyncWebParameter *slotIndexParam = request->getParam(0);
                String slotIndexString = slotIndexParam->value();
                uint8_t slotIndex = lowByte(slotIndexString.toInt());
                if (slotIndex >= SLOTS_TOTAL) {
                    goto fail;
                }

                // name param
                const AsyncWebParameter *slotNameParam = request->getParam(1);
                String slotName = slotNameParam->value();

                char emptySlotName[25] = "                        ";
                strncpy(slotsObject.slot[slotIndex].name, emptySlotName, 25);

                slotsObject.slot[slotIndex].slot = slotIndex;
                slotName.toCharArray(slotsObject.slot[slotIndex].name, sizeof(slotsObject.slot[slotIndex].name));
                slotsObject.slot[slotIndex].presetID = rto->presetID;
                slotsObject.slot[slotIndex].scanlines = uopt->wantScanlines;
                slotsObject.slot[slotIndex].scanlinesStrength = uopt->scanlineStrength;
                slotsObject.slot[slotIndex].wantVdsLineFilter = uopt->wantVdsLineFilter;
                slotsObject.slot[slotIndex].wantStepResponse = uopt->wantStepResponse;
                slotsObject.slot[slotIndex].wantPeaking = uopt->wantPeaking;

                File slotsBinaryOutputFile = LittleFS.open(SLOTS_FILE, "w");
                slotsBinaryOutputFile.write((byte *)&slotsObject, sizeof(slotsObject));
                slotsBinaryOutputFile.close();

                result = true;
            }
        }

        fail:
        request->send(200, "application/json", result ? "true" : "false");
    });

    server.on("/slot/remove", HTTP_GET, [](AsyncWebServerRequest *request) {
        bool result = false;
        int params = request->params();
        char param = 0;
        // getParam(0) is nullptr when no params are present; check before deref
        if (params > 0) {
            const AsyncWebParameter *p = request->getParam(0);
            if (p) {
                param = p->name().charAt(0);
            }
        }
        if (!checkWebAuth(request)) {
            return;
        }
        if (params > 0 && param != 0)
        {
            if (param == '0')
            {
                SerialM.println("Wait...");
                result = true;
            }
            else
            {
                Ascii8 slot = uopt->presetSlot;
                Ascii8 nextSlot;
                auto currentSlot = slotIndexMap.indexOf(slot);

                SlotMetaArray slotsObject;
                File slotsBinaryFileRead = LittleFS.open(SLOTS_FILE, "r");
                slotsBinaryFileRead.read((byte *)&slotsObject, sizeof(slotsObject));
                slotsBinaryFileRead.close();
                String slotName = slotsObject.slot[currentSlot].name;

                // remove preset files
                LittleFS.remove("/preset_ntsc." + String((char)slot));
                LittleFS.remove("/preset_pal." + String((char)slot));
                LittleFS.remove("/preset_ntsc_480p." + String((char)slot));
                LittleFS.remove("/preset_pal_576p." + String((char)slot));
                LittleFS.remove("/preset_ntsc_720p." + String((char)slot));
                LittleFS.remove("/preset_ntsc_1080p." + String((char)slot));
                LittleFS.remove("/preset_medium_res." + String((char)slot));
                LittleFS.remove("/preset_vga_upscale." + String((char)slot));
                LittleFS.remove("/preset_unknown." + String((char)slot));

                uint8_t loopCount = 0;
                uint8_t flag = 1;
                while (flag != 0)
                {
                    slot = slotIndexMap[currentSlot + loopCount];
                    nextSlot = slotIndexMap[currentSlot + loopCount + 1];
                    flag = 0;
                    flag += LittleFS.rename("/preset_ntsc." + String((char)(nextSlot)), "/preset_ntsc." + String((char)slot));
                    flag += LittleFS.rename("/preset_pal." + String((char)(nextSlot)), "/preset_pal." + String((char)slot));
                    flag += LittleFS.rename("/preset_ntsc_480p." + String((char)(nextSlot)), "/preset_ntsc_480p." + String((char)slot));
                    flag += LittleFS.rename("/preset_pal_576p." + String((char)(nextSlot)), "/preset_pal_576p." + String((char)slot));
                    flag += LittleFS.rename("/preset_ntsc_720p." + String((char)(nextSlot)), "/preset_ntsc_720p." + String((char)slot));
                    flag += LittleFS.rename("/preset_ntsc_1080p." + String((char)(nextSlot)), "/preset_ntsc_1080p." + String((char)slot));
                    flag += LittleFS.rename("/preset_medium_res." + String((char)(nextSlot)), "/preset_medium_res." + String((char)slot));
                    flag += LittleFS.rename("/preset_vga_upscale." + String((char)(nextSlot)), "/preset_vga_upscale." + String((char)slot));
                    flag += LittleFS.rename("/preset_unknown." + String((char)(nextSlot)), "/preset_unknown." + String((char)slot));

                    slotsObject.slot[currentSlot + loopCount].slot = slotsObject.slot[currentSlot + loopCount + 1].slot;
                    slotsObject.slot[currentSlot + loopCount].presetID = slotsObject.slot[currentSlot + loopCount + 1].presetID;
                    slotsObject.slot[currentSlot + loopCount].scanlines = slotsObject.slot[currentSlot + loopCount + 1].scanlines;
                    slotsObject.slot[currentSlot + loopCount].scanlinesStrength = slotsObject.slot[currentSlot + loopCount + 1].scanlinesStrength;
                    slotsObject.slot[currentSlot + loopCount].wantVdsLineFilter = slotsObject.slot[currentSlot + loopCount + 1].wantVdsLineFilter;
                    slotsObject.slot[currentSlot + loopCount].wantStepResponse = slotsObject.slot[currentSlot + loopCount + 1].wantStepResponse;
                    slotsObject.slot[currentSlot + loopCount].wantPeaking = slotsObject.slot[currentSlot + loopCount + 1].wantPeaking;
                    // slotsObject.slot[currentSlot + loopCount].name = slotsObject.slot[currentSlot + loopCount + 1].name;
                    strncpy(slotsObject.slot[currentSlot + loopCount].name, slotsObject.slot[currentSlot + loopCount + 1].name, 25);
                    loopCount++;
                }

                File slotsBinaryFileWrite = LittleFS.open(SLOTS_FILE, "w");
                slotsBinaryFileWrite.write((byte *)&slotsObject, sizeof(slotsObject));
                slotsBinaryFileWrite.close();
                SerialM.println("Preset \"" + slotName + "\" removed");
                result = true;
            }
        }

        request->send(200, "application/json", result ? "true" : "false");
    });

    server.on("/filesystem/upload", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkWebAuth(request)) {
            return;
        }
        request->send(200, "application/json", "true");
    });

    server.on(
        "/filesystem/upload", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            if (!checkWebAuth(request)) {
                return;
            }
            request->send(200, "application/json", "true");
        },
        [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            if (!index) {
                if (!isPathSafe(filename)) {
                    return;
                }
                if (filename.startsWith("/")) {
                    request->_tempFile = LittleFS.open(filename, "w");
                } else {
                    request->_tempFile = LittleFS.open("/" + filename, "w");
                }
            }
            if (!request->_tempFile) {
                return;
            }
            if (len) {
                request->_tempFile.write(data, len);
            }
            if (final) {
                request->_tempFile.close();
            }
        });

    server.on("/filesystem/download", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkWebAuth(request)) {
            return;
        }
        if (ESP.getFreeHeap() > 10000) {
            // On ESP32 query parameter ordering and
            // unnamed params (e.g. cache-busters like "&176...") can make getParam(0) unreliable.
            // Always resolve the file path by *name*.
            if (request->hasParam("file")) {
                String path = request->getParam("file")->value();
                if (!path.startsWith("/")) {
                    path = "/" + path;
                }
                if (!isPathSafe(path)) {
                    request->send(400, "application/json", "false");
                    return;
                }
                request->send(LittleFS, path, String(), true);
            } else {
                request->send(200, "application/json", "false");
            }
        } else {
            request->send(200, "application/json", "false");
        }
    });

    server.on("/filesystem/dir", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (ESP.getFreeHeap() > 10000) {
#ifdef ESP8266
            Dir dir = LittleFS.openDir("/");
            String output = "[";

            while (dir.next()) {
                String name = dir.fileName();
                if (!name.startsWith("/")) {
                    name = "/" + name;
                }
                output += "\"";
                output += name;
                output += "\",";
                delay(1); // wifi stack
            }
#else
            File root = LittleFS.open("/");
            String output = "[";
            File file = root.openNextFile();
            while(file){
                String name = file.name();
                if (!name.startsWith("/")) {
                    name = "/" + name;
                }
                output += "\"";
                output += name;
                output += "\",";
                file = root.openNextFile();
                delay(1);
            }
#endif

            output += "]";

            output.replace(",]", "]");

            request->send(200, "application/json", output);
            return;
        }
        request->send(200, "application/json", "false");
    });

    server.on("/filesystem/format", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!checkWebAuth(request)) {
            return;
        }
        request->send(200, "application/json", LittleFS.format() ? "true" : "false");
    });

    // Minimal versioned JSON API. Kept small on purpose (ESP8266 heap is ~40KB).
    server.on("/api/v1/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        String json;
        json.reserve(320);
        json += F("{\"fw\":\"");
        json += FIRMWARE_VERSION;
        json += F("\",\"uptime_ms\":");
        json += millis();
        json += F(",\"heap\":");
        json += ESP.getFreeHeap();
        json += F(",\"video_mode\":");
        json += rto->videoStandardInput;
        json += F(",\"preset_id\":\"0x");
        json += String(rto->presetID, HEX);
        json += F("\",\"preset_slot\":\"");
        char slotChar = (char)uopt->presetSlot;
        if (slotIndexMap.indexOf(slotChar) < 0) {
            slotChar = 'A';
        }
        json += slotChar;
        json += F("\",\"source_disconnected\":");
        json += (rto->sourceDisconnected ? F("true") : F("false"));
        json += F(",\"board_has_power\":");
        json += (rto->boardHasPower ? F("true") : F("false"));
        json += F(",\"hd_bypass\":");
        json += (rto->outModeHdBypass ? F("true") : F("false"));
        json += F(",\"clockgen_detected\":");
        json += (rto->extClockGenDetected ? F("true") : F("false"));
        json += F(",\"frame_time_lock\":");
        json += (uopt->enableFrameTimeLock ? F("true") : F("false"));
        json += F(",\"scanlines\":");
        json += (uopt->wantScanlines ? F("true") : F("false"));
        json += F(",\"wifi_mode\":\"");
        json += (WiFi.getMode() == WIFI_AP ? F("ap") : F("sta"));
        json += F("\",\"ip\":\"");
        json += WiFi.getMode() == WIFI_AP ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
        // cached GBS telemetry (refreshed by loop() -> updateApiTelemetry())
        json += F("\",\"in_htotal\":");
        json += apiTel.inHtotal;
        json += F(",\"in_vtotal\":");
        json += apiTel.inVtotal;
        json += F(",\"pll_divider\":");
        json += apiTel.pllDiv;
        json += F(",\"pll_locked\":");
        json += (apiTel.pllLock ? F("true") : F("false"));
        json += F(",\"hsync_active\":");
        json += (apiTel.hsActive ? F("true") : F("false"));
        json += F(",\"vsync_active\":");
        json += (apiTel.vsActive ? F("true") : F("false"));
        json += F(",\"src_hz\":");
        json += String(apiTel.srcHz, 2);
        json += F(",\"phase_sp\":");
        json += rto->phaseSP;
        json += F(",\"phase_adc\":");
        json += rto->phaseADC;
        json += F(",\"sog_level\":");
        json += rto->currentLevelSOG;
        json += F(",\"preset_pref\":");
        json += (uint8_t)uopt->presetPreference;
        json += F(",\"deint_mode\":");
        json += uopt->deintMode;
        json += F(",\"tbc_mode\":");
        json += (uopt->tbcMode ? F("true") : F("false"));
        json += F(",\"if_auto_offset\":");
        json += (uopt->ifAutoOffset ? F("true") : F("false"));
        json += F(",\"pal_force60\":");
        json += (uopt->PalForce60 ? F("true") : F("false"));
        json += F(",\"peaking\":");
        json += uopt->peakingLevel;
        json += F(",\"mask_off\":");
        json += rto->maskMoireOff;
        json += F(",\"chroma\":");
        json += rto->chromaShift;
        json += F(",\"sync_sel\":");
        json += rto->outSyncSel;
        json += F(",\"sync2_en\":");
        json += (apiTel.sync2En ? F("true") : F("false"));
        json += F(",\"interp\":");
        json += apiTel.intByps;
        json += F(",\"svm\":");
        json += apiTel.svmGain;
        json += F(",\"fx\":");
        json += rto->fxFlags;
        json += F(",\"if_fmt0\":");
        json += apiTel.ifFmt0;
        json += F(",\"if_fmt4\":");
        json += apiTel.ifFmt4;
        json += F(",\"chip_id\":");
        json += apiTel.chipId;
        json += F(",\"chip_rev\":");
        json += apiTel.chipRev;
        json += F(",\"pulldown\":");
        json += apiTel.pullDown;
        json += F(",\"fifo\":");
        json += apiTel.fifoFlags;
        json += F(",\"out_vtotal\":");
        json += apiTel.outVtotal;
        json += F(",\"noise\":");
        json += apiTel.glbNoise;
        json += F(",\"crisp\":");
        json += (rto->crispMode ? F("true") : F("false"));
        json += F(",\"step\":");
        json += apiTel.stepGain;
        json += F(",\"core\":");
        json += apiTel.pkCore;
        json += F(",\"osr\":");
        json += rto->osr;
        json += F(",\"bright\":");
        json += rto->picBright;
        json += F(",\"contrast\":");
        json += rto->picContrast;
        json += F(",\"sat\":");
        json += rto->picSat;
        json += F(",\"hue\":");
        json += rto->picHue;
        json += F(",\"crt\":");
        json += (rto->crtMode ? F("true") : F("false"));
        json += F(",\"int_out\":");
        json += (apiTel.dfieldEn ? F("true") : F("false"));
        json += F(",\"vds_field\":");
        json += apiTel.vdsField;
        json += F(",\"last_reg\":");
        json += apiLastRegRead;
        json += F("}");
        request->send(200, "application/json", json);
    });

    // Same semantics as /sc but under the versioned API path.
    // HTTP firmware upload: POST a .bin (multipart field "firmware" or raw
    // body) to /update. The ESP pulls the image - no PC-side listener needed,
    // so no inbound firewall hole required (unlike espota's TCP callback).
    server.on(
        "/update", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            bool ok = !Update.hasError() && Update.isFinished();
            request->send(ok ? 200 : 500, "text/plain", ok ? "OK, rebooting" : "FAILED");
            if (ok) {
                SerialM.println(F("http update ok, rebooting"));
                delay(300);
#ifdef ESP8266
                ESP.reset();
#else
                ESP.restart();
#endif
            }
        },
        [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            if (index == 0) {
                SerialM.printf("http update: %s (%u bytes)\n", filename.c_str(), request->contentLength());
                Update.runAsync(true);
#ifdef ESP8266
                if (!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000)) {
#else
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
#endif
                    Update.printError(Serial);
                }
            }
            if (!Update.hasError() && Update.write(data, len) != len) {
                Update.printError(Serial);
            }
            if (final && !Update.hasError()) {
                if (Update.end(true)) {
                    SerialM.printf("http update done: %u bytes\n", index + len);
                } else {
                    Update.printError(Serial);
                }
            }
        });

    server.on("/api/v1/cmd", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (ESP.getFreeHeap() > 10000 && request->params() > 0) {
            const AsyncWebParameter *p = request->getParam(0);
            char cmd = p->name().charAt(0);
            if (cmd == ' ') {
                cmd = '+';
            }
            serialCommandQueue.push(cmd);
        }
        request->send(200, "application/json", "true");
    });

    // Same semantics as /uc (user commands) under the versioned API path.
    server.on("/api/v1/ucmd", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (ESP.getFreeHeap() > 10000 && request->params() > 0) {
            const AsyncWebParameter *p = request->getParam(0);
            userCommandQueue.push(p->name().charAt(0));
        }
        request->send(200, "application/json", "true");
    });

    // REA / live register experimentation:
    //   GET /api/v1/reg?seg=3&off=0x2b        -> schedule a read
    //   GET /api/v1/reg?seg=3&off=0x2b&val=4  -> schedule a write
    // The op is executed by loop() (sole I2C owner). The read result lands in
    // apiLastRegRead (see "last_reg" in /api/v1/status) and is also logged
    // to the serial/WebSocket console.
    server.on("/api/v1/reg", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("seg") || !request->hasParam("off")) {
            request->send(400, "application/json", "{\"error\":\"need seg & off\"}");
            return;
        }
        long seg = strtol(request->getParam("seg")->value().c_str(), nullptr, 0);
        long off = strtol(request->getParam("off")->value().c_str(), nullptr, 0);
        long val = -1;
        if (request->hasParam("val")) {
            val = strtol(request->getParam("val")->value().c_str(), nullptr, 0);
        }
        if (seg < 0 || seg > 7 || off < 0 || off > 0xff || val > 0xff) {
            request->send(400, "application/json", "{\"error\":\"range\"}");
            return;
        }
        regOpOff = (uint8_t)off;
        regOpVal = (int16_t)val;
        regOpSeg = (uint8_t)seg; // arm last (slot consumed by loop)
        request->send(200, "application/json", "{\"queued\":true}");
    });

    // GET /api/v1/segdump?seg=3 -> dump all 256 regs of a segment to the
    // serial/WebSocket console (for before/after diffs when probing).
    server.on("/api/v1/segdump", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("seg")) {
            request->send(400, "application/json", "{\"error\":\"need seg\"}");
            return;
        }
        long seg = strtol(request->getParam("seg")->value().c_str(), nullptr, 0);
        if (seg < 0 || seg > 7) {
            request->send(400, "application/json", "{\"error\":\"range\"}");
            return;
        }
        regDumpSeg = (uint8_t)seg;
        request->send(200, "application/json", "{\"queued\":true}");
    });

    server.on("/wifi/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        WiFiMode_t wifiMode = WiFi.getMode();
        request->send(200, "application/json", wifiMode == WIFI_AP ? "{\"mode\":\"ap\"}" : "{\"mode\":\"sta\",\"ssid\":\"" + WiFi.SSID() + "\"}");
    });

    server.on("/gbs/restore-filters", HTTP_GET, [](AsyncWebServerRequest *request) {
        SlotMetaArray slotsObject;
        File slotsBinaryFileRead = LittleFS.open(SLOTS_FILE, "r");
        bool result = false;
        if (slotsBinaryFileRead) {
            slotsBinaryFileRead.read((byte *)&slotsObject, sizeof(slotsObject));
            slotsBinaryFileRead.close();
            auto currentSlot = slotIndexMap.indexOf(uopt->presetSlot);
            if (currentSlot == -1) {
                goto fail;
            }

            uopt->wantScanlines = slotsObject.slot[currentSlot].scanlines;

            SerialM.print(F("slot: "));
            SerialM.println(uopt->presetSlot);
            SerialM.print(F("scanlines: "));
            if (uopt->wantScanlines) {
                SerialM.println(F("on (Line Filter recommended)"));
            } else {
                disableScanlines();
                SerialM.println("off");
            }
            saveUserPrefs();

            uopt->scanlineStrength = slotsObject.slot[currentSlot].scanlinesStrength;
            uopt->wantVdsLineFilter = slotsObject.slot[currentSlot].wantVdsLineFilter;
            uopt->wantStepResponse = slotsObject.slot[currentSlot].wantStepResponse;
            uopt->wantPeaking = slotsObject.slot[currentSlot].wantPeaking;
            result = true;
        }

        fail:
        request->send(200, "application/json", result ? "true" : "false");
    });

    //webSocket.onEvent(webSocketEvent);

    persWM.setConnectNonBlock(true);
#ifdef ESP32
    // ESP32: WiFi.SSID() is the *currently connected* SSID, not the saved one.
    // Use the stored STA config to decide whether to attempt STA or go AP immediately.
    bool hasStoredStaConfig = false;
    {
        wifi_config_t conf;
        memset(&conf, 0, sizeof(conf));
        // Ensure driver is initialized so esp_wifi_get_config works reliably
        WiFi.mode(WIFI_STA);
        delay(10);
        if (esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK && conf.sta.ssid[0] != 0) {
            hasStoredStaConfig = true;
        }
        // PersWiFiManager will reconfigure modes as needed
        WiFi.mode(WIFI_OFF);
        delay(10);
    }
    if (!hasStoredStaConfig) {
        persWM.setupWiFiHandlers();
        persWM.startApMode();
    } else {
        persWM.begin(); // first try connecting to stored network, go AP mode after timeout
    }
#else
    if (WiFi.SSID().length() == 0) {
        // no stored network to connect to > start AP mode right away
        persWM.setupWiFiHandlers();
        persWM.startApMode();
    } else {
        persWM.begin(); // first try connecting to stored network, go AP mode after timeout
    }
#endif

    server.begin();    // Webserver for the site
    webSocket.begin(); // Websocket for interaction
    // Enable heartbeat: ping every 2000ms, pong timeout 3000ms, disconnect after 2 missed pongs
    // This keeps the connection alive during idle periods (client has 2.7s timeout)
    webSocket.enableHeartbeat(2000, 3000, 2);
    yield();

#ifdef HAVE_PINGER_LIBRARY
    // pinger library
    pinger.OnReceive([](const PingerResponse &response) {
        if (response.ReceivedResponse) {
            Serial.printf(
                "Reply from %s: time=%lums\n",
                response.DestIPAddress.toString().c_str(),
                response.ResponseTime);

            pingLastTime = millis() - 900; // produce a fast stream of pings if connection is good
        } else {
            Serial.printf("Request timed out.\n");
        }

        // Return true to continue the ping sequence.
        // If current event returns false, the ping sequence is interrupted.
        return true;
    });

    pinger.OnEnd([](const PingerResponse &response) {
        // detailed info not necessary
        return true;
    });
#endif
}

void initUpdateOTA()
{
    ArduinoOTA.setHostname("GBS OTA");

    // ArduinoOTA.setPassword("admin");
    // Password can be set with it's md5 value as well
    // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
    //ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

    // update: no password is as (in)secure as this publicly stated hash..
    // rely on the user having to enable the OTA feature on the web ui
    // If /web_auth.txt exists, reuse its password for ArduinoOTA as well.
    if (isWebAuthConfigured()) {
        ArduinoOTA.setPassword(webAuthPass.c_str());
    }

    ArduinoOTA.onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
            type = "sketch";
        else // U_LittleFS
            type = "filesystem";

        // NOTE: if updating LittleFS this would be the place to unmount LittleFS using LittleFS.end()
        LittleFS.end();
        SerialM.println("Start updating " + type);
    });
    ArduinoOTA.onEnd([]() {
        SerialM.println("\nEnd");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        // total can be 0 or < 100 on small/fs updates: guard against div by zero
        if (total >= 100) {
            SerialM.printf("Progress: %u%%\r", progress / (total / 100));
        } else if (total > 0) {
            SerialM.printf("Progress: %u%%\r", (progress * 100) / total);
        }
    });
    ArduinoOTA.onError([](ota_error_t error) {
        SerialM.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR)
            SerialM.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR)
            SerialM.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR)
            SerialM.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR)
            SerialM.println("Receive Failed");
        else if (error == OTA_END_ERROR)
            SerialM.println("End Failed");
    });
    ArduinoOTA.begin();
    yield();
}

