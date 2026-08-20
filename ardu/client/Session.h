#pragma once
// ═════════════════════════════════════════════════════════════════════════
// Session.h — 주기 처리(sensorTick·statusTick·cntTick 호출·diag·ram)
//   ⚙ 응용 성질
//     슬롯·프레임 로직. 잠금 대상이 아니다.
// ═════════════════════════════════════════════════════════════════════════
// 🔴🔴 **위치를 옮기지 마라.** `client.ino` 의 **정해진 자리에서** `#include` 된다.
//   자리를 바꾸거나 헤더 순서를 바꾸면 **선언·초기화 순서가 같이 바뀌고**,
//   **그 순간 거동이 바뀌고 산출물이 달라진다.**

// ─────────────────────────────────────────────────────────────────────────
// 주기 처리
#include "EspLink_ladder.h"   // ← EspLink 링크 계층 (REQ-0264). **위치를 옮기지 마라**

// ✏️ `sensorTick()` 은 **`ParkingNode::readSensors()` 로 옮겼다** (REQ-0275 B단계).
//   자리 상태를 훑어 자리 상태를 쓰는 일이라 **자리의 주인이 하는 것이 맞다.**
//   `loop()` 은 이제 `node.readSensors()` 를 부른다 — 흐름이 호출부에 보인다.

static void statusTick(unsigned long now) {
  // §12A.4 무장 여부의 진실은 tmask 다. 그래서 tmask 변화도 occupied/reserved 와 **같은 경로**로
  // 즉시 전송을 트리거해야 한다. 안 그러면 무장 직후 최대 1초 동안 화면이 무장 사실을 모르고,
  // 그 사이에 주입이 들어오면 **주입값이 경고 없이 그려지는 프레임**이 생긴다(§12A.6 위반).
  // 비용은 사실상 0 이다 — 무장·해제·주입은 사람이 누르는 드문 사건이라 전송 횟수가 늘지 않는다.
  uint16_t tmaskNow = node.testArmed ? node.ovrActive : TMASK_ABSENT;

  bool changed = (node.occMask != sentOcc) || (node.resMask != sentRes) || (tmaskNow != sentTmask);
  if (changed && !changePending) { changePending = true; changeAt = now; }
  if (!changed) changePending = false;

  // ── 🔴 슬롯 경계 (2026-08-17) ──────────────────────────────────────────
  //   ⚠ `slotStart += SLOT_MS` — **더한다.** `= now` 로 하면 매 슬롯 오차가 누적돼
  //     주기가 스스로 늘어난다(원장 §3.4 의 `lastStatusAt = millis()` 가 그것이다 —
  //     그 결함이 실측 주기를 1.000s 가 아니라 **1.113s** 로 만들었다).
  //   ⚠ while 인 이유: 오래 막혀 여러 슬롯이 지났으면 **전부 소진**해야 위상이 맞는다.
  //     그렇지 않으면 밀린 만큼 계속 뒤처진 위상으로 돈다.
  while ((uint32_t)(now - slotStart) >= SLOT_MS) {
    slotStart += SLOT_MS;
    slotNo++;
    if (netOnline && !slotSent) { if (slotMissed < 65535) slotMissed++; }  // 보낼 기회를 못 썼다
    slotSent = false;
  }
  const uint32_t slotUsed = (uint32_t)(now - slotStart);
  // **우리 차례는 0~600ms 뿐이다.** 그 뒤는 수신 전용이라 한 바이트도 쓰지 않는다.
  //
  // 🔴 2026-08-17 — **"창 안에서 시작한다"로는 부족하다. 창 안에서 *끝나야* 한다.**
  //   (socket 이 flush 위치를 물어보다가 드러났다 — 내 구현의 실제 구멍이었다.)
  //   창 끝자락(예: 590ms)에 시작하면 배치가 흐르는 동안 **수신 창을 침범한다:**
  //     최악 배치 `BATCH_CAP`(160B) + `AT+CIPSEND=..` 약 18B = 178B @9600bps ≈ **185ms**
  //     → 590 + 185 = 775ms. **175ms 를 남의 창에서 쓴다.**
  //   그러면 그 순간 도착하는 하행과 정확히 겹치고, 그것이 우리가 없애려는 바로 그 조합이다.
  //   ⚠ fdtest 에는 이 가드가 있었는데(`canSend`) 실기로 옮기며 빠뜨렸다.
  //     **최소 표본에서 옳았던 것이 본체로 오면서 사라지는 것** — 흔한 자리다.
  static const uint16_t SLOT_TX_RESERVE_MS = 200;   // 최악 배치 실측 상한(185ms)에 여유를 더한 값
  const bool inTxWindow = (slotUsed + SLOT_TX_RESERVE_MS <= TX_WINDOW_MS);
  bool heartbeatDue = inTxWindow && !slotSent;
  // changeAt 은 전송 실패 시 **미래 시각**으로 밀린다. unsigned 뺄셈으로 비교하면
  // 언더플로로 곧장 참이 되어 백오프가 통째로 무력화된다 → 부호 있는 비교로 본다.
  // ⚠ 2026-08-17 — `debounced` 는 **더 이상 전송을 트리거하지 않는다**(슬롯이 주기를 정한다).
  //   `changePending`/`changeAt` 자체는 남긴다 — 실패 백오프와 `sentOcc` 갱신이 아직 쓴다.
  (void)DEBOUNCE_MS;

  // ★ REQ-0116 — 살아있음 불변식. **이유를 묻지 않는다.**
  //   "온라인이라면서 TX_STALL_MS 동안 한 줄도 못 내보냈다"면 그 자체로 링크 이상이다.
  //   `busy`·`ERROR`·침묵·**아직 이름 없는 무엇**이든 전부 여기에 걸린다.
  //   ⚠ 아래 조기 반환(`return`)보다 **앞**에 있어야 한다. 뒤에 두면 하트비트가 뜨지 않는
  //     순간에는 검사가 건너뛰어져, 정작 아무것도 못 보내는 상황에서 발동하지 못한다.
  // ⚠ `changeAt`(위)과 **같은 함정이고 같은 관용구로 막는다.** 2026-08-17 실기에서 터졌다:
  //   `now` 는 loop() 맨 위에서 한 번만 뜨는데, 그 뒤 `espRead()`/`drainPending()` 이
  //   `lastTxOkAt` 을 **더 나중 시각**으로 갱신한다(하행 ACK 송신 약 64ms · CONNECT 처리).
  //   그러면 `now - lastTxOkAt` 이 음수가 되고 unsigned 로 읽으면 `2^32-64`(=49.7일)이 되어
  //   **어떤 임계값도 즉시 넘어 멀쩡한 링크를 끊는다.** 실제 로그: `정지 감지: 4294967232ms`.
  //   → 부호 있는 비교면 미래 시각은 음수라 발동하지 않는다.
  //
  // ⚠⚠ **폭을 먼저 32비트로 맞춘 뒤 부호를 준다.** 위 `changeAt` 처럼 `(long)` 만 쓰면
  //   **실기에서는 맞고 호스트 시험에서는 틀린다** — AVR 은 `long` 이 32비트라 자연히
  //   감싸이지만, 호스트는 64비트라 감싸이지 않아 **정상적인 millis() 되감김이 음수로 읽힌다.**
  //   실제로 `(long)` 만 썼다가 [7]·[8] 이 깨졌다(58/61). `(uint32_t)` 뺄셈으로 감싸고
  //   `(int32_t)` 로 해석하면 **두 환경에서 같은 값**이 나온다.
  if (netOnline && (int32_t)((uint32_t)now - lastTxOkAt) >= (int32_t)TX_STALL_MS) {
#if DEBUG
    // 원인은 **진단으로만** 남긴다 — 원인별 제어 경로를 만들면 다시 "아는 실패만" 막게 된다.
    Serial.print(F("[NET] ★ 정지 감지: "));
    // ⚠ `(uint32_t)` 캐스트는 실기에서는 무의미하지만(unsigned long 이 곧 32비트)
    //   호스트 테스트에서는 필요하다 — 64비트 `unsigned long` 과 32비트 `lastTxOkAt` 을
    //   섞으면 언더플로가 감싸이지 않아 1.8e19 같은 값이 찍힌다. 판정식과 같은 폭으로 맞춘다.
    Serial.print((unsigned long)(uint32_t)(now - lastTxOkAt));
    Serial.print(F("ms 동안 한 줄도 못 나갔다 (busy "));
    Serial.print(stallBusy);
    Serial.print(F(" / 무응답 ")); Serial.print(stallTimeout);
    Serial.print(F(" / 거부 "));   Serial.print(stallReject);
    // ★ 2단계 진단 — 정지의 원인이 **2단계 자신**일 수 있다. 그것을 숨기지 않는다.
    //   `건너뜀` 이 크고 `SENDOK상한` 이 0 이면 ESP 가 계속 전송 중이라는 뜻이고,
    //   `SENDOK상한` 이 크면 **`SEND OK` 를 못 받고 있다**는 뜻이라 원인이 정반대다.
    //
    // ⚠⚠ **누적 창이 다르다. 한 줄에 있다고 같은 기준으로 비교하지 마라.**
    //   위 셋(busy/무응답/거부)은 **이 정지 구간만** 센다 — 바로 아래에서 0 으로 비워진다.
    //   아래 넷은 **부팅 이후 누적**이다 — 어디서도 비우지 않는다.
    //   그래서 `건너뜀 17` 과 `busy 7` 을 나란히 놓고 크기를 비교하면 **틀린다.**
    //   (CLAUDE.md "숫자 둘을 비교하기 전에 — 어디서 시작하는가")
    //   ★ 라벨에 `누적` 을 박아 두는 이유가 이것이다. 지우지 마라.
    Serial.print(F(") · 누적[건너뜀 ")); Serial.print(sendSkips);
    Serial.print(F(" / SENDOK상한 "));  Serial.print(sendOkTimeouts);
    Serial.print(F(" / SENDFAIL "));    Serial.print(sendFails);
    Serial.print(F(" / ESP리셋 "));     Serial.print(espResets);
    Serial.println(F("] → 링크를 다시 세운다"));
#endif
    stallBusy = stallTimeout = stallReject = 0;
    startSocketRecovery();             // netOnline 을 내리고 CIPCLOSE 사다리로 간다
    return;
  }

  // 🔴 **이벤트는 전송을 만들지 않는다.** `debounced`(변화 감지)는 이제 "지금 보내라"가
  //   아니라 "이번 슬롯 화물에 실려 나간다"는 뜻이다 — 어차피 S 프레임이 매 슬롯 나가므로
  //   변화는 다음 슬롯에 자동으로 실린다. **그래서 조건에서 뺀다.**
  //   ⚠ 이것이 설계의 핵심이다: 부하가 바꿀 수 있는 것은 **화물의 크기**뿐이고 전송 횟수가 아니다.
  //     옛 구조에서는 여기서 이벤트가 추가 전송을 만들어 양의 되먹임 고리가 열렸다.
  if (!netOnline || !heartbeatDue) return;

  uint16_t occSnap = node.occMask, resSnap = node.resMask, tmaskSnap = tmaskNow;
  uint8_t  batchAcks = 0;
  uint16_t batchBytes = 0;
  const uint32_t sendAt = (uint32_t)(millis() - slotStart);   // 슬롯 시작 기준 실제 송신 시각
  bool ok = sendSlotBatch(&batchAcks, &batchBytes);

  // ★ 슬롯당 정확히 1거래 — 성패와 무관하게 이번 슬롯의 기회는 썼다.
  //   ⚠ 실패했다고 같은 슬롯에서 다시 쏘면 **슬롯당 1거래 규칙이 깨지고** 수신 창을 침범한다.
  //     재시도는 다음 슬롯이다. 그것이 이 설계가 폭주를 막는 방식이다.
  slotSent = true;

#if DEBUG
  // ★ monitor 계수용 표지 — **매 슬롯 찍는다. `ack=0` 인 슬롯도 찍는다.**
  //   ⚠ 그래야 `0` 이 "묶을 것이 없었다"이지 "못 셌다"가 아니게 된다(원장 §5.1).
  //   ⚠ 문구에 다른 표지의 문자열(`SEND OK`·`busy`·`+IPD`·`[TX]`)을 넣지 마라 —
  //     2026-08-17 에 monitor 파서가 우리 산문 안의 `SEND OK` 를 세어 `0/7` 을 냈다.
  Serial.print(F("[SLOT] n="));   Serial.print(slotNo);
  Serial.print(F(" tx="));        Serial.print(batchBytes);
  Serial.print(F(" ack="));       Serial.print(batchAcks);
  Serial.print(F(" due="));       Serial.print(sendAt);
  Serial.println(ok ? F(" r=1") : F(" r=0"));
#endif

  lastStatusAt = millis();          // §3.4 어떤 이유로든 S 를 보내면 타이머 리셋 (타이머는 하나)
  if (ok) {
    seqNo++;                        // 나가지 못한 프레임은 번호를 소비하지 않는다
    sentOcc = occSnap;
    sentRes = resSnap;
    sentTmask = tmaskSnap;
    changePending = false;
  } else {
    changeAt = lastStatusAt + SEND_FAIL_BACKOFF_MS - DEBOUNCE_MS;   // 실패 후 재시도 간격 확보
  }
}

