# Arduino ID + Module Name 복합 식별자 수정 명세

## 1. 수정 목적

다중 Arduino 확장본이 모듈명을 전체 node에서 고유하게 만들고 module name만으로 owner를 다시 찾던 문제를 수정했다. 현재 적용한 유일 식별자는 원 구성과 같은 `(Arduino ID, module name)` 복합 키다.

## 2. 수정 전 문제

수정 전에는 `module name → 전체 online Arduino 검색 → owner 재생성` 과정이 추가되어 있었다. 이 때문에 다음 문제가 발생했다.

- `P1/A1`과 `P2/A1`을 다른 모듈로 취급하지 못함
- P2에 `A4/L4`처럼 전역 중복을 피하는 새 모듈명을 만들어야 했음
- 주차면 id와 Arduino 로컬 module name이 불필요하게 결합됨
- 조립표에 이미 있는 device 정보를 name-only `module_owner()`가 다시 추론함
- UI pending 판정이 module index만 비교해 다른 Arduino의 같은 index를 섞을 수 있었음

## 3. 적용한 식별 규칙

다음 주소는 모두 서로 다른 모듈이다.

```text
P1/A1
P2/A1
P1/L1
P2/L1
```

같은 Arduino 하나에 같은 module name을 두 번 등록하는 것은 금지한다. 이 경우에는 복합 키도 둘을 구분할 수 없다.

## 4. 현재 주차장 매핑

| Server spot | Sensor address | Actuator address |
| --- | --- | --- |
| A1 | P1/A1 | P1/L1 |
| A2 | P1/A2 | P1/L2 |
| A3 | P1/A3 | P1/L3 |
| A4 | P2/A1 | P2/L1 |
| E1 | P1/U1 | P1/ED |
| X1 | P1/U2 | P1/XD |

P2의 로컬 module name은 `A1/L1`이지만 Server spot `A4`에 복합 주소로 연결된다.

## 5. 파일별 수정 내역

### `VS_server/server_multi/lot.cpp`

- wildcard module 선언을 `(devid,module)` 선언으로 변경
- A4를 P2/A1, P2/L1에 연결
- LED/Gate 대상을 `ModuleAddress { devid, module }`로 명시
- `send()`, `moduleReady()`, `CmdResult` 처리를 복합 주소 기준으로 변경
- sensor callback의 U1/U2 판정도 P1/U1, P1/U2로 명확화

### `VS_server/server_multi/lot.h`

- 조립표에 없는 `(devid,module)`을 module name만으로 자리 id에 추측 결속하던 fallback 제거
- 같은 module name이라도 devid가 다르면 정상 결속
- 중복 판정을 동일한 `(devid,module)` 쌍으로 한정

### `VS_server/server_multi/nodes.h`

- name-only `module_owner(module)` 제거
- R/C/T는 Zone에 복합 주소로 결속된 sensor node로 라우팅
- 센서 직전 상태 key를 `(devid,module)`로 변경
- 센서 callback에 `devid`를 함께 전달
- 미결속 로그가 누락된 복합 주소와 필요한 `lot.cpp` 선언을 안내하도록 개선

### `downlink.h`, `http.h`, `wsapi.h`

- 자리 id와 같은 module name을 찾던 방식을 제거
- runtime Zone의 복합 센서 결속으로 예약/취소/테스트 대상 Arduino 결정
- Pending에 Server global spot과 Arduino local wire module을 별도로 보존
- A4 예약/취소/테스트는 P2에 기존 frame 형식 그대로 local `A1` token으로 전송

### `parking.h`, `spot.h`, `server.cpp`, `uplink.h`

- name-only `send`, `moduleReady`, `moduleDevice` API 제거
- 공개 제어 API를 복합 주소 형식으로 단일화
- occupancy/value callback 계약에 `devid` 추가

### `wsjson.h`

- G pending 판정을 `devid + local module index`로 변경

### `ardu/parking_p2/parking_p2.ino`

- 전역 고유화를 위해 만든 `A4/L4`를 P2 로컬 `A1/L1`로 복원
- 핀은 D2(IR), D11(LED)로 유지

### 문서

- `README_MULTI.md`를 복합 식별자 규칙으로 개정
- `multiserver_설명.md`의 name-only owner 설명을 현재 구조로 교체
- 본 문서 추가

## 6. Communication 변경

전선 protocol 형식은 변경하지 않았다.

```text
S/D/A/G/R/C/T/M/Q frame: No change
Checksum: No change
TCP port structure: No change
HTTP/WebSocket base protocol: No change
```

변경된 것은 Server 내부의 식별·결속·라우팅 기준과 P2가 D registration으로 보고하는 로컬 module name이다.

## 7. 검증

### Static verification

```text
name-only module_owner code path: REMOVED
name-only send/moduleReady/moduleDevice API: REMOVED
lot assembly composite addresses: VERIFIED
P2 local A1/L1 registration: VERIFIED
git diff --check: PASS
```

### Compile

```text
Server MSVC Debug x64 rebuild: PASS
P1 Arduino Uno compile: PASS
  Flash 30,466 / 32,256 bytes (94%)
  SRAM 1,386 / 2,048 bytes; remaining 662 bytes
P2 Arduino Uno compile: PASS
  Flash 27,344 / 32,256 bytes (84%)
  SRAM 1,284 / 2,048 bytes; remaining 764 bytes
```

### Synthetic integration

P1과 P2가 동시에 같은 `A1/L1`을 등록하도록 TCP client를 구성했다.

```text
P1/A1 + P2/A1 registration: PASS
P1/L1 command -> P1 socket, local idx 7: PASS
P2/L1 command -> P2 socket, local idx 1: PASS
Global spot A4 reservation -> P2 wire module A1: PASS
P2 ACK echo A1 -> Server global spot A4 update: PASS
```

관찰한 G frame은 다음과 같다.

```text
P1: G,<rid>,7,1,...
P2: G,<rid>,1,0,...
P2: R,<rid>,A1,12345678,...
```

### Hardware / physical integration

```text
Hardware test: NOT PERFORMED
Physical P1/P2 integration test: NOT PERFORMED
```

## 8. 변경 후 동작

```text
P1 local A1 → Server spot A1
P2 local A1 → Server spot A4

P1 local L1 ← Server 1번 안내등 명령
P2 local L1 ← Server 4번 안내등 명령
```

모듈 배치를 바꿀 때 Arduino의 로컬 이름을 전역 고유하게 재생성하지 않고 `lot.cpp`의 `(devid,module)` 매핑을 실제 배치에 맞게 수정한다.

## 9. 남은 검증

- P1/P2 실물 Uno upload
- P1/P2 동시 ESP/Wi-Fi/TCP 연결
- P2/A1 IR → Server A4 상태 반영
- Server 4번 안내등 → P2/L1 → ACK → S echo 왕복
- A1~A4 자동 입차/오주차/출차 실물 시나리오
