// parking.h — 🔴 **공개 조립 API.** 이 파일만 읽으면 주차장을 만들 수 있어야 한다
//
// 사용자 요구: *"복잡한 구조는 은닉화하여 간단한 구조로 이용 가능해야 한다"*
//                        *"코드 작성 시점에 해당 코드의 **흐름이 보인다**"*
//
// 그래서 이 헤더의 판정은 둘이다:
//   ① `.cpp` 를 열지 않고 쓸 수 있는가
//   ② 사용 코드를 위에서 아래로 읽으면 **무슨 일이 어떤 순서로** 일어나는지 보이는가
//
// 🔑 무엇을 밖에 두고 무엇을 안에 넣는지의 판별자: **호출자가 그 순서를 바꿀 수 있는가.**
//   바꿀 수 있다(언제 포트를 열지 · 어떻게 루프를 돌지 · 무엇을 어디에 배치할지) → **밖**
//   바꿀 수 없다(한 박자 안의 고정 순서 · 등록 결속 · 재전송·격리) → **안**
//
// ── 쓰는 법 ──────────────────────────────────────────────────────────────
//
//   void buildLot(ParkingLot& lot) {              // ← `lot.cpp` 에 이 함수만 채운다
//       lot.spot("A1").at(0,0).parking().label("1번 자리")
//             .module("P1","A1").module("P1","B1");
//   }
//
// 🔴 **예제는 여기 없다. `EXAMPLES.cpp` 에 있다** — 개념 하나에 함수 하나로,
//   **컴파일되는 코드**로 들어 있다(자리·모듈 없는 자리·일반영역·내 판정·조작 선언·명령·콜백).
//   🔑 **주석 안의 예시는 아무 검사도 안 받아서 조용히 낡는다.** 이 헤더의 예시가 실제로
//     그렇게 낡았었다(없어진 API 를 쓰고 있었다). 그래서 예제를 **컴파일되는 자리로 옮겼다.**
//
// 🔴 **`main()` 은 여기 없다.** 포트를 열고 도는 것은 엔진(`entry.h`)이 한다 —
//   그 순서는 호출자가 못 바꾸므로 드러내 봐야 **지킬 의무만** 생긴다.
//
// ⚠ 조립에는 **순서 요구가 없다** — 자리를 어느 쪽부터 적어도 같다.
#ifndef PARKING_H
#define PARKING_H

#include <string>
#include <vector>
#include <cstddef>
#include <map>
#include <iostream>   // 🔑 기여자가 `std::cout` 을 바로 쓸 수 있게 — include 를 둘로 만들지 않는다
#include "spot.h"      // SpotBehavior — 자리의 동작 방식(기여자가 구현한다)
#include "cmdresult.h" // CmdResult — 명령 결과를 나중에 받는다
#include "control.h"   // ControlDecl — 화면에 그릴 조작 UI 를 기여자가 선언한다

class ParkingLot;

// 🔴 **`<windows.h>` 대비.** 그것은 `IN`·`OUT` 같은 짧은 이름을 **빈 매크로**로 정의하고,
//   그러면 우리 이름이 전처리 단계에서 조용히 지워진다(`enum { IN, OUT }` → `enum { , }`).
//   비용은 두 줄이고, 없앴을 때의 손실은 **다른 플랫폼에서만 나는 빌드 실패**다.
// 🔑 재현은 그 플랫폼 없이도 된다 — `c++ -fsyntax-only -DIN= -DOUT= parking.h`.
#ifdef IN
#undef IN
#endif
#ifdef OUT
#undef OUT
#endif

// 자리 하나. **기여자가 배우는 것은 다섯이다** — `spot` · `at` · `parking` · `label` · `module`.
class Spot {
public:
    // 🔴 **모듈을 붙인다.** 센서인지 명령인지 **여기서 안 가른다.**
    //
    //   ```
    //   lot.spot("A1").module("P1","A1").module("P1","LD");
    //   ```
    // 🔑 **가르지 않는 이유**: **장치가 `kind` 첫 글자로 이미 말한다**(`I` 관측 · `O` 명령).
    //   서버가 등록(`D`)을 받고 그것으로 가른다. **조립 표에서 또 가를 이유가 없다.**
    // ⚠ **딸린 결과**: 등록 전에는 **무엇이 센서인지 모른다.** 그래서 그때는 센서 수가 0 이다 —
    //   **그게 정직한 답이다.** 선언만 보고 세면 장치가 다른 것을 보고해도 안 바뀐다.
    Spot& module(const std::string& devid, const std::string& name);
    // 장치가 하나뿐이면 `devid` 를 빼도 된다 — "아무 장치나 그 이름을 가진 것".
    Spot& module(const std::string& name);

    // 🔴 **이 자리를 주차영역으로 만든다.** 안 부르면 **일반영역**이다.
    //
    //   `parking()` 이 **유일하게 엔진 동작을 바꾼다**:
    //     ① 점유 계산에 든다   ② 예약 대상이다   ③ 옛 격자(폴백)에 든다
    //   그 밖에는 **엔진이 자리가 무엇인지 몰라도 된다**(사용자 확정).
    //
    // ⚠ **기본값이 "일반영역"인 이유**: 안 적었을 때 **점유·예약이 안 생기는 쪽**이
    //   조용히 틀리지 않는다. 반대로 두면 입구가 *"비어 있는 주차 자리"* 로 화면에 뜬다.
    //   > **틀렸을 때 조용한 쪽을 기본값으로 두지 마라.**
    Spot& parking();

    // 화면에 그릴 격자 위치(0-기준 행·열). 🔑 **사용자 인식용이고 엔진은 안 쓴다.**
    // ⚠ 안 부르면 **선언 순서대로 자동 배치**된다.
    // ⚠ 겹치면 서버가 **기동 로그에 두 자리 id 를 지목해 말한다. 막지는 않는다** —
    //   겹침은 표시 문제이고 서버 동작은 멀쩡하다.
    Spot& at(int row, int col);

    // 이 자리의 표시 이름. 안 부르면 화면이 **자리 id** 를 쓴다.
    Spot& label(const std::string& text);

    // ── 심화(안 써도 된다) ─────────────────────────────────────────────
    // 🔑 **위 다섯에 안 든다.** 기본 판정으로 충분하면 건너뛰어라.
    //   이 자리의 점유 판정을 갈아끼운다. 안 부르면 **기본(OR)** 이 쓰인다.
    // ⚠ **참조를 든다. 사본이 아니다** — `static` 이나 전역으로 둬라.
    Spot& behavior(SpotBehavior& b);

private:
    friend class ParkingLot;
    Spot(ParkingLot* lot, std::size_t idx) : lot_(lot), idx_(idx) {}
    ParkingLot* lot_;
    std::size_t idx_;
};

// 🔴🔴 **모듈 하나의 조작 UI 를 선언한다** — 화면이 이것을 보고 그린다.
//
//   ```
//   lot.label("P1","LD","안내등");                     // 🔑 이름은 label 이 정한다
//   lot.control("P1","LD").toggle();                   // 0 / 1
//   lot.control("P1","LC").number(0, 9999999);         // 숫자 칸
//   lot.control("P1","DR").choice()                    // 🔑 **이것이 명령표다**
//           .option(1,"열기").option(2,"닫기").option(3,"잠금").option(4,"해제");
//   ```
//   🔑 **화면은 값의 뜻을 모른다.** 라벨을 그리고 값을 보낼 뿐이다 —
//     그래서 `kind` 가 늘어도 화면을 안 고친다.
//   🔴 **선언 안 한 모듈은 조작 UI 가 없다.** 표시는 그대로 된다.
//   ⚠ 모듈 종류를 안 가린다 — 센서(`IP`)에도 선언할 수 있다.
class Control {
public:
    // ⚠ **이름은 안 받는다.** `lot.label(devid, module, "…")` 이 이름의 정본이다.
    Control& toggle();
    Control& number(long vmin, long vmax);
    Control& choice();
    // ⚠ `{{1,"열기"},…}` 대신 **연쇄**로 둔 이유: 초기화 리스트는 중괄호를 틀리기 쉽고
    //   오류 문구가 길다. **`module().module()` 과 같은 모양**이 기여자에게 낫다.
    Control& option(long value, const std::string& label);
private:
    friend class ParkingLot;
    Control(ParkingLot* lot, std::size_t idx) : lot_(lot), idx_(idx) {}
    ParkingLot* lot_;
    std::size_t idx_;
};

// 주차장 한 곳. **지형을 선언하는 것이 유일한 일**이다.
class ParkingLot {
public:
    Spot spot(const std::string& id);                    // 주차 자리를 만든다
    // 🔴 `gate(id, ...)` 는 없다 —
    //   입구/출구는 `spot(...).label("입구")` 로 만든다. **자리 종류는 `parking()` 하나로 갈린다.**

    // 🔴 조작 UI 선언 (위 `Control` 주석 참조). **같은 (devid,name) 을 다시 부르면 덮어쓴다** —
    //   두 벌이 생기면 화면에 버튼이 둘 뜨고 어느 것이 참인지 아무도 모른다.
    Control control(const std::string& devid, const std::string& name);

    // 🔴🔴 **모듈의 표시 이름**
    //   `lot.label("P1", "LD", "안내등");`
    //
    // 🔑 **모듈 종류를 안 가린다 — 센서에도 붙는다.** 그게 이 함수가 생긴 이유다:
    //   전에는 이름을 줄 데가 `control` 뿐이었고 **`control` 은 조작 가능한 것에만 붙는다.**
    //   그래서 센서의 표시 이름을 **`kind` 의 둘째 글자**(`IP`=주차확인센서)가 메우고 있었다 —
    //   🔴 **정해진 다섯 중에 고르게 하는 구조**였고, 기여자가 자기 이름을 못 붙였다.
    //
    // ⚠ **이름의 정본은 여기 하나다.** `control` 은 위젯만 받는다 —
    //   같은 모듈에 이름이 둘이면 **우선순위 규칙**이 생기고, 규칙이 생기면 사람이 틀린다.
    //   (`choice` 의 `.option(1,"열기")` 는 **버튼 글자**라 다른 것이다.)
    //
    // 안 부르면 화면이 **모듈 이름(2바이트)이나 자기 폴백 표**를 쓴다. 안 써도 된다.
    void label(const std::string& devid, const std::string& module, const std::string& text);

    // 서버가 읽는다. **호출자가 쓸 일은 없다** — 다만 감출 이유도 없다(상태는 드러낸다).
    // 자리에 붙은 모듈 하나의 **선언**. (실제로 붙었는지는 등록이 정한다)
    struct Attach {
        std::string devid;      // "" = **아무 장치나** (1인자 형태)
        std::string name;       // 전선 모듈 이름
        // 🔴 센서/명령을 **여기서 안 가른다** — **장치가 `kind` 첫 글자로 말한다**
        //   (`I` 관측 · `O` 명령). 선언에서 또 가르면 두 곳이 갈릴 수 있다.
        //   ⚠ 그래서 **등록 전에는 이 모듈이 센서인지 모른다.** 그게 정직한 상태다.
        Attach() {}
        Attach(const std::string& d, const std::string& n) : devid(d), name(n) {}
    };
    struct Area {
        std::string id;
        // 🔴 **`"parking"` 아니면 `"area"` 둘뿐**이다.
        std::string kind;
        int row, col;                         // 🔑 `at()` 이 준 격자 위치. **-1 = 자동 배치**
        // ⚠ 이름이 `sensors` 였다. **센서만 담지 않게 됐으므로 바꿨다** —
        //   담는 것이 바뀌었는데 이름이 그대로면 다음 사람이 센서만 있다고 읽는다.
        std::vector<Attach> modules;
        std::string label;                    // 🔑 비면 화면이 `id` 를 쓴다
        // 🔑 `0` 이면 **서버의 기본 판정**을 쓴다. 자리마다 다른 것을 꽂을 수 있다.
        //   ⚠ 소유하지 않는다 — 호출자가 준 것이 살아 있어야 한다(위 `behavior()` 주석).
        SpotBehavior* behavior;
        Area() : row(-1), col(-1), behavior(0) {}
    };
    const std::vector<Area>& areas() const { return areas_; }
    const std::vector<ControlDecl>& controls() const { return controls_; }
    // 키: `devid\tmodule` → 표시 이름. **없으면 비어 있다**(존재/부재 규칙).
    const std::map<std::string, std::string>& labels() const { return labels_; }
    bool empty() const { return areas_.empty(); }

private:
    friend class Spot;
    friend class Control;
    std::vector<Area>        areas_;
    std::vector<ControlDecl> controls_;
    std::map<std::string, std::string> labels_;
};

// 서버. 🔴 **복잡함은 전부 이 뒤에 있다** — 헤더에 자료구조가 하나도 안 나온다.
class ParkingServer {
public:
    explicit ParkingServer(const ParkingLot& lot);
    ~ParkingServer();

    // 🔴 **모듈에 값을 보낸다**
    //   `srv.send("P1", "LCD1", 1234567);`   ← 7자리 숫자
    //   `srv.send("P1", "LED1", 1);`         ← on/off
    //   `srv.send("P1", "DOOR", 2);`         ← 동작 — **뜻은 기여자가 정한 표**
    //
    // 🔑 **`value` 의 뜻을 서버는 모른다.** 장치의 콜백이 그것을 해석한다.
    //   **그 표를 양쪽 주석에 적어라** — 어긋나면 **조용히 다른 동작을 한다.**
    // ⚠ 반환 `true` 는 **"전선 큐에 넣었다"** 이지 "수행됐다"가 아니다.
    //   실제 수행은 장치 ACK 과 다음 상태 프레임이 답한다.
    bool send(const std::string& devid, const std::string& moduleName, long value);

    // ── 묶음 하행 ───────────────────────────────────────────────────────────
    //
    //   하행은 **슬롯당 한 창**(1.2초)에만 나간다. 낱개로 N건이면 **N × 1.2초**다.
    //   묶으면 **하행 1슬롯 + ACK 1슬롯 ≈ 2.4초** 로 끝난다.
    //
    //   ```
    //   ParkingServer::Batch b = srv.batch("P1");
    //   b.add("LD", 1).add("LC", 1234567).add("DR", 2);
    //   ParkingServer::BatchResult r = b.send();
    //   ```
    //
    // 🔴 **원자적이 아니다. 독립이다.** 각 명령이 자기 rid 와 자기 ACK 을 갖는다 —
    //   3건 중 2번이 거절돼도 1·3번은 그대로 수행된다.
    //   **되돌릴 수단이 없으므로 "전부 아니면 전무"를 약속하지 않는다.**
    //   🔑 못 지키는 보장을 API 가 약속하면 **그 약속을 믿고 쓴 코드가 조용히 틀린다.**
    struct BatchResult {
        int queued;      // 전선 큐에 들어간 건수. ⚠ **"보냈다"이지 "됐다"가 아니다**
        int rejected;    // 보내기 전에 거절된 건수(모듈 없음 · 이름 틀림 · 상한 초과 · rid 고갈)
        BatchResult() : queued(0), rejected(0) {}
    };
    class Batch {
    public:
        Batch& add(const std::string& moduleName, long value);
        BatchResult send();          // 🔑 부를 때까지 아무것도 안 나간다
    private:
        friend class ParkingServer;
        Batch(ParkingServer* s, const std::string& d) : srv_(s), devid_(d) {}
        ParkingServer* srv_;
        std::string devid_;
        std::vector<std::pair<std::string, long> > items_;
    };
    Batch batch(const std::string& devid) { return Batch(this, devid); }

    // ── 명령 결과를 나중에 받는다 (콜백) ───────────────────────────────────
    //
    //   `send()` 는 **큐에 넣었다**까지만 말한다. 실제 결과는 장치가 자기 주기에 실어 보내고,
    //   도착할 때 이 함수가 불린다.
    //
    //   ```
    //   static void onResult(const CmdResult& r) {
    //       // r.kindName() : "성공" | "거절" | "무응답"
    //   }
    //   srv.onCommandResult(onResult);
    //   ```
    //
    // 🔴 **갈래 셋을 꼭 갈라 읽어라 — 고치는 곳이 다르다:**
    //   `OK`        장치가 받았고 콜백이 `true` 를 냈다
    //               ⚠ **물리적으로 그렇게 됐다가 아니다.** 차단봉이 걸려도 `true` 가 온다
    //   `REJECTED`  장치가 **답했고** 거절했다 → 값이나 등록을 고쳐라. 재시도는 뜻이 없다
    //   `NO_ANSWER` 🔴 **답이 없다** → 장치·링크 문제다. **콜백 로직을 고쳐도 안 낫는다**
    //
    // ⚠ 이 함수 안에서 오래 걸리는 일을 하지 마라 — 서버의 한 박자 안에서 불린다.
    void onCommandResult(CmdResultFn fn);

    // 🔑 **장치가 붙어서 등록까지 마쳤나.** 명령을 내기 전에 이것부터 봐라 —
    //   안 붙었으면 `send()` 가 `false` 를 내고 로그에 *"노드 `P1` 를 모른다"* 가 찍힌다.
    //   ⚠ **접속만으로는 부족하다.** 등록(`D`)이 끝나야 모듈 이름을 풀 수 있다.
    bool deviceReady(const std::string& devid) const;

    // 🔑 **단조 시계(ms).** `onTick` 에서 주기 동작을 쓰려면 이것이 필요하다 —
    //   `onTick` 은 매 박자 불리므로 "언제 할 것인가"는 호출자가 정해야 한다.
    //
    //   ```
    //   static long long last = 0;
    //   if (srv.nowMs() - last >= 10000) { last = srv.nowMs(); … }
    //   ```
    // ⚠ **벽시계가 아니다.** 기준점 없이 절대 시각으로 읽지 마라 — 구간만 뜻이 있다.
    long long nowMs() const;

    // 한 번에 묶을 수 있는 최대 건수. 🔴 **상수가 아니다** — 손잡이(`--down-cap`)를 따라간다.
    int maxPerBatch() const;

    bool openPorts();        // 포트를 연다. 실패하면 false (이유는 로그에 남는다)
    bool serveOneTick();     // 한 박자: 수신 → 자리 판정 → 하행 송신 → 화면 방송
    void closeDown();        // 요약을 남기고 닫는다

private:
    ParkingServer(const ParkingServer&);              // 복사 금지 — 소켓을 들고 있다
    ParkingServer& operator=(const ParkingServer&);
    struct Impl;
    Impl* p_;
};

// ═══════════════════════════════════════════════════════════════════════════
// 🔴🔴 **기여자가 구현하는 훅 셋** — 선언은 여기, 정의는 `lot.cpp` 에 있다
//
//   이 선언이 없으면 `server.cpp` 가 `#include "lot.cpp"` 로 소스를 통째로 끌어와야 했고,
//   그러면 **번역 단위가 하나뿐**이라 `.cpp` 를 여럿 컴파일하는 도구(Visual Studio)에서
//   `lot.cpp` 가 **두 번 컴파일되어 링크가 깨진다.**
//
//   ✅ macOS 에서 그 오류를 그대로 재현했다(Windows 없이):
//       c++ -c lot.cpp && c++ -c server.cpp && c++ lot.o server.o
//       → duplicate symbol 'onTick(ParkingServer&)' · 'buildLot' · 'onCmdResult'
//   🔑 **셋뿐이었다.** 나머지 헤더는 `static`·`inline` 이라 링크가 안 깨진다 —
//     그래서 *"h 에 모두 정의가 있어서"* 가 아니라 **이 셋이 원인**이었다.
//
// ⚠ **비워 둬도 된다.** `onTick`·`onCmdResult` 는 아무것도 안 해도 서버가 돈다.
//   다만 **정의는 있어야 한다** — 선언만 있고 정의가 없으면 링크에서 "미해결 외부 기호"다.
// ═══════════════════════════════════════════════════════════════════════════
void buildLot(ParkingLot& lot);              // ① 주차장을 조립한다
void onTick(ParkingServer& srv);             // ② 한 박자마다 — 명령을 여기서 낸다
void onCmdResult(const CmdResult& r);        // ③ 명령 결과 — 성공 / 거절 / 무응답

#endif  // PARKING_H
