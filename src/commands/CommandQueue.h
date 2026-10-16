#ifndef COMMAND_QUEUE_H
#define COMMAND_QUEUE_H

#include <Arduino.h>

// Single-producer / single-consumer ring buffer.
// Web server and WiFi callbacks run in the AsyncTCP context (a different task on
// ESP32, and outside loop() on ESP8266), so they must not mutate the String
// buffers that loop() consumes. push() is safe from the callback side, pop()
// from the loop side; no allocation, no locking needed.
struct CommandQueue {
    static const uint8_t CAPACITY = 64;

    bool push(char c)
    {
        uint8_t next = (uint8_t)((head + 1) % CAPACITY);
        if (next == tail) {
            return false; // full: drop instead of corrupting
        }
        buf[head] = c;
        head = next;
        return true;
    }

    bool pop(char &c)
    {
        if (tail == head) {
            return false;
        }
        c = buf[tail];
        tail = (uint8_t)((tail + 1) % CAPACITY);
        return true;
    }

    bool empty() const { return tail == head; }

private:
    volatile uint8_t head = 0;
    volatile uint8_t tail = 0;
    char buf[CAPACITY];
};

extern CommandQueue serialCommandQueue; // 'sc' endpoint / serial-style commands
extern CommandQueue userCommandQueue;   // 'uc' endpoint / user (type 2) commands

// Pending register poke/peek scheduled by /api/v1/reg (and /api/v1/segdump).
// Same SPSC discipline as the queues: the async handler writes the slot,
// loop() (sole I2C owner) consumes and clears it. Single slot, last wins.
extern volatile uint8_t regOpSeg;    // 0xff = idle
extern volatile uint8_t regOpOff;
extern volatile int16_t regOpVal;    // -1 = read, else write this byte
extern volatile uint8_t regDumpSeg;  // 0xff = no dump pending
extern int16_t apiLastRegRead;       // result of last /api/v1/reg read

#endif // COMMAND_QUEUE_H
