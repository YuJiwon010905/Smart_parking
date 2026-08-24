# P1 / P2 Arduino Sketch 변경 설명

## 1. 문서 목적

이 문서는 현재 멀티 디바이스 시험에 사용하는 두 Arduino sketch를 공동 작업자가 이해하고 수정할 수 있도록 설명한다.

대상:

```text
ardu/parking_p1/parking_p1.ino
ardu/parking_p2/parking_p2.ino
```

현재 역할:

| Device | 역할 |
| --- | --- |
| P1 | 기존 Smart Parking hardware 전체 유지 |
| P2 | 신규 A4 IR + L4 LED 멀티 디바이스 시험 |

두 sketch는 서로 다른 firmware다. 같은 폴더에 두 `.ino`를 넣지 않는다.

```text
parking_p1/parking_p1.ino
parking_p2/parking_p2.ino
```

Arduino IDE는 한 폴더 안의 모든 `.ino`를 하나로 합쳐 compile한다. 각 폴더에는 폴더명과 같은 main `.ino` 하나만 둔다.

---

## 2. 현재 Hardware/Module 구성

### P1

`parking_p1.ino`는 기존 최초 구성의 기능을 유지한다.

#### Sensor

| Module | Pin | 처리 |
| --- | --- | --- |
| A1 | D2 | Active-low IR + 100ms debounce |
| A2 | D3 | Active-low IR + 100ms debounce |
| A3 | D4 | Active-low IR + 100ms debounce |
| U1 | Trig D7 / Echo D8 | 입구 초음파, 10cm 미만 감지 |
| U2 | Trig D9 / Echo D10 | 출구 초음파, 10cm 미만 감지 |

#### Actuator

| Module | Pin | 명령 |
| --- | --- | --- |
| ED | Servo D5 | `1=OPEN`, `2=CLOSE` |
| XD | Servo D6 | `1=OPEN`, `2=CLOSE` |
| L1 | D11 | `0=OFF`, nonzero=ON |
| L2 | D12 | `0=OFF`, nonzero=ON |
| L3 | D13 | `0=OFF`, nonzero=ON |

#### Communication

| 기능 | Pin |
| --- | --- |
| ESP TX -> Uno RX | A0 |
| Uno TX -> ESP RX | A1 |
| ESP reset option | A2 (`Config`/link header 설정 확인) |

### P2

P2는 최소 멀티 디바이스 시험 Node다.

| Module | Pin | 처리 |
| --- | --- | --- |
| A4 | D2 | Active-low IR + 100ms debounce |
| L4 | D11 | `0=OFF`, nonzero=ON |

P2에는 Servo, ultrasonic, A3, L3, XD 코드가 없다.

---

## 3. Device ID

### P1 (`parking_p1.ino` 37행)

```cpp
#define DEVICE_ID "P1"
```

### P2 (`parking_p2.ino` 13행)

```cpp
#define DEVICE_ID "P2"
```

`DEVICE_ID`는 다음에 사용된다.

```text
TCP connection 승격
S frame device field
Server Node lookup
Module owner binding
ACK target 검증
```

P1/P2가 같은 ID를 사용하면 같은 Node의 재접속으로 해석되어 서로 연결을 교체할 수 있다.

필수 규칙:

```text
P1 sketch -> DEVICE_ID P1
P2 sketch -> DEVICE_ID P2
각 ID는 1~8자
영문/숫자/_/-만 사용
```

---

## 4. Module registration

### P1 registration (`parking_p1.ino` 384~396행)

```cpp
node.sensor("A1").on(readIR1);
node.sensor("A2").on(readIR2);
node.sensor("A3").on(readIR3);
node.sensor("U1").on(readUltrasonic1);
node.sensor("U2").on(readUltrasonic2);

node.actuator("ED").on(cmdEntryGate);
node.actuator("XD").on(cmdExitGate);
node.actuator("L1").on(cmdLed1);
node.actuator("L2").on(cmdLed2);
node.actuator("L3").on(cmdLed3);
```

P1 D registration/local index:

| Local idx | Module | Kind |
| ---: | --- | --- |
| 0 | A1 | IP |
| 1 | A2 | IP |
| 2 | A3 | IP |
| 3 | U1 | IP |
| 4 | U2 | IP |
| 5 | ED | OG |
| 6 | XD | OG |
| 7 | L1 | OG |
| 8 | L2 | OG |
| 9 | L3 | OG |

### P2 registration (`parking_p2.ino` 59~60행)

```cpp
node.sensor("A4").on(readIR4);
node.actuator("L4").on(cmdLed4);
```

P2 D registration/local index:

| Local idx | Module | Kind |
| ---: | --- | --- |
| 0 | A4 | IP |
| 1 | L4 | OG |

중요:

```text
P1 idx 0 = A1
P2 idx 0 = A4
```

Index는 Arduino 내부 주소다. Server는 D registration의 module 이름과 `lot.cpp` Zone binding을 이용해 global A1/A4로 변환한다.

등록 순서를 바꾸면 G idx와 S bit 위치도 바뀌지만 Server가 매 부팅 D 등록을 다시 받으므로 이름 기반 명령은 계속 동작한다. 단, 실행 중 firmware만 바꾸는 것이 아니라 반드시 Arduino를 재부팅/재연결해야 한다.

---

## 5. P1 변경 사항

P1은 기존 기능 구성을 복원했지만 다음 안정성 수정이 포함되어 있다.

### 5.1 NODE_PROFILE 제거

P1 file은 P1 firmware 전용이므로 profile 분기를 사용하지 않는다.

```cpp
#define DEVICE_ID "P1"
```

한 file에서 P1/P2/P3를 전처리기로 나누는 대신 각 sketch가 자기 hardware만 가진다.

### 5.2 Entrance CLOSE control-flow 수정

기존 코드는 Servo가 attach된 경우 `GATE_CLOSE` write와 `return true`를 건너뛸 수 있었다.

현재 `cmdEntryGate(2)`:

```cpp
if (!enterServo.attached()) {
  enterServo.attach(Servo_1);
}
enterServo.write(GATE_CLOSE);
enterDetachPending = true;
enterDetachAt = millis();
return true;
```

따라서 Servo attach 상태와 관계없이 CLOSE 명령을 실행한다.

### 5.3 Gate echo 수정

일반 actuator는 `arg != 0`이면 ON echo다. Gate는 `2=CLOSE`이므로 기본 규칙을 사용하면 CLOSE 후에도 OPEN echo가 된다.

P1은 다음 hook을 사용한다 (`parking_p1.ino` 283~288행).

```cpp
static bool commandEchoValue(const char* name, uint32_t arg) {
  if (strcmp(name, "ED") == 0 || strcmp(name, "XD") == 0)
    return arg == 1;
  return arg != 0;
}
```

결과:

```text
ED/XD arg=1 -> echo ON(open)
ED/XD arg=2 -> echo OFF(closed)
LED arg=0   -> echo OFF
LED arg!=0  -> echo ON
```

이 hook을 사용하기 위해 P1 folder의 `Modules.h`에도 `COMMAND_ECHO_VALUE` 분기가 추가되어 있다.

### 5.4 Ultrasonic 상세 trace 분리

P1은 module 10개와 Servo library를 포함해 Uno flash 사용량이 높다.

```cpp
#define DEBUG 1
#define ULTRASONIC_TRACE 0
```

일반 DEBUG는 유지하지만 U1/U2가 200ms마다 출력하는 거리 상세 로그만 끈다.

처음 상세 trace를 켠 compile 결과:

```text
32,370B / 32,256B
114B 초과 -> FAIL
```

Trace를 끈 현재 결과:

```text
30,466B / 32,256B (94%)
```

`ULTRASONIC_TRACE=1`로 바꾸면 다시 Uno flash 상한을 넘을 수 있다.

---

## 6. P1 Sensor 처리

### 6.1 IR

P1 A1~A3는 `readDebouncedActiveLow()`를 사용한다.

```text
LOW  -> occupied
HIGH -> empty
candidate가 DEBOUNCE_MS(100ms) 유지될 때 stable 상태 변경
```

현재 `setup()`은 IR pin을 `INPUT`으로 설정한다.

실제 IR module이 출력 신호를 직접 HIGH/LOW로 구동하면 `INPUT`을 사용한다. Open-collector 또는 단순 switch라면 `INPUT_PULLUP`이 필요할 수 있다.

### 6.2 Ultrasonic

공통 상수:

```text
감지 기준: 10cm 미만
측정 주기: 200ms
pulseIn timeout: 20,000us
안정화: 최근 boolean 표본 기반 StableNearState
```

측정 사이에는 cache된 결과를 반환해 매 loop에서 `pulseIn()`이 실행되지 않도록 한다.

주의:

```text
pulseIn()==0은 현재 CLEAR로 처리
sensor fault와 실제 미감지를 구분하지 않음
Hardware test에서 반드시 timeout 빈도를 확인
```

---

## 7. P1 Gate/Servo 처리

명령 값:

```text
1 = OPEN  (90도)
2 = CLOSE (180도)
```

명령을 받으면 Servo를 attach하고 목표 각도를 쓴다. 500ms 뒤 `serviceServoDetach()`가 detach한다.

```text
Server가 gate timing/판단 담당
Arduino는 물리 동작만 실행
delay(500)를 사용하지 않음
```

`loop()`:

```cpp
void loop() {
  node.tick();
  serviceServoDetach();
}
```

Servo 이동 중에도 network/sensor tick이 계속 실행된다.

---

## 8. P2 변경 사항

P2는 멀티 디바이스 경로만 확인하기 위한 최소 firmware다.

```text
Sensor: A4 하나
Actuator: L4 하나
Servo/ultrasonic 없음
```

### A4 handler

```cpp
static bool readIR4() {
  static DigitalDebounceState state = {false, false, false, 0};
  return readDebouncedActiveLow(PIN_IR_A4, state);
}
```

### L4 handler

```cpp
static bool cmdLed4(uint32_t arg) {
  digitalWrite(PIN_LED_L4, arg ? HIGH : LOW);
  return true;
}
```

모든 `uint32_t` 값에 의미를 둔다.

```text
0       -> OFF
1 이상  -> ON
```

### setup

```cpp
pinMode(PIN_IR_A4, INPUT);
pinMode(PIN_LED_L4, OUTPUT);
digitalWrite(PIN_LED_L4, LOW);

node.sensor("A4").on(readIR4);
node.actuator("L4").on(cmdLed4);
```

부팅 시 L4는 반드시 OFF로 초기화한다.

---

## 9. 공통 Framework header

각 sketch folder에는 기존 framework header 사본이 들어 있다.

```text
Module.h
Modules.h
Runtime.h
Commands.h
FrameCodec*.h
EspLink_*.h
Link*.h
Config.h
...
```

Include 순서는 변경하지 않는다.

```cpp
#include "Module.h"
#include "Boot.h"
#include "Config.h"
...
#include "Modules.h"
#include "FrameCodec2.h"
#include "Commands.h"
#include "Session.h"
#include "Runtime.h"
```

이유:

```text
Module.h가 module type/table을 먼저 선언
Config.h가 Slots.h와 network global을 구성
Runtime.h가 앞에서 선언된 함수들을 사용해 begin/tick을 정의
```

P1/P2 folder의 공통 header는 현재 복사본이다. Framework bug fix가 생기면 한 folder만 수정하지 말고 두 folder 모두 같은 변경을 반영해야 한다.

장기적으로 공통 Arduino library로 추출할 수 있지만 현재 include order/global 의존성을 먼저 정리해야 한다.

---

## 10. Config.h

P1/P2는 같은 Server로 연결하므로 다음 값이 같아야 한다.

```text
WIFI_SSID
WIFI_PASS
SERVER_IP
SERVER_PORT
```

Device를 구분하는 값은 `Config.h`가 아니라 각 `.ino`의 `DEVICE_ID`다.

주의:

```text
ESP8266은 2.4GHz Wi-Fi 사용
SERVER_IP 앞뒤 공백 금지
P1/P2 모두 같은 Arduino TCP port 사용
Wi-Fi 비밀번호를 문서나 commit에 추가로 복사하지 않음
```

---

## 11. Protocol 관점

### P1 status 예

P1은 10개 module이므로 S mask 폭이 더 크다.

```text
S,seq,occ,res,uptime,P1,checksum
```

### P2 registration 예

```text
D,*,drain,2,checksum
D,A4,IP,checksum
D,L4,OG,checksum
```

### P2 status

P2 module order:

```text
bit index 0 = A4 sensor
bit index 1 = L4 actuator echo
```

### P2 LED command

Server는 registration에서 L4 local idx=1을 찾는다.

```text
Server -> P2: G,rid,1,1,checksum
P2 -> Server: A,rid,G1,0,checksum
```

ACK result:

```text
0 = handler가 true 반환
3 = module/handler가 명령 거절
ACK 없음 = network/device 문제
```

---

## 12. Compile

Arduino IDE에서 각 folder를 별도 sketch로 연다.

Arduino CLI:

```powershell
arduino-cli compile --fqbn arduino:avr:uno ardu/parking_p1
arduino-cli compile --fqbn arduino:avr:uno ardu/parking_p2
```

필요 library:

```text
SoftwareSerial: arduino:avr 내장
Servo: 1.3.0 (P1만 사용)
```

현재 compile 결과:

| Sketch | Flash | SRAM | 결과 |
| --- | ---: | ---: | --- |
| P1 | 30,466B / 32,256B (94%) | 1,386B / 2,048B, 662B remaining | PASS |
| P2 | 27,338B / 32,256B (84%) | 1,284B / 2,048B, 764B remaining | PASS |

P1은 flash 여유가 작으므로 문자열 로그나 library를 추가한 뒤 반드시 다시 compile한다.

---

## 13. Upload/시험 순서

```text
1. server_multi 실행
2. P1 upload 및 Serial 확인
3. Server에서 P1 D registration 10개 확인
4. P2 upload 및 Serial 확인
5. Server에서 P2 D registration A4/L4 확인
6. A4 IR 차단/해제
7. Web에서 A4 상태 확인
8. L4 ON/OFF G command 전송
9. P2 ACK와 다음 S의 L4 echo 확인
```

현재 `server_multi/lot.cpp`는 4면이므로 P1/P2가 모두 연결되면 A1~A4 자동 controller 조건을 만족할 수 있다.

---

## 14. Hardware 시험 체크리스트

### P1

```text
[ ] A1/A2/A3 각각 LOW/HIGH 변화
[ ] U1/U2 실제 거리와 10cm 판정
[ ] ED/XD OPEN/CLOSE 각도
[ ] ED/XD 500ms 뒤 detach
[ ] L1/L2/L3 ON/OFF
[ ] 장시간 동작 중 SRAM/재부팅 여부
```

### P2

```text
[ ] DEVICE_ID P2 출력
[ ] D registration이 A4/L4 두 개뿐임
[ ] A4 IR active-low 방향 확인
[ ] INPUT과 INPUT_PULLUP 중 실제 sensor에 맞는 설정 확정
[ ] L4 부팅 초기 OFF
[ ] Server L4 command 수신
[ ] ACK result=0
[ ] 다음 S에서 L4 echo 변화
```

### Multi-device

```text
[ ] P1/P2 동시 online
[ ] P2 연결이 P1 연결을 교체하지 않음
[ ] A4 변화가 A1 상태를 덮지 않음
[ ] L4 명령이 P1 socket으로 전송되지 않음
[ ] P2 disconnect 후 P1 기능 유지
[ ] P2 reconnect 후 A4/L4 재결속
```

Hardware Test와 실제 P1/P2 physical integration은 아직 수행하지 않았다.

