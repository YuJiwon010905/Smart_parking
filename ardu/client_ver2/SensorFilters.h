#pragma once

// CODEX FIX: sensor-only preprocessing. No parking, entrance, exit, or gate
// decision is made here; those decisions belong to VS_server.
static const uint16_t IR_DEBOUNCE_MS = 100;

struct DigitalDebounceState {
  bool initialized;
  bool stable;
  bool candidate;
  uint32_t candidateAt;
};

static bool readDebouncedActiveLow(uint8_t pin, DigitalDebounceState& s) {
  const bool raw = (digitalRead(pin) == LOW);
  const uint32_t now = millis();
  if (!s.initialized) {
    s.initialized = true;
    s.stable = s.candidate = raw;
    s.candidateAt = now;
    return s.stable;
  }
  if (raw != s.candidate) {
    s.candidate = raw;
    s.candidateAt = now;
  }
  if (s.stable != s.candidate && now - s.candidateAt >= IR_DEBOUNCE_MS)
    s.stable = s.candidate;
  return s.stable;
}

struct StableNearState {
  uint8_t history;
  uint8_t samples;
  bool value;
};

static bool updateStableNear(StableNearState& s, bool nearNow) {
  s.history = (uint8_t)(((s.history << 1) | (nearNow ? 1 : 0)) & 0x07);
  if (s.samples < 3) s.samples++;
  if (s.samples < 3) {
    s.value = nearNow;
    return s.value;
  }
  const uint8_t ones = (uint8_t)((s.history & 1)
                        + ((s.history >> 1) & 1)
                        + ((s.history >> 2) & 1));
  s.value = (ones >= 2);
  return s.value;
}
