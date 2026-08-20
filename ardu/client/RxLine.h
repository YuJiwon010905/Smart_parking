#pragma once
// ═════════════════════════════════════════════════════════════════════════
// RxLine.h — 수신 줄 조립(§6.2 1단계)
//   🔒 **링크 성질** — 잠금 후보. 클래스화하지 않는다
//     `espRead` 와 한 몸으로 도는 코드다. **잠금의 목적은 파일이 아니라 이 성질이다.**
// ═════════════════════════════════════════════════════════════════════════
// 🔴🔴 **위치를 옮기지 마라.** `client.ino` 의 **정해진 자리에서** `#include` 된다.
//   자리를 바꾸거나 헤더 순서를 바꾸면 **선언·초기화 순서가 같이 바뀌고**,
//   **그 순간 거동이 바뀌고 산출물이 달라진다.**

// ─────────────────────────────────────────────────────────────────────────
// 수신 — 줄 조립 (§6.2 1단계)
// ─────────────────────────────────────────────────────────────────────────
static void handleLine(char* s);

#if DEBUG
// 줄을 **보이지 않는 문자까지 보이게** 찍는다. 눈으로 같아 보여도 뒤에 공백이나 다른 바이트가
// 붙어 있으면 정확일치가 깨지므로, 인쇄 불가 문자는 \xHH 로 펴고 길이를 같이 낸다.
//   예) [AT] "CONNECT" (7)      [AT] "CONNECT\x20" (8)   ← 이 둘은 다르다
// (태그는 호출자가 먼저 찍는다 — F() 의 타입이 실기와 호스트 테스트에서 달라 인자로 못 넘긴다)
static void dbgLine(const char* s, uint8_t n) {
  Serial.print(F("\""));
  for (uint8_t i = 0; i < n; i++) {
    char c = s[i];
    if (c >= 32 && c <= 126) {
      Serial.print(c);
    } else {
      Serial.print(F("\\x"));
      Serial.print(HEXD[((uint8_t)c) >> 4]);
      Serial.print(HEXD[((uint8_t)c) & 0x0F]);
    }
  }
  Serial.print(F("\" ("));
  Serial.print(n);
  Serial.println(')');
}
#endif

static void feedRxChar(char c) {
#if DEBUG
  dbgRxBytes++;
#endif
  if (c == '\n') {
    if (rxOverflow) {
#if DEBUG
      // 넘쳐서 버린 줄도 **버렸다는 사실이 보여야 한다.** 조용히 사라지면
      // "아무것도 안 왔다"와 구분이 안 된다.
      Serial.print(F("[DROP-OVF] 줄이 ")); Serial.print(RX_CAP);
      Serial.println(F("바이트를 넘어 버렸다"));
#endif
      rxLen = 0; rxOverflow = false; return;
    }
    if (rxLen == 0) { return; }
    rxLine[rxLen] = '\0';
    uint8_t n = rxLen;
    rxLen = 0;                       // ★ 파싱 전에 먼저 비운다 — 아래에서 다시 채워질 수 있다
    if (inSend) {
      // ★ REQ-0174 — 줄과 **그 줄의 도착 오프셋**을 같이 미룬다. 여기서 오프셋을 안 들고 가면
      //   나중에 처리 시각으로 계산돼 위험 구간이 통째로 보이지 않는다(위 `rxLineOff` 주석).
      if (!pendReady) {
        memcpy(pendLine, rxLine, (size_t)n + 1); pendLineOff = rxLineOff; pendReady = true;
        // ★ **`penddrop` 의 분모다.** 이것이 0 이면 `penddrop=0` 은 "안 넘쳤다"가 아니라
        //   "셀 일이 없었다"이고, 그 둘은 완전히 다른 결론으로 이어진다.
        if (pendFills < 65535) pendFills++;
        if (curTxLen >= RXBUF_THRESHOLD && pendFillsBig < 65535) pendFillsBig++;
      }
      else {
        // 🔴 2026-08-18 (monitor 요청) — **버린 줄을 센다.**
        //   monitor 는 (가)"우리가 페이로드를 먼저 보냈다" 와 (나)"pendLine 이 첫 줄을 미뤄
        //   순서가 뒤집혔다" 를 **시리얼로 구분할 수 없다** — 버린 줄은 출력이 안 되기 때문이다.
        //   **이 계수기 하나가 그것을 가른다: (가)면 0, (나)면 T2 마다 오른다.**
        //
        // ⚠ 이 칸은 **바이트 흐름 매칭(REQ-0218 ②)과 독립이다.** 줄 조립 경로는 그대로이므로
        //   **매칭이 `SEND OK` 를 구해도 이 수는 오른다.**
        //   🔑 그래서 **고치면서 동시에 원인을 확정할 수 있다** — 읽는 법:
        //     `okstream > 0` **그리고** `penddrop > 0`  →  **(나) 확정**
        //        (버려질 뻔한 `SEND OK` 를 바이트 매칭이 구했다는 뜻)
        //     `okstream == 0`                          →  매칭이 할 일이 없었다 → **(가) 쪽**
        //
        // ⚠ 옛 주석 *"서버가 재전송하므로 잃지 않는다"* 는 **데이터 줄에만 참이다.**
        //   `SEND OK` 는 ESP 가 한 번만 보내는 제어 응답이라 **재전송이 없다.**
        // 🔴 2026-08-18 정정 — **`okstream` 은 "구조"를 못 센다.**
        //   바이트 매처는 7번째 글자에서 발화한다 — **줄이 완성되기 전이다.**
        //   즉 줄 경로와의 경주에서 **항상 이긴다.** 그래서 `okstream` 은
        //   "줄 경로가 놓쳤을 것"이 아니라 **탐지된 SEND OK 총수**다.
        //   ⚠ 그 결과 아래 판정표의 `okstream > 0 → (나)` 는 **뭐든 성공하면 참**이라 쓸 수 없다.
        //
        // ★ **진짜 분자는 이것이다: 버려진 줄 안에 `SEND OK` 가 있었나.**
        //   여기서만 "줄 경로였으면 영영 잃었을 SEND OK" 를 셀 수 있다 —
        //   `rxLine` 에 완성된 줄이 아직 그대로 있다.
        if (strstr(rxLine, "SEND OK") != NULL) {
          if (okLostByLine < 65535) okLostByLine++;
          if (curTxLen >= RXBUF_THRESHOLD && okLostBig < 65535) okLostBig++;
        }
        if (pendDrops < 65535) pendDrops++;
        if (curTxLen >= RXBUF_THRESHOLD && pendDropsBig < 65535) pendDropsBig++;
      }
    } else {
      memcpy(workLine, rxLine, (size_t)n + 1);
      workLineOff = rxLineOff;
      handleLine(workLine);
    }
    return;
  }
  // 🔴 2026-08-18 (REQ-0218 ②) — **`SEND OK` 를 줄이 아니라 바이트 흐름에서 찾는다.**
  //
  // 줄 단위로 찾으면 세 곳에서 놓친다:
  //   ① `inSend` 중 완성된 **두 번째 줄부터는 버려진다**(`pendLine` 은 깊이 1)
  //      ⚠ 그 자리의 주석 *"서버가 재전송하므로 잃지 않는다"* 는 **`SEND OK` 에는 안 통한다** —
  //        **ESP 가 한 번만 보내는 제어 응답**이라 버려지면 영영 안 온다
  //   ② 앞에 다른 바이트가 붙어 한 줄이 되면 `strncmp` **접두 비교**가 실패한다
  //   ③ 줄이 `RX_CAP` 을 넘으면 통째로 버려진다
  // **셋 다 "게이트가 안 풀림 → T2 8초 → 링크 재수립"으로 끝난다.**
  //
  // ★ 슬라이딩 매칭은 **상태 1바이트**면 된다. 줄 조립과 무관하므로 위 셋을 전부 지난다.
  // ⚠ `awaitingSendOk` 일 때만 찾는다 — **기다리지 않을 때는 찾을 이유가 없고**,
  //   그만큼 하행 페이로드에 우연히 `SEND OK` 가 섞여 생기는 오탐 창이 좁아진다.
  if (awaitingSendOk) {
    static const char SENDOK[] = "SEND OK";
    if (c == SENDOK[sendOkMatch]) {
      if (++sendOkMatch == 7) {                    // 찾았다
        sendOkMatch = 0;
        awaitingSendOk = false; sendOkT1Passed = false;
        if (sendOkByStream < 65535) sendOkByStream++;
        if (curTxLen >= RXBUF_THRESHOLD && okStreamBig < 65535) okStreamBig++;
      }
    } else {
      sendOkMatch = (c == SENDOK[0]) ? 1 : 0;      // 겹침 재시작(`SSEND OK` 같은 경우)
    }
  } else {
    sendOkMatch = 0;
  }

  if (c == '\r') return;                                   // AT 응답의 CR. 명세는 CR 을 보내지 않는다
  if (rxLen >= RX_CAP - 1) { rxOverflow = true; return; }  // 넘치는 줄은 통째로 버린다
  // ★ REQ-0174 — **줄의 첫 바이트**에서 슬롯 오프셋을 확정한다. 이것이 진짜 도착 시각이다.
  if (rxLen == 0) rxLineOff = (uint16_t)((uint32_t)millis() - slotStart);
  rxLine[rxLen++] = c;
}

static void espRead(void) {
  while (wifi.available()) feedRxChar((char)wifi.read());
  // ─────────────────────────────────────────────────────────────────────────
  // 🔴 2026-08-17 (REQ-0167) — **SoftwareSerial 링버퍼가 넘쳤는가.** 계측기가 이미 있었다.
  //   `SoftwareSerial::overflow()` 는 라이브러리가 들고 있는 플래그인데
  //   **이 스케치에서 호출이 0건**이었다. RAM 0바이트로 답이 나오는 자리다.
  //
  // ⚠ **읽으면 플래그가 지워진다**(`if (ret) _buffer_overflow = false`, 라이브러리 확인).
  //   그래서 이 수는 **사건 수가 아니라 "내가 확인한 지점들 사이에 한 번이라도 넘쳤나"의 수**다.
  //   → **하한이다.** `0 이 아니다`는 강한 신호지만 `0` 은 "그 지점들 사이엔 안 넘쳤다"까지만 말한다.
  //
  // ★ 위치를 여기로 고른 이유: **읽기 루프 직후**라 "우리가 다 빼낸 뒤에도 넘쳐 있었나"를 본다.
  //   그리고 `espRead` 는 `loop()`·`waitForPrompt()` 양쪽에서 자주 불려 확인 간격이 짧다 —
  //   플래그가 지워지는 계측기라 **자주 볼수록 뭉침이 줄어든다.**
  //
  // ⚠⚠ **이름을 `rxOverflow` 와 헷갈리지 마라 — 다른 계층이다.**
  //   `rxOverflow`(L402) = 우리 **줄 조립 버퍼**(`RX_CAP`=96) 가 넘쳤다 → 그 줄을 버린다
  //   `ssOverflows`(이것) = **SoftwareSerial 링버퍼**(64B) 가 넘쳤다 → 바이트가 사라졌다
  //   전자는 "너무 긴 줄", 후자는 "우리가 늦게 꺼냈다". **원인도 대응도 다르다.**
  if (wifi.overflow()) { if (ssOverflows < 65535) ssOverflows++; }
}

// ═════════════════════════════════════════════════════════════════════════
// 복구 사다리 — 조치부 (상태·상수는 위 "복구 사다리 (REQ-0071)" 블록에 있다)
// ═════════════════════════════════════════════════════════════════════════

// ── 1단 조치: 시리얼 스트림을 씻는다 ──
// 쓰레기(\xFF\xFE\xFC 계열)나 [DROP-OVF] 가 보인 뒤에는 **줄 경계 자체를 믿을 수 없다.**
// 남아 있는 바이트를 파싱하지 않고 버리고 조립 상태를 초기화한다.
// ⚠ 120ms 상한을 둔 이유: 모듈이 끝없이 토하는 상황에서 여기 갇히면 loop() 가 멈춘다(= 행).
//   오프라인 경로에서만 불리므로 이 정도 정지는 하트비트에 영향이 없다.
static void drainSerial(void) {
  unsigned long t0 = millis();
  uint16_t dropped = 0;
  while (millis() - t0 < 120UL) {
    while (wifi.available()) { (void)wifi.read(); if (dropped < 65535) dropped++; }
  }
  rxLen = 0;
  rxOverflow = false;
  pendReady = false;
#if DEBUG
  Serial.print(F("[LADDER] 1단 조치: 시리얼 드레인 "));
  Serial.print(dropped);
  Serial.println(F("바이트 버림 + 줄 조립 초기화"));
#endif
}

// ── 4단 조치: 하드웨어 리셋선을 잡는다/놓는다 ──
// 잡고 있는 동안 ESP 는 사실상 꺼진 것과 같아 3.3V 레일이 가장 잘 회복한다 —
// 그래서 백오프 시간을 **그대로 리셋 유지시간으로 쓴다.** 쉬는 것과 리셋이 한 동작이 된다.
// static 이 아닌 이유: ESP_RST_WIRED=0 이면 호출자가 전부 사라져 -Wunused-function 이 뜬다.
// 전역이면 링커의 --gc-sections 가 최종 이미지에서 빼 주므로 플래시도 먹지 않는다
// (같은 이유로 node.slotOverrideSet() 등도 전역이다 — 위 "수동 오버라이드" 주석 참조).
void espRstAssert(uint16_t holdMs) {
#if ESP_RST_WIRED
  // 🔴 **조치가 실제로 실행된 횟수.** `[LADDER]` 문구는 DEBUG 안이라 DEBUG=0 이면 사라진다.
  //   계수는 밖에 둬서 **"4단이 있다"와 "4단이 돌았다"를 가를 자리**를 만든다(§30 선언 vs 결과).
  if (hwRstAsserts < 65535) hwRstAsserts++;
  hwRstPending = true;                 // 이 조치가 온라인으로 이어지는지 본다
  pinMode(PIN_ESP_RST, OUTPUT);
  digitalWrite(PIN_ESP_RST, LOW);      // ★ 절대 OUTPUT HIGH 로 놓지 않는다(5V 가 3.3V 핀에 실린다)
  espRstHeld = true;
  espRstReleaseAt = millis() + holdMs;
  netOnline = false;
  netHasIp = false;
  cwjapPending = false;
#else
  (void)holdMs;
#endif
}

// 놓기 — 하이임피던스로 되돌린다. 부팅 직후 ESP 는 다른 보율로 쓰레기를 토하므로 버린다.
static void espRstService(unsigned long now) {
  if (!espRstHeld) return;
  if ((long)(now - espRstReleaseAt) < 0) return;
#if ESP_RST_WIRED
  pinMode(PIN_ESP_RST, INPUT);         // 하이임피던스 (위 배선 주석 참조)
#endif
  espRstHeld = false;
  drainSerial();
  netAdvance(NET_CWMODE, 1500);        // 모듈이 부팅할 시간을 주고 처음부터
#if DEBUG
  Serial.println(F("[LADDER] 4단: ESP 리셋 해제 — 부팅을 기다렸다가 CWMODE 부터 다시"));
#endif
}

// ── 단이 정한 조치를 실제로 수행한다 ──
static void applyRung(void) {
  uint16_t back = RUNG_BACKOFF_MS[rung];
  switch (rung) {
    case RUNG_MEASURE:
      // 조치 없음(계측만). FAIL 이어도 IP 가 살아 있을 수 있으므로 CIFSR 로 확인부터 한다
      // — REQ-0064 에서 확인된 동작이라 그대로 둔다. 0단은 "지금까지의 코드" 그 자체다.
      cifsrTries = 0;
      netAdvance(NET_CIFSR, 300);
      break;

    case RUNG_RESYNC:
      drainSerial();
      cifsrTries = 0;
      netAdvance(NET_CIFSR, 300);
      break;

    case RUNG_CWQAP:
#if DEBUG
      Serial.print(F("[LADDER] 2단 조치: "));
      Serial.print(back / 1000U);
      Serial.println(F("초 쉬고 AT+CWQAP 로 결합을 명시적으로 끊은 뒤 다시 붙는다"));
#endif
      netHasIp = false;
      netAdvance(NET_CWQAP, back);
      break;

    case RUNG_SOFTRST:
#if DEBUG
      Serial.print(F("[LADDER] 3단 조치: "));
      Serial.print(back / 1000U);
      Serial.println(F("초 쉬고 AT+RST (모듈 소프트 리셋)"));
#endif
      netHasIp = false;
      netAdvance(NET_RST, back);
      break;

    case RUNG_HWRST:
#if ESP_RST_WIRED
#if DEBUG
      Serial.print(F("[LADDER] 4단 조치: ESP RST 선을 "));
      Serial.print(back / 1000U);
      Serial.println(F("초 동안 잡는다 (그동안 모듈은 꺼진 것과 같다)"));
#endif
      espRstAssert(back);
#else
      // 배선이 없으면 **있는 척하지 않는다.** 없는 조치를 로그에 성공으로 남기면
      // 다음 사람이 "4단까지 해 봤는데 안 되더라"는 틀린 결론을 얻는다.
#if DEBUG
      // ⚠ 문구를 약하게 쓰지 마라. 20분치 로그를 훑는 사람이 바로 위의 `↑↑ 단 상승 → 4단`
      //   만 보고 "하드웨어 리셋까지 해 봤는데 안 되더라"는 **틀린 결론**을 얻으면 안 된다.
      //   그래서 실제로 조치하지 못한 단은 전부 `미실행` 을 달아 둔다 — `grep 미실행` 한 번에 걸린다.
      // 🔴 문구를 고쳤다 (2026-08-19) — 옛 문구는 **"물려라"고 시켰고 그것이 틀린 지시였다.**
      //   실제로 그 문장을 읽고 배선을 전제한 사고가 났다(원장 §36).
      Serial.println(F("[LADDER] 4단 건너뜀: 하드웨어 리셋은 **설계상 배제**다(미배선이 아니다)"));
      Serial.println(F("[LADDER]   전원·EN·RST-LOW 상태면 RST 핀을 흔들어도 동일하고,"));
      Serial.println(F("[LADDER]   유일하게 풀리는 펌웨어 hang 은 송수신 로직 재설계로 제거했다"));
      Serial.print(F("[LADDER]   → "));
      Serial.print(back / 1000U);
      Serial.println(F("초 쉬고 AT+RST 를 되풀이한다 (이것이 정상 경로다)"));
#endif
      netHasIp = false;
      netAdvance(NET_RST, back);
#endif
      break;

    default:  // RUNG_POWER
      // ★ 5단은 **구현하지 않았다.** 전원을 코드로 끊으려면 로우사이드 MOSFET(또는 트랜지스터)
      //   스위치가 필요한데 그 부품이 없다. 없는 하드웨어를 있는 것처럼 코드만 넣으면
      //   로그가 거짓말을 하게 된다. 그래서 **단은 이름과 전이만 남기고 조치는 4단을
      //   최장 백오프로 되풀이하는 것**으로 정직하게 대체한다.
#if DEBUG
      // ⚠ **5단은 4단과 성격이 다르다** — 배제가 아니라 **미구현**이다.
      //   전원·EN 계열 고장에서는 전원 재투입이 **실제로 듣는다**(2026-08-19 실측: 그것만 풀었다).
      Serial.println(F("[LADDER] ⚠5단 미실행: 전원 재투입은 **부품 미보유로 미구현**이다(배제가 아니다)"));
      Serial.println(F("[LADDER]   필요 부품: 로우사이드 MOSFET 스위치(예: 2N7000/AO3400 + 10k)"));
      Serial.print(F("[LADDER]   대신 4단 조치를 "));
      Serial.print(back / 1000U);
      Serial.println(F("초 백오프로 되풀이한다(종착역)"));
#endif
      netHasIp = false;
#if ESP_RST_WIRED
      espRstAssert(back);
#else
      netAdvance(NET_RST, back);
#endif
      break;
  }
}

// ── 실패 사건이 들어오는 유일한 문 ──
// why: 무엇이 실패했는가. 계측 한 줄을 정확히 쓰기 위해서만 쓰인다(단 선택에는 3번만 관여).
//
// ⚠ **espReset() 의 switch 안에서 이 함수를 부를 때 주의할 것** — 새 호출 지점을 추가할 사람에게.
//   그 시점에는 이미 `netSendStep(sent)` 로 명령이 나간 뒤다. 그런데 1단 조치는 `drainSerial()`
//   이라 **방금 보낸 명령의 응답을 그 자리에서 버린다.** 지금 있는 두 호출 지점은 둘 다 안전하다:
//     · NET_CIFSR  — 버려도 되는 응답이다(cifsrTries 를 이미 소진해 쓸모없음이 확정됐다)
//     · NET_CIPCLOSE — LF_AT_JAMMED 는 rung 을 최소 3단으로 올리므로 1단(드레인)에 닿지 않는다
//   세 번째 호출 지점을 넣는다면 이 두 성질 중 하나가 성립하는지 먼저 확인하라.
enum { LF_CWJAP_FAIL = 0, LF_CWJAP_TIMEOUT, LF_AT_JAMMED };

static void ladderFail(uint8_t why) {
  unsigned long now = millis();

  // ★ 이중 계수 방지. 한 번의 실패가 두 경로로 들어오면(예: 응답 FAIL 과 무응답 타임아웃이
  //   겹치면) 모든 단의 한계가 사실상 절반이 되어 1분 만에 4단까지 치솟는다.
  //   "건너뛰지 않는다"(원칙 2)를 지키려면 세는 것부터 정확해야 한다.
  if (ladderEverFailed && (now - lastLadderFailAt) < 1000UL) {
#if DEBUG
    Serial.println(F("[LADDER] 1초 안에 들어온 중복 실패 신호 — 세지 않는다"));
#endif
    return;
  }
  ladderEverFailed = true;
  lastLadderFailAt = now;

#if DEBUG
  // ── 0단: 계측. 이 한 줄이 "12초 결합 소실" 가설을 판에서 직접 검증한다 ──
  Serial.print(F("[LADDER] 실패("));
  switch (why) {
    case LF_CWJAP_TIMEOUT: Serial.print(F("CWJAP 무응답")); break;
    case LF_AT_JAMMED:     Serial.print(F("AT 계층 잠김"));  break;
    default:               Serial.print(F("CWJAP FAIL"));    break;
  }
  Serial.print(F(") CWJAP소요 "));
  Serial.print((now - cwjapSentAt) / 1000UL);
  Serial.print(F("s · 연속 "));
  Serial.print(cwjapFails);
  Serial.print(F("회 · 결합유지 "));
  if (assocAt) { Serial.print((now - assocAt) / 1000UL); Serial.print('s'); }
  else         { Serial.print(F("없음")); }
  Serial.print(F(" · 현재 "));
  Serial.print(rung);
  Serial.print(F("단("));
  Serial.print(rungFails);
  Serial.println(F("회 누적)"));
#endif
  assocAt = 0;

  // ── 단 올리기 ──
  if (why == LF_AT_JAMMED && rung < RUNG_SOFTRST) {
    // AT 명령 자체가 안 먹는 상태다. 결합의 문제가 아니라 모듈이 꼬인 것이므로
    // 씻기(1단)·결합해제(2단)로는 원리적으로 못 낫는다 → 리셋 단으로 직행한다.
    // 이것은 "아래 단을 건너뛰지 않는다"의 위반이 아니라 **다른 사다리에 올라탄 것**이다.
    rung = RUNG_SOFTRST;
    rungFails = 0;
#if DEBUG
    Serial.println(F("[LADDER] ↑ 3단(AT+RST) 직행 — AT 계층이 잠겼으면 아래 단은 의미가 없다"));
#endif
  } else {
    if (rungFails < 254) rungFails++;
    if (rungFails >= RUNG_LIMIT[rung] && rung < RUNG_MAX) {
      rung++;
      rungFails = 0;
#if DEBUG
      Serial.print(F("[LADDER] ↑↑ 단 상승 → "));
      Serial.print(rung);
      Serial.println(F("단"));
#endif
    }
  }

  // 진단 사슬(GMR→CWCOUNTRY→CWLAP)은 REQ-0064 그대로 **딱 한 번만** 끼워 넣는다.
  // 사다리와 경쟁시키지 않는다 — 한 번 지나가면 lapDone 이 서서 다시는 오지 않는다.
  if (cwjapFails >= 2 && !lapDone) { netAdvance(NET_GMR, 500); return; }

  applyRung();
}

