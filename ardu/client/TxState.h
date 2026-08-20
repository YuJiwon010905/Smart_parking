#pragma once
// ═════════════════════════════════════════════════════════════════════════
// TxState.h — 송신 상태 · 슬롯 상태
//   ⚙ 응용 성질 — 잠금 대상이 아니다
// ═════════════════════════════════════════════════════════════════════════
// 🔴🔴 **위치를 옮기지 마라.** `client.ino` 의 **정해진 자리에서** `#include` 된다.
//   헤더 순서가 바뀌면 선언·초기화 순서가 같이 바뀌어 **산출물이 달라진다.**

// ─────────────────────────────────────────────────────────────────────────
// 송신 상태
// ─────────────────────────────────────────────────────────────────────────
static uint16_t      seqNo = 0;                 // §2.4 uint16 순환. 재부팅하면 0
static unsigned long lastStatusAt = 0;          // §3.4 타이머는 하나
static unsigned long lastSendEndAt = 0;
static uint16_t      sentOcc = 0xFFFF, sentRes = 0xFFFF;  // 아직 아무것도 안 보냈다는 표시
static bool          changePending = false;
static unsigned long changeAt = 0;

// tmask 도 변화 감지에 넣는다. 실제 tmask 는 10비트뿐이라 0xFFFF 를
// "필드 없음(=해제)" 표식으로 쓸 수 있다 — 어떤 실제 값과도 겹치지 않는다.
static const uint16_t TMASK_ABSENT = 0xFFFF;
static uint16_t       sentTmask = 0xFFFE;   // 실제 값·ABSENT 어느 쪽과도 다른 초기값

// ─────────────────────────────────────────────────────────────────────────
// 슬롯 상태
// ─────────────────────────────────────────────────────────────────────────
// 🔴 `slotStart` 는 **더해서** 전진시킨다(`+= SLOT_MS`). `= now` 로 하면 매 슬롯 오차가
//   누적돼 **주기가 스스로 늘어난다** — 1.000s 로 설계한 것이 실측 1.113s 가 된다.
static uint32_t slotStart   = 0;
static uint32_t slotNo      = 0;
static bool     slotSent    = false;   // 이번 슬롯에서 이미 보냈다(슬롯당 정확히 1거래)
static uint16_t slotOow     = 0;       // 수신 창 **밖**에 하행이 도착한 수 — 설계 위반 계수
static uint16_t slotMissed  = 0;       // 송신 창을 통째로 놓친 슬롯 수(보낼 기회를 못 씀)

#include "EspLink_state.h"   // ← EspLink 링크 계층 (REQ-0264). **위치를 옮기지 마라**
