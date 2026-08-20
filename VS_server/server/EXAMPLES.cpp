// EXAMPLES.cpp — **컴파일되는 사용 예제.** 개념 하나에 함수 하나.
// ═══════════════════════════════════════════════════════════════════════════
// 🔴 **왜 주석이 아니라 코드인가**
//   주석 안의 예시는 **아무 검사도 안 받는다.** 컴파일도 안 되고 시험도 없다.
//   그래서 코드가 바뀌면 **조용히 낡고**, 기여자는 그것을 정답으로 베낀다.
//   실제로 `parking.h` 의 헤더 예시가 그렇게 낡아 있었다(없어진 API 를 쓰고 있었다).
//
//   → 이 파일은 **컴파일된다.** API 가 바뀌면 **여기서 빨강이 난다.**
//     그것이 예제를 낡지 않게 하는 유일한 장치다.
//   검사 : `sh net/sync_check.sh` 가 **두 트리의 `parking.h` 로** 이 파일을 문법 검사한다.
//
// ⚠ **여기 있는 것은 "이렇게 쓴다" 이지 "지금 보드에서 돈다" 가 아니다.**
//   없는 하드웨어를 흉내 내지 않는다 — 컴파일만 보장한다.
// ⚠ **아무도 이 함수들을 부르지 않는다.** 실제로 도는 것은 `lot.cpp` 뿐이다.
//   여기서 골라 `lot.cpp` 로 옮겨 붙여라.
// ═══════════════════════════════════════════════════════════════════════════
#include "parking.h"

// ── ① 가장 작은 주차장 ─────────────────────────────────────────────────
//   배우는 것은 다섯뿐이다 : spot · at · parking · label · module
void example_minimal(ParkingLot& lot) {
    lot.spot("A1")                    // 자리 하나
        .at(0, 0)                     // 화면 격자 (행,열) — 안 쓰면 선언 순서대로 놓인다
        .parking()                    // 🔴 주차영역. 안 적으면 일반영역(점유·예약 없음)
        .label("1번 자리")             // 화면 이름 — 안 쓰면 자리 id 가 뜬다
        .module("P1", "A1");          // 모듈. 센서인지 명령인지는 **장치가 등록에서 말한다**
}

// ── ② 자리만 잡아 두기 (모듈 없음) ──────────────────────────────────────
//   🔑 정상이다. 서버가 `active:{ok:false,reason:"no_modules"}` 를 실어 주고
//     화면이 **비활성**으로 그린다. 점유는 여전히 `unknown` 이다 — 무지를 지우는 것이 아니라
//     **그 이유를 보이게** 하는 것이다.
void example_empty_spot(ParkingLot& lot) {
    lot.spot("A2").at(0, 1).parking().label("2번 자리");
}

// ── ③ 일반영역 ─────────────────────────────────────────────────────────
//   `parking()` 을 **안 적으면** 일반영역이다. 점유를 보고할 의무가 없으므로
//   모듈이 0개여도 비활성이 아니다.
void example_area(ParkingLot& lot) {
    lot.spot("E1").at(0, 2).label("입구");
}

// ── ④ 센서 이중화 + 내 판정 ─────────────────────────────────────────────
//   기본은 OR(하나라도 차면 찼다). 두 오류의 대가가 다르기 때문이다 —
//   빈 자리를 "찼다"고 하면 손해는 자리 하나이고, **찬 자리를 "비었다"고 하면 운전자가 못 댄다.**
struct BothOccupied : SpotBehavior {
    virtual bool occupied(const std::vector<SensorReading>& s) const {
        int known = 0, ones = 0;
        for (size_t i = 0; i < s.size(); i++)
            if (s[i].known) { known++; if (s[i].value) ones++; }   // 모르는 센서는 안 센다
        return known > 0 && ones == known;
    }
};
static BothOccupied g_bothOccupied;   // 🔴 서버보다 오래 살아야 한다(static/전역)

void example_behavior(ParkingLot& lot) {
    lot.spot("A3").at(0, 3).parking()
        .module("P1", "A3").module("P1", "B3")
        .behavior(g_bothOccupied);
    // ⚠ `SensorReading` 은 `known` 과 `value` 가 **따로다.** `known == false` 는
    //   "아직 값을 못 받았다" 이지 "비었다" 가 아니다. 한 `bool` 로 합치면 모름이 거짓으로 무너진다.
}

// ── ⑤ 화면 조작 선언 — 위젯은 셋뿐이다 ───────────────────────────────────
//   **뜻을 아는 사람이 선언한다.** 화면은 입력 형태만 알고 뜻은 라벨로만 받는다.
void example_controls(ParkingLot& lot) {
    lot.control("P1", "LD").toggle();                 // 0 / 1
    lot.control("P1", "L2").number(0, 9999999);       // 숫자 입력 칸 (범위는 서버가 판정한다)
    lot.control("P1", "DR").choice()                  // 🔑 이 선언이 곧 명령표다
        .option(1, "열기")
        .option(2, "닫기")
        .option(3, "잠금");
    // 🔑 이름의 정본은 여기 하나다. 조작 못 하는 모듈에도 붙는다.
    lot.label("P1", "A1", "왼쪽 센서");
}

// ── ⑥ 명령 내기 — 한 박자마다 불리는 자리에서 ────────────────────────────
//   🔴 조립 시점에는 장치가 아직 안 붙어 있다. **그래서 명령은 여기서 낸다.**
void example_send(ParkingServer& srv) {
    if (!srv.deviceReady("P1")) return;               // 안 붙었으면 아무것도 안 한다

    srv.send("P1", "LD", 1);                          // 단건. true 는 "큐에 넣었다" 이지 "됐다" 가 아니다

    static long long last = 0;                        // 주기 동작은 단조 시계로
    if (srv.nowMs() - last >= 10000) {
        last = srv.nowMs();
        ParkingServer::Batch b = srv.batch("P1");     // 묶음 — 한 창에 같이 나간다
        b.add("LD", 1).add("L2", 7654321);
        ParkingServer::BatchResult r = b.send();      // 상한은 srv.maxPerBatch() (지금 4)
        (void)r;                                      // 🔴 넘기면 한 건도 안 보내고 거절한다
    }
}

// ── ⑦ 명령 결과 — 갈래가 셋이고 고치는 곳이 다르다 ────────────────────────
//   성공     : 장치가 받았고 콜백이 true 를 냈다
//   거절     : 장치가 **답했고** 거절했다 → 값·등록을 고쳐라. **재시도는 뜻이 없다**
//   무응답   : 🔴 답이 없다(재전송 소진) → **장치·링크 문제다.** 콜백 로직을 고쳐도 안 낫는다
void example_result(const CmdResult& r) {
    std::cout << "[명령] " << r.module << " " << r.value << " → " << r.kindName() << "\n";
}
