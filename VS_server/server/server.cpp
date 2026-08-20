// 주차 관제 서버 — 명세: docs/net/parking-protocol.md (v1, 얼림)
//
// 한 프로세스가 리스너 둘을 연다:
//   TCP  9991 — 아두이노. LF 종단 압축 ASCII 라인 (명세 §2)
//   HTTP 9900 — index.html / data_log.json 정적 서빙 + GET /ws 를 WebSocket 으로 업그레이드 (§5)
//
// 외부 라이브러리를 쓰지 않는다. SHA-1 · base64 · WebSocket 프레이밍을 직접 구현했다.
//
// 빌드
//   macOS/Linux : c++ -std=c++11 -O2 -o server server.cpp
//   Windows     : cl /EHsc /std:c++14 /utf-8 server.cpp ws2_32.lib
//                 ^^^^^^^ **/utf-8 을 빼지 마라.**
//
//   이 파일은 BOM 없는 UTF-8 이고 안내 문구가 한글이다("예약되었습니다" 등).
//   BOM 이 없으면 MSVC 는 소스를 시스템 ANSI 코드페이지(한국어 윈도우면 CP949)로 읽어
//   그 문자열을 깨뜨린다. **컴파일은 그대로 통과한다** — 그래서 빌드 로그로는 절대 못 잡고
//   브라우저 화면에서 처음 보인다. BOM 을 넣어 해결하지 않는 이유는 POSIX 도구들이
//   BOM 을 성가셔하기 때문이다. 플래그로 해결하는 것이 맞다.
//
// 실행
//   ./server                  # 아두이노 TCP 9991 · HTTP/WS 9900
//   ./server --selftest       # SHA-1+base64 와 라인 체크섬 자가검증 (RFC 6455 §1.3 벡터)
//
// 하드웨어 없이 시험하려면 (명세 §10):
//   python3 ../net/fake_arduino.py                      # 가짜 아두이노
//   python3 ../net/ws_probe.py --listen 5               # 브라우저 대신 WS 관찰
//
// ⚠ 이 파일을 고칠 때 알아둘 것: now_ms() 와 epoch_ms() 는 **원점이 다르다**(각 함수 주석 참조).
//   POSIX 에서는 우연히 둘 다 Unix epoch 라 섞어 써도 티가 안 나지만, 윈도우에서는
//   now_ms() 가 부팅 후 경과 시간이라 섞는 순간 수십 년짜리 값이 나온다.
//
// 왜 크로스 플랫폼인가: 원본은 Winsock 전용이라 이 팀의 macOS 에서 빌드조차 되지 않았다.
// 빌드도 못 하는 코드는 검증할 수 없고, 검증할 수 없으면 "구현했다"고 말할 수 없다.
#define _CRT_SECURE_NO_WARNINGS
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cctype>
// <ctime> 를 직접 넣는다. POSIX 에서는 아래 <sys/time.h> 가 우연히 끌어와서 통과했지만
// 그 include 는 #else 안이라 Windows 에는 없다 — 즉 macOS 빌드 성공은 우연이었다.
#include <ctime>
#include <csignal>      // sig_atomic_t / SIGINT — 윈도우에도 있어야 하므로 공통 블록에 둔다
#include <string>
#include <vector>
#include <algorithm>    // 복구시간 중앙값 — nth_element
#include <map>
#include <set>            // 창 원자성 — 이번 창에 미룬 배치 표지
#include <fstream>
#include <sstream>
#include <iostream>

// ---------------------------------------------------------------- 플랫폼 어댑터
#ifdef _WIN32
  // 이 두 define 은 반드시 <winsock2.h> **앞**에 와야 한다. 뒤에 두면 아무 효과가 없다.
  //  · FD_SETSIZE: 윈도우 기본값이 64 다. FD_SET 은 넘치면 **조용히 무시**하므로
  //    브라우저가 여러 대 붙었을 때 원인 없는 무응답으로 보인다. 비용은 fd_set 크기뿐이다.
  //  · NOMINMAX: <windows.h> 의 min/max 매크로가 표준 함수와 충돌하는 것을 막는다.
  //    지금은 std::min/max 를 안 쓰지만 나중에 누가 쓰면 그때 깨진다. 비용 0 의 예방책이다.
  #define FD_SETSIZE 256
  #define NOMINMAX
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  #include <direct.h>     // _mkdir — 로그 디렉터리 생성
  #pragma comment(lib, "ws2_32.lib")
  typedef SOCKET sock_t;
  #define BAD_SOCK INVALID_SOCKET
  static void closesock(sock_t s) { closesocket(s); }
  static int  sockerr() { return WSAGetLastError(); }
#else
  #include <sys/socket.h>
  #include <sys/select.h>
  #include <sys/time.h>
  #include <sys/stat.h>   // mkdir — 로그 디렉터리 생성
  #include <sys/file.h>   // flock — 한 로그 파일에 두 인스턴스가 붙는 것을 막는다
  #include <fcntl.h>      // open  — 위 잠금용 fd
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <errno.h>
  #include <signal.h>
  #ifdef __APPLE__
    #include <mach-o/dyld.h>   // _NSGetExecutablePath — 경계 줄의 bin= 필드
  #endif
  typedef int sock_t;
  #define BAD_SOCK (-1)
  static void closesock(sock_t s) { close(s); }
  static int  sockerr() { return errno; }
#endif

#include "config.h"      // 손잡이 — 포트·타이밍·상한·유도값과 그 근거
#include "server_device.h"   // 디바이스 계층 잎 유틸 — SHA-1·base64·ws_accept·체크섬
#include "server_seam.h"
#include "parking.h"     // 🔴 **공개 조립 API** — 사용 코드가 읽는 유일한 헤더
#include "ridpool.h"
#include "ledger.h"     // 노드 대장 — 재기동을 건너 "누가 있었나"를 기억한다
#include "spot.h"       // 자리의 동작 방식 — 기여자가 구현하는 콜백
#include "cmdresult.h"  // 명령 결과를 나중에 받는다 — 성공 / 거절 / 무응답     // rid 발행·격리·영속 — 전선에 안 닿는 축
// ⚠ `lot.h` 는 `Zone`·`ParkingLot` 을 쓰므로 **그 뒤에** include 한다(아래 zone.h 다음).     // 이음매 계약 — DeviceEvent / DeviceCommand

#include "runtime.h"     // 프로세스 바닥 — 시각·시그널·로그·경로·pid

// 🔴🔴 **`Node` — 통신 단위 하나.** 설계: `docs/net/DESIGN-bridge-node-module-zone.md`
// 지금은 **주차 노드 하나뿐**이라 `Server` 안에 `park` 로 한 벌만 산다. 나중에 `map<devid,Node>` 가 된다.
//
// 🔑 **이 단계의 목적은 "그릇"만 바꾸는 것이다. 거동이 바뀔 자리가 없어야 한다.**
//   기존 이름(`ard_last_ms` 등)을 **참조 별칭**으로 남겨 **호출부 약 150곳을 한 곳도 안 고친다.**
//   ⚠ 참조 멤버는 `operator=` 를 지운다 — **`Server` 가 어디서도 대입되지 않는 것을 먼저 확인했다**
//     (선언 13곳 전부 생성이고 대입 0곳).
#include "node.h"
#include "zone.h"
#include "lot.h"         // 🔴 주차장 지형이 스스로 답한다 — 전선에 안 닿는 축
struct Pending {             // 아두이노에 내려보내고 ACK 를 기다리는 요청
    uint16_t wire_rid;
    sock_t   ws_fd;          // 요청한 브라우저 (BAD_SOCK = 서버 자체 재동기화)
    std::string browser_rid;
    std::string slot, user_id;   // user_id = **전선에 실제로 나간 값**(ASCII 0*8 또는 빈 값)
    std::string plate;           // 서버가 보관할 원래 값(UTF-8 번호판 등). 전선에 안 나간다
    char kind;               // 'R' | 'C' | 'T' | 'M' | 'G'
    // 🔴 `kind=='G'` 전용 — **모듈 인덱스**다. `slot` 은 사람이 읽을 자리 이름이고
    //   전선에 나가는 것은 이 숫자다. **둘을 한 칸에 넣지 않는다** — 자리 이름과 모듈 순서는
    //   다른 것이고(§5), 섞으면 재등록으로 순서가 바뀔 때 무엇이 틀렸는지 못 가린다.
    int  mod_idx;
    char top;                // kind=='T' 일 때의 op: 'A'|'D'|'S'|'X'
    // 🔴 `G` 의 **인자**(LCD 7자리 같은 값). `top` 과 **칸이 따로다** — `top` 은 `T` 의 op 다.
    //   ⚠ 한 칸에 두 뜻을 담지 마라. **값이 갈리기 전까지 아무도 모른다.**
    long g_arg;              // kind=='G' 일 때 장치로 보낼 인자
    long long sent_ms;
    int tries;
    // 🔴 **큐에 있는 동안은 ACK 시계를 돌리지 않는다.** 하행이 창을 기다리는 사이에
    // `ACK_TIMEOUT_MS` 가 흐르면 **전선에 나가기도 전에 재전송이 걸린다** — 그러면
    // 같은 rid 가 큐에 두 번 들어가고, 내가 없애려던 증폭이 큐 안에서 다시 생긴다.
    // `tick()` 은 이 값이 true 인 항목을 건너뛴다. `sent_ms` 는 **실제 송신 시각**이다.
    bool queued;
    // 🔴 **화면이 시킨 명령인가** (2026-08-20 · REQ-0281)
    //   ⚠ `ws_fd != BAD_SOCK` 만으로는 못 가른다 — 차단봉(`open_gate`)도 화면이 시키지만
    //     그쪽은 **옛 계약대로 `ack` 봉투**로 답해야 한다. 새 경로만 `cmd_result` 를 낸다.
    //   **한 칸에 두 뜻을 담지 않는다** — 위 `top`/`g_arg` 에서 이미 배운 것이다.
    bool web_cmd;
    // 🔴 ctor 가 없어서 `dispatch` 가 `top`·`queued` 를 안 세운 채 복사해 왔다.
    // 지금은 모든 경로가 곧바로 덮으므로 실동작은 맞지만 **`-Wall -Wextra` 가 이걸 안 잡는다** —
    // `Server` 의 여섯 칸이 무경고로 통과했던 것과 같은 이유다. 여기서 닫는다.
    Pending() : wire_rid(0), ws_fd(BAD_SOCK), kind(0), mod_idx(-1), top(0), g_arg(0), sent_ms(0), tries(0), queued(false), web_cmd(false) {}
};

struct Conn {
    enum Kind { HTTP, WS } kind;
    std::string inbuf;
    // 🔴 **`get_map` 상한은 연결별이다.**
    //   전역 창이면 **화면 여섯이 재접속하는 것만으로 상한을 넘긴다** — 각자 한 번씩 물었는데
    //   누군가는 거절당한다. 서버 재기동 직후가 정확히 그 상황이고, **거절당한 화면은
    //   지형을 못 받아 빈 채로 남는다.**
    //   ⚠ 주석은 원래 *"화면 하나가 1초에"* 라고 말하고 있었다 — **구현이 그 말과 달랐다.**
    //   🔑 고친 것은 상한값이 아니라 **누구를 세는가**다.
    long long getmap_win_ms; int getmap_in_win;
    Conn() : kind(HTTP), getmap_win_ms(0), getmap_in_win(0) {}
};

struct Server {
    // ─── 목차 ─────────────────────────────────────────────────────────
    //  🔑 **이 구조체는 29줄이고 그중 대부분이 아래 목차다.**
    //    각 줄은 `struct Server` 의 **몸통 조각**이고 그 자리에 펼쳐진다.
    //    `.o` 바이트 동일로 이 형태가 거동을 안 바꾼다는 것이 증명돼 있다.
    //  🔴 **순서가 문법이다** — 뒤엣것이 앞엣것을 쓴다. 함부로 바꾸면 컴파일이 깨진다.
    //  🔒 표시는 `.claude/protected.json` 잠금 — 장치 쪽과 한 계약의 양끝이다.
    // ──────────────────────────────────────────────────────────────────
#include "state.h"         // 무엇을 기억하는가 — 필드·계수기·중첩 타입 전부
#include "metrics.h"       // 소크 관측 · 복구 계측 · 지표 문장
#include "nodes.h"         // 다중 노드 — 등록·승격·결속·라우팅
#include "wire.h"          // 송신 helper · WebSocket 프레임 (§5.2)
#include "wsjson.h"        // 봉투 만들기 — map / state / snapshot JSON
#include "seam.h"          // 이음매: 디바이스 → 도메인
#include "persist.h"       // data_log.json 읽기·쓰기 (§9)
#include "downlink.h"      // 🔒 하행 — 발행·재전송·회수 (전선 계약. 잠금)
#include "uplink.h"        // 🔒 상행 — 장치 프레임 파서 (전선 계약. 잠금)
#include "wsapi.h"         // 브라우저 → 서버 — WS 명령 수신 (§5.4)
#include "http.h"          // HTTP 요청 · WS 업그레이드 · 정적 파일
#include "listen.h"        // 포트 열고 닫기 — openPorts / closeDown
#include "serve.h"         // 한 박자 — serveOneTick (수신→판정→하행→방송)
    // 옛 진입점 — 셋을 순서대로 부른다. **이 순서가 곧 흐름이다.**
    int run() {
        if (!openPorts()) return 1;
        while (serveOneTick()) { }
        closeDown();
        return 0;
    }
};

// 🔴 자가검증은 `selftest.h` 에 있다.
//   ⚠ **이 자리를 옮기지 마라** — 전처리 결과가 같아야 그 증명이 성립한다.

// 🔴 **기여자가 여는 파일은 `lot.cpp` 이고, 엔진 진입점(`main()`)은 `entry.h` 에 있다.**
//   🔑 `main.cpp` 라는 파일은 **없다** — v2 에서 둘로 갈렸다. 둘 다 이 파일 끝에서 include 한다.
//   원래 자리에서 그대로 `#include` 한다 — **전처리 결과가 같아야 `.o` 0 차이가 성립한다.**
//   ⚠ `#include` 위치를 옮기는 순간 그것은 "분리"가 아니라 "변경"이다. 이 자리를 지켜라.
// ── 🔴 프로세스 초기화 — **호출자가 몰라도 되는 것**
//
//   `WSAStartup`·`SIGPIPE`·`SIGINT/TERM` 을 여기서 챙긴다.
//   🔑 **Winsock 이 무엇인지 몰라도 주차장을 만들 수 있어야 한다** — 설정은 자동화한다.
//
//   🔴 **`ParkingServer` 의 생성자로 옮기지 않았다.** 이유가 있다:
//     `--selftest` 는 `ParkingServer` 를 만들지 않는데 **소켓쌍에 쓰므로 `SIGPIPE` 가 필요하다.**
//     생성자로 옮기면 **자가검증 경로에서 그 보호가 조용히 벗겨진다** — 죽어야 알 수 있는 종류다.
//   ✅ 그래서 **정적 객체**로 둔다. `main` 보다 먼저 돌고 **두 경로를 다 덮는다.**
//   ⚠ 다른 정적에 의존하지 않는다(시그널 등록과 Winsock 시작뿐) — 초기화 순서 문제가 없다.
struct ProcessInit {
    ProcessInit() {
#ifdef _WIN32
        WSADATA w;
        if (WSAStartup(MAKEWORD(2,2), &w) != 0) std::cerr << "Winsock 초기화 실패\n";
#else
        signal(SIGPIPE, SIG_IGN);   // 끊긴 소켓에 write 해도 프로세스가 죽지 않게
#endif
        // 소크 시험은 Ctrl-C 로 끝난다 — 그때 요약을 남기고 정상 종료한다
        signal(SIGINT,  on_stop_signal);
        signal(SIGTERM, on_stop_signal);
    }
    ~ProcessInit() {
#ifdef _WIN32
        WSACleanup();
#endif
    }
};
static ProcessInit g_process_init;

// ── 🔴 공개 조립 API 구현
//
//   헤더(`parking.h`)에는 **자료구조가 하나도 안 나온다.** 여기 `Impl` 이 `Server` 를 들고 있고
//   호출자는 그 존재를 모른다 — 그것이 은닉이다.
//   ⏳ 지금은 **얇은 층**이다. `Server` 3,900줄을 쪼개는 것은 3단계이고, 그때 이 층은 안 바뀐다.
//   🔑 그래서 **사용 코드가 먼저 돌고** 내부는 나중에 정리된다.

// 🔑 셋 다 **한 곳으로 모은다** — 규칙이 세 곳에 생기면 갈린다.
// 🔴 v2 — `sensor()`/`actuator()` 를 `module()` 하나로 합쳤다(2026-08-20).
//   센서/명령 구분은 **장치가 등록에서 말하는 `kind` 첫 글자**가 한다.
Spot& Spot::module(const std::string& devid, const std::string& name) {
    lot_->areas_[idx_].modules.push_back(ParkingLot::Attach(devid, name));
    return *this;
}
Spot& Spot::module(const std::string& name) {
    // devid 를 안 준 형태 = "아무 장치나 그 이름을 가진 것"
    lot_->areas_[idx_].modules.push_back(ParkingLot::Attach("", name));
    return *this;
}
Spot& Spot::parking() {
    lot_->areas_[idx_].kind = "parking";
    return *this;
}
Spot& Spot::at(int row, int col) {
    // ⚠ 음수는 **자동 배치**로 되돌린다. 0-기준 격자라 음수 자리는 없다.
    ParkingLot::Area& a = lot_->areas_[idx_];
    a.row = (row < 0) ? -1 : row;
    a.col = (col < 0) ? -1 : col;
    return *this;
}

Spot& Spot::behavior(SpotBehavior& b) {
    lot_->areas_[idx_].behavior = &b;      // 🔑 참조를 주소로 든다. 사본이 아니다
    return *this;
}

Spot ParkingLot::spot(const std::string& id) {
    for (size_t i = 0; i < areas_.size(); i++)
        if (areas_[i].id == id) return Spot(this, i);      // 같은 자리를 두 번 불러도 안전하다
    // 🔴 **기본은 "area"(일반영역)다.** 주차 자리로 만들려면 `.parking()` 을 **적어야** 한다 —
    //   안 적었을 때 **점유·예약이 안 생기는 쪽**이 조용히 틀리지 않는다.
    Area a; a.id = id; a.kind = "area";
    areas_.push_back(a);
    return Spot(this, areas_.size() - 1);
}

// 🔴 `ParkingLot::gate()` 가 여기 있었다. **없앴다**(v2) — `spot(...).label("입구")` 로 만든다.

Spot& Spot::label(const std::string& text) {
    lot_->areas_[idx_].label = text;
    return *this;
}
void ParkingLot::label(const std::string& devid, const std::string& module, const std::string& text) {
    // 🔑 **덮어쓴다.** 같은 모듈에 이름이 둘이면 어느 것이 참인지 아무도 모른다 —
    //   나중 선언이 이기는 것이 조용히 갈리는 것보다 낫다(`control` 과 같은 규칙).
    labels_[devid + "\t" + module] = text;
}
Control ParkingLot::control(const std::string& devid, const std::string& name) {
    // 🔑 **같은 (devid,name) 은 덮어쓴다.** 두 벌이 생기면 화면에 버튼이 둘 뜨고
    //   어느 것이 참인지 아무도 모른다 — 조용히 갈리는 것보다 나중 선언이 이기는 것이 낫다.
    for (size_t i = 0; i < controls_.size(); i++)
        if (controls_[i].devid == devid && controls_[i].name == name)
            return Control(this, i);
    ControlDecl c; c.devid = devid; c.name = name;
    controls_.push_back(c);
    return Control(this, controls_.size() - 1);
}
Control& Control::toggle() {
    lot_->controls_[idx_].widget = ControlDecl::TOGGLE; return *this;
}
Control& Control::number(long vmin, long vmax) {
    ControlDecl& c = lot_->controls_[idx_];
    c.widget = ControlDecl::NUMBER; c.vmin = vmin; c.vmax = vmax; return *this;
}
Control& Control::choice() {
    lot_->controls_[idx_].widget = ControlDecl::CHOICE; return *this;
}
Control& Control::option(long value, const std::string& label) {
    // ⚠ `choice()` 없이 부르면 위젯이 `NONE` 인 채로 목록만 쌓인다.
    //   **막지 않는다 — 기동 로그가 지목해 말한다.** 증상이 보이는 오류는 막는 것보다 말하는 것이 낫다.
    lot_->controls_[idx_].options.push_back(std::make_pair(value, label));
    return *this;
}

struct ParkingServer::Impl {
    ParkingLot lot;      // 🔑 **사본을 든다** — 호출자가 뒤에 표를 바꿔도 서버가 안 흔들린다
    Server     srv;
};

ParkingServer::ParkingServer(const ParkingLot& lot) : p_(new Impl) {
    p_->lot = lot;
    p_->srv.lot_ = &p_->lot;
}
bool ParkingServer::send(const std::string& devid, const std::string& moduleName, long value) {
    return p_->srv.send_to_module(devid, moduleName, value);
}
ParkingServer::Batch& ParkingServer::Batch::add(const std::string& moduleName, long value) {
    items_.push_back(std::make_pair(moduleName, value));
    return *this;
}
ParkingServer::BatchResult ParkingServer::Batch::send() {
    BatchResult r;
    srv_->p_->srv.send_batch(devid_, items_, &r.queued, &r.rejected);
    items_.clear();               // 🔑 두 번 보내지 않는다
    return r;
}
int ParkingServer::maxPerBatch() const { return p_->srv.max_per_batch(); }
long long ParkingServer::nowMs() const { return now_ms(); }
void ParkingServer::onCommandResult(CmdResultFn fn) { p_->srv.cmd_cb_ = fn; }
bool ParkingServer::deviceReady(const std::string& devid) const {
    const Node* n = p_->srv.node_by_devid(devid);
    return n && n->reg_done;
}

ParkingServer::~ParkingServer() { delete p_; }

bool ParkingServer::openPorts()    { return p_->srv.openPorts(); }
bool ParkingServer::serveOneTick() { return p_->srv.serveOneTick(); }
void ParkingServer::closeDown()    { p_->srv.closeDown(); }

// 🔴 **`#include "lot.cpp"` 가 여기 있었다. 2026-08-20 에 뺐다.**
//   `lot.cpp` 는 이제 **진짜 번역 단위**다 — 훅 셋의 선언이 `parking.h` 에 있고
//   링크가 그 둘을 잇는다.
//   🔑 그래서 **폴더의 `.cpp` 를 전부 컴파일하는 것이 정답이 된다**(Visual Studio 의 기본값).
//   ⚠ 빌드가 두 줄이 됐다: `c++ -c server.cpp && c++ -c lot.cpp && c++ *.o -o srv`
//     (또는 `c++ -o srv server.cpp lot.cpp` 한 줄)
#include "entry.h"      // 엔진 진입점 — `main()` 은 여기 있다(기여자는 안 연다)



