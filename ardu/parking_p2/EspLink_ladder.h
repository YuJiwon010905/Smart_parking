#pragma once
// ═════════════════════════════════════════════════════════════════════════
// EspLink — **ESP/WiFi 링크 계층** (REQ-0264 · 사용자 지시로 분리)
//   이 파일: espInit · espReset(사다리 구동) — 링크 재수립
//   원본 `조별과제샘플/ardu/client.ino` 의 **3366~3561 행을 원문 그대로** 옮긴 것이다.
//   ⚠ 그 행 번호는 **분리 이전 판(3822줄) 기준**이다. 지금 `client.ino` 에서 찾지 마라.
// ═════════════════════════════════════════════════════════════════════════
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
// 복구 사다리(REQ-0051):
//   전송 실패 → CIPCLOSE → (CLOSED 오면 정상 경로) → CIPSTART
//              → 그래도 ALREADY CONNECTED 만 오면 다시 CIPCLOSE
//              → CIPCLOSE 3회가 안 먹으면 **AT+RST 로 올라가 전체 초기화**
// 마지막 층이 없으면 또 무한 루프가 된다.
// ─────────────────────────────────────────────────────────────────────────
// 🔴 `espInit()` — **UART 를 열고 부팅 사다리를 시작만 한다** (설계문서 §1.2 · 축 4)
//
// ⚠ **반환값을 두지 않는다.** `init` 이 진짜로 아는 것은 **"UART 를 열었다"** 뿐이다:
//   · 사다리는 약 12초 걸리고 **실패할 수 있다.** 여기서 막으면 WiFi 없는 자리에서
//     **영영 `setup` 을 못 나온다.**
//   · 🔴 **모듈이 여러 장치에 흩어지므로** 한 대가 WiFi 를 못 잡아도 나머지가 돌아야 하고,
//     **그 한 대도 로컬 센서는 계속 읽어야 한다. 링크가 없다고 물리 세계가 멈추지 않는다.**
//   · 1단(`AT+RST`)의 응답은 **비동기**라 `setup` 에서는 아직 안 왔다.
//     **그 이상을 반환값에 담으면 거짓이 된다.**
// 🔮 반환값을 둔다면 **"모듈 기동 실패(하드웨어)"와 "아직 링크 없음(정상)"을 갈라라.**
//   합치면 호출부가 정상 상태를 오류로 읽는다.
// 🔴🔴 **이것은 거동 변경이다. 다음 굽기 항목이고 자기 축을 갖는다** (2026-08-18)
//
// ⚠ **`espRead`/`espWrite`/`espReset` 과 성격이 다르다:**
//   ```
//   espRead·espWrite·espReset : 이름만 바꿈 → hex 차이 **0**   (이미 한 함수였다)
//   🔴 espInit               : 둘을 합침   → hex **15줄 상이** (원래 한 함수가 아니었다)
//   ```
// **무엇이 바뀌나** — `millis()` 호출 시점이 **29줄만큼 앞당겨진다:**
//   ```
//   원래 : wifi.begin(9600)  … 29줄(randomSeed · 핀 모드 초기화 · pinMode) …  netStepAt = millis()
//   지금 : 셋을 한자리에서
//   ```
// ⚠ **무해하다**(`netStepWait = 500` 이라 여유가 크다). **그러나 변화가 없는 것은 아니다.**
//   🔑 **무해를 근거로 "변화 0"이라고 적으면 다음 사람이 그 축을 안 센다.**
// ⚠ `always_inline` 을 강제해도 그대로였다 — **원인이 인라인이 아니라 순서**임을 배제로 확인했다.
// 🔴 **어느 쪽으로 옮겨도 순서는 바뀐다** — 원래 코드가 **두 곳에 흩어져 있었기 때문**이다.
//   `netStep` 자리로 옮기면 이번엔 `wifi.begin` 이 29줄 늦어진다.
//
// > **흩어진 것을 모으면 그것이 곧 거동 변경이다.**
// > **경계가 있다는 것과 한 함수였다는 것은 다르다.**
static void espInit(void) {
  wifi.begin(9600);                 // §3.3 AT+UART_DEF 로 보율을 바꾸지 않는다
  netStep     = 0;
  netStepAt   = millis();
  netStepWait = 500;
}

static void espReset(unsigned long now) {
  if (netOnline) {
    // ★ REQ-0071 — 사다리는 **연속 30초 온라인**이라야 0단으로 내려온다.
    //   붙는 즉시 내리면 "붙었다가 12초 뒤 끊김"이 반복될 때 영원히 0~1단만 오간다.
    if (rung != RUNG_MEASURE && (long)(now - onlineSince) >= (long)LADDER_RESET_MS) {
#if DEBUG
      Serial.print(F("[LADDER] ↓ 30초 연속 온라인 — 사다리를 0단으로 내린다 (직전 "));
      Serial.print(rung);
      Serial.println(F("단)"));
#endif
      rung = RUNG_MEASURE;
      rungFails = 0;
    }
    return;
  }

  // ★ 4단이 ESP 를 리셋으로 잡고 있는 동안에는 어떤 AT 명령도 내보내지 않는다.
  //   (잡고 있는 시간 자체가 백오프다 — 위 espRstAssert 주석 참조)
  espRstService(now);
  if (espRstHeld) return;

  if (now - netStepAt < netStepWait) return;

  // ★ REQ-0064 ② — **IP 없이는 CIPSTART 를 쏘지 않는다.**
  //   문지기를 "보내기 직전" 한 곳에 두는 이유: 사다리로 들어오는 길이 여럿(부팅·CLOSED 복구·
  //   전송실패 복구)이라 각 진입점에서 막으면 언젠가 한 곳을 빠뜨린다. 여기 하나면 전부 지난다.
  if (netStep == NET_CIPSTART && !netHasIp) {
#if DEBUG
    Serial.println(F("[NET] IP 가 없다 → CIPSTART 를 보내지 않고 CWJAP 로 되돌아간다"));
#endif
    netStep = NET_CWJAP;
  }

  // ★★ REQ-0071 — **답이 오지 않은 CWJAP 도 실패다.**
  //   여기까지 왔는데 아직 cwjapPending 이면, 직전 CWJAP 가 대기 상한(30초)을 넘도록
  //   OK 도 FAIL 도 내지 않았다는 뜻이다. 예전 코드는 이 경우 아무 계수 없이 CWJAP 를
  //   다시 쐈다 — **사다리가 절대 올라가지 못하는 구멍**이 정확히 여기였다.
  //   사다리가 다음 단계를 정하므로 이번 틱은 전송하지 않고 넘긴다.
  if (netStep == NET_CWJAP && cwjapPending) {
    cwjapPending = false;
    if (cwjapFails < 250) cwjapFails++;
    ladderFail(LF_CWJAP_TIMEOUT);
    return;
  }

  uint8_t sent = netStep;
  netSendStep(sent);
  netStepAt = now;
  netStepWait = NET_WAIT[sent];

  // ⚠ 아래 분기가 정하는 것은 **"응답이 안 왔을 때 갈 곳"** 이다(예전처럼 무조건 갈 곳이 아니다).
  //   정상 전진은 handleLine() 이 응답을 보고 netAdvance() 로 시킨다.
  switch (sent) {
    case NET_CWJAP:
      // 답이 없으면 그냥 다시 묻는다. **전진하지 않는다** — 그게 이번 사고의 원인이었다.
      netStep = NET_CWJAP;
      break;

    case NET_CIFSR:
      // CIFSR 은 로컬 질의라 답이 빠르다. 세 번 물어도 쓸 IP 가 없으면 결합부터 다시.
      if (++cifsrTries >= 3) {
        // ★ 2026-08-17 — 소진 **사유**를 먼저 읽는다. 아래 리셋보다 앞이어야 한다(§8.2-12)
        const bool refused = cifsrRefused;
        cifsrTries = 0;
        cifsrRefused = false;
        // ★ 판별자 ② — **응답이 가비지여도 성립한다.** 18:48:12 리셋에서 CIFSR 응답이
        //   통째로 깨져 `0.0.0.0` 문자열이 안 나왔고, 판별자 ① 만으로는 놓쳤다(monitor 실측).
        //   "세 번 물어도 쓸 IP 가 없었다"는 **문자열이 아니라 우리 쪽 상태**라 안 깨진다.
        // ⚠ `busy` 로 거부돼 소진된 것이면 **IP 소실이 아니다.** 세지 않는다(원장 §8.2-12).
        if (!refused) noteIpLoss();
#if DEBUG
        if (refused)
          Serial.println(F("[NET] CIFSR 3회 소진 — 그러나 busy 거부였다. IP 소실로 세지 않는다"));
        Serial.println(F("[NET] CIFSR 3회에도 IP 가 없다 → CWJAP 부터 다시"));
#endif
        netStep = NET_CWJAP;
        // ★★ REQ-0071 — 여기가 사다리의 **두 번째 구멍**이었다.
        //   `CWJAP 는 OK 인데 CIFSR 은 IP 가 없다`가 계속되면 CWJAP↔CIFSR 사이를 영원히 돈다.
        //   CWJAP 가 FAIL 을 내지 않으므로 위의 두 입구(FAIL·무응답) 어느 쪽도 열리지 않는다.
        //   이것도 명백한 실패 사건이므로 사다리에 알린다.
        ladderFail(LF_CWJAP_FAIL);
        // 0·1단의 조치는 "CIFSR 로 다시 확인"인데, 방금 세 번 물어서 없다는 것을 확인했다.
        // 그 경우에만 기존 동작(결합부터 다시)으로 되돌린다. 2단 이상의 강한 조치는 그대로 둔다.
        if (netStep == NET_CIFSR) netAdvance(NET_CWJAP, 1000);
      } else {
        netStep = NET_CIFSR;
      }
      break;

    case NET_CIPMUX:
      // 응답이 없어도 CIPSTART 로는 간다(구형 펌웨어가 조용한 경우가 있다).
      // 대신 `busy` 였다면 handleLine 이 이미 같은 단계로 되돌려 놓는다.
      netStep = NET_CIPSTART;
      break;

    // ── 진단 사슬은 응답이 없어도 다음 칸으로 넘어간다(진단이 결합을 오래 막으면 안 된다) ──
    case NET_GMR:       netStep = NET_CWCOUNTRY; break;
    case NET_CWCOUNTRY: netStep = NET_CWLAP;     break;


    case NET_CWLAP:
      // 스캔이 조용히 끝나는 펌웨어도 있다. 한 번 쐈으면 진단은 끝난 것으로 본다.
      lapDone = true;
      netStep = NET_CWJAP;
      break;

    case NET_CWQAP:
      // ⚠ 이 case 는 반드시 있어야 한다. 없으면 아래 default 가 `sent + 1` 로 가는데
      //   CWQAP 는 열거의 **마지막**이라 NET_STEP_COUNT 가 되어 NET_WAIT[] 를 범위 밖으로
      //   읽는다(조용한 메모리 오류). 답이 없어도 결합 해제는 시도된 것으로 보고 넘어간다.
      netStep = NET_CWJAP;
      break;

    case NET_CIPCLOSE:
      closeAttempts++;
      if (closeAttempts >= CLOSE_ATTEMPT_LIMIT) {
        // 로컬 명령이 세 번 연속 안 먹었다 = AT 계층이 꼬였다 → 사다리 상승
#if DEBUG
        Serial.print(F("[NET] CIPCLOSE "));
        Serial.print(closeAttempts);
        Serial.println(F("회 실패 → AT+RST 로 전체 초기화(사다리 상승)"));
#endif
        netStep = NET_RST;
        staleSocket = false;         // RST 가 모듈 상태를 통째로 지운다
        closeAttempts = 0;
        netStepWait = 200;           // 곧 RST 를 쏜다 (사다리가 더 긴 백오프로 덮어쓸 수 있다)
        // ★ REQ-0071 — 로컬 명령이 세 번 연속 안 먹은 것은 **AT 계층이 잠긴 것**이다.
        //   이것도 실패 사건이므로 사다리에 알린다. 이 통보가 없으면 소켓 복구는
        //   CIPCLOSE↔RST 사이를 자기들끼리 영원히 돌 뿐 3단 위로 올라가지 못한다.
        //   (사다리가 이미 3단 이상이면 여기서 4단으로 올라간다 = 소프트 리셋이 안 먹었다는 뜻)
        ladderFail(LF_AT_JAMMED);
      } else {
        // CLOSED 가 오면 handleLine 이 CIFSR 로 보낸다.
        // 안 오더라도 **다시 올라가는 길은 남겨 둔다**(닫힘 통보가 없는 모듈도 있다).
        // 여기도 CIPSTART 가 아니라 CIFSR 이다 — 이유는 위 CLOSED 분기의 주석과 같다.
        netStep = NET_CIFSR;
        cifsrTries = 0;
      }
      break;

    case NET_CIPSTART:
      // 응답(CONNECT / ALREADY CONNECTED / CLOSED)은 handleLine 이 처리한다.
      // 아무 응답도 안 오면: 낡은 소켓 의심 중이면 닫기부터, 아니면 다시 CIPSTART.
      netStep = staleSocket ? (uint8_t)NET_CIPCLOSE : (uint8_t)NET_CIPSTART;
      break;

    default:
      netStep = (uint8_t)(sent + 1);   // 부팅 순서 진행 (RST→CWMODE→CWJAP→CIPMUX→CIPSTART)
      break;
  }
}
