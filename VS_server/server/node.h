// node.h — 통신 단위 — ESP 하나 = 소켓 하나. **주차 자리가 아니다** (2026-08-19 · REQ-0272 2단계)
//
// 🔴 **`server.cpp` 가 원래 자리에서 `#include` 한다.** 전처리 결과가 같아야
//   산출물(`.o`)이 **0 차이**이고, 그래야 "떼어 냈지만 아무것도 안 바꿨다"가 값으로 증명된다.
//   ⚠ **`#include` 위치를 옮기는 순간 그것은 분리가 아니라 변경이다.**
//
// ⚠ 헤더 가드를 안 넣었다 — **한 번만 include 되는 조각**이고, 가드를 넣으면
//   전처리 결과가 달라져 이 단계의 증명이 깨진다. 다음 단계(진짜 헤더화)에서 넣는다.

// ⚠ **낱말**: 여기의 `Node` 는 통신 단위(ESP 하나 = 소켓 하나 = 자기 1.2초 주기)다.
//   **주차 자리가 아니다.** 자리는 `Zone`(§0 낱말표).
struct Node {
    // ⚠ **필드 이름을 `AuxNode` 쪽에 맞췄다**(REQ-0203 2단계). 반대로 하면 보조 노드
    //   호출부 20여 곳을 고쳐야 하는데, 이쪽은 **별칭 7줄만** 바꾸면 된다.
    //   🔑 **적게 고치는 쪽을 고른 것이 아니라, 고치는 자리가 내가 방금 만든 자리인 쪽**을 골랐다.
    std::string devid;          // 전선의 장치 id. "" = 아직 미정(first-S-wins)
    sock_t      fd;             // 이 노드의 연결
    std::string peer;           // "IP:포트" — `devid` 가 고유하지 않을 때 유일한 구분자(REQ-0215)
    std::string buf;            // 수신 조립 버퍼
    bool        seen;           // 유효 프레임을 한 번이라도 받았나
    long long   last_ms;        // 마지막 **유효** 프레임 수신 시각(단조 시계) — 유휴 마감의 기준
    long long   last_epoch_ms;  // 같은 사건의 벽시계 — **로그 대조용이지 계산용이 아니다**

    // ── `AuxNode` 에서 흡수한 것들 (REQ-0203 2단계) ─────────────────────────────
    // 🔑 **보조 노드는 처음부터 `Node` 의 부분집합이었다.** 두 구조체를 유지하면
    //    "노드마다 있는 상태"가 두 곳에 나뉘어 **다음 단계(`map<devid,Node>`)에서 합칠 수 없다.**
    long long   connected_ms;     // 접속 시각
    long long   frames;           // 누적 유효 프레임
    long long   drops;            // 버린 줄
    bool        online;           // §3.4 엣지 판정용 직전 상태
    int         offline_episodes;

    // ── 등록(§5 `D`/`Q`) ────────────────────────────────────────────────────────
    // 🔴 **이 단계에서 등록은 *관측*이지 제어가 아니다.** 하행 경로를 한 줄도 안 바꾼다 —
    //    지금 하행(`R`·`C`·`T`·`M`)은 **자리(슬롯 번호)** 로 주소를 정하므로 모듈 구성과 무관하다.
    //    **모듈 신원 기반 명령이 생길 때** 비로소 이 상태가 제어에 쓰인다(설계 §5).
    // ⚠ 그래서 옛 펌웨어(등록을 모르는 노드)도 **종전 그대로 동작한다.** 분기를 안 만든다.
    int         reg_n;          // 선언된 모듈 수. -1 = `D,*` 를 아직 못 받았다
    int         reg_drain;      // 선언된 슬롯당 ACK 배출 하한. -1 = 미선언
    bool        reg_done;       // `reg_n` 개를 다 받았다
    bool        reg_giveup;     // `Q` 3회에도 안 와서 굳혔다(node_unregistered)
    long long   reg_first_ms;   // 승격(첫 `S`) 시각 — `REG_TIMEOUT` 의 기준
    int         q_sent;         // 보낸 `Q` 수
    long long   last_q_ms;      // 마지막 `Q` — 슬롯당 1회로 묶는다
    std::vector<std::pair<std::string, std::string> > mods;   // (name, kind) · **순서가 곧 idx**

    // ── 🔴 ②-c — **자리 비트열은 노드의 것이다** (REQ-0262/0263 · 2026-08-19)
    //   전에는 `Server` 에 한 벌뿐이었다. 그래서 보조 노드의 모듈은 값 경로가 없었고
    //   `state_json()` 이 그것을 정직하게 `known:false` 로 냈다(설계 §8.9).
    //   🔴 **이 필드가 `Node` 로 오기 전에 보조 노드의 줄을 파서에 넣으면**
    //     그 `S` 가 **주 노드의 비트열을 덮는다.** 그래서 ②-c 가 ②-b 보다 먼저다.
    int         mod_bits[REG_MODS_MAX];   // 자리 비트열(자리 10칸을 넘는 비트 포함)
    int         mod_bits_n;               // 해독한 비트 수. **0 = 안 읽었다**(모른다이지 0 이 아니다)
    // ── 🔴 `V` 프레임이 싣는 **센서가 잰 값**. 비트와 **다른 축**이다
    //   `mod_val_has` 가 false 면 *"못 쟀다"* 다 — 🔴 **`0` 으로 접지 마라.**
    //   ⚠ 그리고 `mod_bits` 의 `known`(= 폭이 닿았나)과도 **다른 것**이다.
    //     폭은 전송 계층이고 이것은 측정 계층이다. 오늘 그 둘을 겸한 이름이 결함을 숨겼다.
    long        mod_val[REG_MODS_MAX];
    bool        mod_val_has[REG_MODS_MAX];
    long long   mod_val_ms[REG_MODS_MAX];  // 받은 시각. 화면에 `age_ms` 로 나간다

    Node() : fd(BAD_SOCK), seen(false), last_ms(0), last_epoch_ms(0),
             connected_ms(0), frames(0), drops(0), online(false), offline_episodes(0),
             reg_n(-1), reg_drain(-1), reg_done(false), reg_giveup(false),
             reg_first_ms(0), q_sent(0), last_q_ms(0), mod_bits_n(0) {
        // 🔑 값은 **없음**이 기본이다. `0` 이 아니라 "아직 못 받았다" 로 시작한다
        for (int i = 0; i < REG_MODS_MAX; i++) { mod_val[i] = 0; mod_val_has[i] = false; mod_val_ms[i] = 0; }
    }

    void reg_reset() {          // 세션이 새로 서면 등록도 처음부터다
        reg_n = -1; reg_drain = -1; reg_done = false; reg_giveup = false;
        reg_first_ms = 0; q_sent = 0; last_q_ms = 0; mods.clear();
    }
};

// 🔴 **`AuxNode` 는 이제 `Node` 다**(REQ-0203 2단계). 이름만 남긴다 —
// 호출부 20여 곳이 `AuxNode` 로 적혀 있고 **그것을 지금 바꾸면 이 단계가 커진다.**
// ⚠ **다음 단계에서 이 이름도 없앤다.** 지금 남긴 이유는 **한 단계에 한 가지만 바꾸려는 것**이다.
// 🔑 별명이라 **보조 노드도 등록 상태를 갖게 된다** — 쓰지 않을 뿐 구조가 이미 준비된다.
typedef Node AuxNode;

struct UnknownSock {
    sock_t fd;
    std::string buf;
    long long since_ms;
    std::string peer;          // "IP:포트" — 승격 시 세션으로 넘어간다
    UnknownSock() : fd(BAD_SOCK), since_ms(0) {}
};
