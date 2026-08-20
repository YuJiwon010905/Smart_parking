// entry.h — **엔진의 진입점.** `main()` 이 여기 있다.
// ═══════════════════════════════════════════════════════════════════════════
// 🔴 **기여자는 이 파일을 안 연다.** 여는 것은 `lot.cpp` 하나다.
//
//   `main()` 을 기여자 파일에 두면 **"각자 자기 `main()` 을 갖는다"** 로 배운다.
//   🔴 **그 오해는 로컬에서 한 번도 안 깨진다** — 혼자니까 잘 돈다.
//     합칠 때 깨지고, **그때는 이미 그 모델 위에 코드를 썼다.**
//
// 🔑 판별자는 우리가 이미 세운 것이다:
//     🔓 호출자가 **바꿀 수 있는** 순서 → 드러낸다
//     🔒 호출자가 **바꿀 수 없는** 순서 → 감춘다. 드러내 봐야 **지킬 의무만** 생긴다
//   `openPorts → 루프 → closeDown` 은 기여자가 못 바꾼다. 그래서 여기 있다.
//
// ⚠ 단독 컴파일되지 않는다. `server.cpp` 안 그 자리에 include 된다.
// ═══════════════════════════════════════════════════════════════════════════
#include <windows.h> // 1. 헤더 추가

// dev_server 판 — 🔴 **콘솔이 기본이다.** 파일 로그도 자가검증도 없다.
int main(int argc, char** argv) {
    int rc = 0;
    SetConsoleOutputCP(65001);
#include "cli.h"      // 손잡이 해석 — --log · --port-offset · --park-dev · 시험용 셋
    // `--selftest` 는 로그 파일을 열지 않는다 — 자가검증 출력이 운영 로그에 섞이면
    // 인스턴스 경계가 무의미해진다.
    {
        // 🔴 **`open_log()` 를 안 부른다.** 출력은 stdout 하나다(사용자 확정).
        //   ⚠ 끄면 사라진다. 남겨야 하면 `./srv > dev.log` 로 직접 받아라.
        (void)log_path;

        // 🔑 **기여자의 조립을 부른다.** 이 한 줄이 `lot.cpp` 와 엔진의 유일한 접점이다.
        ParkingLot lot;
        buildLot(lot);

        // 🔑 **어디서 시작하는지를 값으로 말한다.** 문서로 설명하면 또 길어진다.
        {
            size_t mods = 0;
            for (size_t i = 0; i < lot.areas().size(); i++) mods += lot.areas()[i].modules.size();
            char b[160];
            snprintf(b, sizeof(b), "[기동] lot.cpp 의 buildLot() 을 부른다 → 자리 %zu개 · 모듈 %zu개",
                     lot.areas().size(), mods);
            logf("=", b);
        }

        ParkingServer srv(lot);
        srv.onCommandResult(onCmdResult);       // ③ — 기여자가 비워 뒀으면 아무 일도 안 한다

        if (!srv.openPorts()) return 1;
        while (srv.serveOneTick()) {
            onTick(srv);                        // ② — 기여자가 비워 뒀으면 아무 일도 안 한다
        }
        srv.closeDown();
        rc = 0;
    }
    return rc;   // 🔑 `WSACleanup` 도 정적 소멸자가 한다
}
