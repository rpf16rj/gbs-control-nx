#include "IRRemote.h"

#ifdef HAVE_IR_REMOTE

#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include "../core/Globals.h"
#include "../config/Config.h"
#include "../commands/CommandQueue.h"
#include "../web/SerialMirror.h"

// Receiver pin. Override with -DPIN_IR_RECV=<gpio> in build_flags.
#ifndef PIN_IR_RECV
#ifdef ESP32
#if defined(CONFIG_IDF_TARGET_ESP32C3)
#define PIN_IR_RECV 3 // free GPIO on C3 (keep off USB/UART/I2C pins)
#else
#define PIN_IR_RECV 19 // classic ESP32: GPIO19 is input-capable, no boot strap
#endif
#else
#define PIN_IR_RECV D4 // ESP8266 D1 mini: GPIO2 idles high (IR receivers idle high)
#endif
#endif

static IRrecv irrecv(PIN_IR_RECV, 1024, 50, true);
static decode_results irResults;

// Map a decoded NEC code to a command char + which queue it belongs to.
// 's' queue = serialCommandQueue (type 1), 'u' = userCommandQueue (type 2).
struct IRBinding {
    uint32_t code;
    char queue; // 's' or 'u'
    char cmd;
};

// Codes below are the common 21-key "Car MP3" NEC remote (0x00FFxxxx).
static const IRBinding irBindings[] = {
    {0xFFA25D, 'u', '5'}, // CH-  : toggle FrameTimeLock
    {0xFF629D, 'u', 'i'}, // CH   : toggle FTL method (VTotal+VSST / VTotal)
    {0xFFE21D, 'u', '9'}, // CH+  : TBC mode toggle
    {0xFF22DD, 's', '-'}, // |<<  : shift image left
    {0xFF02FD, 's', '+'}, // >>|  : shift image right
    {0xFFC23D, 'u', '4'}, // >||  : save custom preset
    {0xFFE01F, 'u', 'z'}, // -    : SOG level --
    {0xFFA857, 'u', '5'}, // +    : (same as CH-, FTL toggle is more useful)
    {0xFF906F, 's', 'D'}, // EQ   : debug view toggle
    {0xFF6897, 'u', '0'}, // 0    : PAL force 60Hz toggle
    {0xFF9867, 'u', '7'}, // 100+ : scanlines toggle
    {0xFFB04F, 'u', 'q'}, // 200+ : bob deinterlace (tap twice? 'q' sets bob)
    {0xFF30CF, 'u', 'f'}, // 1    : 1280x960 output
    {0xFF18E7, 'u', 'h'}, // 2    : 720x480/576 output
    {0xFF7A85, 'u', 'g'}, // 3    : 1280x720 output
    {0xFF10EF, 'u', 'p'}, // 4    : 1280x1024 output
    {0xFF38C7, 'u', 's'}, // 5    : 1920x1080 output
    {0xFF5AA5, 'u', 'L'}, // 6    : downscale output
    {0xFF42BD, 'u', 'r'}, // 7    : motion adaptive deinterlace
    {0xFF4AB5, 'u', 'K'}, // 8    : scanline strength cycle
    {0xFF52AD, 'u', 'x'}, // 9    : preferScalingRgbhv toggle
};

void initIRRemote()
{
    irrecv.enableIRIn();
    SerialM.print(F("IR remote receiver on GPIO "));
    SerialM.println(PIN_IR_RECV);
}

void pollIRRemote()
{
    if (!irrecv.decode(&irResults)) {
        return;
    }

    if (irResults.repeat) {
        // key held down: re-run the last command for repeat-capable keys
        // (shift commands benefit from auto-repeat)
        irrecv.resume();
        return;
    }

    uint32_t code = irResults.value & 0xFFFFFFFF;
    bool handled = false;
    for (const auto &b : irBindings) {
        if (b.code == code) {
            if (b.queue == 's') {
                serialCommandQueue.push(b.cmd);
            } else {
                userCommandQueue.push(b.cmd);
            }
            SerialM.print(F("IR cmd '"));
            SerialM.print(b.cmd);
            SerialM.println(F("' queued"));
            handled = true;
            break;
        }
    }
    if (!handled) {
        // print unknown codes so the table can be extended for other remotes
        SerialM.print(F("IR unknown code: 0x"));
        SerialM.println(code, HEX);
    }
    irrecv.resume();
}

#else

void initIRRemote() {}
void pollIRRemote() {}

#endif // HAVE_IR_REMOTE
