#include "input.h"
#include "board.h"
#include <RotaryEncoder.h>

// Interrupt-driven quadrature decode, matching the proven launcher setup:
// RotaryEncoder in TWO03 latch mode gives one position step per physical detent.
static RotaryEncoder *encoder = nullptr;
static IRAM_ATTR void onEncoderTick() {
    if (encoder) encoder->tick();
}

static long lastPos = 0;
static bool selHeld = false, bkHeld = false;
static unsigned long selEdge = 0, bkEdge = 0;
static const unsigned long kBtnDebounceMs = 180;

void inputInit() {
    // GPIO0 (wheel click) and GPIO6 (back) read LOW when pressed.
    pinMode(SEL_BTN, INPUT_PULLUP);
    pinMode(BK_BTN, INPUT_PULLUP);
    encoder = new RotaryEncoder(ENCODER_INA, ENCODER_INB, RotaryEncoder::LatchMode::TWO03);
    attachInterrupt(digitalPinToInterrupt(ENCODER_INA), onEncoderTick, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENCODER_INB), onEncoderTick, CHANGE);
    lastPos = encoder->getPosition();
}

// Small queue for events pushed in from elsewhere (the web remote). Written from the web
// server's handler and read from the UI loop — both run on the main task, so a plain ring is
// enough; `volatile` only guards against the compiler caching the indices.
static constexpr int kInjectMax = 8;
static volatile InputEvent injected[kInjectMax];
static volatile uint8_t injHead = 0, injTail = 0;

void inputInject(InputEvent e) {
    const uint8_t next = (uint8_t)((injHead + 1) % kInjectMax);
    if (next == injTail) return; // full: drop rather than block the web request
    injected[injHead] = e;
    injHead = next;
}

static void (*idleHook)() = nullptr;
static bool inIdle = false;

void inputSetIdleHook(void (*fn)()) { idleHook = fn; }

InputEvent inputPoll() {
    // Whatever the screen is, waiting for input is also the moment to serve the network. The
    // guard is because the hook ends up inside the web handlers, and one of those could find
    // its way back here.
    if (idleHook && !inIdle) {
        inIdle = true;
        idleHook();
        inIdle = false;
    }
    if (injTail != injHead) { // remote events first, so they never sit behind idle polling
        const InputEvent e = injected[injTail];
        injTail = (uint8_t)((injTail + 1) % kInjectMax);
        return e;
    }
    // Buttons are checked BEFORE the wheel so a press/back can never be starved by wheel
    // events — spurious encoder ticks (e.g. during USB drive mode) used to make BACK do
    // nothing because the wheel branch returned first on every call.
    const bool sel = digitalRead(SEL_BTN) == BTN_ACT;
    if (sel && !selHeld && millis() - selEdge > kBtnDebounceMs) {
        selHeld = true;
        selEdge = millis();
        return EV_PRESS;
    }
    if (!sel) selHeld = false;

    const bool bk = digitalRead(BK_BTN) == BTN_ACT;
    if (bk && !bkHeld && millis() - bkEdge > kBtnDebounceMs) {
        bkHeld = true;
        bkEdge = millis();
        return EV_BACK;
    }
    if (!bk) bkHeld = false;

    // Wheel — one detent per call. A large jump is treated as noise and snapped (not emitted),
    // so a burst accumulated while we were busy can't send the highlight spinning.
    const long pos = encoder->getPosition();
    const long d = pos - lastPos;
    if (d > 6 || d < -6) {
        lastPos = pos;
        return EV_NONE;
    }
    if (d != 0) {
        if (pos > lastPos) {
            ++lastPos;
            return EV_LEFT;
        }
        --lastPos;
        return EV_RIGHT;
    }
    return EV_NONE;
}

void inputDrain() {
    if (encoder) lastPos = encoder->getPosition();
    selHeld = bkHeld = false;
    injTail = injHead; // a new screen starts clean, remote presses included
}
