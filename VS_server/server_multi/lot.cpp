// lot.cpp — **기여자가 여는 유일한 파일.** 자기 주차장은 여기만 고치면 된다.
//
//   빌드  cd 조별과제샘플/server
//         c++ -std=c++11 -O2 -w -DBUILD_ID='"내판본"' -o server_test server.cpp
//   실행  ./server_test --port-web=10290 --port-ardu=9188 --port-cam=9211
//         🔴 **자기 시험은 포트를 직접 줘라.** 운영 기본값(웹 9990 · 아두이노 8888 · 카메라 8911)을
//           그대로 쓰면 운영과 부딪히거나 **네 인스턴스가 운영이 된다.**
//         ⚠ `5900` 은 macOS 화면공유가 쓴다. 피해라
//         ⚠ 셋이 서로 겹치면 서버가 **어느 둘인지 이름으로 말하고 안 뜬다**
//
// 🔑 **여기에 `main()` 은 없다.** 서버를 여닫고 도는 것은 엔진이 한다 —
//   그 순서는 기여자가 바꿀 수 없으므로 드러내 봐야 **지킬 의무만** 생긴다.
//
// 🔑 **설명은 여기 없다.** 같은 폴더의 `GUIDE-sample.md` 를 읽어라 (저장소에서는 `조별과제샘플/dev_server/`) —
//   이 파일은 **따라 치는 것**이고 그 문서는 **읽는 것**이다.
//   틀린 것은 서버가 말해 준다(문구와 뜻은 같은 폴더의 `GUIDE-server-says.md`).
//
// ⚠ **단독으로 *링크* 하지 마라.** 빌드는 `c++ … server.cpp` 다 — 여기엔 `main()` 도 엔진도 없다.
//   다만 **문법 검사는 혼자서도 통과한다**(`c++ -fsyntax-only lot.cpp`) —
//   그래야 편집기가 타입을 알고 빨간 줄이 안 뜬다.

// 🔑 **이 한 줄이 조립 API 전부를 들인다** — `ParkingLot` · `ParkingServer` · `CmdResult` · `SpotBehavior`.
//   include 를 둘로 만들지 않는다. "이 파일만 읽으면 된다"가 깨진다.
#include "parking.h"

// ① 주차장을 조립한다 — 🔴 **이것만 채우면 돌아간다**       자세히: GUIDE-sample.md §조립
//
// 🔑 **배우는 것은 다섯이다**: `spot` · `at` · `parking` · `label` · `module`
void buildLot(ParkingLot& lot) {
    // 모듈의 전역 신원은 `(Arduino ID, module name)` 복합 키다.
    // 자리 id(A1~A4)와 Arduino 내부 모듈명은 다른 개념이므로,
    // P2의 첫 센서/안내등은 P1과 같은 A1/L1을 사용해도 된다.
    lot.spot("A1").at(0, 0).parking().label("1번 자리")
        .module("P1", "A1").module("P1", "L1");
    lot.spot("A2").at(0, 1).parking().label("2번 자리")
        .module("P1", "A2").module("P1", "L2");
    lot.spot("A3").at(0, 2).parking().label("3번 자리")
        .module("P1", "A3").module("P1", "L3");
    lot.spot("A4").at(0, 3).parking().label("4번 자리")
        .module("P2", "A1").module("P2", "L1");
    /*lot.spot("A5").at(0, 4).parking().label("5번 자리")
        .module("A5").module("L5");*/

    // Entrance/exit sensors and gates are visible as general areas. They are
    // deliberately not marked parking: approach detection must not consume a
    // parking space or participate in reservation selection.
    lot.spot("E1").at(4, 0).label("입구")
        .module("P1", "U1").module("P1", "ED");
    lot.spot("X1").at(4, 4).label("출구")
        .module("P1", "U2").module("P1", "XD");

    lot.label("P1", "A1", "1번 주차 감지");
    lot.label("P1", "A2", "2번 주차 감지");
    lot.label("P1", "A3", "3번 주차 감지");
    lot.label("P2", "A1", "4번 주차 감지");
    lot.label("P1", "U1", "입구 차량 감지");
    lot.label("P1", "U2", "출구 차량 감지");
    lot.label("P1", "ED", "입구 차단기");
    lot.label("P1", "XD", "출구 차단기");
    lot.label("P1", "L1", "1번 안내등");
    lot.label("P1", "L2", "2번 안내등");
    lot.label("P1", "L3", "3번 안내등");
    lot.label("P2", "L1", "4번 안내등");

    // CODEX FIX: ED/XD/L1~L3 are intentionally not exposed as direct web
    // controls. The state machine below is the single actuator decision owner.
}
// CODEX FIX: parking_test.ino의 중앙 판단 상태를 서버로 이동했다.
// Arduino는 A1~A3/U1/U2를 읽고 ED/XD/L1~L3 명령을 실행할 뿐이다.
namespace {
const long long GATE_OPEN_TIMEOUT_MS = 5000;
const int PARKING_SLOT_COUNT = 4;

struct ModuleAddress {
    const char* devid;
    const char* module;
};

const ModuleAddress PARKING_LED[PARKING_SLOT_COUNT] = {
    {"P1", "L1"}, {"P1", "L2"}, {"P1", "L3"}, {"P2", "L1"}
};
const ModuleAddress ENTRY_GATE = {"P1", "ED"};
const ModuleAddress EXIT_GATE  = {"P1", "XD"};

struct ParkingController {
    bool slotKnown[PARKING_SLOT_COUNT];
    bool occupied[PARKING_SLOT_COUNT];
    bool entryBaseline[PARKING_SLOT_COUNT];
    bool entranceKnown, entranceDetected;
    bool exitKnown, exitDetected;

    bool entranceLatched;
    bool exitLatched;
    bool entryActive;
    bool exitActive;
    bool exitPassing;
    bool abortEntry;
    bool abortExit;
    bool retryEntryClose;
    bool retryExitClose;

    int assignedSlot;
    int wrongSlot;
    long long entryOpenedAt;
    long long exitOpenedAt;
    long long entryRetryAt;
    long long exitRetryAt;
    unsigned long long entryGeneration;

    ParkingController()
        : entranceKnown(false), entranceDetected(false),
          exitKnown(false), exitDetected(false),
          entranceLatched(false), exitLatched(false),
          entryActive(false), exitActive(false), exitPassing(false),
          abortEntry(false), abortExit(false),
          retryEntryClose(false), retryExitClose(false),
          assignedSlot(-1), wrongSlot(-1),
          entryOpenedAt(0), exitOpenedAt(0),
          entryRetryAt(0), exitRetryAt(0), entryGeneration(0) {
        for (int i = 0; i < PARKING_SLOT_COUNT; i++) {
            slotKnown[i] = false;
            occupied[i] = false;
            entryBaseline[i] = false;
        }
    }
};

ParkingController g_ctrl;

int parkingIndex(const std::string& spot) {
    if (spot == "A1") return 0;
    if (spot == "A2") return 1;
    if (spot == "A3") return 2;
    if (spot == "A4") return 3;
    //if (spot == "A5") return 4;
    return -1;
}

std::string parkingName(int i) {
    return (i >= 0 && i < PARKING_SLOT_COUNT) ? std::string("A") + char('1' + i) : std::string("??");
}

bool allParkingKnown() {
    for (int i = 0; i < PARKING_SLOT_COUNT; i++) if (!g_ctrl.slotKnown[i]) return false;
    return true;
}

int firstFreeParkingSlot(ParkingServer& srv) {
    if (!allParkingKnown()) return -2;       // 아직 안전하게 판단할 자료가 없다
    for (int i = 0; i < PARKING_SLOT_COUNT; i++)
        if (!g_ctrl.occupied[i] && srv.parkingSpotAvailable(parkingName(i))) return i;
    return -1;                               // 만차
}

int occupiedCount() {
    int n = 0;
    for (int i = 0; i < PARKING_SLOT_COUNT; i++) if (g_ctrl.occupied[i]) n++;
    return n;
}

int parkingLedIndex(const std::string& devid, const std::string& module) {
    for (int i = 0; i < PARKING_SLOT_COUNT; i++)
        if (devid == PARKING_LED[i].devid && module == PARKING_LED[i].module) return i;
    return -1;
}

bool queueEntryBatch(ParkingServer& srv, int selected, long gateCommand) {
    // 조립 표와 같은 복합 주소로 보낸다. 모듈명 단독 owner
    // 자동 탐색은 하지 않는다. device를 넘는 명령은 각자 rid/ACK를 갖는다.
    bool ok = true;
    for (int i = 0; i < PARKING_SLOT_COUNT; i++) {
        if (!srv.send(PARKING_LED[i].devid, PARKING_LED[i].module,
                      selected == i ? 1 : 0)) ok = false;
    }
    if (!srv.send(ENTRY_GATE.devid, ENTRY_GATE.module, gateCommand)) ok = false;
    if (!ok) srv.log("[입차] 분산 LED/입구 차단기 발행 실패");
    return ok;
}

bool queueExitGate(ParkingServer& srv, long command) {
    if (srv.send(EXIT_GATE.devid, EXIT_GATE.module, command)) return true;
    srv.log(std::string("[출차] 출구 차단기 ") + (command == 1 ? "OPEN" : "CLOSE")
            + " 발행 실패");
    return false;
}

void clearEntryState() {
    g_ctrl.entryActive = false;
    g_ctrl.assignedSlot = -1;
    g_ctrl.wrongSlot = -1;
    g_ctrl.entryOpenedAt = 0;
    if (!g_ctrl.entranceDetected) g_ctrl.entranceLatched = false;
}

void clearExitState() {
    g_ctrl.exitActive = false;
    g_ctrl.exitPassing = false;
    g_ctrl.exitOpenedAt = 0;
    if (!g_ctrl.exitDetected) g_ctrl.exitLatched = false;
}

void beginEntry(ParkingServer& srv) {
    const int available = firstFreeParkingSlot(srv);
    if (available == -2) {
        srv.log("[입차] 주차 센서 초기값이 모두 오지 않아 배정을 보류한다");
        return;
    }
    if (available < 0) {
        srv.log("[입차] PARKING_FULL — 빈 주차면 없음");
        return;
    }
    // 입구 감지만으로 자리를 자동 배정하지 않는다. 사용자가 8080 화면에서
    // 빈자리를 선택하면 onUserSlotSelection()이 다시 서버 상태를 검증한 뒤 배정한다.
    g_ctrl.assignedSlot = -1;
    g_ctrl.wrongSlot = -1;
    g_ctrl.entryGeneration++;
    g_ctrl.entryActive = true;
    g_ctrl.entryOpenedAt = srv.nowMs();
    srv.log("[입차] ENTRY_APPROACH → 사용자 주차면 선택 대기");
}

void finishEntry(ParkingServer& srv, const std::string& reason) {
    if (!queueEntryBatch(srv, -1, 2)) return;  // LED 모두 OFF + ED CLOSE
    srv.log("[입차] " + reason + " · 안내등 OFF · 입구 게이트 CLOSE 요청");
    clearEntryState();
}

void controllerTick(ParkingServer& srv) {
    if (!g_ctrl.entranceDetected && !g_ctrl.entryActive) g_ctrl.entranceLatched = false;
    if (!g_ctrl.exitDetected && !g_ctrl.exitActive) g_ctrl.exitLatched = false;

    // CLOSE ACK 실패는 다음 tick에서 같은 안전 명령을 새 RID로 다시 발행한다.
    if (g_ctrl.retryEntryClose && srv.moduleReady(ENTRY_GATE.devid, ENTRY_GATE.module)
        && srv.nowMs() >= g_ctrl.entryRetryAt) {
        g_ctrl.retryEntryClose = false;
        g_ctrl.entryRetryAt = srv.nowMs() + 1200;
        if (!queueEntryBatch(srv, -1, 2)) g_ctrl.retryEntryClose = true;
    }
    if (g_ctrl.retryExitClose && srv.moduleReady(EXIT_GATE.devid, EXIT_GATE.module)
        && srv.nowMs() >= g_ctrl.exitRetryAt) {
        g_ctrl.retryExitClose = false;
        g_ctrl.exitRetryAt = srv.nowMs() + 1200;
        if (!queueExitGate(srv, 2)) g_ctrl.retryExitClose = true;
    }

    if (g_ctrl.abortEntry) {
        g_ctrl.abortEntry = false;
        finishEntry(srv, "입구 OPEN 명령 실패로 세션 중단");
    }
    if (g_ctrl.abortExit) {
        g_ctrl.abortExit = false;
        if (queueExitGate(srv, 2)) {
            srv.log("[출차] 출구 OPEN 명령 실패로 CLOSE 요청");
            clearExitState();
        }
    }

    // 새 입차 접근은 U1의 상승 상태를 한 번만 소비한다.
    if (g_ctrl.entranceKnown && g_ctrl.entranceDetected
        && !g_ctrl.entranceLatched && !g_ctrl.entryActive) {
        g_ctrl.entranceLatched = true;
        beginEntry(srv);
    }

    if (g_ctrl.entryActive && g_ctrl.assignedSlot >= 0) {
        // 배정된 자리에 차량이 들어오면 정상 주차 완료다.
        if (g_ctrl.occupied[g_ctrl.assignedSlot]) {
            finishEntry(srv, "PARKING_COMPLETE " + parkingName(g_ctrl.assignedSlot));
        } else {
            // 배정 당시 이미 차 있던 자리가 비워지면 그 이후의 재점유는
            // 이번 입차 차량의 오주차 후보가 될 수 있다.
            for (int i = 0; i < PARKING_SLOT_COUNT; i++)
                if (i != g_ctrl.assignedSlot && g_ctrl.entryBaseline[i]
                    && !g_ctrl.occupied[i]) g_ctrl.entryBaseline[i] = false;

            // 다른 빈 자리가 먼저 0→1이면 오주차로 한 번만 기록한다.
            if (g_ctrl.wrongSlot >= 0 && !g_ctrl.occupied[g_ctrl.wrongSlot]) {
                srv.log("[입차] WRONG_PARKING_CLEARED " + parkingName(g_ctrl.wrongSlot)
                        + " · 원래 배정 " + parkingName(g_ctrl.assignedSlot));
                g_ctrl.entryBaseline[g_ctrl.wrongSlot] = false;
                g_ctrl.wrongSlot = -1;
            }
            if (g_ctrl.wrongSlot < 0) {
                for (int i = 0; i < PARKING_SLOT_COUNT; i++) {
                    // 배정 전에 이미 차 있던 자리는 오주차 사건이 아니다.
                    if (i != g_ctrl.assignedSlot && !g_ctrl.entryBaseline[i]
                        && g_ctrl.occupied[i]) {
                        g_ctrl.wrongSlot = i;
                        srv.log("[입차] WRONG_PARKING " + parkingName(i)
                                + " · EXPECTED " + parkingName(g_ctrl.assignedSlot));
                        break;
                    }
                }
            }
        }

        // 주차가 끝나지 않은 채 오래 열렸으면 서버가 닫는다. U1이 아직
        // 감지 중이면 차량 위로 닫지 않고 다음 timeout 구간까지 보류한다.
        if (g_ctrl.entryActive
            && srv.nowMs() - g_ctrl.entryOpenedAt >= GATE_OPEN_TIMEOUT_MS) {
            if (g_ctrl.entranceDetected) {
                srv.log("[입차] OPEN timeout이지만 U1 감지 중 — 안전을 위해 CLOSE 보류");
                g_ctrl.entryOpenedAt = srv.nowMs();
            } else {
                finishEntry(srv, "ENTRY_TIMEOUT");
            }
        }
    }

    // 배정 전 차량이 입구를 떠나면 선택 세션만 종료한다. 아직 게이트/LED 명령을
    // 내리지 않았으므로 actuator 정리 명령은 필요하지 않다.
    if (g_ctrl.entryActive && g_ctrl.assignedSlot < 0 && !g_ctrl.entranceDetected) {
        srv.log("[입차] 선택 전 입구 감지 해제 → 세션 종료");
        clearEntryState();
    }

    // 출구는 U2 상승 시 서버가 허가하고, 감지 후 하강하면 통과 완료다.
    if (g_ctrl.exitKnown && g_ctrl.exitDetected
        && !g_ctrl.exitLatched && !g_ctrl.exitActive) {
        g_ctrl.exitLatched = true;
        if (srv.moduleReady(EXIT_GATE.devid, EXIT_GATE.module) && queueExitGate(srv, 1)) {
            g_ctrl.exitActive = true;
            g_ctrl.exitPassing = true;
            g_ctrl.exitOpenedAt = srv.nowMs();
            srv.log("[출차] EXIT_APPROACH → 출구 게이트 OPEN 요청");
        }
    }

    if (g_ctrl.exitActive && g_ctrl.exitPassing && !g_ctrl.exitDetected) {
        if (queueExitGate(srv, 2)) {
            srv.log("[출차] EXIT_COMPLETE → 출구 게이트 CLOSE 요청");
            clearExitState();
        }
    } else if (g_ctrl.exitActive
               && srv.nowMs() - g_ctrl.exitOpenedAt >= GATE_OPEN_TIMEOUT_MS) {
        // parking_test.ino는 10초 뒤 무조건 닫았지만, 감지 중인 차량 위로
        // 닫히는 위험을 피하기 위해 서버 수정본은 U2 해제까지 기다린다.
        if (g_ctrl.exitDetected) {
            srv.log("[출차] OPEN timeout이지만 U2 감지 중 — 안전을 위해 CLOSE 보류");
            g_ctrl.exitOpenedAt = srv.nowMs();
        } else if (queueExitGate(srv, 2)) {
            srv.log("[출차] EXIT_TIMEOUT → 출구 게이트 CLOSE 요청");
            clearExitState();
        }
    }
}
} // namespace

UserEntryStatus userEntryStatus() {
    UserEntryStatus s;
    s.active = g_ctrl.entryActive;
    s.awaiting_selection = g_ctrl.entryActive && g_ctrl.assignedSlot < 0;
    if (g_ctrl.assignedSlot >= 0) s.selected_slot = parkingName(g_ctrl.assignedSlot);
    s.generation = g_ctrl.entryGeneration;
    return s;
}

bool userEntryGuideReady(const ParkingServer& srv) {
    bool ready = srv.moduleReady(ENTRY_GATE.devid, ENTRY_GATE.module);
    for (int i = 0; i < PARKING_SLOT_COUNT; i++)
        ready = ready && srv.moduleReady(PARKING_LED[i].devid, PARKING_LED[i].module);
    return ready;
}

bool onUserSlotSelection(ParkingServer& srv, const std::string& slot,
                         std::string& code, std::string& message) {
    const int selected = parkingIndex(slot);
    if (!g_ctrl.entryActive) {
        code = "ENTRY_INACTIVE";
        message = "입차 차량이 감지된 뒤 선택할 수 있습니다.";
        return false;
    }
    if (g_ctrl.assignedSlot >= 0) {
        code = "ALREADY_ASSIGNED";
        message = "이미 주차면이 배정되었습니다.";
        return false;
    }
    if (selected < 0 || !g_ctrl.slotKnown[selected]) {
        code = "SLOT_UNAVAILABLE";
        message = "현재 상태를 확인할 수 없는 주차면입니다.";
        return false;
    }
    if (g_ctrl.occupied[selected] || !srv.parkingSpotAvailable(slot)) {
        code = "SLOT_UNAVAILABLE";
        message = "이미 사용 중이거나 예약된 주차면입니다.";
        return false;
    }

    if (!userEntryGuideReady(srv)) {
        code = "SYSTEM_NOT_READY";
        message = "주차 안내 장치를 준비하고 있습니다. 잠시 후 다시 시도해 주세요.";
        return false;
    }

    if (!queueEntryBatch(srv, selected, 1)) {
        code = "COMMAND_FAILED";
        message = "주차 안내를 시작하지 못했습니다. 다시 시도해 주세요.";
        return false;
    }

    g_ctrl.assignedSlot = selected;
    g_ctrl.wrongSlot = -1;
    for (int i = 0; i < PARKING_SLOT_COUNT; i++)
        g_ctrl.entryBaseline[i] = g_ctrl.occupied[i];
    g_ctrl.entryOpenedAt = srv.nowMs();
    srv.log("[입차] USER_SELECTED " + slot + " · 안내등 ON · 입구 게이트 OPEN 요청");
    code = "ASSIGNED";
    message = slot + " 주차면으로 안내를 시작합니다.";
    return true;
}

// 서버의 주기 tick: timeout과 재시도 판단도 Arduino가 아니라 여기서 수행한다.
void onTick(ParkingServer& srv) {
    controllerTick(srv);
}

void onCmdResult(const CmdResult& r) {
    std::cout << "[명령] " << r.devid << "/" << r.module << " " << r.value
              << " → " << r.kindName() << "\n";
    if (r.kind == CmdResult::OK) return;

    // OPEN 실패는 다음 서버 tick에서 세션을 안전하게 정리한다.
    if (r.devid == ENTRY_GATE.devid && r.module == ENTRY_GATE.module
        && r.value == 1 && g_ctrl.entryActive) g_ctrl.abortEntry = true;
    if (r.devid == EXIT_GATE.devid && r.module == EXIT_GATE.module
        && r.value == 1 && g_ctrl.exitActive) g_ctrl.abortExit = true;
    if (r.devid == ENTRY_GATE.devid && r.module == ENTRY_GATE.module && r.value == 2)
        g_ctrl.retryEntryClose = true;
    if (r.devid == EXIT_GATE.devid && r.module == EXIT_GATE.module && r.value == 2)
        g_ctrl.retryExitClose = true;
    if (parkingLedIndex(r.devid, r.module) >= 0 && r.value == 0)
        g_ctrl.retryEntryClose = true;
    if (parkingLedIndex(r.devid, r.module) >= 0 && r.value != 0 && g_ctrl.entryActive)
        g_ctrl.abortEntry = true;
}

void onControllerReset(ParkingServer& srv) {
    // LED OFF + 입구 CLOSE, 출구 CLOSE를 모두 서버가 명령한다.
    if (!queueEntryBatch(srv, -1, 2)) g_ctrl.retryEntryClose = true;
    if (!queueExitGate(srv, 2))       g_ctrl.retryExitClose = true;

    clearEntryState();
    clearExitState();
    g_ctrl.abortEntry = g_ctrl.abortExit = false;
    // 센서가 계속 활성화된 상태에서 RESET 직후 새 차량으로 중복 처리하지 않는다.
    g_ctrl.entranceLatched = g_ctrl.entranceDetected;
    g_ctrl.exitLatched = g_ctrl.exitDetected;
    srv.log("[제어기] SESSION_RESET · LED OFF · 양쪽 게이트 CLOSE 요청");
}

// 현재 Arduino는 bool S 프레임만 보내고 V 값 프레임은 보내지 않는다.
void onSensorValue(ParkingServer& srv, const std::string& spot,
                   const std::string& devid, const std::string& module,
                   long value) {
    srv.log("[값] " + spot + " · " + devid + "/" + module + " " + std::to_string(value));
}

// A1~A3, U1, U2의 모든 판단 상태는 서버가 갱신한다.
void onOccupancy(ParkingServer& srv, const std::string& spot,
                 const std::string& devid, const std::string& module, bool occupied,
                 const SensorMeasure& measure) {
    const int i = parkingIndex(spot);
    if (i >= 0) {
        g_ctrl.slotKnown[i] = true;
        g_ctrl.occupied[i] = occupied;
        srv.log("[주차면] " + parkingName(i) + (occupied ? " OCCUPIED" : " EMPTY")
                + " · 사용 " + std::to_string(occupiedCount()) + "/" + std::to_string(PARKING_SLOT_COUNT));
    } else if (devid == "P1" && module == "U1") {
        g_ctrl.entranceKnown = true;
        g_ctrl.entranceDetected = occupied;
        srv.log(std::string("[입구센서] ") + (occupied ? "DETECTED" : "CLEAR"));
    } else if (devid == "P1" && module == "U2") {
        g_ctrl.exitKnown = true;
        g_ctrl.exitDetected = occupied;
        srv.log(std::string("[출구센서] ") + (occupied ? "DETECTED" : "CLEAR"));
    }

    if (measure.has)
        srv.log("[센서값] " + spot + " · " + devid + "/" + module + " "
                + std::to_string(measure.value));

    // S 프레임 처리 중 호출되므로 여기서 만든 명령은 현재 하행 창에 실릴 수 있다.
    controllerTick(srv);
}
