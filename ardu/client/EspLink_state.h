#pragma once
// ═════════════════════════════════════════════════════════════════════════
// EspLink — **ESP/WiFi 링크 계층** (REQ-0264 · 사용자 지시로 분리)
//   이 파일: 링크 상태 · 복구 사다리 문서 · ESP 하드리셋 배선 · netAdvance · netSendStep
//   원본 `조별과제샘플/ardu/client.ino` 의 **669~932 행을 원문 그대로** 옮긴 것이다.
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
// 접속 상태 기계 (논블로킹 — loop() 를 막지 않는다)
// ─────────────────────────────────────────────────────────────────────────
static bool          netOnline = false;
static uint8_t       netStep = 0;
static unsigned long netStepAt = 0;
static uint16_t      netStepWait = 500;

// ─────────────────────────────────────────────────────────────────────────
// REQ-0064 — **타이머 구동에서 응답 구동으로.**
//
// 실기에서 step=4 정체 + `no ip` 무한반복이 났다. 실측 로그가 원인을 정확히 보여 줬다:
//
//   [NET] 2 CWJAP
//   [AT] "AT+CWJAP="unknowns_network2.4","..."" (55)
//   [DIAG] offline step=3 rx=170 ... up=6s      ← 응답 없음
//   [DIAG] offline step=3 rx=170 ... up=9s      ← 응답 없음 (rx 가 12초간 고정)
//   [NET] 3 CIPMUX=0
//   [AT] "busy p..." (9)                        ← ★ 거부됐다
//   [NET] 4 CIPSTART
//   [AT] "busy p..." (9)                        ← ★ 또 거부
//   [AT] "FAIL" (4)                             ← ★★ up≈20s, 이제야 CWJAP 의 결과
//   [AT] "no ip" (5)                            ← 이후 영원히 반복
//
// 읽어야 할 것 셋:
//   1. **이 펌웨어의 CWJAP 는 약 16초 걸린다.** NET_WAIT 의 9000ms 는 턱없이 짧았다.
//   2. 그 사이 사다리가 타이머만 보고 전진해 CIPMUX·CIPSTART 를 쐈고 **전부 `busy p...` 로 거부**됐다.
//      → **`AT+CIPMUX=0` 은 한 번도 적용된 적이 없다.** `OK` 가 로그에 없다. 조용한 두 번째 고장이다.
//   3. `no ip` 는 원인이 아니라 **결과**다. 상류는 CWJAP 실패다.
//
// 그래서 각 단계는 이제 **응답이 올 때까지 기다린다.** NET_WAIT 는 "다음 단계로 가는 시각"이 아니라
// **"이만큼 기다려도 답이 없으면 재시도"** 라는 뜻으로 바뀌었다. 전진은 handleLine() 이
// netAdvance() 로 시킨다 — 응답을 본 쪽이 다음 단계를 정한다.
//
// ⚠ 이 보드의 펌웨어는 **구형 ai-thinker** 다([System Ready, Vendor:www.ai-thinker.com]).
//   ESP-AT v2.2 문서와 어휘가 다르다. 실측으로 확인된 차이:
//     · `WIFI CONNECTED` / `WIFI GOT IP` 를 **아예 내지 않는다**
//     · `+CWJAP:<n>` 사유 코드도 없다 — 성공은 `OK`, 실패는 `FAIL` 한 줄이 전부다
//   그래서 IP 확보 판정을 문구에 기대지 않고 **`AT+CIFSR` 로 직접 물어본다.**
//   이것이 펌웨어 세대와 무관하게 동작하는 유일한 방법이다. 신형 문구(GOT IP 등)도
//   같이 받아 두되, 그것만 믿지는 않는다.
// ─────────────────────────────────────────────────────────────────────────
enum {
  NET_RST = 0, NET_CWMODE, NET_CWJAP,
  NET_CIFSR,                          // ★ IP 를 실제로 받았는지 물어본다 (REQ-0064 ②)
  NET_CIPMUX, NET_CIPSTART,
  NET_CIPCLOSE,                       // 복구 전용 — 낡은 소켓을 닫는다(REQ-0051)
  // ── 아래 셋은 **진단 전용 사슬**이다. CWJAP 2회 실패 때 한 번만, 순서대로 지나간다.
  //    주 사다리(부팅·복구)는 여기를 절대 지나지 않는다 — 검증된 경로를 건드리지 않으려는 것이다.
  //      GMR → CWCOUNTRY → CWLAP → (CWJAP 재시도)
  NET_GMR,                            // 펌웨어 판 확정 — ecn 열거 범위와 CWCOUNTRY 지원 여부가 여기서 갈린다
  NET_CWCOUNTRY,                      // 규제도메인 1~13 우회 시도 (구형이면 ERROR — 그 자체가 결론이다)
  NET_CWLAP,                          // 주변 AP 목록 (REQ-0064 ⑤)
  // ⓘ `AT+CWJAP?`(질의형) 단계를 넣었다가 뺐다 — **이 펌웨어는 `ERROR` 로 답한다(실측).**
  //   미지원 명령을 매 실패마다 쏘면 로그만 더럽힌다. 같은 질문(결합했는가)은
  //   `AT+CIFSR` 이 실주소를 답하는지로 더 확실하게 답할 수 있다.
  NET_CWQAP,                          // ★ REQ-0071 2단 — 명시적 결합 해제 후 다시 붙는다
  NET_STEP_COUNT
};
// 각 단계의 **응답 대기 상한**. 넘으면 그 단계를 다시 시도한다(전진이 아니다).
//   CWJAP 30000 : 실측 16초 + 여유. 짧게 잡은 것이 이번 사고의 직접 원인이었다
//   CWLAP  9000 : 스캔은 채널을 훑으므로 오래 걸린다
//   CWQAP  3000 : 모듈 로컬 동작이라 왕복이 없다. 3초면 넉넉하다
static const uint16_t NET_WAIT[] = { 2500, 800, 30000, 2500, 1200, 8000, 800, 1500, 1500, 9000, 3000 };
static_assert(sizeof(NET_WAIT) / sizeof(NET_WAIT[0]) == NET_STEP_COUNT,
              "NET_WAIT 길이가 단계 수와 다르다 — 새 단계를 넣고 대기시간을 빠뜨렸다");

// 마지막으로 **실제로 쏜** 명령. netStep 은 이미 "답이 없을 때 갈 곳"으로 앞서 가 있으므로,
// 도착한 응답이 무엇에 대한 답인지는 이 변수로 판정해야 한다.
static uint8_t netLastSent = 0xFF;

static bool    netHasIp = false;      // ★ CIPSTART 의 전제조건 (REQ-0064 ②)
static uint8_t cwjapFails = 0;        // CWJAP 연속 실패 횟수 — 2회면 CWLAP 진단을 켠다
static uint8_t cifsrTries = 0;        // CIFSR 로 IP 를 못 본 횟수 — 3회면 CWJAP 로 되돌아간다
static bool    lapDone = false;       // CWLAP 은 **한 번만** 쏜다(스캔 중엔 접속을 못 한다)
static bool    lapFound = false;      // 그 스캔에서 우리 SSID 가 보였는가

static void netAdvance(uint8_t step, uint16_t wait) {
  netStep = step;
  netStepAt = millis();
  netStepWait = wait;
}

// ─────────────────────────────────────────────────────────────────────────
// 복구 사다리 상태 (REQ-0051)
//
// 실기 증상: 끊긴 뒤 `ALREADY CONNECTED` 만 무한 반복되고 연결이 안 된다. 원인은
// **CIPSTART 로는 ESP 의 낡은 소켓을 지울 수 없다**는 것이다. ESP 는 죽은 소켓을 아직
// 열려 있다고 믿고 CIPSTART 에 `ALREADY CONNECTED` 로 답하고, 그걸 온라인으로 받으면
//   goOffline → CIPSTART → ALREADY → online → 3회 실패 → goOffline → …
// 로 영원히 돈다. 감지(REQ-0049)는 맞았고 **복구가 반쪽이었다.**
//
// 그래서 사다리를 만든다: **CIPCLOSE → (CLOSED) → CIPSTART → 안 되면 AT+RST.**
//
// staleSocket: "ESP 가 낡은 소켓을 붙들고 있다고 의심한다". 전송 실패로 오프라인이 될 때 선다.
//   이 플래그가 서 있는 동안의 `ALREADY CONNECTED` 는 **"붙어 있다"가 아니라 정반대 신호**이므로
//   온라인으로 올리지 않는다. 실제 `CONNECT` 나 `CLOSED` 에서 내린다.
//   ⚠ 플래그가 없을 때(=초기 접속 경합)의 `ALREADY CONNECTED` 는 진짜로 "이미 붙었다"는 뜻이라
//     그대로 온라인으로 받고 **캐시도 비우지 않는다**(REQ-0035 [18]-4 불변식).
//
// CLOSE_ATTEMPT_LIMIT = 3 의 근거:
//   CIPCLOSE 는 **모듈 로컬 동작**이라 네트워크 왕복이 없다 — 정상 모듈이면 대기 창(800ms)
//   안에 반드시 응답한다. 즉 한 번 실패는 `busy p...` 같은 일시적 사정일 수 있지만
//   **세 번 연속 로컬 명령이 안 먹으면 그건 AT 계층 자체가 꼬인 것**이고, 그때 올바른
//   대응은 재시도가 아니라 모듈 리셋이다. 1회는 너무 이르고(일시적 busy 하나로 14초 RST 사이클),
//   더 크게 잡으면 무한 루프 시간만 길어진다. 3회면 약 2.5초 안에 사다리를 다 올라간다.
// ─────────────────────────────────────────────────────────────────────────
static bool    staleSocket = false;
static uint8_t closeAttempts = 0;
static const uint8_t CLOSE_ATTEMPT_LIMIT = 3;

// ═════════════════════════════════════════════════════════════════════════
// 복구 사다리 (REQ-0071) — **같은 자극을 반복하는 것은 사다리가 아니다**
//
// 실측이 말한 것: 45분 정상동작 뒤 한 번 불안해지자 **18분 동안 한 번도 복구하지 못했다.**
// 그동안 코드가 한 일은 CWJAP 재시도 91회(꼬리에서 19회 연속)가 전부였다.
// 라우터 관리화면이 이유를 설명했다 — **IP 할당까지는 매번 성공하고 약 12초 뒤 결합이
// 소실된다.** 즉 `CWJAP FAIL` 은 "AP 에 못 붙었다"가 아니라 **"붙어서 DHCP 까지 받았는데
// 12초 뒤 끊어졌다"** 이고, 그렇다면 **같은 CWJAP 를 다시 쏘는 것은 같은 12초를 다시
// 만드는 것**이다. 재시도 횟수를 늘려도 원리적으로 낫지 않는다. 단을 올려야 한다.
//
// 원칙 셋(REQ-0071):
//   1. 실패가 쌓이면 반드시 **더 강한 단**으로 올라간다 — 같은 자극 무한 반복 금지
//   2. 아래 단을 건너뛰지 않는다 — 건강한 모듈을 하드웨어 리셋으로 두들기지 않는다
//   3. 모든 단 전이를 시리얼 한 줄(`[LADDER]`)로 남긴다 — 어느 칸에서 죽었는지 로그로 재구성된다
//
//   단 0  계측만                        — 항상. CWJAP 소요시간·연속 실패수·**결합 유지시간**
//   단 1  시리얼 드레인 + CIFSR 재확인   — 쓰레기·동기 어긋남을 씻는다
//   단 2  AT+CWQAP 로 명시적 결합 해제 후 CWJAP
//   단 3  AT+RST (모듈 소프트 리셋)
//   단 4  ESP 하드웨어 리셋선            — 배선 필요(ESP_RST_WIRED). REQ-0063 ② 가 여기 들어왔다
//   단 5  ESP 전원 재투입                — **부품 미보유로 미구현.** 아래 applyRung() 참조
//   단 6  AVR 워치독                     — 파일 상단 ENABLE_WDT. REQ-0063 ① 이 여기 들어왔다
//
// ── 왜 단이 올라갈수록 쉬는 시간을 늘리는가 ──
// 근본원인 1순위 후보가 **전원**이다(ESP-01 송신 피크 300~430mA vs Uno 3.3V 핀 공급 50mA).
// 전원이 원인이라면 **쉬는 시간 자체가 조치**다 — 결합을 시도하지 않는 동안 RF 부하가 사라져
// 레귤레이터와 커패시터가 회복한다. 백오프는 예의가 아니라 치료의 일부다.
//   ⚠ 이것은 **정황이지 확정이 아니다.** 전원을 정상화한 뒤 12초 소실이 사라지는지 봐야 확정된다.
//
// ── 사다리를 언제 0단으로 되돌리는가 (이 규칙이 없으면 사다리가 무력해진다) ──
// **온라인이 되자마자 되돌리면 안 된다.** 관측된 고장은 "붙었다가 12초 뒤 끊김"의 반복이라,
// 붙는 순간 초기화하면 사다리는 영원히 0~1단을 오가며 절대 위로 못 올라간다 —
// 고치려는 바로 그 무한반복이 이름만 바꿔 되살아난다. 그래서 **연속 30초 온라인**을 요구한다.
// 30초는 관측된 12초 주기보다 넉넉히 위라, 깜빡이는 링크는 **증명 가능하게** 사다리를 못 내린다.
// ═════════════════════════════════════════════════════════════════════════
enum { RUNG_MEASURE = 0, RUNG_RESYNC, RUNG_CWQAP, RUNG_SOFTRST, RUNG_HWRST,
       RUNG_POWER, RUNG_MAX = RUNG_POWER };

// 그 단에서 몇 번 실패하면 위로 올라가는가.
//   0단 1회 : 첫 실패는 곧바로 씻어 본다. 가장 싼 조치라 늦출 이유가 없다
//   1~4단 2회: 한 번은 우연일 수 있지만 두 번이면 그 단으로는 안 되는 것이다
//              (REQ-0071 표의 "N단이 2회 실패하면 N+1단" 을 그대로 옮겼다)
//   5단 255 : 종착역이다. 더 올라갈 곳이 없다
static const uint8_t  RUNG_LIMIT[]      = {    1,    2,    2,     2,     2,   255 };
// 그 단으로 올라간 뒤 다음 시도까지 쉬는 시간(ms). REQ-0071 권고를 그대로 썼다.
static const uint16_t RUNG_BACKOFF_MS[] = {    0,    0, 5000, 15000, 30000, 60000 };
static_assert(sizeof(RUNG_LIMIT) / sizeof(RUNG_LIMIT[0]) == RUNG_MAX + 1,
              "RUNG_LIMIT 길이가 단 수와 다르다");
static_assert(sizeof(RUNG_BACKOFF_MS) / sizeof(RUNG_BACKOFF_MS[0]) == RUNG_MAX + 1,
              "RUNG_BACKOFF_MS 길이가 단 수와 다르다");

static uint8_t       rung = RUNG_MEASURE;
static uint8_t       rungFails = 0;              // 현재 단에서 쌓인 실패 수
static bool          ladderEverFailed = false;   // 아래 이중계수 가드의 첫 호출 예외
static unsigned long lastLadderFailAt = 0;

// 사다리를 0단으로 되돌리기 위해 요구하는 연속 온라인 시간(위 주석 참조)
static const uint16_t LADDER_RESET_MS = 30000;
static unsigned long  onlineSince = 0;

// ── 0단 계측 ──
// cwjapPending 은 **"CWJAP 를 쏴 놓고 아직 OK 도 FAIL 도 못 받았다"** 는 뜻이다.
// 이게 없으면 응답이 아예 없는 CWJAP(=사고 당시 자주 있었다)가 30초마다 조용히
// 되풀이될 뿐 실패로 세어지지 않아 **사다리가 영원히 0단에 머문다.** 사다리를 실제로
// 굴리는 것은 이 한 개의 불리언이다.
static bool          cwjapPending = false;
static unsigned long cwjapSentAt = 0;
// 결합(IP 확보) 시각. 0 이면 결합 없음. **다음 실패 때 "결합 유지 N초"를 찍는 근거**이고,
// 그 숫자가 12초 근처로 계속 찍히면 사용자 관측(라우터 화면)이 장치 로그로 확증된다.
static unsigned long assocAt = 0;

// ── 4단 배선: ESP 하드웨어 리셋선 ─────────────────────────────────────────
// **핀 A2 를 쓴다.** 남는 핀을 다시 세어서 고른 것이지 임의로 뺏은 것이 아니다
// (배선 금지목록과 모듈 표의 핀 칸을 그대로 대조했다. 10칸을 다 쓰는 최대 구성 기준):
//   D0,D1 USB시리얼 / D2~D6 센서 5칸 / D7,D8 ESP 시리얼 / D9~D12 센서 4칸 /
//   D13 온보드LED / A0 센서 1칸 / A1 난수시드 / A4,A5 I2C 예약
//   → 남는 것은 **A2 와 A3 둘뿐**이고 그중 A2 를 썼다. **센서 칸은 하나도 옮기지 않았다.**
// ⚠ 이름 혼동: 여기의 **핀 A2** 는 **자리 A2**(= 핀 D3)와 아무 상관이 없다. 위 §1 주석 참조.
//
// ⚠⚠ 전기적으로 가장 중요한 것 — **이 핀을 OUTPUT HIGH 로 만들지 마라.**
//   Uno 의 HIGH 는 5V 이고 ESP-01 의 RST 는 3.3V 로직이다. 5V 를 밀어넣으면 모듈이 상한다.
//   그래서 **오픈드레인처럼** 쓴다:
//     누름(리셋) = pinMode(OUTPUT) + digitalWrite(LOW)
//     놓음       = pinMode(INPUT)   ← 하이임피던스. 라인은 ESP 쪽 풀업이 HIGH 로 잡는다
//   (AVR 코어의 pinMode(INPUT) 은 DDR 과 PORT 를 함께 0 으로 만든다 → 내부 풀업도 꺼진다.
//    즉 이 상태에서 우리 쪽은 라인에 아무 전압도 걸지 않는다.)
//   ⚠ 그러므로 **RST 쪽에 풀업이 있어야 한다.** 놓았을 때 라인이 뜨면 모듈이 불확정 상태가
//     되어 4단이 없느니만 못해진다. 모듈에 풀업이 없으면 10kΩ 을 RST↔3.3V 사이에 달아라.
//   ⚠ 리셋과 놓음의 GND 기준이 같아야 한다 — Uno GND 와 ESP GND 는 이미 공통이어야 정상이다.
// ═════════════════════════════════════════════════════════════════════════
// 🔴 **이 값은 *물리 배선 상태* 다. 코드로 검증할 수 없다 — 출처를 여기 적는다.**
//   ★ **근거 출처**: 사용자 직접 확인 · **2026-08-19** · 원문 인용:
//     > *"저런 조건이어서 하드웨어 리셋을 **배제했다.** 저런 조건이면 하드웨어 리셋을 해도
//     >  동일한 상황이다. 그래서 배제한 것이고, 경우 중에 **딱 1개 펌웨어 hang 문제만**
//     >  해결 가능한데 **hang 을 막기 위해 송수신 로직을 재설계하여 적용**하였고 현재는 해당
//     >  케이스가 발생하지 않는 구조이다. **그래서 하드웨어 리셋이 불필요해졌다.**"*
//   🔴 **그러므로 `0` 은 "아직 안 물렸다"가 아니라 "물릴 필요가 없다"다.**
//     전원 불안정·CH_PD(EN) LOW·RST LOW 는 **칩이 전기적으로 안 도는 상태**라 RST 핀을
//     흔들어도 동일하다. UART 계층 고장도 RST 핀과 무관하다. **남는 용도는 펌웨어 hang 뿐이었고
//     그것을 송수신 로직 재설계로 없앴다** — 그 물건이 `EspLink_*.h` 다.
//   ⚠ **이 문단은 사용자의 설계 확정을 인용한 것이지 내 실측이 아니다.**
//     내가 잰 것은 §36.1(1 로 두면 `AT+RST` 경로가 사라진다) 하나다.
//   ⚠ **바꾸려면 사용자에게 다시 물어라.** 문서·기억·"물려 있을 것이다"는 근거가 아니다.
//   🔑 **왜 출처를 적나**: 같은 날 이 값을 1 로 올렸다가 되돌렸다. 그때 주석에는
//     *"배선 확인됨"* 이라고만 있고 **누가 언제 확인했는지가 없었다.** 출처가 있었으면
//     정정이 왔을 때 **코드에서 바로 짚였다.** ★ **물리 상태를 근거로 쓰는 코드는 출처를 적는다.**
// ═════════════════════════════════════════════════════════════════════════
#define ESP_RST_WIRED 0   // 🔴 **되돌렸다 (2026-08-19 16:2x) — 사용자 확정: "리셋선은 없다"**
//   ⚠ 앞서 1 로 올린 근거("배선 확인됨")가 **틀렸다.** 애매한 답을 확인으로 읽은 것이었다.
//   🔴 **선이 없는데 1 로 두면 오히려 나쁘다.** `applyRung()` 이 이렇게 갈린다:
//     ```
//     ESP_RST_WIRED=1 → espRstAssert(back)      … 선이 없으면 **아무 일도 안 일어난다**
//                                                  그리고 `AT+RST` 로 **안 빠진다**
//     ESP_RST_WIRED=0 → netAdvance(NET_RST, back) … 최소한 `AT+RST` 는 시도한다
//     ```
//     즉 4·5단이 **백오프만 그대로 쓰고 조치는 0** 이 된다. **실질 복구 시도가 줄어든다.**
//   🔑 그리고 부팅 로그가 `배선됨(A2)` 이라고 **거짓을 말하게 된다** —
//     `⚠4단 미실행` 표지는 monitor 를 살린 그 표지다(§33.6).
//     ★ **표지를 지우는 변경은 그 표지가 지키던 진실을 같이 지운다.**
//   🔮 선을 실제로 물리면 그때 다시 1 로 올린다. **그전에는 0 이 참이다.**
                                        //   1 로 두면 없는 배선을 전제해 4단이 "리셋했다"고 거짓 로그를 남긴다.
static const uint8_t PIN_ESP_RST = A2;

static bool          espRstHeld = false;
static uint16_t      hwRstAsserts = 0;   // 4단(하드리셋) 실행 횟수
// 🔴 **`ESP_RST_WIRED=0` 인 지금 이 값은 판정에 못 쓴다.** `espRstAssert` 의 본문이 통째로
//   `#if` 밖이라 **호출돼도 안 센다** → 항상 `0/0` 이다. 그리고 그게 **정직한 값**이다:
//   **선이 없으므로 "4단이 듣는가"라는 물음 자체가 성립하지 않는다.**
//   ⚠ **`0/0` 을 "4단이 아직 안 왔다"로 읽지 마라 — "4단이 없다"가 맞다.**
//   🔮 선을 물리고 `ESP_RST_WIRED=1` 로 올린 뒤부터 이 값이 판정 자료가 된다.
static uint16_t      hwRstOk      = 0;   // 그중 **온라인까지 간** 횟수 = 4단이 **들었는지**
static bool          hwRstPending = false;
// 🔑 **분모를 같이 둔다**: `hwrst=<실행>/<성공>`. `0` 이 혼자 서면 "안 일어났다"인지
//   "듣지 않았다"인지 못 가른다(monitor 규칙). `2/0` 이면 4단이 안 듣는 것이고,
//   `2/2` 면 듣는 것이다 — 그때 5단(MOSFET)이 필요한지가 갈린다.
static unsigned long espRstReleaseAt = 0;

// 무엇을 보냈는지 찍는다(REQ-0042 3순위). 이게 없으면 5초마다 CIPSTART 를 재시도하는지조차
// 로그로 확인할 수 없다.
#if DEBUG
#define DBG_NET(n, name) do { Serial.print(F("[NET] " n " " name)); Serial.println(); } while (0)
#else
#define DBG_NET(n, name) do {} while (0)
#endif

static void netSendStep(uint8_t s) {
  switch (s) {
    case NET_RST:     wifi.print(F("AT+RST\r\n"));      DBG_NET("0", "RST");      break;
    case NET_CWMODE:  wifi.print(F("AT+CWMODE=1\r\n")); DBG_NET("1", "CWMODE=1"); break;
    case NET_CWJAP:   wifi.print(F("AT+CWJAP=\"" WIFI_SSID "\",\"" WIFI_PASS "\"\r\n"));
                      DBG_NET("2", "CWJAP (응답까지 최대 30초 기다린다)"); break;
    case NET_CIFSR:   wifi.print(F("AT+CIFSR\r\n"));    DBG_NET("3", "CIFSR (IP 를 실제로 받았는가)"); break;
    case NET_CIPMUX:  wifi.print(F("AT+CIPMUX=0\r\n")); DBG_NET("4", "CIPMUX=0"); break;
    case NET_CIPSTART:wifi.print(F("AT+CIPSTART=\"TCP\",\"" SERVER_IP "\"," SERVER_PORT "\r\n"));
                      DBG_NET("5", "CIPSTART"); break;
    case NET_CIPCLOSE:wifi.print(F("AT+CIPCLOSE\r\n")); DBG_NET("6", "CIPCLOSE (낡은 소켓 닫기)"); break;
    case NET_GMR:     wifi.print(F("AT+GMR\r\n"));      DBG_NET("7", "GMR (펌웨어 판 — 진단)"); break;
    case NET_CWCOUNTRY:
                      // 규제도메인을 KR(1~13)로 넓혀 채널 12/13 결합을 열어 보려는 시도.
                      // 구형 펌웨어에는 이 명령이 없다 → ERROR. 그 ERROR 가 곧 "코드로는 못 고친다"의 확증이다.
                      wifi.print(F("AT+CWCOUNTRY_DEF=0,\"KR\",1,13\r\n"));
                      DBG_NET("8", "CWCOUNTRY (채널 12/13 우회 시도 — 진단)"); break;
    case NET_CWLAP:   wifi.print(F("AT+CWLAP\r\n"));    DBG_NET("9", "CWLAP (주변 AP 목록 — 진단)"); break;
    case NET_CWQAP:   wifi.print(F("AT+CWQAP\r\n"));    DBG_NET("10", "CWQAP (결합 해제 — 사다리 2단)"); break;
    default: break;
  }
  // ★ REQ-0071 — CWJAP 의 "답이 오지 않는 경우"를 실패로 세기 위한 표식(위 cwjapPending 주석).
  //   RST·CWQAP 는 진행 중인 결합 시도를 무효로 만드는 명령이므로 표식을 내린다.
  //   내리지 않으면 리셋 뒤 첫 틱에서 "무응답"으로 오인되어 한 칸이 공짜로 올라간다.
  if (s == NET_CWJAP)                        { cwjapPending = true; cwjapSentAt = millis(); }
  else if (s == NET_RST || s == NET_CWQAP)   { cwjapPending = false; }
  netLastSent = s;
}

// "이 줄에 쓸 만한 IPv4 가 들어 있는가" — CIFSR 응답 판정용.
//   구형: `192.168.0.7` 만 덜렁 온다 / 신형: `+CIFSR:STAIP,"192.168.0.7"`
// 둘 다 받으려고 문구가 아니라 **숫자 모양**으로 찾는다. 0.0.0.0 은 IP 가 아직 없다는 뜻이다.
static bool hasUsableIp(const char* s) {
  for (const char* p = s; *p; p++) {
    if (*p < '0' || *p > '9') continue;
    if (p != s && p[-1] >= '0' && p[-1] <= '9') continue;   // 숫자의 중간이면 건너뛴다
    uint8_t oct = 0, dots = 0, first = 0;
    const char* q = p;
    for (;;) {
      if (*q >= '0' && *q <= '9') { oct = (uint8_t)(oct * 10 + (*q - '0')); q++; continue; }
      if (dots == 0) first = oct;
      if (*q == '.') { dots++; oct = 0; q++; continue; }
      break;
    }
    if (dots == 3 && first != 0) return true;               // 0.x.x.x = 아직 IP 없음
  }
  return false;
}

