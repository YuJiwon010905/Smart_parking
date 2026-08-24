# Multi-Arduino Server

`VS_server/server_multi`는 여러 Arduino가 한 서버에 동시에 연결되는 구현이다.

## 식별자 원칙

모듈의 유일한 주소는 `(Arduino ID, module name)` 복합 키다.

```text
P1/A1 != P2/A1
P1/L1 != P2/L1
```

모듈명을 전체 Arduino에서 고유하게 다시 만들지 않는다. `lot.cpp`가 각 자리에 실제 복합 주소를 선언한다.

```cpp
lot.spot("A1").module("P1", "A1").module("P1", "L1");
lot.spot("A4").module("P2", "A1").module("P2", "L1");
```

## 현재 Arduino 구성

```text
P1: A1, A2, A3, U1, U2, ED, XD, L1, L2, L3
P2: A1, L1
```

P2의 로컬 `A1/L1`은 서버 조립표에서 전역 주차면 `A4`의 센서와 안내등으로 결속된다.

## Build

```powershell
msbuild server.vcxproj /t:Rebuild /p:Configuration=Debug /p:Platform=x64
```

## 2026-08-24 검증

```text
MSVC Debug x64 rebuild: PASS
Arduino Uno P1 compile: PASS
Arduino Uno P2 compile: PASS
Synthetic P1/A1 + P2/A1 simultaneous registration: PASS
Synthetic P1/L1 + P2/L1 device-targeted G routing: PASS
Synthetic global A4 -> P2 local A1 R/ACK mapping: PASS
Physical hardware test: NOT PERFORMED
Physical integration test: NOT PERFORMED
```

상세 수정 내역은 저장소 루트의 `COMPOSITE_MODULE_ID_FIX.md`를 참조한다.
