// zone.h — 주차 영역과 자리 상태 — **노드와 개별 구조다. 연결되지 않는다**(사용자 확정) (2026-08-19 · REQ-0272 2단계)
//
// 🔴 **`server.cpp` 가 원래 자리에서 `#include` 한다.** 전처리 결과가 같아야
//   산출물(`.o`)이 **0 차이**이고, 그래야 "떼어 냈지만 아무것도 안 바꿨다"가 값으로 증명된다.
//   ⚠ **`#include` 위치를 옮기는 순간 그것은 분리가 아니라 변경이다.**
//
// ⚠ 헤더 가드를 안 넣었다 — **한 번만 include 되는 조각**이고, 가드를 넣으면
//   전처리 결과가 달라져 이 단계의 증명이 깨진다. 다음 단계(진짜 헤더화)에서 넣는다.

// ---------------------------------------------------------------- 상태
// 🔴🔴 **`Zone` — 주소를 갖는 장소 하나**(설계 §0·§1 · REQ-0203 4a)
// ⚠ **`Node`(통신 단위)와 다른 것이다.** 자리는 여러 노드에 걸칠 수 있고, 한 노드가 여러 자리를 가질 수 있다.
// 🔑 **`cells` 를 목록으로 둔다** — 지금은 **항상 길이 1**이고 그것을 코드가 강제한다.
//   좌표를 필드에 하나 박으면 큰 자리가 생길 때 **자료형이 바뀌어 읽는 모든 곳이 같이 바뀐다.**
//   목록이면 **길이 제한만 푼다.** ⚠ **목록으로 두는 것과 아무 길이나 받는 것은 다르다** —
//   후자는 **검증되지 않은 경로를 여는 것**이라 지금 막는다.
struct Zone {
    std::string id;                                     // 전역 고유 · **좌표에서 유도하지 않는다**
    std::string kind;                                   // parking | entrance | exit
    std::vector<std::pair<int,int> > cells;             // (row, col) · **지금은 길이 1**
    std::vector<std::pair<std::string,std::string> > modules;  // 🔴 **(devid, name)** — name 만 쓰면 안 된다
};

struct Slot {
    int occupied;            // 아두이노가 진실값을 준다
    int reserved;            // 서버가 durable owner (§7.4)
    std::string user_id;     // 빈 문자열 = null
    long long reserved_at;   // 0 = null
    Slot() : occupied(0), reserved(0), reserved_at(0) {}
};
