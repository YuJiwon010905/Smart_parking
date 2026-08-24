# P1 / P2 Arduino Sketch 변경 설명

## 식별자 원칙

Arduino module의 전역 신원은 `(DEVICE_ID, module name)`이다.

```text
P1/A1 != P2/A1
P1/L1 != P2/L1
```

서로 다른 Arduino는 같은 로컬 module name을 사용할 수 있다. 모듈명을 서버 전체에서 고유하게 만들기 위해 `A4/L4`처럼 다시 번호를 붙이지 않는다.

## 현재 폴더

```text
ardu/parking_p1/parking_p1.ino
ardu/parking_p2/parking_p2.ino
```

각 sketch는 별도 firmware이며 Arduino IDE에서 독립적으로 compile/upload한다.

## P1 구성

```text
DEVICE_ID: P1

Sensor:
- A1: D2
- A2: D3
- A3: D4
- U1: Trig D7 / Echo D8
- U2: Trig D9 / Echo D10

Actuator:
- ED: Servo D5
- XD: Servo D6
- L1: D11
- L2: D12
- L3: D13
```

Registration:

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

## P2 구성

```text
DEVICE_ID: P2

Sensor:
- A1: D2, Active-low IR + debounce

Actuator:
- L1: D11, 0=OFF / nonzero=ON
```

Registration:

```cpp
node.sensor("A1").on(readIR1);
node.actuator("L1").on(cmdLed1);
```

P1에도 A1/L1이 있지만 DEVICE_ID가 다르므로 충돌하지 않는다.

## Server 매핑

```cpp
lot.spot("A1")
    .module("P1", "A1")
    .module("P1", "L1");

lot.spot("A4")
    .module("P2", "A1")
    .module("P2", "L1");
```

Arduino의 local index와 Server spot id는 별개다.

```text
P1 local idx 0 = P1/A1 -> Server spot A1
P2 local idx 0 = P2/A1 -> Server spot A4
```

## 통신

전선 frame 형식은 바뀌지 않았다.

```text
Arduino -> Server: S / D / A / V
Server -> Arduino: G / R / C / T / M / Q
```

Server는 D registration에서 각 Arduino의 local module index를 확인하고, 명령은 대상 `devid`의 socket으로만 보낸다. ACK도 `Pending.devid`와 발신 Arduino가 일치해야 완료된다.

자리 단위 R/C/T도 Server spot과 local module을 구분한다. Server의 A4 예약은 P2에 `R,<rid>,A1,...`로 전달되며 P2는 자기 local A1의 예약 bit를 갱신한다.

## Compile 결과

2026-08-24, FQBN `arduino:avr:uno`:

```text
P1: PASS
- Flash 30,466 / 32,256 bytes (94%)
- SRAM 1,386 / 2,048 bytes
- Remaining SRAM 662 bytes

P2: PASS
- Flash 27,344 / 32,256 bytes (84%)
- SRAM 1,284 / 2,048 bytes
- Remaining SRAM 764 bytes
```

## Hardware 확인 항목

```text
[ ] P1/P2 각각 Uno에 upload
[ ] P1/P2가 서로 다른 DEVICE_ID로 동시 연결
[ ] Server log에서 P1/A1과 P2/A1 모두 등록 확인
[ ] P2 IR 변화가 Server spot A4로 반영
[ ] Server의 4번 안내등 명령이 P2/L1에 도착
[ ] P2 ACK와 다음 S echo 확인
```

Hardware test와 physical integration test는 아직 수행하지 않았다.
