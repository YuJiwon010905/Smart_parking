/* parking_p2.ino — P2 multi-device test node: IR A4 + LED L4 */
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

static const uint8_t PIN_IR_A4 = 2;
static const uint8_t PIN_LED_L4 = 11;

static bool readIR4() {
  static DigitalDebounceState state = {false, false, false, 0};
  return readDebouncedActiveLow(PIN_IR_A4, state);
}

static bool cmdLed4(uint32_t arg) {
  digitalWrite(PIN_LED_L4, arg ? HIGH : LOW);
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
  pinMode(PIN_IR_A4, INPUT);
  pinMode(PIN_LED_L4, OUTPUT);
  digitalWrite(PIN_LED_L4, LOW);

  node.sensor("A4").on(readIR4);
  node.actuator("L4").on(cmdLed4);

#if DEBUG
  Serial.println(F("[P2] A4 L4 registered"));
#endif
}

void loop() {
  node.tick();
}
