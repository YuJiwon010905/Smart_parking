# Multi-Arduino Server 변경 설명

## 핵심 원칙

Server와 Arduino가 모듈을 구분하는 전역 신원은 `(Arduino ID, module name)`이다. 다른 Arduino와 같은 모듈명을 사용하는 것은 정상이며, 같은 Arduino 안의 모듈명만 중복되지 않아야 한다.

## 현재 배치

| 주차면/영역 | Sensor address | Actuator address |
| --- | --- | --- |
| A1 | P1/A1 | P1/L1 |
| A2 | P1/A2 | P1/L2 |
| A3 | P1/A3 | P1/L3 |
| A4 | P2/A1 | P2/L1 |
| E1 | P1/U1 | P1/ED |
| X1 | P1/U2 | P1/XD |

`A4`는 서버의 주차면 id이고 `P2/A1`은 실제 센서 주소다. 두 이름을 같게 만들 필요가 없다.

## Server 라우팅

모듈 제어와 준비 확인은 항상 두 값을 모두 지정한다.

```cpp
srv.send("P2", "L1", 1);
srv.moduleReady("P2", "L1");
```

모듈명만 받아 전체 node에서 owner를 찾던 API는 제거했다. 자리 단위 R/C/T는 runtime `Zone`에 복합 주소로 결속된 센서의 Arduino로 보낸다.

Server의 global spot과 Arduino의 local sensor name은 Pending에서 별도 값이다. 예를 들어 A4 예약은 P2 socket으로 `R,<rid>,A1,...`을 보내며, P2가 `A1`을 echo한 ACK는 Server의 A4 상태에 반영한다.

`Pending.devid`는 ACK 발신 Arduino 검증에 사용한다. UI의 pending 상태도 `devid + local module index`를 비교하여 P1/L1과 P2/L1을 섞지 않는다.

## Sensor callback

센서 콜백은 자리 id와 함께 `devid`, `module`을 모두 전달한다.

```cpp
void onOccupancy(ParkingServer& srv,
                 const std::string& spot,
                 const std::string& devid,
                 const std::string& module,
                 bool occupied,
                 const SensorMeasure& measure);
```

직전 센서 상태도 `(devid,module)`로 보관한다.

## Disconnect/Reconnect

Arduino 연결이 끊기면 해당 `devid`의 runtime 결속과 queue만 정리한다. 재연결 후 같은 `(devid,module)` 등록이 오면 조립표의 자리에 다시 결속한다.

## 검증 결과

```text
Server MSVC Debug x64 rebuild: PASS
P1 Arduino Uno compile: PASS
P2 Arduino Uno compile: PASS
P1/P2 same-name A1/L1 registration: PASS
P1/L1 -> P1 socket local idx 7: PASS
P2/L1 -> P2 socket local idx 1: PASS
Global A4 reservation -> P2 local A1 wire token: PASS
P2 local A1 ACK -> Server global A4 mapping: PASS
Hardware test: NOT PERFORMED
Physical integration test: NOT PERFORMED
```

전체 파일별 수정 내역은 `../../COMPOSITE_MODULE_ID_FIX.md`를 참조한다.
