#include "UserCommandHandler.h"
#include "../core/Globals.h"
#include "../web/SerialMirror.h"
#include "../config/WiFiConfig.h"
#include "../utils/Logging.h"
#include "../storage/PreferencesManager.h"
#include "../storage/SlotManager.h"
#include "../gbs/GBSController.h"
#include "../gbs/GBSVideoProcessing.h"
#include "../gbs/GBSPhase.h"
#include "../gbs/GBSSync.h"
#include "../video/Scanlines.h"
#include "../video/Deinterlacing.h"
#include "../video/VideoInput.h"
#include "../video/framesync.h"
#include "../core/slot.h"
#include "../gbs/tv5725.h"
#include <LittleFS.h>
#ifdef ESP8266
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif

typedef TV5725<GBS_ADDR> GBS;

static void setFrameTimeLockMethod(uint8_t method)
{
    if (uopt->frameTimeLockMethod == method) {
        return;
    }
    if (!rto->extClockGenDetected) {
        FrameSync::reset(uopt->frameTimeLockMethod);
    }
    uopt->frameTimeLockMethod = method;
    saveUserPrefs();
    activeFrameTimeLockInitialSteps();
}

void handleType2Command(char argument)
{
    myLog("user", argument);
    switch (argument) {
        case '0':
            SerialM.print(F("pal force 60hz "));
            if (uopt->PalForce60 == 0) {
                uopt->PalForce60 = 1;
                SerialM.println("on");
            } else {
                uopt->PalForce60 = 0;
                SerialM.println("off");
            }
            saveUserPrefs();
            break;
        case '1':
            // reset to defaults button
            webSocket.disconnect();
            loadDefaultUserOptions();
            saveUserPrefs();
            Serial.println(F("options set to defaults, restarting"));
            delay(60);
#ifdef ESP8266
            ESP.reset(); // don't use restart(), messes up websocket reconnects
#else
            ESP.restart();
#endif
            //
            break;
        case '2': {
            // re-run the sampling phase optimization sweep (SP + ADC phase)
            SerialM.print(F("auto phase... "));
            boolean ok = optimizePhaseSP();
            SerialM.print(ok ? F("ok, phaseSP: ") : F("failed, phaseSP: "));
            SerialM.print(rto->phaseSP);
            SerialM.print(F(" phaseADC: "));
            SerialM.println(rto->phaseADC);
        } break;
        case '3': // load custom preset
        {
            uopt->presetPreference = OutputCustomized; // custom
            if (rto->videoStandardInput == 14) {
                // vga upscale path: let synwatcher handle it
                rto->videoStandardInput = 15;
            } else {
                // normal path
                applyPresets(rto->videoStandardInput);
            }
            saveUserPrefs();
        } break;
        case '4': // save custom preset
            savePresetToLittleFS();
            uopt->presetPreference = OutputCustomized; // custom
            saveUserPrefs();
            break;
        case '5':
            //Frame Time Lock toggle
            uopt->enableFrameTimeLock = !uopt->enableFrameTimeLock;
            saveUserPrefs();
            if (uopt->enableFrameTimeLock) {
                SerialM.println(F("FTL on"));
            } else {
                SerialM.println(F("FTL off"));
            }
            if (!rto->extClockGenDetected) {
                FrameSync::reset(uopt->frameTimeLockMethod);
            }
            if (uopt->enableFrameTimeLock) {
                activeFrameTimeLockInitialSteps();
            }
            break;
        case '6':
            //
            break;
        case '7':
            uopt->wantScanlines = !uopt->wantScanlines;
            SerialM.print(F("scanlines: "));
            if (uopt->wantScanlines) {
                SerialM.println(F("on (Line Filter recommended)"));
            } else {
                disableScanlines();
                SerialM.println("off");
            }
            saveUserPrefs();
            break;
        case '8': {
            // sharpness/peaking level: 0=off, 1=normal, 2=strong, 3=max
            uopt->peakingLevel = (uopt->peakingLevel + 1) & 3;
            SerialM.print(F("peaking level: "));
            SerialM.println(uopt->peakingLevel);
            saveUserPrefs();
            if (uopt->wantPeaking && uopt->peakingLevel > 0) {
                static const uint8_t pkLbGain[4] = {0, 0x16, 0x28, 0x3f};
                static const uint8_t pkLhGain[4] = {0, 0x18, 0x28, 0x3f};
                static const uint8_t stepGain[4] = {1, 1, 4, 8};
                GBS::VDS_PK_Y_H_BYPS::write(0);
                GBS::VDS_PK_LB_GAIN::write(pkLbGain[uopt->peakingLevel]);
                GBS::VDS_PK_LH_GAIN::write(pkLhGain[uopt->peakingLevel]);
                if (rto->presetID != 0x05 && rto->presetID != 0x15) {
                    GBS::VDS_STEP_GAIN::write(stepGain[uopt->peakingLevel]);
                }
            } else {
                GBS::VDS_PK_Y_H_BYPS::write(1);
            }
        } break;
        case '9': {
            // "TBC mode": relaxed sync processor tolerances for unstable sources
            // (VHS, RF-modulated, off-spec csync). Wider coast windows and ignore.
            uopt->tbcMode = !uopt->tbcMode;
            SerialM.print(F("TBC mode: "));
            SerialM.println(uopt->tbcMode ? F("on") : F("off"));
            saveUserPrefs();
            updateSpDynamic(1); // re-apply tolerances immediately
        } break;
        case 'a':
            webSocket.disconnect();
            Serial.println(F("restart"));
            delay(60);
#ifdef ESP8266
            ESP.reset(); // don't use restart(), messes up websocket reconnects
#else
            ESP.restart();
#endif
            break;
        case 'R':
            uopt->reverseRotaryEncoderForOledMenu ^= 1;
            SerialM.print(F("reverse OLED rotary: "));
            SerialM.println(uopt->reverseRotaryEncoderForOledMenu ? F("on") : F("off"));
            saveUserPrefs();
            break;
        case 'e': // print files on LittleFS
        {
#ifdef ESP8266
            Dir dir = LittleFS.openDir("/");
            while (dir.next()) {
                SerialM.print(dir.fileName());
                SerialM.print(" ");
                SerialM.println(dir.fileSize());
                delay(1); // wifi stack
            }
#else
            File root = LittleFS.open("/");
            File file = root.openNextFile();
            while(file){
                SerialM.print(file.name());
                SerialM.print(" ");
                SerialM.println(file.size());
                file = root.openNextFile();
                delay(1);
            }
#endif
            ////
            File f = LittleFS.open("/preferencesv2.txt", "r");
            if (!f) {
                SerialM.println(F("failed opening preferences file"));
            } else {
                SerialM.print(F("preset preference = "));
                SerialM.println((uint8_t)(f.read() - '0'));
                SerialM.print(F("frame time lock = "));
                SerialM.println((uint8_t)(f.read() - '0'));
                SerialM.print(F("preset slot = "));
                SerialM.println((uint8_t)(f.read()));
                SerialM.print(F("frame lock method = "));
                SerialM.println((uint8_t)(f.read() - '0'));
                SerialM.print(F("auto gain = "));
                SerialM.println((uint8_t)(f.read() - '0'));
                SerialM.print(F("scanlines = "));
                SerialM.println((uint8_t)(f.read() - '0'));
                SerialM.print(F("component output = "));
                SerialM.println((uint8_t)(f.read() - '0'));
                SerialM.print(F("deinterlacer mode = "));
                SerialM.println((uint8_t)(f.read() - '0'));
                SerialM.print(F("line filter = "));
                SerialM.println((uint8_t)(f.read() - '0'));
                SerialM.print(F("peaking = "));
                SerialM.println((uint8_t)(f.read() - '0'));
                SerialM.print(F("preferScalingRgbhv = "));
                SerialM.println((uint8_t)(f.read() - '0'));
                SerialM.print(F("6-tap = "));
                SerialM.println((uint8_t)(f.read() - '0'));
                SerialM.print(F("pal force60 = "));
                SerialM.println((uint8_t)(f.read() - '0'));
                SerialM.print(F("matched = "));
                SerialM.println((uint8_t)(f.read() - '0'));
                SerialM.print(F("step response = "));
                SerialM.println((uint8_t)(f.read() - '0'));
                // file order after #15: full height, calibrate ADC, scanline
                // strength, disable ext clkgen, reverse OLED rotary, tbc, ifautoofst
                SerialM.print(F("wantFullHeight = "));
                SerialM.println((uint8_t)(f.read() - '0'));
                SerialM.print(F("enableCalibrationADC = "));
                SerialM.println((uint8_t)(f.read() - '0'));
                SerialM.print(F("scanlineStrength = "));
                SerialM.println((uint8_t)(f.read() - '0'));
                SerialM.print(F("disable external clock generator = "));
                SerialM.println((uint8_t)(f.read() - '0'));
                if (f.available()) {
                    SerialM.print(F("reverse OLED rotary = "));
                    SerialM.println((uint8_t)(f.read() - '0'));
                }
                if (f.available()) {
                    SerialM.print(F("tbc mode = "));
                    SerialM.println((uint8_t)(f.read() - '0'));
                }
                if (f.available()) {
                    SerialM.print(F("if auto offset = "));
                    SerialM.println((uint8_t)(f.read() - '0'));
                }

                f.close();
            }
        } break;
        case 'f':
        case 'g':
        case 'h':
        case 'p':
        case 's':
        case 'L': {
            // load preset via webui
            uint8_t videoMode = getVideoMode();
            if (videoMode == 0 && GBS::STATUS_SYNC_PROC_HSACT::read())
                videoMode = rto->videoStandardInput; // last known good as fallback
            //else videoMode stays 0 and we'll apply via some assumptions

            if (argument == 'f')
                uopt->presetPreference = Output960P; // 1280x960
            if (argument == 'g')
                uopt->presetPreference = Output720P; // 1280x720
            if (argument == 'h')
                uopt->presetPreference = Output480P; // 720x480/768x576
            if (argument == 'p')
                uopt->presetPreference = Output1024P; // 1280x1024
            if (argument == 's')
                uopt->presetPreference = Output1080P; // 1920x1080
            if (argument == 'L')
                uopt->presetPreference = OutputDownscale; // downscale

            rto->useHdmiSyncFix = 1; // disables sync out when programming preset
            if (rto->videoStandardInput == 14) {
                // vga upscale path: let synwatcher handle it
                rto->videoStandardInput = 15;
            } else {
                // normal path
                applyPresets(videoMode);
            }
            saveUserPrefs();
        } break;
        case 'i':
            // toggle active frametime lock method
            if (!rto->extClockGenDetected) {
                FrameSync::reset(uopt->frameTimeLockMethod);
            }
            if (uopt->frameTimeLockMethod == 0) {
                uopt->frameTimeLockMethod = 1;
            } else if (uopt->frameTimeLockMethod == 1) {
                uopt->frameTimeLockMethod = 0;
            }
            saveUserPrefs();
            activeFrameTimeLockInitialSteps();
            break;
        case 'j':
            // FTL method: VTotal + VSST
            setFrameTimeLockMethod(0);
            break;
        case 'k':
            // FTL method: VTotal only
            setFrameTimeLockMethod(1);
            break;
        case 'l':
            // cycle through available SDRAM clocks
            {
                uint8_t PLL_MS = GBS::PLL_MS::read();
                uint8_t memClock = 0;

                if (PLL_MS == 0)
                    PLL_MS = 2;
                else if (PLL_MS == 2)
                    PLL_MS = 7;
                else if (PLL_MS == 7)
                    PLL_MS = 4;
                else if (PLL_MS == 4)
                    PLL_MS = 3;
                else if (PLL_MS == 3)
                    PLL_MS = 5;
                else if (PLL_MS == 5)
                    PLL_MS = 0;

                switch (PLL_MS) {
                    case 0:
                        memClock = 108;
                        break;
                    case 1:
                        memClock = 81;
                        break; // goes well with 4_2C = 0x14, 4_2D = 0x27
                    case 2:
                        memClock = 10;
                        break; // feedback clock
                    case 3:
                        memClock = 162;
                        break;
                    case 4:
                        memClock = 144;
                        break;
                    case 5:
                        memClock = 185;
                        break; // slight OC
                    case 6:
                        memClock = 216;
                        break; // !OC!
                    case 7:
                        memClock = 129;
                        break;
                    default:
                        break;
                }
                GBS::PLL_MS::write(PLL_MS);
                ResetSDRAM();
                // persist override so the choice survives reboots/presets
                // (helps boards that need a specific SDRAM clock vs banding)
                uopt->sdramClock = PLL_MS;
                saveUserPrefs();
                if (memClock != 10) {
                    SerialM.print(F("SDRAM clock: "));
                    SerialM.print(memClock);
                    SerialM.println("Mhz");
                } else {
                    SerialM.print(F("SDRAM clock: "));
                    SerialM.println(F("Feedback clock"));
                }
            }
            break;
        case 'm':
            SerialM.print(F("Line Filter: "));
            if (uopt->wantVdsLineFilter) {
                uopt->wantVdsLineFilter = 0;
                GBS::VDS_D_RAM_BYPS::write(1);
                SerialM.println("off");
            } else {
                uopt->wantVdsLineFilter = 1;
                GBS::VDS_D_RAM_BYPS::write(0);
                SerialM.println("on");
            }
            saveUserPrefs();
            break;
        case 'n':
            SerialM.print(F("ADC gain++ : "));
            uopt->enableAutoGain = 0;
            setAdcGain(GBS::ADC_RGCTRL::read() - 1);
            SerialM.println(GBS::ADC_RGCTRL::read(), HEX);
            break;
        case 'o':
            SerialM.print(F("ADC gain-- : "));
            uopt->enableAutoGain = 0;
            setAdcGain(GBS::ADC_RGCTRL::read() + 1);
            SerialM.println(GBS::ADC_RGCTRL::read(), HEX);
            break;
        case 'A': {
            uint16_t htotal = GBS::VDS_HSYNC_RST::read();
            uint16_t hbstd = GBS::VDS_DIS_HB_ST::read();
            uint16_t hbspd = GBS::VDS_DIS_HB_SP::read();
            if ((hbstd > 4) && (hbspd < (htotal - 4))) {
                GBS::VDS_DIS_HB_ST::write(GBS::VDS_DIS_HB_ST::read() - 4);
                GBS::VDS_DIS_HB_SP::write(GBS::VDS_DIS_HB_SP::read() + 4);
            } else {
                SerialM.println("limit");
            }
        } break;
        case 'B': {
            uint16_t htotal = GBS::VDS_HSYNC_RST::read();
            uint16_t hbstd = GBS::VDS_DIS_HB_ST::read();
            uint16_t hbspd = GBS::VDS_DIS_HB_SP::read();
            if ((hbstd < (htotal - 4)) && (hbspd > 4)) {
                GBS::VDS_DIS_HB_ST::write(GBS::VDS_DIS_HB_ST::read() + 4);
                GBS::VDS_DIS_HB_SP::write(GBS::VDS_DIS_HB_SP::read() - 4);
            } else {
                SerialM.println("limit");
            }
        } break;
        case 'C': {
            // vert mask +
            uint16_t vtotal = GBS::VDS_VSYNC_RST::read();
            uint16_t vbstd = GBS::VDS_DIS_VB_ST::read();
            uint16_t vbspd = GBS::VDS_DIS_VB_SP::read();
            if ((vbstd > 6) && (vbspd < (vtotal - 4))) {
                GBS::VDS_DIS_VB_ST::write(vbstd - 2);
                GBS::VDS_DIS_VB_SP::write(vbspd + 2);
            } else {
                SerialM.println("limit");
            }
        } break;
        case 'D': {
            // vert mask -
            uint16_t vtotal = GBS::VDS_VSYNC_RST::read();
            uint16_t vbstd = GBS::VDS_DIS_VB_ST::read();
            uint16_t vbspd = GBS::VDS_DIS_VB_SP::read();
            if ((vbstd < (vtotal - 4)) && (vbspd > 6)) {
                GBS::VDS_DIS_VB_ST::write(vbstd + 2);
                GBS::VDS_DIS_VB_SP::write(vbspd - 2);
            } else {
                SerialM.println("limit");
            }
        } break;
        case 'q':
            if (uopt->deintMode != 1) {
                uopt->deintMode = 1;
                disableMotionAdaptDeinterlace();
                if (GBS::GBS_OPTION_SCANLINES_ENABLED::read()) {
                    disableScanlines();
                }
                saveUserPrefs();
            }
            SerialM.println(F("Deinterlacer: Bob"));
            break;
        case 'r':
            if (uopt->deintMode != 0) {
                uopt->deintMode = 0;
                saveUserPrefs();
                // will enable next loop()
            }
            SerialM.println(F("Deinterlacer: Motion Adaptive"));
            break;
        case 't':
            // unused now
            SerialM.print(F("6-tap: "));
            if (uopt->wantTap6 == 0) {
                uopt->wantTap6 = 1;
                GBS::VDS_TAP6_BYPS::write(0);
                GBS::MADPT_Y_DELAY_UV_DELAY::write(GBS::MADPT_Y_DELAY_UV_DELAY::read() - 1);
                SerialM.println("on");
            } else {
                uopt->wantTap6 = 0;
                GBS::VDS_TAP6_BYPS::write(1);
                GBS::MADPT_Y_DELAY_UV_DELAY::write(GBS::MADPT_Y_DELAY_UV_DELAY::read() + 1);
                SerialM.println("off");
            }
            saveUserPrefs();
            break;
        case 'u':
            // restart to attempt wifi station mode connect
            delay(30);
            WiFi.mode(WIFI_STA);
#ifdef ESP8266
            WiFi.hostname(device_hostname_partial); // _full
#else
            WiFi.setHostname(device_hostname_partial);
#endif
            delay(30);
#ifdef ESP8266
            ESP.reset();
#else
            ESP.restart();
#endif
            break;
        case 'v': {
            uopt->wantFullHeight = !uopt->wantFullHeight;
            saveUserPrefs();
            uint8_t vidMode = getVideoMode();
            if (uopt->presetPreference == 5) {
                if (GBS::GBS_PRESET_ID::read() == 0x05 || GBS::GBS_PRESET_ID::read() == 0x15) {
                    applyPresets(vidMode);
                }
            }
        } break;
        case 'w':
            uopt->enableCalibrationADC = !uopt->enableCalibrationADC;
            saveUserPrefs();
            break;
        case 'x':
            uopt->preferScalingRgbhv = !uopt->preferScalingRgbhv;
            SerialM.print(F("preferScalingRgbhv: "));
            if (uopt->preferScalingRgbhv) {
                SerialM.println("on");
            } else {
                SerialM.println("off");
            }
            saveUserPrefs();
            break;
        case 'X':
            SerialM.print(F("ExternalClockGenerator "));
            if (uopt->disableExternalClockGenerator == 0) {
                uopt->disableExternalClockGenerator = 1;
                SerialM.println("disabled");
            } else {
                uopt->disableExternalClockGenerator = 0;
                SerialM.println("enabled");
            }
            saveUserPrefs();
            break;
        case 'z':
            // sog slicer level
            if (rto->currentLevelSOG > 0) {
                rto->currentLevelSOG -= 1;
            } else {
                rto->currentLevelSOG = 16;
            }
            setAndUpdateSogLevel(rto->currentLevelSOG);
            optimizePhaseSP();
            SerialM.print("Phase: ");
            SerialM.print(rto->phaseSP);
            SerialM.print(" SOG: ");
            SerialM.print(rto->currentLevelSOG);
            SerialM.println();
            break;
        case 'E':
            SerialM.print(F("IF Auto Offset: "));
            toggleIfAutoOffset();
            uopt->ifAutoOffset = GBS::IF_AUTO_OFST_EN::read() ? 1 : 0;
            saveUserPrefs();
            if (uopt->ifAutoOffset) {
                SerialM.println("on");
            } else {
                SerialM.println("off");
            }
            break;
        case 'F':
            // freeze pic
            if (GBS::CAPTURE_ENABLE::read()) {
                GBS::CAPTURE_ENABLE::write(0);
            } else {
                GBS::CAPTURE_ENABLE::write(1);
            }
            break;
        case 'K':
            // scanline strength
            if (uopt->scanlineStrength >= 0x10) {
                uopt->scanlineStrength -= 0x10;
            } else {
                uopt->scanlineStrength = 0x50;
            }
            if (rto->scanlinesEnabled) {
                GBS::MADPT_Y_MI_OFFSET::write(uopt->scanlineStrength);
                GBS::MADPT_UV_MI_OFFSET::write(uopt->scanlineStrength);
            }
            saveUserPrefs();
            SerialM.print(F("Scanline Brightness: "));
            SerialM.println(uopt->scanlineStrength, HEX);
            break;
        case 'Z':
            // brightness++
            GBS::VDS_Y_OFST::write(GBS::VDS_Y_OFST::read() + 1);
            SerialM.print(F("Brightness++ : "));
            SerialM.println(GBS::VDS_Y_OFST::read(), DEC);
            break;
        case 'T':
            // brightness--
            GBS::VDS_Y_OFST::write(GBS::VDS_Y_OFST::read() - 1);
            SerialM.print(F("Brightness-- : "));
            SerialM.println(GBS::VDS_Y_OFST::read(), DEC);
        break;
        case 'N':
            // contrast++
            GBS::VDS_Y_GAIN::write(GBS::VDS_Y_GAIN::read() + 1);
            SerialM.print(F("Contrast++ : "));
            SerialM.println(GBS::VDS_Y_GAIN::read(), DEC);
        break;
        case 'M':
            // contrast--
            GBS::VDS_Y_GAIN::write(GBS::VDS_Y_GAIN::read() - 1);
            SerialM.print(F("Contrast-- : "));
            SerialM.println(GBS::VDS_Y_GAIN::read(), DEC);
        break;
        case 'Q':
             // pb/u gain++
            GBS::VDS_UCOS_GAIN::write(GBS::VDS_UCOS_GAIN::read() + 1);
            SerialM.print(F("Pb/U gain++ : "));
            SerialM.println(GBS::VDS_UCOS_GAIN::read(), DEC);
            break;
        case 'H':
             // pb/u gain--
            GBS::VDS_UCOS_GAIN::write(GBS::VDS_UCOS_GAIN::read() - 1);
            SerialM.print(F("Pb/U gain-- : "));
            SerialM.println(GBS::VDS_UCOS_GAIN::read(), DEC);
            break;
        break;
        case 'P':
            // pr/v gain++
            GBS::VDS_VCOS_GAIN::write(GBS::VDS_VCOS_GAIN::read() + 1);
            SerialM.print(F("Pr/V gain++ : "));
            SerialM.println(GBS::VDS_VCOS_GAIN::read(), DEC);
            break;
        case 'S':
            // pr/v gain--
            GBS::VDS_VCOS_GAIN::write(GBS::VDS_VCOS_GAIN::read() - 1);
            SerialM.print(F("Pr/V gain-- : "));
            SerialM.println(GBS::VDS_VCOS_GAIN::read(), DEC);
            break;
        case 'O':
            // info
            if (GBS::ADC_INPUT_SEL::read() == 1)
            {
                SerialM.println("RGB reg");
                SerialM.println(F("------------ "));
                SerialM.print(F("Y_OFFSET: "));
                SerialM.println(GBS::VDS_Y_OFST::read(), DEC);
                SerialM.print(F("U_OFFSET: "));
                SerialM.println( GBS::VDS_U_OFST::read(), DEC);
                SerialM.print(F("V_OFFSET: "));
                SerialM.println(GBS::VDS_V_OFST::read(), DEC);
                SerialM.print(F("Y_GAIN: "));
                SerialM.println(GBS::VDS_Y_GAIN::read(), DEC);
                SerialM.print(F("USIN_GAIN: "));
                SerialM.println(GBS::VDS_USIN_GAIN::read(), DEC);
                SerialM.print(F("UCOS_GAIN: "));
                SerialM.println(GBS::VDS_UCOS_GAIN::read(), DEC);
            }
            else
            {
                SerialM.println("YPbPr reg");
                SerialM.println(F("------------ "));
                SerialM.print(F("Y_OFFSET: "));
                SerialM.println(GBS::VDS_Y_OFST::read(), DEC);
                SerialM.print(F("U_OFFSET: "));
                SerialM.println( GBS::VDS_U_OFST::read(), DEC);
                SerialM.print(F("V_OFFSET: "));
                SerialM.println(GBS::VDS_V_OFST::read(), DEC);
                SerialM.print(F("Y_GAIN: "));
                SerialM.println(GBS::VDS_Y_GAIN::read(), DEC);
                SerialM.print(F("USIN_GAIN: "));
                SerialM.println(GBS::VDS_USIN_GAIN::read(), DEC);
                SerialM.print(F("UCOS_GAIN: "));
                SerialM.println(GBS::VDS_UCOS_GAIN::read(), DEC);
            }
            break;
        case 'U':
            // default
            if (GBS::ADC_INPUT_SEL::read() == 1)
            {
                GBS::VDS_Y_GAIN::write(128);
                GBS::VDS_UCOS_GAIN::write(28);
                GBS::VDS_VCOS_GAIN::write(41);
                GBS::VDS_Y_OFST::write(0);
                GBS::VDS_U_OFST::write(0);
                GBS::VDS_V_OFST::write(0);
                GBS::ADC_ROFCTRL::write(adco->r_off);
                GBS::ADC_GOFCTRL::write(adco->g_off);
                GBS::ADC_BOFCTRL::write(adco->b_off);
                SerialM.println("RGB:defauit");
            }
            else
            {
                GBS::VDS_Y_GAIN::write(128);
                GBS::VDS_UCOS_GAIN::write(28);
                GBS::VDS_VCOS_GAIN::write(41);
                GBS::VDS_Y_OFST::write(254);
                GBS::VDS_U_OFST::write(3);
                GBS::VDS_V_OFST::write(3);
                GBS::ADC_ROFCTRL::write(adco->r_off);
                GBS::ADC_GOFCTRL::write(adco->g_off);
                GBS::ADC_BOFCTRL::write(adco->b_off);
                SerialM.println("YPbPr:defauit");
            }
            break;
        case 'b': {
            // dual-output boards (GBS-8220): toggle the 2nd sync output driver.
            // Experiment: check whether port 2 loses sync (or vsync only).
            uint8_t en = GBS::DAC_RGBS_S1EN::read();
            GBS::DAC_RGBS_S1EN::write(!en);
            SerialM.print(F("S1 sync out driver: "));
            SerialM.println(!en ? F("on") : F("off"));
        } break;
        case 'c': {
            // cycle output sync source, 0_4f bits 6-7 (VDS / HDBypass / SP).
            // Experiment: changes the sync signal on both VGA outputs.
            rto->outSyncSel = (rto->outSyncSel + 1) & 3;
            GBS::OUT_SYNC_SEL::write(rto->outSyncSel);
            SerialM.print(F("OUT_SYNC_SEL: "));
            SerialM.println(rto->outSyncSel);
            } break;
        case 'd': {
            // "aperture grille" experiment: deliberate offsets on VDS_HSCALE
            // create periodic column brightness variation (vertical dark
            // stripes) - the horizontal equivalent of what scanlines do with
            // line interpolation. Cycle through +/- offsets back to normal.
            static const int8_t offs[8] = {0, 1, 2, 4, -1, -2, -4, -8};
            rto->maskIdx = (rto->maskIdx + 1) % 8;
            if (rto->maskIdx == 1 || rto->maskHscaleBase == 0xffff) {
                rto->maskHscaleBase = GBS::VDS_HSCALE::read(); // capture preset's value
            }
            rto->maskMoireOff = offs[rto->maskIdx];
            GBS::VDS_HSCALE::write((uint16_t)((int)rto->maskHscaleBase + rto->maskMoireOff));
            SerialM.print(F("mask moire off: "));
            SerialM.print((int)rto->maskMoireOff);
            SerialM.print(F(" hscale: 0x"));
            SerialM.println(GBS::VDS_HSCALE::read(), HEX);
            } break;
        case 'Y': {
            // composite-style chroma shift (color fringing, mask-like effect):
            // cycles U delay, V delay, both, none
            rto->chromaShift = (rto->chromaShift + 1) & 3;
            GBS::VDS_U_DELAY::write(rto->chromaShift & 1);
            GBS::VDS_V_DELAY::write((rto->chromaShift >> 1) & 1);
            SerialM.print(F("chroma shift (u,v): "));
            SerialM.print(rto->chromaShift & 1);
            SerialM.print(F(","));
            SerialM.println((rto->chromaShift >> 1) & 1);
            } break;
        case 'I': {
            // shift image left (move display window, keep size)
            uint16_t st = GBS::VDS_DIS_HB_ST::read(), sp = GBS::VDS_DIS_HB_SP::read();
            if (st > 4 && sp > 4) {
                GBS::VDS_DIS_HB_ST::write(st - 4);
                GBS::VDS_DIS_HB_SP::write(sp - 4);
            } else {
                SerialM.println("limit");
            }
        } break;
        case 'J': {
            // shift image right
            uint16_t st = GBS::VDS_DIS_HB_ST::read(), sp = GBS::VDS_DIS_HB_SP::read();
            uint16_t htotal = GBS::VDS_HSYNC_RST::read();
            if (st < htotal - 4 && sp < htotal - 4) {
                GBS::VDS_DIS_HB_ST::write(st + 4);
                GBS::VDS_DIS_HB_SP::write(sp + 4);
            } else {
                SerialM.println("limit");
            }
        } break;
        case 'V': {
            // shift image up
            uint16_t st = GBS::VDS_DIS_VB_ST::read(), sp = GBS::VDS_DIS_VB_SP::read();
            if (st > 4 && sp > 4) {
                GBS::VDS_DIS_VB_ST::write(st - 2);
                GBS::VDS_DIS_VB_SP::write(sp - 2);
            } else {
                SerialM.println("limit");
            }
        } break;
        case 'W': {
            // shift image down
            uint16_t st = GBS::VDS_DIS_VB_ST::read(), sp = GBS::VDS_DIS_VB_SP::read();
            uint16_t vtotal = GBS::VDS_VSYNC_RST::read();
            if (st < vtotal - 4 && sp < vtotal - 4) {
                GBS::VDS_DIS_VB_ST::write(st + 2);
                GBS::VDS_DIS_VB_SP::write(sp + 2);
            } else {
                SerialM.println("limit");
            }
        } break;
        case '*': {
            // SVM (scan velocity modulation): analog-domain edge emphasis,
            // the trick CRTs used for "bright crisp" edges. The chip powers
            // it (DAC_RGBS_SPD=0) but the firmware never sets a gain.
            // SVM needs a non-zero VCLK delay or the modulation has nothing
            // to act on (that's why gain alone showed no visible effect).
            // Cycle: off -> 0x40 -> 0x80 -> 0xC0
            static const uint8_t svmGains[4] = {0x00, 0x40, 0x80, 0xC0};
            static uint8_t svmIdx = 0;
            svmIdx = (svmIdx + 1) & 3;
            rto->svmGain = svmGains[svmIdx];
            uint8_t on = rto->svmGain != 0;
            GBS::DAC_RGBS_SPD::write(0); // keep SVM powered
            GBS::VDS_SVM_GAIN::write(rto->svmGain);
            GBS::VDS_SVM_2ND_BYPS::write(on ? 0 : 1);
            GBS::VDS_SVM_VCLK_DELAY::write(on ? 4 : 0);
            GBS::VDS_SVM_OFFSET::write(on ? 0x20 : 0);
            SerialM.print(F("SVM gain: 0x"));
            SerialM.println(rto->svmGain, HEX);
            } break;
        case '!': {
            // Real scaler-filter bypass: VDS_TAP6_BYPS (3_24 bit3) disables
            // the 6-tap scaling filter -> hard pixel edges. Plain on/off;
            // the input-path interpolators (3_40) caused color corruption,
            // so they stay untouched. Preset reapply resets TAP6 - the
            // state is restored via rto->interpBypass.
            rto->interpBypass = !rto->interpBypass;
            setTap6Bypass(rto->interpBypass);
            SerialM.print(F("interp bypass: "));
            SerialM.println(rto->interpBypass ? F("on (tap6)") : F("off"));
            } break;
        // --- REA-discovered blocks (preset arrays bypass them; fxFlags bits
        // are re-applied in doPostPresetLoadSteps) ---
        // (command chars chosen from URL-unreserved set: - _ ~ ( ))
        case '-': {
            // noise reduction: Y + C paths + diff LPF + motion-adaptive
            // threshold (3_52 b4-b7, bypassed in presets). Also enables
            // still-region processing driven by the global noise estimate.
            rto->fxFlags ^= 0x01;
            uint8_t on = rto->fxFlags & 0x01;
            GBS::VDS_NR_Y_BYPS::write(on ? 0 : 1);
            GBS::VDS_NR_C_BYPS::write(on ? 0 : 1);
            GBS::VDS_NR_DIF_LPF5_BYPS::write(on ? 0 : 1);
            GBS::VDS_NR_MI_TH_EN::write(on ? 1 : 0);
            GBS::VDS_NR_EN_GLB_STILL::write(on ? 1 : 0);
            if (on) {
                GBS::VDS_NR_STILL_GAIN::write(6);
                GBS::VDS_NR_MI_GAIN::write(4);
            }
            SerialM.print(F("noise reduction: "));
            SerialM.println(on ? F("on") : F("off"));
            } break;
        case '_': {
            // black level expansion (3_2A b0, bypassed in presets;
            // level/gain already tuned by preset: 3_28/3_29)
            rto->fxFlags ^= 0x02;
            uint8_t on = rto->fxFlags & 0x02;
            GBS::VDS_BLEV_BYPS::write(on ? 0 : 1);
            SerialM.print(F("black expand: "));
            SerialM.println(on ? F("on") : F("off"));
            } break;
        case '~': {
            // white level expansion (3_56 b7, bypassed; gain s3_58)
            rto->fxFlags ^= 0x04;
            uint8_t on = rto->fxFlags & 0x04;
            GBS::VDS_W_LEV_BYPS::write(on ? 0 : 1);
            SerialM.print(F("white expand: "));
            SerialM.println(on ? F("on") : F("off"));
            } break;
        case '(': {
            // blue stretch (3_73 b3, bypassed)
            rto->fxFlags ^= 0x08;
            uint8_t on = rto->fxFlags & 0x08;
            GBS::VDS_BLUE_BYPS::write(on ? 0 : 1);
            SerialM.print(F("blue stretch: "));
            SerialM.println(on ? F("on") : F("off"));
            } break;
        case ')': {
            // vertical + chroma peaking (3_4E b1/b3, bypassed;
            // chroma gain s3_4F=0x1E already set by preset)
            rto->fxFlags ^= 0x10;
            uint8_t on = rto->fxFlags & 0x10;
            GBS::VDS_PK_Y_V_BYPS::write(on ? 0 : 1);
            GBS::VDS_C_VPK_BYPS::write(on ? 0 : 1);
            SerialM.print(F("vert/chroma peaking: "));
            SerialM.println(on ? F("on") : F("off"));
            } break;
        case 'y': {
            // U/V swap - hue inversion (3_24 b0). Pure fun/reversible.
            rto->fxFlags ^= 0x20;
            uint8_t on = rto->fxFlags & 0x20;
            GBS::VDS_UV_FLIP::write(on);
            SerialM.print(F("uv flip: "));
            SerialM.println(on ? F("on") : F("off"));
            } break;
        case 'G': {
            // color-correction engines C1 + C2 (3_65 b5 / 3_6B b5, bypassed;
            // gains/tag slopes already set by presets)
            rto->fxFlags ^= 0x40;
            uint8_t on = rto->fxFlags & 0x40;
            GBS::VDS_C1_BYPS::write(on ? 0 : 1);
            GBS::VDS_C2_BYPS::write(on ? 0 : 1);
            SerialM.print(F("color correction: "));
            SerialM.println(on ? F("on") : F("off"));
            } break;
        case ',': {
            // chroma noise suppressor (3_60 b2, bypassed)
            rto->fxFlags ^= 0x80;
            uint8_t on = rto->fxFlags & 0x80;
            GBS::VDS_NS_BYPS::write(on ? 0 : 1);
            SerialM.print(F("chroma noise supp: "));
            SerialM.println(on ? F("on") : F("off"));
            } break;
        case ';': {
            // "crisp pixel" macro: tap6 bypass + 5-band peaking boost +
            // coring against noise + stronger step response
            rto->crispMode = !rto->crispMode;
            applyCrispMode(rto->crispMode);
            SerialM.print(F("crisp mode: "));
            SerialM.println(rto->crispMode ? F("on") : F("off"));
            } break;
        case ':': {
            // step response strength (3_2B b0-3): steeper luma edges
            static const uint8_t sg[4] = {1, 4, 8, 0xF};
            static uint8_t sgIdx = 0;
            sgIdx = (sgIdx + 1) & 3;
            GBS::VDS_STEP_GAIN::write(sg[sgIdx]);
            SerialM.print(F("step gain: 0x"));
            SerialM.println(sg[sgIdx], HEX);
        } break;
        case '<': {
            // peaking coring on all 5 bands: higher values sharpen only
            // strong edges, rejecting noise amplification
            static const uint8_t coreLv[4] = {0, 1, 2, 4};
            static uint8_t coreIdx = 0;
            coreIdx = (coreIdx + 1) & 3;
            GBS::VDS_PK_LB_CORE::write(coreLv[coreIdx]);
            GBS::VDS_PK_LH_CORE::write(coreLv[coreIdx]);
            GBS::VDS_PK_HL_CORE::write(coreLv[coreIdx]);
            GBS::VDS_PK_HB_CORE::write(coreLv[coreIdx]);
            GBS::VDS_PK_HH_CORE::write(coreLv[coreIdx]);
            SerialM.print(F("peak coring: "));
            SerialM.println(coreLv[coreIdx]);
        } break;
        case ']': {
            // sampling pixel clock (PLLAD_MD) +1: nudge toward the
            // console's true dot clock to reduce vertical banding/moire.
            // Re-run auto phase ('2') after large adjustments.
            uint16_t md = GBS::PLLAD_MD::read();
            if (md < 0xFFE) {
                GBS::PLLAD_MD::write(md + 1);
                latchPLLAD();
            }
            SerialM.print(F("pixel clock: "));
            SerialM.println(GBS::PLLAD_MD::read());
        } break;
        case '\'': {
            uint16_t md = GBS::PLLAD_MD::read();
            if (md > 0x101) {
                GBS::PLLAD_MD::write(md - 1);
                latchPLLAD();
            }
            SerialM.print(F("pixel clock: "));
            SerialM.println(GBS::PLLAD_MD::read());
        } break;
        case '.': {
            // manual ADC sampling phase nudge (+1 of 32)
            rto->phaseADC = (rto->phaseADC + 1) & 0x1f;
            setAndLatchPhaseADC();
            SerialM.print(F("phaseADC: "));
            SerialM.println(rto->phaseADC);
        } break;
        case '/': {
            // manual SP phase nudge (+1 of 32)
            rto->phaseSP = (rto->phaseSP + 1) & 0x1f;
            setAndLatchPhaseSP();
            SerialM.print(F("phaseSP: "));
            SerialM.println(rto->phaseSP);
        } break;
        case '>': {
            // cycle ADC oversampling 1x -> 2x -> 4x -> 1x
            // higher osr = finer phase granularity; the KS register limits
            // what's possible, setOverSampleRatio() auto-decrements.
            // 4x can destabilize sampling on some sources: if hsync activity
            // is gone after settling, revert to the previous ratio.
            uint8_t prev = rto->osr;
            uint8_t next = (rto->osr == 1) ? 2 : (rto->osr == 2) ? 4 : 1;
            setOverSampleRatio(next, false); // apply now
            latchPLLAD();
            delay(60); // let the pll settle before re-measuring phase
            optimizePhaseSP();
            delay(900);
            if (rto->osr != prev && rto->videoStandardInput != 0 &&
                GBS::STATUS_SYNC_PROC_HSACT::read() == 0) {
                SerialM.println(F("sync lost after oversample - reverting"));
                setOverSampleRatio(prev, false);
                latchPLLAD();
                delay(60);
                optimizePhaseSP();
            }
            SerialM.print(F("oversample: "));
            SerialM.print(rto->osr);
            SerialM.print(F("x (requested "));
            SerialM.print(next);
            SerialM.println(F("x)"));
        } break;
        case '`': {
            // EXPERIMENT: interlaced output ("same as input" for 480i sources
            // on 15kHz CRTs). VDS_DFIELD_EN is unused by all presets; enabling
            // it should make the output field-alternating. Watch 'out_vtotal'
            // telemetry - it should roughly double if fields are emitted.
            uint8_t on = !GBS::VDS_DFIELD_EN::read();
            GBS::VDS_DFIELD_EN::write(on);
            GBS::VDS_FIELDAB_EN::write(1); // field machinery needs both fields
            delay(60);
            SerialM.print(F("interlace out: "));
            SerialM.print(on ? F("on") : F("off"));
            SerialM.print(F(" field="));
            SerialM.print(GBS::STATUS_VDS_FIELD::read());
            SerialM.print(F(" out_vt="));
            SerialM.println(GBS::STATUS_VDS_VERT_COUNT::read());
        } break;
        // --- picture proc-amp on the chip's YUV matrix (s3_35..3_3C).
        // Deltas (-8..8) are applied over each preset's base values;
        // saved into a profile like any other register state. ---
        case '{':
        case '}': {
            rto->picBright += (argument == '}') ? 1 : -1;
            if (rto->picBright > 8) rto->picBright = 8;
            if (rto->picBright < -8) rto->picBright = -8;
            applyPicAdjust();
            SerialM.print(F("brightness: "));
            SerialM.println(rto->picBright);
        } break;
        case '[':
        case '^': {
            rto->picContrast += (argument == '^') ? 1 : -1;
            if (rto->picContrast > 8) rto->picContrast = 8;
            if (rto->picContrast < -8) rto->picContrast = -8;
            applyPicAdjust();
            SerialM.print(F("contrast: "));
            SerialM.println(rto->picContrast);
        } break;
        case '$':
        case '|': {
            rto->picSat += (argument == '|') ? 1 : -1;
            if (rto->picSat > 8) rto->picSat = 8;
            if (rto->picSat < -8) rto->picSat = -8;
            applyPicAdjust();
            SerialM.print(F("saturation: "));
            SerialM.println(rto->picSat);
        } break;
        case '"': {
            // hue as a one-button cycle through a signed range (derived
            // from the current value so preset reloads can't desync it)
            static const int8_t hues[7] = {0, 3, 6, 8, -8, -6, -3};
            uint8_t idx = 0;
            for (uint8_t i = 0; i < 7; i++) {
                if (hues[i] == rto->picHue) {
                    idx = i;
                    break;
                }
            }
            rto->picHue = hues[(idx + 1) % 7];
            applyPicAdjust();
            SerialM.print(F("hue: "));
            SerialM.println(rto->picHue);
        } break;
        default:
            break;
    }
}

