# Multi-Arduino Server 변경 설명

## 1. 문서 목적

이 문서는 `VS_server/server_multi`가 기존 `VS_server/server`에서 무엇이 달라졌는지 공동 작업자가 빠르게 이해하도록 정리한 문서다.

중점:

```text
여러 Arduino 동시 연결
Arduino별 상태 분리
Module 소유 Arduino 자동 탐색
대상 Arduino별 하행 명령
ACK 발신 Arduino 검증
Disconnect 후 module 재결속
```

기존 서버 소스는 보존되어 있다.

```text
기존: VS_server/server/
다중 Arduino 사본: VS_server/server_multi/
```

---

## 2. 현재 시험 구성

현재 `lot.cpp`와 Arduino sketch가 기대하는 구성은 다음과 같다.

| Device | Sensor | Actuator |
| --- | --- | --- |
| P1 | A1, A2, A3, U1, U2 | ED, XD, L1, L2, L3 |
| P2 | A4 | L4 |

현재 활성 주차면:

```text
A1, A2, A3, A4
```

`lot.cpp` 42~43행의 A5/L5는 주석 처리되어 있으며 60행의 `PARKING_SLOT_COUNT`는 4다.

```cpp
const int PARKING_SLOT_COUNT = 4;
```

따라서 현재 자동 입차 controller는 A1~A4와 L1~L4까지만 요구한다.

---

## 3. 가장 중요한 설계 변경

### 3.1 Device를 `lot.cpp`에 고정하지 않는다

기존 방식:

```cpp
lot.spot("A1")
    .module("P1", "A1")
    .module("P1", "L1");
```

현재 방식 (`lot.cpp` 33~41행):

```cpp
lot.spot("A1")
    .module("A1")
    .module("L1");
```

Device ID를 생략한 `module(name)`은 wildcard 선언이다. 실제 소유 Arduino는 부팅 후 들어오는 `D` registration으로 결정된다.

```text
P1 D,A1,IP -> A1 owner는 P1
P2 D,A4,IP -> A4 owner는 P2
P2 D,L4,OG -> L4 owner는 P2
```

Module을 다른 Arduino로 옮길 때 Server의 device ID를 고칠 필요가 없다. 해당 Arduino sketch의 registration만 옮긴다.

### 3.2 Module 이름은 전체 Arduino에서 고유해야 한다

Server의 `module_owner()`는 같은 이름을 등록한 Node를 찾는다 (`nodes.h` 638행).

```cpp
Node* module_owner(const std::string& module);
```

동시에 두 Arduino가 `L4`를 등록하면 owner를 임의 선택하지 않고 충돌로 처리한다.

필수 규칙:

```text
A1~A4, U1, U2, ED, XD, L1~L4는 전체 online Arduino에서 각각 하나만 등록
한 Arduino 내부에서도 module name 중복 금지
```

### 3.3 Module index는 Arduino 내부 주소다

각 Arduino의 `D` 등록 순서가 해당 장치의 `G idx`와 `S` bit 위치다.

예:

```text
P1 index 0 = A1
P2 index 0 = A4
```

P2 index 0을 global A1로 읽으면 안 된다. `sync_parking_slots_from_nodes()`가 `(devid,module)` Zone binding을 사용해 P2 index 0을 A4로 변환한다 (`nodes.h` 455행).

---

## 4. 파일별 주요 변경 사항

### `lot.cpp`

변경 목적:

```text
4개 주차면 중앙 제어
Device-independent module 선언
Module 이름으로 LED/Gate 명령
```

핵심 변경:

* A1~A4/L1~L4를 wildcard module로 선언.
* U1/ED, U2/XD도 device를 고정하지 않음.
* `PARKING_SLOT_COUNT=4`.
* `slotKnown`, `occupied`, `entryBaseline` 배열을 상수 크기로 관리.
* `parkingIndex()`는 Arduino module index가 아니라 Zone/spot 이름 A1~A4를 사용.
* `queueEntryBatch()`는 `srv.send("L1", ...)`처럼 module 이름으로 owner를 자동 탐색.
* 입차 전에 ED와 L1~L4의 `moduleReady()` 확인.
* 출차 전에 XD의 `moduleReady()` 확인.

현재 입차 하행:

```text
L1~L4: 배정 자리만 ON, 나머지는 OFF
ED: OPEN(1) 또는 CLOSE(2)
```

주의:

```text
여러 Arduino에 걸친 명령은 하나의 실행 transaction이 아니다.
P1 명령 성공 후 P2 명령이 실패하는 부분 성공이 가능하다.
각 명령은 별도 rid와 ACK를 가진다.
```

### `server.cpp`

`Pending`에 target device를 추가했다 (`server.cpp` 117행).

```cpp
std::string devid;
```

Module 이름만으로 명령할 수 있는 public API를 추가했다.

```cpp
bool ParkingServer::send(const std::string& moduleName, long value);
bool ParkingServer::moduleReady(const std::string& moduleName) const;
std::string ParkingServer::moduleDevice(const std::string& moduleName) const;
```

`deviceReady()`는 등록 완료뿐 아니라 실제 online 상태도 확인한다.

### `parking.h`

공개 API 선언이 추가됐다.

```cpp
bool send(const std::string& moduleName, long value);
bool moduleReady(const std::string& moduleName) const;
std::string moduleDevice(const std::string& moduleName) const;
```

기존 API도 유지된다.

```cpp
bool send(const std::string& devid,
          const std::string& moduleName,
          long value);
```

Device를 정확히 알고 강제로 지정해야 할 때는 기존 API를 사용할 수 있다.

### `node.h`

기존에는 module bit 일부만 Node별이었다. 현재는 다음 상태도 Arduino별로 보관한다.

```text
seq / uptime
occupied module bits
reservation bits
test override bits
registration state
last frame time
DMAX/window state
```

`session_reset()`은 특정 Node의 session 상태만 초기화한다.

### `state.h`

`DownQ`에 대상 Arduino가 추가됐다 (`state.h` 113행).

```cpp
std::string devid;
```

Queue 자체는 Server에 하나지만 각 항목에 `devid`가 있어 다른 Arduino의 송신 창에서 섞여 나가지 않는다.

### `nodes.h`

주요 추가/변경:

```text
module_owner(module)
sync_parking_slots_from_nodes()
node_send_failed()
all_nodes() const/non-const
추가 Arduino도 상·하행 활성
```

`module_owner()`는 D registration 목록에서 owner를 찾는다.

`sync_parking_slots_from_nodes()`는 각 Node의 local bit를 global A1~A4로 변환한다.

### `lot.h`

Disconnect된 Arduino의 실제 binding을 제거하는 `unbindDevice()`가 추가됐다 (`lot.h` 109행).

필요한 이유:

```text
기존: A4가 P2에 결속
P2 disconnect
A4를 P3 sketch로 이동
P3 reconnect
```

P2 binding을 제거하지 않으면 P3의 A4가 duplicate owner로 거절된다.

### `serve.h`

기존 aux Node는 `D`만 처리하고 `S/A`를 버렸다.

현재는 P2/P3도 다음 frame을 모두 `on_ard_line(Node&, line)`으로 전달한다.

```text
S: 상태
D: registration
A: ACK
V: 숫자 sensor 값
```

Connection 종료 시 해당 device queue와 binding만 제거한다.

### `uplink.h`

주요 변경:

* 모든 parser가 인자로 받은 `Node& n`을 사용.
* `decode_mod_bits(n, ...)`로 node별 module count 적용.
* S의 device ID와 socket이 알고 있는 `n.devid`가 다르면 폐기.
* 상태 갱신 후 `sync_parking_slots_from_nodes()` 실행.
* `flush_downq(n, ...)`로 해당 Node의 queue만 송신.
* ACK 처리 전에 `Pending.devid == n.devid` 검사 (`uplink.h` 442행).

ACK device 검증이 필요한 이유:

```text
P2 명령 rid=10
P1에서 우연히 rid=10 ACK 도착
```

이 경우 P2 Pending을 완료하면 안 된다.

### `wire.h`

`flush_downq()`가 target Node를 받도록 변경됐다 (`wire.h` 271행).

```cpp
void flush_downq(Node& node,
                 const char* why,
                 bool ignore_window,
                 int max_n);
```

Queue에서 `q.devid == node.devid`인 줄만 payload로 만들고 `node.fd`에 전송한다.

Disconnect 시 `clear_downq_for(devid, why)`로 해당 장치 queue만 실패 처리한다.

### `downlink.h`

주요 변경:

* R/C target을 주차면 sensor module owner로 결정.
* G command의 target `devid`를 Pending/DownQ에 저장.
* `send_to_module()`에서 target Node registration/online 확인.
* Q registration recovery를 모든 Node에 수행.
* S가 없을 때 DMAX fallback도 Node별로 수행.
* `CmdResult.devid`를 실제 `Pending.devid`로 설정.

### `metrics.h`

`node_online(const Node&)`가 추가됐다.

P1 전체 상태 대신 실제 command 대상 Node가 online인지 확인할 수 있다.

P1 session 종료 시 P1 binding과 P1 queue만 정리한다.

### `wsapi.h` / `http.h`

Web command와 번호판 예약이 P1 online 여부만 보지 않고 실제 target module/slot owner Node를 확인한다.

예:

```text
P1 online, P2 offline, A4 예약 요청
-> P1이 online이어도 A4 owner P2가 offline이므로 거절
```

### `wsjson.h` / `index.html`

기존 `device` object는 P1 호환용으로 유지한다.

새 `devices[]` 배열에는 모든 Arduino 상태가 들어간다.

```json
"devices": [
  {"device_id":"P1","online":true,"uptime":100,"seq":20},
  {"device_id":"P2","online":true,"uptime":80,"seq":18}
]
```

UI 상단은 연결된 Arduino ID와 online 수를 표시한다.

```text
P1, P2
연결됨 (2/2)
```

Zone/module 표시는 기존 `(devid,module)` 구조를 사용한다.

### `persist.h`

`data_log.json`에도 `devices[]`를 저장한다.

Slot 목록은 실제 parking Zone A1~A4만 저장한다.

### `server.vcxproj` / `.filters`

복사 당시 존재하지 않던 `*_codex_fixed.*` 참조를 제거했다.

기존 project에서 제외돼 있던 다음 파일을 Debug x64 build에 포함했다.

```text
lot.cpp
server.cpp
```

---

## 5. Runtime 흐름

### 5.1 연결과 등록

```text
Arduino TCP connect
    -> first valid S에서 DEVICE_ID 확인
    -> Node(P1/P2) 생성 또는 재연결
    -> D,*에서 module count/drain 확인
    -> D,name,kind 누적
    -> reg_done
    -> Lot.bind(devid, modules)
```

### 5.2 Sensor 상태

```text
P2 IR A4
    -> P2 node.occMask local bit 0
    -> S frame
    -> on_ard_line(P2, S)
    -> P2.mod_bits[0]
    -> Zone binding (P2,A4)
    -> global parking A4
    -> onOccupancy(... spot=A4, module=A4)
```

### 5.3 LED 명령

```text
Server: srv.send("L4", 1)
    -> module_owner("L4") = P2
    -> P2 registration에서 L4 local idx 확인
    -> Pending.devid=P2
    -> DownQ.devid=P2
    -> 다음 P2 S frame에서 flush_downq(P2)
    -> P2 socket으로 G,rid,idx,1
    -> P2 cmdLed4()
```

### 5.4 ACK

```text
P2 A,rid,Gx,0
    -> Pending lookup
    -> Pending.devid와 P2 비교
    -> 일치할 때만 Pending 완료
    -> CmdResult{devid=P2,module=L4,value=1,OK}
```

### 5.5 Disconnect

```text
P2 disconnect
    -> P2 queue 실패 처리
    -> P2 module state unknown
    -> Lot.unbindDevice("P2")
    -> A4/L4 owner 없음
    -> UI node_offline/module_absent
```

---

## 6. Build와 실행

### Visual Studio

다음 project를 연다.

```text
VS_server/server_multi/server.vcxproj
```

권장 configuration:

```text
Debug | x64
```

### Developer PowerShell

```powershell
cd C:\dev\Smart_parking\VS_server\server_multi
msbuild server.vcxproj /t:Rebuild /p:Configuration=Debug /p:Platform=x64
```

출력:

```text
VS_server/server_multi/x64/Debug/server.exe
```

시험 포트 실행 예:

```powershell
.\x64\Debug\server.exe `
  --port-web=12090 `
  --port-ardu=12191 `
  --port-cam=12211
```

Arduino `Config.h`의 Server IP/port와 실행 Server가 일치해야 한다.

---

## 7. 현재까지 검증된 내용

```text
MSVC Debug x64 rebuild: PASS
P1/P2/P3 synthetic simultaneous TCP registration: PASS
Node별 S 상태 분리: PASS
Module-owner 기반 G routing: PASS
P1/P2/P3 socket별 명령 분리: PASS
Device-matched ACK 6건: PASS
devices[] JSON parse: PASS
index.html inline JavaScript syntax: PASS
Physical hardware test: NOT PERFORMED
```

현재 실제 사용할 P1-original + P2-A4/L4 조합은 sketch compile까지 완료됐고 physical integration은 아직 수행하지 않았다.

---

## 8. 알려진 제한과 주의사항

### Cross-device transaction

L1~L4와 ED 명령은 여러 Arduino에 분산될 수 있다. 모든 명령이 동시에 성공한다는 보장은 없다.

```text
P1 LED 성공
P2 LED 실패
ED OPEN 성공
```

같은 부분 성공이 가능하다. 안전 요구가 높아지면 LED ACK 확인 후 Gate OPEN으로 넘어가는 controller 상태가 필요하다.

### Test command

Slot 없는 `test_arm/test_disarm`은 호환을 위해 primary `park` Node 중심이다. 모든 Arduino broadcast가 필요하면 별도 결과 집계가 필요하다.

### Metrics

기능 routing은 multi-node지만 일부 장기 soak/health summary는 여전히 primary P1 중심이다.

### Module 이동

Module을 다른 Arduino로 옮길 때 두 장치가 동시에 같은 이름을 등록하지 않도록 한다.

권장 순서:

```text
기존 owner 전원/연결 종료
새 sketch upload
새 owner 연결
D registration/Zone binding 확인
```

### Original Server와 혼동 금지

실행 파일과 project 경로를 확인한다.

```text
사용: VS_server/server_multi/x64/Debug/server.exe
기존: VS_server/server/... (multi-node full control 미지원)
```

---

## 9. 공동 작업자 변경 체크리스트

Module 추가/이동 시:

```text
[ ] Arduino DEVICE_ID가 고유한가
[ ] Module 이름이 전체 Arduino에서 고유한가
[ ] Arduino sketch에 실제 pin/handler가 있는가
[ ] node.sensor/node.actuator 등록 이름이 2글자인가
[ ] lot.cpp에 module(name) wildcard 선언이 있는가
[ ] D registration 완료 로그가 있는가
[ ] Web map에서 실제 (devid,module)로 보이는가
[ ] G command가 올바른 socket으로 나가는가
[ ] ACK의 devid/module이 기대값과 같은가
[ ] Disconnect/reconnect 후 binding이 복구되는가
```

