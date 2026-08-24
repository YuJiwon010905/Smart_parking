#pragma once
// ═════════════════════════════════════════════════════════════════════════
// EspLink — **ESP/WiFi 링크 계층** (REQ-0264 · 사용자 지시로 분리)
//   이 파일: AT+CIPSEND 프롬프트 대기 · espWrite — SEND OK 축의 본체
//   원본 `조별과제샘플/ardu/client.ino` 의 **1774~2047 행을 원문 그대로** 옮긴 것이다.
//   ⚠ 그 행 번호는 **분리 이전 판(3822줄) 기준**이다. 지금 `client.ino` 에서 찾지 마라.
// ═════════════════════════════════════════════════════════════════════════
//
// 🔴🔴 **이 파일들이 하드웨어 리셋을 불필요하게 만든 물건이다** (사용자 확정 2026-08-19)
//   사용자: *"경우 중에 **딱 1개 펌웨어 hang 문제만** 해결 가능한데 **hang 을 막기 위해
//   송수신 로직을 재설계하여 적용**하였고 현재는 해당 케이스가 발생하지 않는 구조이다.
//   **그래서 하드웨어 리셋이 불필요해졌다.**"*
//   → **`ESP_RST_WIRED = 0` 의 근거가 여기 있다**(`EspLink_state.h` 의 그 정의 주석 참조).
//   🔴 **그러므로 이 파일을 되돌리면 하드웨어 리셋 없이 버티던 근거가 같이 사라진다.**
//     프롬프트 대기·T2 게이트·재동기가 그 재설계의 몸통이다.
//   ⚠ **이건 사용자의 설계 확정을 인용한 것이지 내 실측이 아니다.**
//
// 🔴 **왜 나눴나** — 창 P 에서 확인된 링크 안정성(`T2 0/451` · `SEND OK` 축 · 링크 사건 0)이
//   **슬롯·프레임 수정에 노출돼 있었다.** 사용자 지시: *"슬롯에 대한 수정이 아닌 수정사항에
//   의해 수정되지 않도록"*. **파일이 곧 잠금이다** — 슬롯 REQ 로 이 파일을 고치지 마라.
//
// 🔴🔴 **위치를 옮기지 마라. 순서 보존이 `hex` 차이 0 의 조건이다.**
//   이 파일은 `client.ino` 의 **원래 그 자리에서** `#include` 된다.
//   전처리 결과 텍스트가 같으면 산출물이 같고, 그래서 이 분리는 **굽기 축이 아니다**(실측 확인).
//   ⚠ 다른 자리로 옮기거나 여러 헤더의 순서를 바꾸면 **그 순간 거동 변경이 된다** —
//     원장 §19(`espInit`: 흩어진 것을 모으면 그것이 곧 거동 변경이다)가 그 실측 선례다.
//
// ⚠ **`arduino/client/EspLink.h`(08-16)와 다른 것이다.** 그쪽은 REQ-0091 의 추출 실험이고
//   **칩에 올라간 적이 없다**(원장 §27.5). 참고만 하고 그것으로 굽지 마라.

// ─────────────────────────────────────────────────────────────────────────
// 송신 — AT+CIPSEND 뒤 '>' 프롬프트를 실제로 기다린다
// ─────────────────────────────────────────────────────────────────────────
// ── 프롬프트 대기 결과는 **셋이 아니라 넷**이다 (REQ-0116) ──
// 예전에는 bool 이었다 — `>` 를 봤는가 아닌가. 그래서 **ESP 가 "지금 바쁘다"고 명시적으로
// 답한 것**과 **아무 답도 없는 것**이 같은 값(false)으로 뭉개졌고, 둘 다 똑같이 실패로 세어
// 3번이면 멀쩡한 소켓을 끊었다. 링크는 문제가 없는데 우리가 끊는 것 — **자해**였다.
// (실측 2026-08-16: `busy s...` 거부가 `send_fail` 로 계상되는 것을 AT 로그로 확인)
static const uint8_t PROMPT_OK      = 0;   // '>' 수신 — ESP 는 **데이터 모드**다
static const uint8_t PROMPT_BUSY    = 1;   // "busy" — 거부됐다. ESP 는 **명령 모드**다
static const uint8_t PROMPT_REJECT  = 2;   // "ERROR" — 거부됐다. ESP 는 **명령 모드**다
static const uint8_t PROMPT_TIMEOUT = 3;   // 무응답 — **데이터 모드일 수 있다**(더미로 마감해야 한다)

static inline char lowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static bool waitForPromptIsReject(char c, uint8_t* mBusy, uint8_t* mErr, uint8_t* out) {
  // ⚠ `pendLine` 을 보지 않고 **바이트 흐름에서 직접** 찾는 이유:
  //   송신 중에는 완성된 줄이 pendLine 에 **첫 줄 하나만** 담기고 나머지는 버려진다
  //   (feedRxChar). 앞선 줄이 자리를 차지하고 있으면 정작 이 CIPSEND 의 `busy` 응답이
  //   버려져 못 본다. 그래서 기존 버퍼 의미를 건드리지 않고 여기서 따로 훑는다.
  // ⚠ 대소문자를 가리지 않는 이유: 실측에서 `ERROR` 와 `Error` 가 **둘 다** 관측됐다.
  static const char W_BUSY[] = "busy";
  static const char W_ERR[]  = "error";
  char lc = lowerAscii(c);

  *mBusy = (lc == W_BUSY[*mBusy]) ? (uint8_t)(*mBusy + 1) : (uint8_t)(lc == W_BUSY[0] ? 1 : 0);
  if (W_BUSY[*mBusy] == '\0') { *out = PROMPT_BUSY; return true; }

  *mErr  = (lc == W_ERR[*mErr])  ? (uint8_t)(*mErr + 1)  : (uint8_t)(lc == W_ERR[0]  ? 1 : 0);
  if (W_ERR[*mErr] == '\0')  { *out = PROMPT_REJECT; return true; }

  return false;
}

static uint8_t waitForPrompt(void) {
  unsigned long t0 = millis();
  uint8_t mBusy = 0, mErr = 0, verdict = PROMPT_TIMEOUT;
  while (millis() - t0 < PROMPT_TIMEOUT_MS) {
    while (wifi.available()) {
      char c = (char)wifi.read();
      if (c == '>') return PROMPT_OK;
      feedRxChar(c);                 // 대기 중 들어온 데이터도 버리지 않는다
      // 거부가 확인되면 **더 기다리지 않는다.** 이 CIPSEND 에 `>` 는 오지 않는다.
      if (waitForPromptIsReject(c, &mBusy, &mErr, &verdict)) return verdict;
    }
  }
  return PROMPT_TIMEOUT;              // 포기는 정상 동작이다 — 다음 하트비트가 곧 온다
}

// line 은 LF 없는 문자열. LF 는 여기서 붙인다(전선 종단은 LF 하나 — §2.1)
// ─────────────────────────────────────────────────────────────────────────
// 🔴 2026-08-17 — 몸통을 `espWrite()` 로 뺐다. **한 거래에 여러 줄**을 담기 위해서다.
//
// 왜 나눴나: 슬롯 구조는 **슬롯당 정확히 1거래**다. 3건을 보내려면 묶는 것 말고 방법이 없다
//   (안 묶으면 3건에 3.6초가 걸린다). **배치는 최적화가 아니라 구조적 필수다.**
//
// ⚠ **한 줄 64B 상한(§2.1)은 그대로다.** 바뀐 것은 "한 CIPSEND 에 줄이 몇 개인가"뿐이고
//   줄 자체의 규약은 안 건드린다 — 서버 파서는 LF 로 가르므로 그대로 동작한다.
//
// ⚠⚠ **길이를 먼저 확정하고 그 길이만큼 정확히 쓴다.** `AT+CIPSEND=N` 을 선언한 뒤 N 과 다르게
//   쓰면 스트림이 어긋나고, 그것이 원장 §1.4 가 설명하는 (a)/(b) 갈래를 우리 손으로 만드는 것이다.
//   → 그래서 배치는 **호출 전에 버퍼에 완성**되어 있어야 한다. `waitForPrompt()` 안에서
//     `espRead()` 가 돌아 **캐시가 바뀔 수 있으므로** 길이를 나중에 다시 계산하면 안 된다.
static bool espWrite(const char* line, uint16_t len) {
  // ★ `ramProbe()` 는 **조기 반환보다 앞**에 있어야 한다. 뒤에 두면 오프라인 동안 한 번도 안 불려
  //   `[RAM]` 이 초기값 65535 를 찍고, 소크 로그를 나중에 읽는 사람이 계측 고장으로 오해한다.
  //   (실제로 그렇게 찍혔다 — REQ-0064 관측 중 발견.)
  ramProbe();
  if (!netOnline) return false;
  if (len == 0 || len > BATCH_CAP) return false;

  // ── 2단계: 앞 전송이 아직 안 끝났으면 **이번 주기는 보내지 않는다** ────────
  // ⚠ 이것은 **실패가 아니다.** ESP 가 아직 보내는 중이라 우리가 스스로 양보한 것이다.
  //   그래서 `noteSendResult()` 를 부르지 않는다 — 부르면 정상 동작을 자해로 세게 된다.
  //   (이 갈래는 `sendLine` 의 나머지를 통째로 건너뛰므로 카운터에 닿지 않는다.)
  // ⚠ 그럼에도 **막히면 반드시 빠져나온다**: 여기서 계속 건너뛰면 `lastTxOkAt` 이 갱신되지
  //   않아 살아있음 불변식(10초)이 발동한다. 아래 상한(3초)은 그보다 먼저 푸는 1차 방어다.
  if (awaitingSendOk) {
    const uint32_t waited = (uint32_t)(millis() - sendOkWaitFrom);

    // ── 1단 (0 ~ T1) · 2단 (T1 ~ T2) — 둘 다 **조용히 기다린다** ─────────────
    //   차이는 `okto` 를 한 번 세느냐뿐이다. **2단에서 두들기던 것이 이번 수정의 전부다.**
    if (waited < SEND_OK_GIVEUP_MS) {
      if (waited >= SEND_OK_TIMEOUT_MS && !sendOkT1Passed) {
        sendOkT1Passed = true;                       // 라운드당 한 번만 센다
        if (sendOkTimeouts < 65535) sendOkTimeouts++;
#if DEBUG
        Serial.println(F("[TX-WAIT] ★ T1 초과 — 그래도 계속 기다린다(두들기지 않는다)"));
#endif
      }
      if (sendSkips < 65535) sendSkips++;
#if DEBUG
      Serial.println(F("[TX-WAIT] 앞 전송의 SEND OK 를 아직 못 봤다 — 이번 주기는 건너뛴다"));
#endif
      return false;
    }

    // ── 3단 (> T2) — 끝내 안 온다. **두들기지 말고 링크를 다시 세운다** ───────
    //   ⚠ 옛 동작은 여기서 "예전 동작으로 복귀"였고, 그것이 `busy` 폭풍의 시작이었다.
    awaitingSendOk = false; sendOkT1Passed = false;
    if (sendOkGiveups < 65535) sendOkGiveups++;
#if DEBUG
    Serial.println(F("[TX-WAIT] ★★ T2 초과 — SEND OK 가 끝내 안 온다 → 링크를 다시 세운다"));
#endif
    startSocketRecovery();
    return false;
  }

  inSend = true;
  // ★ 2026-08-18 — **지금 보내는 배치 길이를 남긴다.** 수신 계수(`penddrop`/`okstream`)가
  //   `inSend` 중에 일어나므로 이 값으로 **"그때 어느 대역이었나"** 를 귀속할 수 있다.
  //   ⚠ `inSend` 와 **같은 자리에서** 갱신한다. 떨어뜨리면 두 값이 어긋난 순간이 생긴다.
  curTxLen = (uint8_t)(len > 255 ? 255 : len);
  // ★ `SEND_GAP_MS` 는 **없애지 않는다. 하한으로 남긴다.**
  //   `SEND OK` 가 즉시 와도 연속 CIPSEND 를 너무 촘촘히 쏘면 `busy p...`(앞 **명령** 처리 중)가
  //   난다 — 이 값은 원래 그걸 막으려고 있던 것이고 2단계가 겨냥하는 `busy s...`(앞 **전송**
  //   처리 중)와 **다른 사건**이다. 둘을 같은 것으로 보고 지우면 옛 결함이 되살아난다.
  while (millis() - lastSendEndAt < SEND_GAP_MS) espRead();   // 연속 CIPSEND 간격

  wifi.print(F("AT+CIPSEND="));
  wifi.print((unsigned int)(len + 1));                     // +1 = LF 도 전선에 나간다
  wifi.print(F("\r\n"));

  uint8_t pr = waitForPrompt();
  bool ok = (pr == PROMPT_OK);
  if (ok) {
    // 🔴 2026-08-18 (REQ-0218 ①) — **쓰면서 링버퍼를 비운다.**
    //
    // 왜: 예전에는 `len` 바이트를 **통째로** 쓰고 그 사이 아무것도 안 꺼냈다.
    //   그동안 ESP 는 **에코를 되돌려 보낸다.** `SoftwareSerial` 수신 링버퍼는 **64B** 인데
    //   우리가 1B 쓰는 동안 ESP 도 1B 보내므로(같은 보율) **64B 를 쓰면 버퍼가 정확히 찬다.**
    //   → 그 뒤 도착분이 **조용히 사라지고, 그 뒤에 오는 `SEND OK` 도 같이 사라진다.**
    //
    // ★ 실측 지문(REQ-0218): `[AT]` 줄 길이가 **63 에 20건 몰려 있다.** `63 + LF = 64B` —
    //   **링버퍼 크기와 정확히 같다.** `dbgLine()` 은 자르지 않으므로 로그 문제가 아니다.
    //   그리고 선언 크기 **48B 이상에서 `SEND OK` 손실률이 2~4배**로 올랐다(세 창 동일 방향).
    //
    // ⚠ **왜 16바이트마다인가**: 최악에도 링버퍼에 16B 만 쌓인다 → **64B 대비 4배 여유.**
    //   더 촘촘히 하면 오버헤드만 늘고, 더 성기면 여유가 준다.
    for (uint16_t i = 0; i < len; i++) {
      wifi.write((uint8_t)line[i]);
      if ((i & 0x0F) == 0x0F) espRead();     // 16B 마다 꺼낸다
    }
    wifi.write('\n');
    espRead();                                // 마지막 조각도 비운다
    // ★ 2단계: 여기서부터 ESP 가 **실제로 WiFi 로 보내는 중**이다. 그 끝을 알려주는 것이
    //   `SEND OK` 다. 추측(80ms) 대신 그 신호를 기다린다 — 단, 비블로킹으로.
    awaitingSendOk = true;
    // ★ 새 라운드가 시작됐다 — T1 걸쇠를 반드시 푼다.
    //   ⚠ 안 풀면 `okto` 가 **부팅 후 딱 한 번만** 오르고 그 뒤로 영영 0 이다.
    //     자기교정 계기가 조용히 죽는 것이라 §6.5 의 취지가 통째로 사라진다.
    //     (2026-08-17 회귀 시험이 이 결함을 잡았다 — 코드보다 시험이 먼저 맞았다)
    sendOkT1Passed = false;
    sendOkWaitFrom = millis();
  } else if (pr != PROMPT_TIMEOUT) {
    // ─────────────────────────────────────────────────────────────────────
    // ★ REQ-0116 — **거부됐을 때는 더미를 넣지 않는다.**
    //
    // `busy`/`ERROR` 는 ESP 가 **명령을 받지 않았다는 확답**이다. 즉 ESP 는 **명령 모드**에
    // 그대로 있고 **아무 바이트도 기다리지 않는다.** 여기에 더미를 쓰면 그 바이트가
    // **AT 명령으로 해석되어** `ERROR` 를 낳고 스트림을 더 어지럽힌다.
    // (실측 2026-08-16: 더미 뒤에 `Error`/`busy s...` 가 붙고 `SEND OK` 가 오지 않았다.)
    //
    // ❌ **2026-08-16 밤 정정**: 예전에 여기 *"`#####` 가 **에코로 되돌아온 것**이 더미가
    //   소켓이 아니라 AT 해석기로 갔다는 직접 증거"* 라고 적혀 있었다. **틀렸다.**
    //   **정상 전송에서도 페이로드는 그대로 에코된다**(`[AT] "\xA6,12382,…" (41)` 뒤 `SEND OK`).
    //   즉 **에코는 명령 모드/데이터 모드를 가르지 못한다.** 가르는 것은 **`SEND OK` 뿐**이다.
    //   ⚠ 그리고 `SEND OK` 는 **6초 뒤에 올 수도 있다**(20:54:02 → :08 실측).
    //   결론(거부가 확인되면 더미를 넣지 않는다)은 유지된다 — 그건 **에코가 아니라
    //   `busy`/`ERROR` 라는 명시적 거부 응답**에 근거하기 때문이다.
    //
    // 더미는 **아래 PROMPT_TIMEOUT 갈래에서만 옳다.** 거기서는 ESP 가 데이터 모드에 있을
    // 수 있어 약속한 길이를 채워야 하지만, 여기서는 채울 약속 자체가 없다.
    // ─────────────────────────────────────────────────────────────────────
#if DEBUG
    if (pr == PROMPT_BUSY) {
      // ★ 이 줄은 monitor 가 세는 지표다. `busy` 는 고장이 아니라 "나중에"라는 뜻이다.
      Serial.println(F("[TX-BUSY] ESP 가 바쁘다(거부) — 실패로 세지 않는다. 다음 주기에 다시 보낸다"));
    } else {
      Serial.println(F("[TX-REJECT] CIPSEND 거부(ERROR) — 더미를 넣지 않는다"));
    }
#endif
  } else {
    // ─────────────────────────────────────────────────────────────────────
    // ★ REQ-0064 — **프롬프트를 놓쳤을 때 스트림 동기를 회복한다.**
    //
    // 실기에서 4분마다 링크가 끊긴 원인이 정확히 여기였다. 관측된 사슬:
    //   `AT+CIPSEND=38` 송신 → 300ms 안에 `>` 못 받음 → 페이로드를 **안 씀**
    //   → **그런데 ESP 는 여전히 38바이트를 기다린다**
    //   → 다음 루프의 `AT+CIPSEND=38\r\n` 이 **페이로드로 먹힌다**
    //   → 로그에 `"...P1,33AT+CIPSEND=38"` 처럼 **두 줄이 붙어 찍힌다**(그 증거)
    //   → 이후 연쇄 실패 3회 → goOffline → 링크 재수립
    //
    // 고치는 방향은 타임아웃 상향이 **아니다.** 그건 증상만 늦출 뿐 같은 사슬이 남는다.
    // **약속한 길이만큼 버릴 바이트를 채워 진행 중인 CIPSEND 를 끝내고 명령 모드로 되돌린다.**
    //
    // 채움 문자를 `#` 으로 고른 이유: 서버가 **반드시 버리는** 바이트라 우연히 유효 프레임으로
    // 파싱될 가능성이 없다.
    //
    // ✏️ 2026-08-16 정정 — 예전에 여기 *"체크섬 이전에 **타입**에서 걸린다"* 고 적혀 있었는데
    //   **사실과 반대다.** 서버의 검사 순서는 **길이(64B) → 체크섬 → 타입** 이고
    //   (`server.cpp:906/1429/1541`, socket-engineer 확인), `###…` 은 쉼표가 없어
    //   `verify_line()` 의 체크섬 단계에서 즉시 걸린다. **타입 검사까지 가지도 못한다.**
    //   → 결론(반드시 버려진다)은 그대로지만 **근거가 틀렸었다.** 그리고 그 말은
    //     **채움 문자를 무엇으로 고르든 들어가는 칸이 같다**는 뜻이다.
    //   ⚠ 교훈: 이 주석은 **펌웨어가 서버 동작을 확인 없이 가정한 것**이었다.
    //     남의 도메인 동작을 근거로 쓸 때는 그쪽에 확인하고 출처를 적어라.
    //
    // ⚠ ESP 가 실은 명령 모드였다면(CIPSEND 가 안 먹힌 경우) 이 바이트들은 AT 명령으로
    //   해석된다. 예전 주석은 이것을 **"무해하다"** 고 단정했는데 **그것도 낙관이었다** —
    //   실측에서 스트림이 더 엉키는 것이 관측됐다. 그래서 REQ-0116 부터는 **거부가 확인되면
    //   아예 이 갈래로 오지 않는다**(위 PROMPT_BUSY/PROMPT_REJECT 처리).
    //   여기 남은 것은 **정말로 아무 답이 없었던 경우**뿐이고, 그때는 ESP 가 데이터 모드일
    //   수 있으므로 채우는 쪽이 여전히 안전하다.
    // ─────────────────────────────────────────────────────────────────────
    // 🔴 2026-08-18 — **여기에도 espRead 가 필요하다.** 정상 경로(위)에만 넣고
    //   이 갈래를 빠뜨렸었다. 더미도 페이로드와 똑같이 UART 로 나가고, 쓰는 동안
    //   SoftwareSerial 은 cli() 로 수신 인터럽트를 끈다 — **하행 바이트가 64B 링버퍼에서 사라진다.**
    //   `len` 이 90B 면 더미도 90B 다. 크기 조건이 정상 경로와 같다.
    //   ⚠ 넣는 이유는 "축이 같아서"가 아니다. **음성 결과를 해석할 수 있게 하려는 것**이다 —
    //     안 고치고 구웠는데 T2 가 안 줄면 "고장 난 쓰기 경로가 남아서"라는 경쟁 설명이
    //     살아 있어 결과에 아무 의미가 없다. 그걸 치우려면 굽기를 한 번 더 해야 한다.
    //   ✅ 혼입은 사후 검증된다: 이 갈래는 `resync`(promptResyncs)로 따로 세어진다.
    //     창 G 에서 `resync=3 / up=540` 이었다 — 창 H 에서 비슷하면 이 경로는 판정을 못 움직인다.
    for (uint16_t i = 0; i < len; i++) {
      wifi.write('#');
      if ((i & 0x0F) == 0x0F) espRead();            // 16B 마다 꺼낸다 (정상 경로와 같은 주기)
    }
    wifi.write('\n');                                      // 합계 len+1 = 약속한 길이와 정확히 같다
    espRead();
    if (promptResyncs < 65535) promptResyncs++;
#if DEBUG
    Serial.print(F("[TX-RESYNC] 프롬프트 놓침 → 더미 "));
    Serial.print((unsigned int)(len + 1));
    Serial.println(F("바이트로 스트림 복구(서버는 이 줄을 버린다)"));
#endif
  }
  lastSendEndAt = millis();
  inSend = false;

#if DEBUG
  Serial.print(ok ? F("[TX] ") : F("[TX-DROP] "));
  Serial.println(line);
#endif
  // ★ REQ-0049: 이 결과가 유일한 연속 실패 신호다. 여기서만 센다.
  //   goOffline() 이 netOnline 을 내릴 수 있으므로 inSend 를 내린 **뒤**에 부른다.
  //
  // ★ REQ-0116: **`busy` 는 세지 않는다.** 연속 실패 카운터의 뜻은 "링크가 죽었을 것이다"인데,
  //   `busy` 는 ESP 가 **살아서 응답한** 것이라 링크 상태에 대한 증거가 아니다. 그걸 세면
  //   ESP 가 바쁜 몇 초 동안 3회가 채워져 **멀쩡한 소켓을 우리가 끊는다**(= 자해).
  //   ⚠ 성공도 아니므로 **0 으로 되돌리지도 않는다.** 앞선 진짜 실패가 있었다면 그대로 남는다
  //     — 그래서 `noteSendResult(true)` 가 아니라 **호출 자체를 건너뛴다.**
  //   `ERROR`(PROMPT_REJECT)는 그대로 센다. 명령이 실제로 거부된 것이고 링크 이상일 수 있다.
  if (pr != PROMPT_BUSY) noteSendResult(ok);

  // ★ REQ-0116 살아있음 불변식의 기준점 갱신.
  //   `lastTxOkAt` 은 **실제로 나간** 프레임에서만 갱신된다 — `busy` 를 안 세는 것과 달리
  //   여기서는 `busy` 도 "못 나갔다"로 취급한다. 세는 것과 나간 것은 다른 문제다.
  if (ok) {
    lastTxOkAt = millis();
    stallBusy = stallTimeout = stallReject = 0;
  } else if (pr == PROMPT_BUSY)   { if (stallBusy    < 255) stallBusy++;    }
    else if (pr == PROMPT_REJECT) { if (stallReject  < 255) stallReject++;  }
    else                          { if (stallTimeout < 255) stallTimeout++; }
  return ok;
}

// ★ 한 줄 래퍼 — **기존 규약을 그대로 유지한다**(§2.1 한 줄 최대 64바이트, LF 포함).
//   호출부(`sendAck` 등)는 이 함수를 그대로 쓴다. 배치는 `espWrite` 를 직접 쓴다.
static bool sendLine(const char* line) {
  const size_t n = strlen(line);
  if (n == 0 || n > 63) return false;
  return espWrite(line, (uint16_t)n);
}
