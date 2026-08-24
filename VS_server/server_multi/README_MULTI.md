# Multi-Arduino Server Copy

이 디렉터리는 `VS_server/server`를 보존한 채 만든 다중 Arduino 구현 사본이다.

## Build

Visual Studio에서 `server.vcxproj`를 열거나 Developer PowerShell에서 다음을 실행한다.

```powershell
msbuild server.vcxproj /t:Rebuild /p:Configuration=Debug /p:Platform=x64
```

## Runtime ownership

`lot.cpp`는 device를 고정하지 않는다.

```cpp
lot.spot("A1").module("A1").module("L1");
```

각 Arduino가 보내는 `D` registration으로 실제 `(devid,module)` 결속을 만든다. Module 이름은 전체 online node 사이에서 고유해야 한다.

## Current test sketches

```text
ardu/parking_p1: A1, A2, A3, U1, U2, ED, XD, L1, L2, L3
ardu/parking_p2: A4, L4
ardu/parking_p3: optional previous example (not required for the two-board connection test)
```

P1 original + P2 A4/L4 연결·등록 시험용 구성이다. 현재 `lot.cpp`는 여전히 A1~A5/L1~L5 5면 자동 controller이므로 A5/L5가 없는 상태에서는 자동 입차가 보류된다. 자동 입차 왕복까지 시험하려면 active slot을 4면으로 바꾸는 별도 `lot.cpp` 변경이 필요하다.

## Verified

```text
MSVC Debug x64 rebuild: PASS
Arduino Uno compile P1/P2/P3: PASS
P1/P2/P3 simultaneous D/S registration: PASS
Dynamic A1~A5/U1/U2 binding: PASS
Device-targeted G routing: PASS
Device-matched ACK handling: PASS
Persistence JSON devices[]: PASS
Hardware test: NOT PERFORMED
```
