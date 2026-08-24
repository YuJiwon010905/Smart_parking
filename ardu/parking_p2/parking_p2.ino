/* parking_p2.ino — P2 multi-device node: local IR A1 + LED L1 */
#include <SoftwareSerial.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>
#include <stdio.h>
#include <string.h>
#include "SensorFilters.h"

#ifndef DEBUG
#define DEBUG 1
#endif

#define DEVICE_ID "P2"
static_assert(sizeof(DEVICE_ID) > 1 && sizeof(DEVICE_ID) <= 9,
              "DEVICE_ID must be 1..8 chars");

static const uint8_t PIN_IR_A1 = 2;
static const uint8_t PIN_LED_L1 = 11;

static bool readIR1() {
  static DigitalDebounceState state = {false, false, false, 0};
  return readDebouncedActiveLow(PIN_IR_A1, state);
}

static bool cmdLed1(uint32_t arg) {
  digitalWrite(PIN_LED_L1, arg ? HIGH : LOW);
  return true;
}

// Framework include order must remain unchanged.
#include "Module.h"
#include "Boot.h"
#include "Config.h"
#include "RxBuf.h"
#include "Diag.h"
#include "TxState.h"
#include "Checksum.h"
#include "RxLine.h"
#include "LinkGate.h"
#include "Counters.h"
#include "LinkRecovery.h"
#include "FrameCodec.h"
#include "Modules.h"
#include "FrameCodec2.h"
#include "Commands.h"
#include "Session.h"
#include "Runtime.h"

void setup() {
  Serial.begin(115200);
  node.begin();

  // Active-low IR module. If the physical sensor requires an internal pull-up,
  // change INPUT to INPUT_PULLUP after checking its output circuit.
  pinMode(PIN_IR_A1, INPUT);
  pinMode(PIN_LED_L1, OUTPUT);
  digitalWrite(PIN_LED_L1, LOW);

  // P1에도 A1/L1이 있지만 전역 신원은 P2/A1, P2/L1이므로 충돌하지 않는다.
  node.sensor("A1").on(readIR1);
  node.actuator("L1").on(cmdLed1);

#if DEBUG
  Serial.println(F("[P2] local A1 L1 registered"));
#endif
}

void loop() {
  node.tick();
}
