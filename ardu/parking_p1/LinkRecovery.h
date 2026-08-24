#pragma once
// ═════════════════════════════════════════════════════════════════════════
// LinkRecovery.h — IP 소실 · 소켓 복구 · 오프라인 전이
//   🔒 **링크 성질** — 잠금 후보. 클래스화하지 않는다
//     `espRead` 와 한 몸으로 도는 코드다. **잠금의 목적은 파일이 아니라 이 성질이다.**
// ═════════════════════════════════════════════════════════════════════════
// 🔴🔴 **위치를 옮기지 마라.** `client.ino` 의 **정해진 자리에서** `#include` 된다.
//   헤더 순서가 바뀌면 선언·초기화 순서가 같이 바뀌어 **산출물이 달라진다.**


// IP 소실을 한 번만 세는 자리. 두 판별자 어느 쪽이든 여기로 들어온다.
static void noteIpLoss(void) {
  if (ipLossLatched) return;             // 같은 사건의 두 번째 증상 — 세지 않는다
  ipLossLatched = true;
  if (espResets < 65535) espResets++;
#if DEBUG
  Serial.print(F("[NET] ★ ESP 가 IP 를 잃었다 — 모듈 리셋으로 본다. 누적 사건 "));
  Serial.println(espResets);
#endif
}

// 소켓 복구 진입 — **전송이 안 되는 것을 이유로 오프라인이 되는 모든 경로**가 여기를 통과해야 한다.
// (연속 실패 카운터 / `link is not valid` 둘 다.) 🔴 한 곳이라도 `CIPSTART` 로 바로 가면
// 그 경로가 무한 루프가 된다 — 아래 CIPCLOSE 문단이 그 이유다.
static void startSocketRecovery(void) {
  netOnline = false;
  sendFailStreak = 0;
  // 대기를 **반드시 푼다.** 소켓이 사라지는 마당에 앞 전송의 `SEND OK` 는 영영 오지 않는다.
  //   안 풀면 다시 온라인이 된 뒤 첫 송신이 통째로 건너뛰어지고, 최악에는 상한(3초)을 태우고
  //   나서야 첫 프레임이 나간다. **복구 직후가 가장 급한 순간인데 거기서 늦어진다.**
  awaitingSendOk = false; sendOkT1Passed = false;
  // **보류된 ACK 도 여기서 버린다.** 소켓이 사라지면 그 `rid` 에 대한 답은 새 소켓에서
  //   의미가 없다. `awaitingSendOk` 를 푸는 바로 이 자리에 둔다 — 한 곳에서만 처리하면
  //   빠뜨릴 수 없다. ⚠ **멱등 캐시는 비우지 않는다**(아래 이유). 큐만 비운다 — 다른 물건이다.
  ackQ.clearQueue();
  // 운영 계수 — **전송이 안 되어 링크를 다시 세우는 모든 경로가 여기를 지난다.**
  //   여기 한 곳에서만 세면 중복도 누락도 없다.
  if (linkDrops < 65535) linkDrops++;
  // ⚠ 여기서 멱등 캐시를 비우지 않는다. 이 판정은 **추정**이고, 링크가 실은 살아 있었다면
  //   재시도에 `ALREADY CONNECTED` 가 와서 그대로 복귀한다 — 그때 캐시를 비웠다면
  //   **살아 있는 연결의 멱등성이 깨진다.**
  //   캐시는 실제 연결 생명주기 신호인 `CLOSED` / `CONNECT` 에서만 비운다.

  // 🔴 **`CIPSTART` 가 아니라 `CIPCLOSE` 부터** 간다.
  //   `CIPSTART` 는 ESP 의 낡은 소켓을 절대 못 지운다 — `ALREADY CONNECTED` 만 돌아온다.
  //   닫히면 ESP 가 `CLOSED` 를 내고 기존 경로가 그것을 처리한다
  //   (오프라인 확정 + 캐시 비움 + `CIPSTART` 재시도) → **정상 생명주기 신호에 다시 올라탄다.**
  staleSocket = true;
  netStep = NET_CIPCLOSE;
  netStepAt = millis();
  netStepWait = 0;                   // 즉시 닫기를 시도한다
}

static void goOffline(void) {
#if DEBUG
  Serial.print(F("[NET] 전송 "));
  Serial.print(sendFailStreak);
  Serial.println(F("회 연속 실패 → 오프라인 전환. 낡은 소켓부터 닫는다(CIPCLOSE)"));
#endif
  startSocketRecovery();
}

// 실패를 세는 곳은 **sendLine() 한 곳뿐이다.** 수신된 오류 문구로도 세면 한 번의 실패가
// 두 번 계수되어(sendLine 이 false + 오류 줄 도착) N 이 사실상 절반이 된다. 그래서 아래
// handleLine() 의 오류 문구 처리는 **세지 않고 로그만** 남긴다(link is not valid 는 예외).
static void noteSendResult(bool ok) {
  if (ok) { sendFailStreak = 0; return; }
  if (sendFailStreak < 255) sendFailStreak++;
#if DEBUG
  Serial.print(F("[NET] 전송 실패 "));
  Serial.print(sendFailStreak);
  Serial.print('/');
  Serial.println(SEND_FAIL_LIMIT);
#endif
  if (sendFailStreak >= SEND_FAIL_LIMIT) goOffline();
}

#include "EspLink_tx.h"   // ← EspLink 링크 계층. **위치를 옮기지 마라**

