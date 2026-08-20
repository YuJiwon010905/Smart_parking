#pragma once
// ═════════════════════════════════════════════════════════════════════════
// EspLink_at.h — AT 응답 어휘 해석 · handleLine — **링크 계층**   (REQ-0273 리팩터링 · 2026-08-19)
//   원본 `조별과제샘플/ardu/client.ino` 의 **2130~2715 행을 원문 그대로** 옮긴 것이다.
//   ⚠ 행 번호는 **이 분리 이전 판(2980줄) 기준**이다.
// ═════════════════════════════════════════════════════════════════════════
// 🔴🔴 **위치를 옮기지 마라. 순서 보존이 `hex` 차이 0 의 조건이다.**
//   이 파일은 `client.ino` 의 **원래 그 자리에서** `#include` 된다. 다른 자리로 옮기거나
//   헤더들의 순서를 바꾸면 **그 순간 거동 변경이 되고 굽기 축이 하나 는다**(원장 §19·§31).

// ─────────────────────────────────────────────────────────────────────────
// 한 줄 처리 — §6.2 의 4단계
// ─────────────────────────────────────────────────────────────────────────
// 대소문자 무시 n바이트 비교
static bool eqNoCase(const char* a, const char* b, uint8_t n) {
  for (uint8_t i = 0; i < n; i++) {
    char x = a[i], y = b[i];
    if (x >= 'a' && x <= 'z') x = (char)(x - 32);
    if (y >= 'a' && y <= 'z') y = (char)(y - 32);
    if (x != y) return false;
  }
  return true;
}

// TCP 접속 성공 줄인가? (REQ-0042 2순위 — 판정을 넓히되 옛 버그는 되살리지 않는다)
//
// ⚠ **부분문자열 검색으로 되돌리지 마라.** 옛 원본은 원시 스트림에서 "CONNECT" 를 찾아서
//    `WIFI CONNECTED` 안의 것에 걸렸다. 그건 와이파이 연결일 뿐 TCP 접속이 아니다 —
//    그 오인 때문에 "붙지도 않았는데 online" 이 되고 프레임이 허공에 나간다.
//    여기서는 (1) 호출 전에 WIFI 로 시작하는 줄을 걸러내고 (2) **길이를 정확히 맞춰** 판정한다.
//    길이 검사가 핵심이다 — "CONNECTED"(9)는 "CONNECT"(7)와 길이가 달라 통과하지 못한다.
//
// 받아들이는 변형과 근거:
//   "CONNECT"      표준 AT 펌웨어(ESP8266 AT ≥ 0.50)의 CIPSTART 성공 응답
//   "Linked"       구형 AT 펌웨어(0.2x 대)가 같은 자리에서 내는 응답 — 대소문자 무시
//   "<n>,CONNECT"  CIPMUX=1 형식의 링크ID 접두. 우리는 CIPMUX=0 이지만 모듈 상태가
//                  남아 있을 수 있어 벗겨 준다
//   앞뒤 공백/CR   모듈에 따라 붙어 온다
// ─────────────────────────────────────────────────────────────────────────
// ⚠⚠ 구형 ai-thinker 펌웨어(AT v0.018 / SDK 0.9.2)의 **어휘가 다르다.** 실측으로 확인했다.
//     문서(ESP-AT v2.x)만 보고 문자열을 정하면 이 보드에서는 그 분기가 **영원히 안 걸린다.**
//
//   문서/신형          이 보드의 실제 출력        확인된 로그
//   ─────────────      ────────────────────      ─────────────────────────
//   ALREADY CONNECT →  **ALREAY CONNECT**        [AT] "ALREAY CONNECT" (14)   ← D 가 빠진 펌웨어 오타
//   CLOSED          →  **Unlink**                [AT] "Unlink" (6)
//   CONNECT         →  Linked                    [AT] "Linked" (6)            (이미 처리돼 있었다)
//
// 앞의 둘을 못 잡으면 REQ-0051 의 낡은 소켓 사다리와 REQ-0036 의 캐시 비우기가 **둘 다 죽는다.**
// 실제로 죽어 있었다 — 복구가 되긴 했지만 의도한 즉시 분기가 아니라 CIPSTART 대기 상한(5초)이
// 만료되는 경로로 우회해서였다. 즉 매 복구마다 불필요한 5초를 태우고 있었다.
// ─────────────────────────────────────────────────────────────────────────
static bool isAlreadyConnectLine(const char* s) {
  // `ALREADY` 와 `ALREAY` 를 함께 받는다. 접두를 요구하므로 평범한 `CONNECT` 는 여기 안 걸린다.
  return strstr(s, "ALREA") != NULL && strstr(s, "CONNECT") != NULL;
}

static bool isClosedLine(const char* s) {
  return strstr(s, "CLOSED") != NULL || strstr(s, "Unlink") != NULL || strstr(s, "UNLINK") != NULL;
}

static bool isConnectLine(const char* s) {
  while (*s == ' ' || *s == '\t') s++;                       // 앞 공백
  if (s[0] >= '0' && s[0] <= '4' && s[1] == ',') s += 2;     // "<링크ID>," 접두
  uint8_t n = (uint8_t)strlen(s);
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) n--;  // 뒤 공백

  if (n == 7 && eqNoCase(s, "CONNECT", 7)) return true;
  if (n == 6 && eqNoCase(s, "LINKED",  6)) return true;
  return false;
}

// 프레임 후보 한 줄을 검사해서 처리한다 (§6.2 3·4단계). `+IPD` 경로와 평문 경로가 **공유**한다.
static void handleFrameLine(char* cand) {
  uint8_t len = (uint8_t)strlen(cand);
  if (len == 0 || len > 63) return;                    // §2.1 한 줄 최대 64바이트(LF 포함)

  // 타입 문자. 모르는 타입은 조용히 버린다(§2.1-7)
  //   🔴 2026-08-18 `Q` 추가 — 서버가 "등록을 다시 보내라"고 묻는 프레임(명세 §5 ③).
  //     ⚠ **없으면 등록이 한 번 실패했을 때 복구 경로가 아예 없다.** 서버는 미완료로 두고
  //       장치는 자기가 실패한 줄 모른다(등록에 ACK 이 없다) → 그 노드는 영영 미등록이다.
  if (cand[0] != 'R' && cand[0] != 'C' && cand[0] != 'T' && cand[0] != 'M' && cand[0] != 'Q' && cand[0] != 'G') return;

  // 체크섬. AT 잡음이 우연히 R 로 시작해도 여기서 걸린다
  if (!checksumOk(cand, len)) {
    // ★ REQ-0174 ② — **계수는 `DEBUG` 밖.** 원문 출력만 안이다(원장 §8.4-2 의 경계:
    //   정수 계수는 밖 · 장문 진단은 안). 이 칸이 없으면 창 B 에서 손으로 못 찾고,
    //   그러면 "무손실"이 서버 계수만으로 선언된다.
    if (cksumNg < 65535) cksumNg++;
#if DEBUG
    Serial.print(F("[CKSUM NG] ")); Serial.println(cand);
#endif
    return;
  }

  // ── 🔴 `G` — 조작 명령 `G,<rid>,<idx>,<op>,<ck>` (socket 확정 2026-08-19) ──
  //   `idx` = 등록 순서(= 비트열 자리) · `op` = 1(열다) / 0(닫다)
  //   🔑 **`name` 이 아니라 `idx` 인 이유**: 이미 합의된 순서이고(등록 순서가 비트 자리를 정한다),
  //     삼중 검산으로 지켜지며, 우리 쪽은 **배열 인덱스라 조회가 없다.**
  //   ⚠ **조용히 버리지 않는다** — 거절도 ACK 을 보낸다. 안 그러면 서버가 `ack_timeout` 을 내고
  //     **"안 갔다"와 "갔는데 거절됐다"가 같은 칸에 섞인다.**
  if (cand[0] == 'G') {
    char*   gf[6];
    uint8_t gn = splitFields(cand, gf, 6);
    uint16_t grid = 0;
    if (gn == 0xFF || gn < 4 || !parseU16(gf[1], &grid)) return;   // rid 를 모르면 ACK 를 못 만든다
    uint16_t gidx = 0;
    uint32_t garg = 0;   // 🔴 **인자는 32비트** — LCD 7자리 같은 값이 온다(§2026-08-19)
    const bool okIdx = parseU16(gf[2], &gidx) && parseU32(gf[3], &garg);

    // §4.2 멱등 — 이미 본 rid 면 같은 ACK 를 다시 보낸다
    int8_t ghit = ackQ.find(grid);
    if (ghit >= 0) {
      sendAck(ackQ.at(ghit).rid, ackQ.at(ghit).slot[0], ackQ.at(ghit).slot[1], ackQ.at(ghit).result);
      return;
    }

    // 🔴 **하드코딩 분기를 없앴다 (2026-08-19).** 어떤 모듈이 명령을 받을 수 있는지는
    //   `setup()` 의 `router.on(...)` 등록이 정한다 — **여기는 어느 모듈인지 모른다.**
    //   그래서 **기여자가 자기 액추에이터를 붙일 수 있다.** 옛 코드는 가상 차단봉만 알았다.
    //   ⚠ 등록이 없으면 `result=3`(수행 불가) — **조용히 성공으로 답하지 않는다.**
    uint8_t gres = 3;                       // 기본 = 수행할 수 없다(§2.4 result=3)
    if (okIdx && gidx <= 0xFF && router.dispatch((uint8_t)gidx, garg)) gres = 0;
#if DEBUG
    // 🔴 **거절의 *까닭* 을 가른다.** 셋은 완전히 다른 고장이고 고치는 곳도 다르다:
    //     해독실패 → 프레임/서버 쪽    · 등록없음 → `setup()` 의 `router.on`
    //     콜백거절 → 기여자의 핸들러
    //   ⚠ 이걸 안 가르면 셋이 다 `[G] 거절` 한 줄로 보이고, **기여자는 어디를 볼지 모른다.**
    if (gres != 0) {
      Serial.print(F("[G] 거절 idx=")); Serial.print(gidx);
      if (!okIdx || gidx > 0xFF)                    Serial.println(F(" — 인자 해독 실패"));
      else if (!router.has((uint8_t)gidx))          Serial.println(F(" — 등록 없음"));
      else { Serial.print(F(" — 콜백이 거절 arg=")); Serial.println(garg); }
    }
    else           { Serial.print(F("[G] idx=")); Serial.print(gidx);
                     Serial.print(F(" arg=")); Serial.println(garg); }
#endif
    // ⚠ 캐시에 넣어야 재전송(같은 rid)이 같은 답을 받는다 — 멱등이 여기서도 성립해야 한다
    commitAck(grid, 'G', (char)('0' + (gidx % 10)), gres);
    return;
  }

  // ── 🔴 `Q` — 서버가 등록 재전송을 요구한다 (명세 §5 ③) ────────────────────
  //   ⚠ **`processCommand` 를 태우지 않는다.** 그쪽은 `rid` 를 요구하는데 `Q` 에는 없다
  //     (재전송 요구이지 명령이 아니다). 태우면 `rid` 파싱에서 조용히 버려진다.
  //   ⚠ **ACK 를 보내지 않는다.** 응답은 **다음 자기 송신 창의 `D`** 그 자체다.
  //   🔑 무한 반복 방지는 **서버 몫**이다(슬롯당 1회 · 3회 무응답이면 포기하고
  //     `node_unregistered` 로 굳힌다). 장치가 자체 상한을 두면 **교착**이 된다 —
  //     장치는 안 보내고 서버는 계속 묻는다. 여기서는 단순히 응답하고 `regSends` 로 진단만 남긴다.
  if (cand[0] == 'Q') {
    requestRegistrationNow();      // ★ 이미 승격됐다 — S 를 다시 보낼 이유가 없다
#if DEBUG
    Serial.println(F("[REG] Q 수신 — 다음 송신 창에 등록을 다시 보낸다"));
#endif
    return;
  }

  processCommand(cand);
}

static void handleLine(char* s) {
#if DEBUG
  // ★ REQ-0042 1순위: **받은 줄을 전부 찍는다.** 예전에는 WIFI 로 시작하는 줄만 찍어서
  //   모듈이 실제로 무엇을 응답하는지 아무도 볼 수 없었다.
  dbgLineCnt++;
  Serial.print(F("[AT] "));
  dbgLine(s, (uint8_t)strlen(s));
#endif

  // ═══════════════════════════════════════════════════════════════════════
  // ★ 데이터 줄이면 **AT 해석을 일절 거치지 않고** 곧장 프레임 처리로 간다.
  //
  // 왜 맨 앞인가 — 이건 결함 하나가 아니라 **결함 유형**을 닫는 자리다.
  // 아래 AT 해석부는 `Unlink`·`CLOSED`·`busy`·`no ip`·`ERROR`·`SEND FAIL` 같은 문자열을
  // **부분일치**로 찾는다. 서버가 보낸 프레임 안에 그 단어가 섞이면(특히 userid 같은 자유 필드)
  // 프레임이 AT 응답으로 오인돼 **통째로 삼켜지고**, 심하면 `netOnline` 까지 뒤집힌다.
  // 소문자인 `busy`·`no ip` 는 사람이 쓰는 문자열에 섞일 확률이 특히 높다.
  //
  // 키워드마다 가드를 하나씩 붙이는 방식은 **다음에 키워드를 추가할 때 또 뚫린다.**
  // 그래서 개별 방어가 아니라 순서로 막는다: 데이터 줄은 애초에 AT 해석부에 도달하지 않는다.
  //
  // 트레이드오프(작지만 기록해 둔다): `Unlink+IPD,...` 처럼 AT 응답과 데이터가 한 줄에 붙어 오면
  // 이제 AT 쪽을 놓친다. 그러나 링크 이벤트를 놓치는 것은 전송 실패 감지(REQ-0049)가 받아 주는 반면,
  // 데이터를 링크 이벤트로 오인하는 것은 **상태를 오염시킨다.** 놓치는 쪽이 안전하다.
  // ═══════════════════════════════════════════════════════════════════════
  {
    char* ipd = strstr(s, "+IPD,");
    if (ipd) {
      // ── 🔴 슬롯 위반 표지 (2026-08-17 · REQ-0164 ②) ───────────────────────
      //   설계상 하행은 **수신 창(600~1200ms)** 에만 와야 한다. 우리 송신 창에 오면
      //   `SoftwareSerial` 반이중 때문에 **우리가 귀를 닫고 있는 동안** 도착한 것이다.
      //
      //   ⚠⚠ **이 줄이 없으면 설계 위반이 조용하다.** 슬롯이 성공하면 지표는 `0` 이 되는데,
      //     `0` 은 "설계가 먹었다"와 "못 셌다"를 구별하지 못한다(원장 §5.1·§8.2-5).
      //     **이 표지가 그 둘을 가르는 유일한 줄이다.**
      //
      //   🔴 **2026-08-17 (REQ-0174) — 여기서 `millis()` 를 뜨면 안 된다.**
      //     송신 중 도착한 줄은 `pendLine` 에 갇혔다가 **송신이 끝난 뒤** 이 함수에 온다.
      //     그러면 **위험 구간에 도착한 것이 바로 그 이유로 늦게 찍혀 판정에서 빠진다** —
      //     재려던 사건을 측정 지점이 밀어내는 것이다.
      //     실측 반증: 서버 재전송이 난 구간에서 `oow` 가 0 이었다(socket, 2차 주입).
      //   ✅ 이제 **줄의 첫 바이트를 받은 순간 확정된 오프셋**을 쓴다(`feedRxChar` 참조).
      const uint32_t sinceSlot = (uint32_t)workLineOff;
      if (sinceSlot < TX_WINDOW_MS) {
        if (slotOow < 65535) slotOow++;
#if DEBUG
        //   ⚠ 문구에 다른 표지 문자열을 넣지 마라(`SEND OK`·`busy`·`[TX]`).
        Serial.print(F("[SLOT-OOW] +"));
        Serial.print(sinceSlot);
        Serial.println(F("ms"));
#endif
      }
      char* colon = strchr(ipd, ':');
      if (!colon) return;                 // 길이 필드가 안 끝났다 — 쓸 수 없는 줄
      handleFrameLine(colon + 1);         // `+IPD,<len>:` 도 `+IPD,<id>,<len>:` 도 첫 `:` 뒤가 본문이다
      return;
    }
  }

  // (a) 접속 상태 키워드. "WIFI CONNECTED" 를 TCP CONNECT 로 오인하지 않는다.
  //     ⚠ 이 제외는 되살리면 안 되는 버그를 막는 자리다 — 지우지 마라.
  //
  //     ★ REQ-0064 ①: **버리기 전에 읽는다.** 예전에는 여기서 곧장 return 이라
  //     `WIFI GOT IP`(성공)와 `WIFI DISCONNECT`(끊김)까지 같이 버렸고, 그래서 코드에
  //     "와이파이가 붙었는가"를 알 수단이 아예 없었다. return 은 그대로 두고 — 지우면
  //     `WIFI CONNECTED` 가 isConnectLine 까지 흘러가 옛 오인 버그가 되살아난다 —
  //     그 앞에서 상태만 걷어 간다.
  //     (⚠ 이 보드의 구형 펌웨어는 WIFI * 줄을 내지 않는다. 신형 펌웨어 대비 경로다.)
  if (strncmp(s, "WIFI", 4) == 0) {
    if (strstr(s, "GOT IP")) {
      netHasIp = true;
      cwjapFails = 0;
      cwjapPending = false;             // 결합이 끝났다 — 무응답 판정 대상에서 뺀다
      assocAt = millis();               // 결합 유지시간 계측 시작 (REQ-0071 0단)
#if DEBUG
      Serial.println(F("[NET] WIFI GOT IP — IP 확보. 9초 대기창을 끝까지 기다리지 않고 바로 다음 단계로"));
#endif
      if (!netOnline) netAdvance(NET_CIFSR, 200);
    } else if (strstr(s, "DISCONNECT")) {
      netHasIp = false;                 // ★ 전제조건이 깨졌다 — CIPSTART 를 막는다
#if DEBUG
      Serial.println(F("[NET] WIFI DISCONNECT — IP 를 잃었다"));
#endif
      // ★ REQ-0071: 여기서 곧장 CWJAP 로 되돌아가던 것이 **같은 자극의 반복**이었다.
      //   결합이 끊긴 것은 그 자체로 실패 사건이므로 사다리가 다음 조치를 정하게 한다.
      //   (이 보드의 구형 펌웨어는 WIFI 줄을 내지 않는다 — 신형 대비 경로다.)
      if (!netOnline) { cwjapPending = false; ladderFail(LF_CWJAP_FAIL); }
    }
    return;                             // ★ 유지 (위 경고 참조)
  }

  // ── (a2) 사다리 응답 처리 (REQ-0064) ─────────────────────────────────────
  // 어떤 명령에 대한 답인지는 netStep 이 아니라 **netLastSent** 로 본다.
  // netStep 은 이미 "답이 없으면 갈 곳"으로 앞서 가 있기 때문이다.
  if (!netOnline && netLastSent < NET_STEP_COUNT) {

    // `busy p...` = **앞 명령이 아직 안 끝났다.** 이번 사고의 핵심 증거다.
    // 전진하면 안 된다 — 전진했기 때문에 CIPMUX 가 통째로 씹혔다.
    if (strstr(s, "busy")) {
#if DEBUG
      Serial.println(F("[NET] busy — ESP 가 앞 명령을 처리 중이다. 전진하지 않는다"));
#endif
      // CWJAP 대기 중이면 재전송도 하지 않는다. 진행 중인 결합을 방해할 뿐이고,
      // 결과(OK/FAIL)는 어차피 곧 온다.
      // ⚠ 되쏘아도 되는 단계만 되쏜다.
      //   · CWJAP  — 진행 중인 결합을 방해할 뿐이다. 결과(OK/FAIL)는 곧 온다
      //   · CWLAP  — 재스캔은 9초를 통째로 태우고 그동안 결합이 불가능하다.
      //              lapDone 이 이미 서 있어 "한 번만 스캔한다"는 규칙도 깨진다
      //   · CIPCLOSE — 사다리 상승 계수(closeAttempts)를 우회해 무한 반복이 된다
      // ⚠⚠ **`NET_RST` 은 여기 있으면 안 된다. 실측으로 자해가 확인됐다.**
      //   `busy` 대응의 뜻은 **"기다렸다 다시 물어본다"** 인데, `AT+RST` 는 물어보는 게 아니라
      //   **"처음부터 다시 시작해라"** 다. 성질이 다른 명령을 같은 목록에 둔 것이 결함이었다.
      //   실측(REQ-0064): 부팅 직후 ESP 가 앞선 CWJAP 를 처리하는 12초 동안
      //   **`AT+RST` 를 1.5초마다 8회** 되쏘고 있었다 — **진행 중인 결합을 매번 죽인 것**이다.
      //   RST 는 되쏘지 않는다. 응답이 없으면 NET_WAIT[NET_RST](2500ms)가 알아서 재시도한다.
      // ★ 2026-08-17 — 이 `busy` 가 CIFSR 에 대한 것이면 **소진 사유가 "IP 없음"이 아니다.**
      //   여기서 표시해 두고, 3회 소진 지점에서 `noteIpLoss()` 를 건너뛴다(§8.2-12).
      if (netLastSent == NET_CIFSR) cifsrRefused = true;
      switch (netLastSent) {
        case NET_CIFSR: case NET_CIPMUX: case NET_CIPSTART:
          netAdvance(netLastSent, 1500);   // 이 셋은 되쏴도 상태를 망가뜨리지 않는 질의/설정이다
          break;
        default:
          break;                        // 기다린다 — 해당 단계의 대기 상한이 알아서 처리한다
      }
      return;
    }

    // CWJAP 의 결과
    if (netLastSent == NET_CWJAP) {
      // 신형 펌웨어의 사유 코드. 이 보드에는 안 오지만 오면 그대로 사람 말로 찍는다.
      if (strncmp(s, "+CWJAP:", 7) == 0) {
#if DEBUG
        Serial.print(F("[NET] ★ CWJAP 실패 사유: "));
        switch (s[7]) {
          case '1': Serial.println(F("접속 시간초과")); break;
          case '2': Serial.println(F("비밀번호가 틀렸다")); break;
          case '3': Serial.println(F("그 SSID 의 AP 를 못 찾았다")); break;
          case '4': Serial.println(F("접속 실패")); break;
          default:  Serial.println(s); break;
        }
#endif
        return;                          // 계수는 뒤따라오는 FAIL 에서 한다
      }
      if (strstr(s, "FAIL") || strstr(s, "ERROR")) {
        if (cwjapFails < 250) cwjapFails++;
        netHasIp = false;
        cwjapPending = false;            // 답이 왔다 — 무응답 판정 대상에서 뺀다
#if DEBUG
        Serial.print(F("[NET] ★ CWJAP 실패 "));
        Serial.print(cwjapFails);
        Serial.println(F("회 (구형 펌웨어는 사유를 주지 않는다)"));
#endif
        // ★ REQ-0071 — 여기가 사다리의 주 입구다.
        //   예전에는 이 자리에서 곧장 CIFSR→CWJAP 로 돌아갔고, 그래서 91회를 되풀이했다.
        //   이제는 **몇 번째 실패인지에 따라 조치가 달라진다.** 진단 사슬 1회 삽입도
        //   ladderFail() 안으로 옮겼다(판정이 한 곳에만 있어야 어긋나지 않는다).
        ladderFail(LF_CWJAP_FAIL);
        return;
      }
      if (strcmp(s, "OK") == 0) {
#if DEBUG
        Serial.println(F("[NET] CWJAP OK — 결합됐다. IP 를 실제로 받았는지 CIFSR 로 확인한다"));
#endif
        cwjapFails = 0;
        cifsrTries = 0;
        cwjapPending = false;
        assocAt = millis();              // 결합 유지시간 계측 시작 (REQ-0071 0단)
        netAdvance(NET_CIFSR, 300);
        return;
      }
    }

    // 2단(CWQAP)의 결과. 구형 펌웨어는 결합이 없으면 `no ap` 를 먼저 내기도 한다 — 셋 다 완료다.
    if (netLastSent == NET_CWQAP &&
        (strcmp(s, "OK") == 0 || strstr(s, "ERROR") || strstr(s, "no ap"))) {
#if DEBUG
      Serial.println(F("[LADDER] 2단: 결합 해제 완료 → 깨끗한 상태에서 CWJAP 재시도"));
#endif
      netAdvance(NET_CWJAP, 500);
      return;
    }

    // CIFSR 의 결과 — **여기가 IP 확보의 단일 판정점이다.**
    if (netLastSent == NET_CIFSR) {
      if (strncmp(s, "AT+", 3) != 0 && hasUsableIp(s)) {   // 명령 에코는 제외
        netHasIp = true;
        cifsrTries = 0;
        ipLossLatched = false;              // ★ IP 를 실제로 되찾았다 — 다음 소실은 새 사건이다
        cifsrRefused  = false;              // ★ 라운드가 끝났다 — 묵은 `busy` 표시가 다음 판정을 가리면 안 된다
        if (!assocAt) assocAt = millis();   // 결합 유지시간 계측 시작 (REQ-0071 0단)
#if DEBUG
        Serial.print(F("[NET] ★ IP 확보: "));
        Serial.println(s);
#endif
        netAdvance(NET_CIPMUX, 200);
        return;
      }
      // ★ REQ-0125 — **ESP 가 상태를 통째로 잃은 지문.** (진단 전용, 제어에 쓰지 않는다)
      //   `hasUsableIp()` 는 `0.0.0.0` 을 IP 로 치지 않으므로 위 갈래에서 이미 떨어진다.
      //   여기서는 **그것을 세기만** 한다 — 흐름은 기존 `cifsrTries` 경로가 그대로 처리한다.
      //   ⚠ 세는 것과 끊는 것을 섞지 않는다. 사다리는 **원인이 아니라 결과(IP 가 있는가)** 를
      //     보기 때문에 이름 모를 고장에도 동작한다(§6.1). 그 성질을 깨지 않으려는 것이다.
      else if (strncmp(s, "AT+", 3) != 0 && strcmp(s, "0.0.0.0") == 0) {
        noteIpLoss();                    // 판별자 ① — 응답이 멀쩡할 때 잡힌다
      }
    }

    // CIPMUX 의 결과. 구형 펌웨어는 이미 그 값이면 `no change` 로 답한다 — 둘 다 성공이다.
    if (netLastSent == NET_CIPMUX && (strcmp(s, "OK") == 0 || strstr(s, "no change"))) {
#if DEBUG
      Serial.println(F("[NET] CIPMUX=0 적용됨 → CIPSTART"));
#endif
      netAdvance(NET_CIPSTART, 200);
      return;
    }

    // CWCOUNTRY 우회 시도의 결과. **ERROR 가 나오는 것이 정상이자 결론이다.**
    if (netLastSent == NET_CWCOUNTRY && (strcmp(s, "OK") == 0 || strstr(s, "ERROR"))) {
#if DEBUG
      if (strcmp(s, "OK") == 0) {
        Serial.println(F("[NET] ★ CWCOUNTRY 가 먹었다 — 규제도메인 1~13. 채널 12 결합이 열렸을 수 있다"));
      } else {
        Serial.println(F("[NET] CWCOUNTRY 미지원(ERROR) — 이 펌웨어로는 채널 12/13 을 코드로 못 연다."));
        Serial.println(F("[NET]   → 공유기 채널을 1/6/11 로 바꾸는 것이 확정 해법이다"));
      }
#endif
      netAdvance(NET_CWLAP, 300);
      return;
    }

    // CWLAP 스캔 종료. 이 판정 한 줄이 "비번이냐 AP 냐" 를 가른다.
    if (netLastSent == NET_CWLAP) {
      if (strncmp(s, "+CWLAP:", 7) == 0) {
        if (strstr(s, WIFI_SSID)) lapFound = true;
        return;                          // 목록 자체는 위 [AT] 로그가 이미 다 찍었다
      }
      if (strcmp(s, "OK") == 0 || strstr(s, "ERROR")) {
        lapDone = true;
#if DEBUG
        Serial.println(F("[NET] ── AP 스캔 판정 ──────────────────────────"));
        if (lapFound) {
          Serial.println(F("[NET] 설정한 SSID 가 목록에 **있다** → AP 는 보인다. 결합만 거부된다."));
          // ⚠ 순서 주의: 위 +CWLAP 줄의 **마지막 숫자가 채널**이다. 12 나 13 이면 그것이 1순위다.
          //   ESP8266 은 12/13 을 스캔으로는 보면서 결합만 막히는 실패 양상이 알려져 있다
          //   (기본 규제도메인 US=1~11). "보였으니 붙을 수 있다"는 추론은 성립하지 않는다.
          Serial.println(F("[NET]   1순위: 위 줄의 **마지막 숫자(채널)** 를 봐라. 12/13 이면 공유기를 1/6/11 로 바꿔라"));
          Serial.println(F("[NET]   2순위: 비밀번호   3순위: PMF(802.11w)/WPA3 전환모드 끄기"));
        } else {
          Serial.println(F("[NET] 설정한 SSID 가 목록에 **없다** → 비번 문제가 아니다."));
          Serial.println(F("[NET]   2.4GHz 인가 / 전파가 닿는가 / SSID 철자가 맞는가를 봐라."));
        }
#endif
        netAdvance(NET_CWJAP, 1000);
        return;
      }
    }

    // `no ip` = IP 없이 CIPSTART 를 쏜 것이다. **재시도가 아니라 CWJAP 로 되돌아간다** (REQ-0064 ④).
    if (strstr(s, "no ip")) {
      netHasIp = false;
#if DEBUG
      Serial.println(F("[NET] no ip — IP 가 없다. CIPSTART 재시도를 접고 CWJAP 로 되돌아간다"));
#endif
      netAdvance(NET_CWJAP, 1000);
      return;
    }
  }
  // ── 멱등 캐시를 비우는 지점이 아래 **두 곳**이다. 중복처럼 보이지만 지우지 마라. ──────
  //
  // 지켜야 할 성질: 서버가 재시작하면 wire_rid 가 1부터 다시 시작하므로, 옛 세션의 rid 가
  // 캐시에 남아 있으면 새 서버의 명령이 "재수신"으로 삼켜진다. 그것도 result=0 이라
  // **성공으로 보인다** — 오류도 타임아웃도 안 난다(REQ-0032).
  //
  // 왜 CLOSED 가 주 방어선인가:
  //   재접속은 netOnline==false 를 요구하고(espReset 첫 줄), netOnline 이 런타임에 false 가 되는
  //   곳은 아래 CLOSED 분기 하나뿐이다. 즉 **재접속이 실제로 일어났다면 CLOSED 는 반드시
  //   탐지된 것이다.** CLOSED 문구가 틀리면 재접속 자체가 안 되므로(= 눈에 보이는 고장)
  //   스테일 캐시가 생길 조건이 애초에 만들어지지 않는다.
  //
  // 왜 CONNECT 쪽도 남기는가:
  //   부팅 후 첫 연결은 CLOSED 를 거치지 않는다(그때 캐시는 어차피 비어 있지만 무해하다).
  //   그리고 이중 방어다 — 한쪽 문구 가정이 깨져도 다른 쪽이 받는다.
  //   비우는 비용은 정수 두 개를 0 으로 되돌리는 것뿐이라 중복이 손해가 아니다.
  //
  // 왜 ALREADY CONNECTED 에서는 비우면 안 되는가:
  //   그건 **기존 연결이 그대로 살아 있다**는 응답이다(서버도 그대로다). espReset() 이 CIPSTART 를
  //   5초마다 재시도하므로, 여기서 비우면 살아 있는 연결의 멱등성이 재시도마다 깨진다.
  // ─────────────────────────────────────────────────────────────────────────
  // 여기부터는 **데이터가 아닌 줄**만 도달한다(위 +IPD 조기 처리가 걸러 냈다).
  if (isAlreadyConnectLine(s)) {
    // ★ REQ-0051: 이 응답의 의미가 **상황에 따라 정반대**다. 둘을 갈라야 한다.
    if (staleSocket) {
      // 전송 실패로 오프라인이 된 뒤라면 "붙어 있다"가 아니라
      // **"ESP 가 낡은 소켓을 붙들고 있다"** 는 신호다. 온라인으로 올리면 무한 루프가 된다.
#if DEBUG
      Serial.println(F("[NET] ALREADY CONNECTED — 낡은 소켓 의심 중이므로 믿지 않는다 → CIPCLOSE"));
#endif
      netStep = NET_CIPCLOSE;
      netStepAt = millis();
      netStepWait = 0;
      return;
    }
    // 낡은 소켓 의심이 없을 때 = **초기 접속 경합.** CIPSTART 가 두 번 나가고 첫 번째가
    // 실제로 성공한 경우라 진짜로 "이미 붙었다"는 뜻이다. 여기서는 그대로 온라인으로 받고
    // **캐시를 비우지 않는다** — 새 연결이 아니므로(REQ-0035 [18]-4 불변식).
    netOnline = true;
    onlineSince = millis();
    if (hwRstPending) { if (hwRstOk < 65535) hwRstOk++; hwRstPending = false; }  // 4단이 들었다            // 사다리 복귀 판정의 기준 시각 (REQ-0071)
    lastTxOkAt  = millis();            // ★ 살아있음 불변식의 출발점 — 붙자마자 발동하지 않게 한다
    sendFailStreak = 0;
    markNeedsRegistration();           // ★ 새 소켓 = 서버가 우리를 모른다. **세 곳 전부**에서 예약한다(아래)
    awaitingSendOk = false; sendOkT1Passed = false;            // ★ 2단계: 새 소켓이다 — 앞 소켓의 SEND OK 를 기다리지 않는다
    closeAttempts = 0;
#if DEBUG
    Serial.println(F("[NET] online (ALREADY CONNECTED · 초기 경합)"));
#endif
    return;
  }
  if (isConnectLine(s)) {
    netOnline = true;
    onlineSince = millis();
    if (hwRstPending) { if (hwRstOk < 65535) hwRstOk++; hwRstPending = false; }  // 4단이 들었다            // 사다리 복귀 판정의 기준 시각 (REQ-0071)
    lastTxOkAt  = millis();            // ★ 살아있음 불변식의 출발점 — 붙자마자 발동하지 않게 한다
    sendFailStreak = 0;
    markNeedsRegistration();           // ★ 새 소켓 = 서버가 우리를 모른다. **세 곳 전부**에서 예약한다(아래)
    awaitingSendOk = false; sendOkT1Passed = false;            // ★ 2단계: 새 소켓이다 — 앞 소켓의 SEND OK 를 기다리지 않는다
    staleSocket = false;               // 진짜로 새로 붙었다 — 낡은 소켓이 아니다
    closeAttempts = 0;
    // TCP 까지 올라왔다는 것은 그 아래(결합·IP)가 전부 성립했다는 뜻이다 — 진단 카운터를 되돌린다.
    cwjapFails = 0;
    cifsrTries = 0;
    netHasIp = true;
    if (!assocAt) assocAt = millis();
    ackQ.clearCache();
#if DEBUG
    Serial.println(F("[NET] online (CONNECT) + 캐시 비움"));
#endif
    return;
  }
  if (isClosedLine(s)) {
    netOnline = false;
    sendFailStreak = 0;
    // 🔴 **이 호출은 세 곳에 있다. 세다 틀리기 쉬워 여기 적어 둔다** (2026-08-18 정정):
    //   ① `isConnectLine`      정상 접속 (CONNECT / Linked)
    //   ② `isAlreadyConnectLine` 초기 경합 (ALREADY / **ALREAY** CONNECT — 구형 펌웨어 오타)
    //   ③ 여기                 소켓 닫힘 (CLOSED / Unlink)
    //   ⚠ 옛 주석이 *"두 전이 모두에서"* 였다. **거동은 맞았고 주석만 틀렸는데**,
    //     그 문구가 "두 곳만 보면 된다"로 읽혀 **다음 사람이 셋째를 빠뜨린다** —
    //     실제로 내가 셀 때도 처음에 두 곳으로 알았다.
    //   🔑 빠뜨리면 **조용히 안 된다**: 재접속 후 등록이 안 되고 시리얼에는 아무것도 안 나온다.
    //     서버만 `Q` 3회 뒤 `node_unregistered` 로 굳힌다. → 시험 [35] 가 세 경로를 전부 밟는다.
    markNeedsRegistration();
    awaitingSendOk = false; sendOkT1Passed = false;            // ★ 2단계: 소켓이 닫혔다 — 그 SEND OK 는 영영 오지 않는다
    // 닫혔다는 통보다 — 낡은 소켓 의심이 해소됐다. 사다리도 내려온다.
    staleSocket = false;
    closeAttempts = 0;
    ackQ.clearCache();                     // ★ 주 방어선 — 위 주석 참조
    // ★ REQ-0064 — 예전에는 여기서 곧장 CIPSTART 로 갔다. 그런데 **소켓이 죽은 이유가
    //   와이파이가 끊긴 것일 수 있다**(공유기 재부팅 — 상시가동에서 가장 흔한 경우다).
    //   이 펌웨어는 `WIFI DISCONNECT` 를 내지 않으므로 netHasIp 는 참으로 남아 있고,
    //   그러면 문지기를 통과해 IP 없이 CIPSTART 가 나간다 — 고치려던 바로 그 상황이다.
    //   CIFSR 은 로컬 질의라 300ms 면 끝난다. **재진입 때마다 IP 를 다시 확인한다.**
    netStep = NET_CIFSR; netStepAt = millis(); netStepWait = 300;
    cifsrTries = 0;
    return;
  }

  // ── REQ-0049 ② 송신 오류 응답 (보조 경로) ────────────────────────────────
  // 온라인 중에 우리가 보내는 AT 명령은 **`AT+CIPSEND=` 하나뿐**이다
  // (espReset() 이 `if (netOnline) return;` 으로 시작하므로 온라인 중엔 다른 명령이 안 나간다).
  // 따라서 **온라인 중에 오는 오류 응답은 반드시 CIPSEND 에 대한 것**이다 — 그래서 여기서
  // 판정해도 "관계없는 AT 실패를 연결 문제로 오인"하는 사고가 구조적으로 생기지 않는다.
  //
  // 다만 세기는 하지 않는다(위 noteSendResult 주석의 이중 계수 문제). 처리를 둘로 나눈다:
  //   · `link is not valid` → **즉시 오프라인.** 링크가 무효라고 모듈이 명시한 것이라 모호하지 않다
  //   · `SEND FAIL` / `ERROR` / `busy`  → **로그만.** 이 송신이 실패했다는 뜻이지 링크가 죽었다는
  //     확증은 아니다. 대응되는 sendLine() 실패가 이미 카운터를 올리고 있으므로 3회면 잡힌다.
  //   ⚠ 실제 문구는 아직 추정이다. REQ-0042 의 [AT] 전체 로깅이 올라간 빌드에서 그 순간의
  //     로그가 오면 어느 문구인지 확정하고 좁힐 수 있다.
  if (netOnline) {
    if (strstr(s, "link is not valid")) {
#if DEBUG
      Serial.println(F("[NET] link is not valid → 즉시 오프라인, 낡은 소켓 닫기(CIPCLOSE)"));
#endif
      // ★ 여기도 CIPSTART 로 바로 가면 REQ-0051 의 무한 루프에 빠진다 — 같은 복구 경로를 탄다.
      startSocketRecovery();
      return;
    }
    // ── 2단계: `SEND OK` — ESP 가 **앞 전송을 실제로 마쳤다**고 알려주는 신호 ──────
    // 지금까지 이 줄을 **아무도 안 봤다.** 그래서 고정 80ms 추측으로 대신했고, 그 추측이
    // 틀리는 순간이 `busy s...` 였다. 여기서 대기를 푸는 것이 2단계의 전부다.
    // ⚠ `strstr` 이 아니라 접두 비교인 이유: 서버 프레임에 우연히 "SEND OK" 가 섞여도
    //   여기까지 오지 않지만(데이터 줄은 맨 앞에서 갈린다), 판정은 좁을수록 좋다.
    if (strncmp(s, "SEND OK", 7) == 0) {
      awaitingSendOk = false; sendOkT1Passed = false;
      return;
    }
    // ── 2단계: `SEND FAIL` — **원래 있던 구멍이다** ──────────────────────────────
    // 이건 `busy`(거부)와도 무응답(프롬프트 놓침)과도 **완전히 다른 사건**이다:
    // 프롬프트까지 정상으로 받고 페이로드도 다 썼는데 **전송이 실패한 것** = 진짜 송신 실패.
    // 그런데 sendLine 은 `>` 를 봤다는 이유로 이미 `noteSendResult(true)` 를 불러
    // **연속 카운터를 0 으로 되돌린 뒤**였다. 즉 프레임이 안 나갔는데 성공으로 세고 있었다.
    // ⚠ 이중 계수가 아니다 — 그 성공 계상을 **여기서 되돌리는** 것이다.
    if (strncmp(s, "SEND FAIL", 9) == 0) {
      awaitingSendOk = false; sendOkT1Passed = false;
      if (sendFails < 65535) sendFails++;
#if DEBUG
      Serial.println(F("[NET] ★ SEND FAIL — 페이로드까지 썼는데 전송이 실패했다. 실패로 센다"));
#endif
      noteSendResult(false);
      return;
    }
    if (strstr(s, "ERROR") || strstr(s, "busy")) {
#if DEBUG
      Serial.print(F("[NET] 송신 오류 응답(로그만, 카운터는 sendLine 이 센다): "));
      Serial.println(s);
#endif
      return;
    }
  }

  // (b) `+IPD` 가 없는 평문 줄. 한 TCP 세그먼트에 프레임이 여러 개 실려 오면 두 번째 줄부터는
  //     `+IPD` 접두가 없으므로 이 경로로 들어온다 — 그래서 같은 검사를 그대로 태운다.
  //     (T 는 개정 3, M 은 개정 5. 넷 다 R/C 와 **같은 파서**를 탄다 — §2.4 · §12B.4)
  handleFrameLine(s);
}

static void drainPending(void) {
  if (!pendReady) return;
  memcpy(workLine, pendLine, RX_CAP);
  // ★ REQ-0174 — **도착 오프셋을 같이 넘긴다.** 이 경로가 정확히 "송신 중 도착"이라
  //   여기서 오프셋을 안 들고 오면 `[SLOT-OOW]` 가 위험 구간을 통째로 놓친다.
  workLineOff = pendLineOff;
  pendReady = false;
  handleLine(workLine);
}
