/*
 * client.ino — 주차 관제 노드 (장치 쪽 샘플)
 *
 * 🔴 **기여자가 여는 파일은 이것 하나다.** 고칠 자리에 🔓 표시가 있다:
 *      DEVICE_ID · 망 설정(Config.h) · 명령/센서 핸들러 · setup() 의 등록 한 줄
 * 📖 설명서 : **같은 폴더의 `GUIDE.md`** — 🔴 **처음이면 §0 부터 읽어라**
 *      (프로젝트 이름을 `client` 로 안 만들면 **첫 컴파일에서 막힌다**. §0 이 그 이유다)
 *
 * 보드 : Arduino Uno (`arduino:avr:uno`) · ESP-01 AT 펌웨어 · SoftwareSerial 9600bps
 * 배선 : A0 = ESP TX → Uno / A1 = Uno → ESP RX
 *
 * 🔴 `String` 을 쓰지 마라(AVR 힙 단편화). 프레임은 고정 char[] + snprintf 로 만든다.
 */

#include <SoftwareSerial.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>
#include <stdio.h>
#include <string.h>
#include <Servo.h>
#include "SensorFilters.h"

// 진단 출력 스위치. `#ifndef` 인 이유: **소스를 고치지 않고** DEBUG=0 빌드를 만들 수 있어야
//   두 빌드의 차이가 DEBUG 하나뿐임이 보장된다. 소스를 고쳐 가며 구우면 그 보장이 깨진다.
//     arduino-cli compile --build-property "compiler.cpp.extra_flags=-DDEBUG=0" ...
#ifndef DEBUG
#define DEBUG 1
#endif

// ██████████████████████████████████████████████████████████████████████████
// █  🔴🔴  **이 노드의 이름 — 반드시 자기 것으로 바꿔라**                    █
// ██████████████████████████████████████████████████████████████████████████
//
//   규격 : **1~8자** · 영문자·숫자·`_`·`-` 만 · 예: "P1" "kim" "lab-3"
//
// 🔴 **안 바꿔도 로컬에서는 잘 된다. 여럿을 한 서버에 모으는 순간 서로를 쫓아낸다.**
//   증상이 "내 자리가 가끔 사라진다"라 원인을 찾기 매우 어렵다 → `GUIDE.md` §1
// ⚠ 값을 바꾸면 **다시 구워야** 한다(컴파일 상수다).
// ██████████████████████████████████████████████████████████████████████████
#define DEVICE_ID    "P1"        // ← 🔴 **여기를 바꿔라**

// 길이를 컴파일이 막는다 — 주석은 안 읽혀도 이건 못 지나간다.
//   `sizeof` 는 NUL 을 포함하므로 1~8자 = 2~9 바이트다.
static_assert(sizeof(DEVICE_ID) > 1 && sizeof(DEVICE_ID) <= 9,
              "DEVICE_ID 는 1~8자여야 한다 (명세 §2.3). 빈 값도 9자 이상도 안 된다");


// ██████████████████████████████████████████████████████████████████████████
// █  🔓  **여기가 네 자리다 — 내 모듈이 하는 일을 여기 쓴다**                █
// ██████████████████████████████████████████████████████████████████████████
//
//   명령 핸들러 :  `bool 이름(uint32_t arg)`   — arg 는 서버가 보낸 값
//   센서 핸들러 :  `bool 이름()`              — 반환 true = 찼다
//
// 🔑 **함수를 쓰고 아래 `setup()` 에서 한 줄로 붙인다.** 배열도 등록표도 없다.
// ██████████████████████████████████████████████████████████████████████████
// ─────────────────────────────────────────────────────────────────────────
// 🔓 **초음파 센서 (HC-SR04)** — Trig 7번 · Echo 8번
//
// 🔑 **핀은 이 함수 안의 일이다.** 핀 모드는 `setup()` 에서 `pinMode` 로 잡고
//   여기서 그 핀을 쓴다 — 아두이노에서 늘 하던 그대로다. 핀이 둘이어도 등록은 한 줄이다.
//
// 🔴 **문턱 판정은 여기서 한다.** 초음파는 거리(숫자)를 내는데 자리 상태는 참/거짓이다 —
//   *"몇 cm 아래면 찼다고 볼 것인가"* 는 **장치를 단 사람만 안다.**
//
// ⚠ **`pulseIn` 은 최대 타임아웃만큼 블로킹한다.** 매 `loop()` 마다 재면 그만큼 수신이 밀린다.
//   → **간격을 두고 값을 캐시한다.** 200ms 면 자리 상태 변화를 놓치지 않으면서
//     블로킹이 전체 시간의 12% 아래로 떨어진다.
// ─────────────────────────────────────────────────────────────────────────
// Parking IR sensor
#define IR_1 2
#define IR_2 3
#define IR_3 4
static bool readIR1() {
  static DigitalDebounceState s = {false, false, false, 0};
  return readDebouncedActiveLow(IR_1, s);
  //input_pullup 이라 찼으면 LOW 반환
}
static bool readIR2() {
  static DigitalDebounceState s = {false, false, false, 0};
  return readDebouncedActiveLow(IR_2, s);
}
static bool readIR3() {
  static DigitalDebounceState s = {false, false, false, 0};
  return readDebouncedActiveLow(IR_3, s);
}

// Servo
#define Servo_1 5
#define Servo_2 6
Servo enterServo;     // Servo 객체
Servo exitServo;

const unsigned long SERVO_MOVE_TIME_MS=500UL;   // Servo 가 목표 각도까지 이동하는 데 허용할 시간
bool enterDetachPending = false;                // 최적: 500UL~60UL
bool exitDetachPending = false;
unsigned long enterDetachAt = 0;
unsigned long exitDetachAt = 0;

//차단기 각도
const int GATE_OPEN=90;//90
const int GATE_CLOSE=180;//180
// CODEX FIX: gate timing and OPEN/CLOSE decisions are owned by VS_server.
// Arduino keeps no entrance/exit session timer; it only executes ED/XD commands.

// Entry Gate
// 1 = OPEN 2 = CLOSE
// SERVER 가 판단해서 명령을 보냄
// 아두이노는 이유를 판단하지 않고 그대로 실행
 
static bool cmdEntryGate(uint32_t arg){ 
  switch(arg){
    case 1:
      if(!enterServo.attached()){   // 이전 OPEN 에서 Detach 되었으면 다시 연결
      enterServo.attach(Servo_1);
      }
      enterServo.write(GATE_OPEN);
      enterDetachPending = true;    // 즉시  detach 하지 않고 이동 완료 시간을 예약
      enterDetachAt = millis();
#if DEBUG
      Serial.println(F("[ED] ENTRY GATE OPEN"));
#endif
      return true;
    case 2:
      enterDetachPending = false;
      if(!enterServo.attached()){
      enterServo.attach(Servo_1);
      enterServo.write(GATE_CLOSE); // arg = 2 는 숫자가 0 이 아니지만 실제 의미는 CLOSE
      enterDetachPending=true;
      enterDetachAt = millis();
#if DEBUG
      Serial.println(F("[ED] ENTRY GATE CLOSE"));
#endif
      return true;
    default:
#if DEBUG
      Serial.print(F("[ED] 거절 — 잘못된 arg: "));
      Serial.println(arg);
#endif    
      return false;
  }
  }
}

static bool cmdExitGate(uint32_t arg){ 
  switch(arg){
    case 1:
      if(!exitServo.attached()){
         exitServo.attach(Servo_2);
       }
       exitServo.write(GATE_OPEN);
       exitDetachPending = true;
       exitDetachAt = millis();
      return true;
    case 2:
       exitDetachPending = false;
       if (!exitServo.attached()) {
       exitServo.attach(Servo_2);
       }
      exitServo.write(GATE_CLOSE); // arg = 2 는 숫자가 0 이 아니지만 실제 의미는 CLOSE
      exitDetachPending = true;
      exitDetachAt = millis();
      return true;
    default:
      return false;
  }
}

#define US1_TRIG        7        // 🔓 Trig — 출력. 출처: 사용자 "에코가 4번핀 트리거 2번핀"
#define US1_ECHO        8        // 🔓 Echo — 입력
#define US_NEAR_CM     10      // 이보다 가까우면 "차가 있다"
// CODEX FIX: 타임아웃은 현재 10cm 문턱에서 계산한다.
// 왕복 µs ÷ 58 = cm, 여유 2배이므로 10cm 기준 1,160µs다.
//   ⚠ 4m(25,000µs)까지 기다리면 **무응답 슬롯마다 25ms 를 버린다** — 슬롯(1,200ms)의 2%가
//     측정 12회에서 25%가 된다. 문턱에서 유도하면 7%다.
//#define US_TIMEOUT_US  ((unsigned long)US_NEAR_CM * 58UL * 2UL)   // 1,160µs
#define US_TIMEOUT_US 10000UL
#define US_PERIOD_MS   200      // 🔓 재는 간격 (그 사이에는 캐시를 돌려준다)

static bool readUltrasonic1() {
  static uint32_t lastAt  = 0;
  static StableNearState stable = {0, 0, false};
  const uint32_t now = millis();
  if (lastAt != 0 && (now - lastAt) < US_PERIOD_MS) return stable.value;   // 캐시
  lastAt = now;

  digitalWrite(US1_TRIG, LOW);   delayMicroseconds(2);
  digitalWrite(US1_TRIG, HIGH);  delayMicroseconds(10);   // 10µs 펄스가 HC-SR04 규격이다
  digitalWrite(US1_TRIG, LOW);
  const uint32_t us = pulseIn(US1_ECHO, HIGH, US_TIMEOUT_US);   // 무응답이면 0
  // 🔴 타임아웃(0)은 **"반사가 없다" = 비었다**로 읽는다. 오류로 읽으면 값이 흔들린다.

  // 혹시나 초음파센서 동작 안하면 이 명령어로 값 받아와서 뭐가 문젠지 테스트 가능,,,
  /*#if DEBUG
  Serial.print(F("[U1] us="));
  Serial.print(us);
  Serial.print(F(" cm="));
  if (us == 0) Serial.print(F("TIMEOUT"));
  else Serial.print(us / 58.0);
  Serial.print(F(" detected="));
  Serial.println((us != 0) && ((us / 58UL) < US_NEAR_CM));
#endif*/
  
  const bool nearNow = (us != 0) && ((us / 58UL) < US_NEAR_CM);
  return updateStableNear(stable, nearNow);
}

// 🔓 **두 번째 초음파 — US2** (Trig 9 · Echo 10)
//
// 🔑 **위 함수를 복사해서 핀만 바꿨다.** 센서를 하나 더 달 때 그것이 가장 쉬운 길이다 —
//   `setup()` 의 등록 줄(`node.sensor("US2").on(readUltrasonic2)`)은 **한 글자도 안 바뀐다.**
//   🔓 즉 **함수 내용만 갈면 그 자리의 센서 종류가 바뀐다.** 이름이 계약이고 함수가 구현이다.
//
// ⚠ 초음파를 **셋 이상** 달 거면 이 복사가 아파진다. 그때는 공통 함수로 뽑아라 —
//   다만 둘까지는 복사가 더 읽기 쉽다(핀이 함수 안에 그대로 보인다).
// 🔴 예산을 세라 : 초음파 하나가 6.96ms 다. **연속 66ms** 가 상한이고 그것의 50%(33ms)를 쓴다
//   → 지금 둘 = 14ms. `GUIDE.md` §4 의 표가 그 수를 준다.
#define US2_TRIG 9   // 배선 출처: 사용자 2026-08-20 "트리거가 9 에코가 10. 하나 더달았다"
#define US2_ECHO 10   // ⚠ 배선은 코드로 검증할 수 없다 — 사람만 안다
static bool readUltrasonic2() {
  static uint32_t lastAt  = 0;
  static StableNearState stable = {0, 0, false};
  const uint32_t now = millis();
  if (lastAt != 0 && (now - lastAt) < US_PERIOD_MS) return stable.value;   // 캐시
  lastAt = now;

  digitalWrite(US2_TRIG, LOW);   delayMicroseconds(2);
  digitalWrite(US2_TRIG, HIGH);  delayMicroseconds(10);
  digitalWrite(US2_TRIG, LOW);
  const uint32_t us = pulseIn(US2_ECHO, HIGH, US_TIMEOUT_US);

// 출구 초음파 정상동작 테스트 코드
/*#if DEBUG
  Serial.print(F("[U2] us="));
  Serial.print(us);
  Serial.print(F(" cm="));
  if (us == 0) Serial.print(F("TIMEOUT"));
  else Serial.print(us / 58.0);
  Serial.print(F(" detected="));
  Serial.println((us != 0) && ((us / 58UL) < US_NEAR_CM));
#endif*/
  const bool nearNow = (us != 0) && ((us / 58UL) < US_NEAR_CM);
  return updateStableNear(stable, nearNow);
}

#define LED_1 11            // 🔓 11번 LED
#define LED_2 12            // 🔓 12번 LED
#define LED_3 13            // 🔓 13번 LED
static bool cmdLed1(uint32_t arg) {
  digitalWrite(LED_1, arg ? HIGH : LOW);
  // 🔑 **거절 로그가 없는 이유**: 이 핸들러는 `false` 를 낼 자리가 없다.
  //   모든 값에 뜻이 있다(0=끔 · 그 외=켬). 거절이 없으면 남길 것도 없다.
  return true;
}
static bool cmdLed2(uint32_t arg) {
  digitalWrite(LED_2, arg ? HIGH : LOW);
  return true;
}
static bool cmdLed3(uint32_t arg) {
  digitalWrite(LED_3, arg ? HIGH : LOW);
  return true;
}

static bool cmdLCD(uint32_t arg) {
  if (arg > 9999999UL) {                 // 🔴 표시할 수 없는 값은 **거절한다**
#if DEBUG
    // 🔴 **거절도 남긴다.** 이 줄이 없으면 "안 불렸다"와 구분이 안 된다.
    Serial.print(F("[LC] 거절 — 7자리 초과: ")); Serial.println(arg);
#endif
    return false;
  }
#if DEBUG
  Serial.print(F("[LC] ")); Serial.println(arg);   // 실물 LCD 라이브러리는 여기에 붙인다
#endif
  return true;
}

// ─────────────────────────────────────────────────────────────────────────
// 🔴🔴 **순서와 위치를 바꾸지 마라** — 전처리 결과가 바뀌면 산출물이 달라진다.
//   ⚠ `Module.h` 가 맨 앞인 것은 뒤의 헤더들이 모듈 표를 쓰기 때문이다.
//   🔑 새 헤더는 **목록 끝**에.   🔒 = 링크 계층 · 👁 = 관측
// ─────────────────────────────────────────────────────────────────────────
#include "Module.h"            // 🔓 모듈 등록 — **목록 맨 앞이어야 한다** CODEX FIX: register all 10 configured modules 
#include "Boot.h"              // 부팅 원인 기록(MCUSR 미러)
#include "Config.h"            // 🔓 배선·망 설정·타이밍·반송파 슬롯 — **SSID/IP 를 여기서 바꾼다**
#include "RxBuf.h"             // 🔒 수신 버퍼 셋(rxLine·workLine·pendLine)
#include "Diag.h"              // 👁 현장 진단·RAM 계측
#include "TxState.h"           // 송신 상태·슬롯 상태
#include "Checksum.h"          // 체크섬(§2.2) — 순수 함수
#include "RxLine.h"            // 🔒 수신 줄 조립(§6.2 1단계)
#include "LinkGate.h"          // 🔒 살아있음 불변식·SEND OK 게이트
#include "Counters.h"          // 👁 운영 계수기 [CNT] — 계수기를 더하는 것은 정상 작업이다
#include "LinkRecovery.h"      // 🔒 IP 소실·소켓 복구·오프라인 전이
#include "FrameCodec.h"        // S·D·A 프레임 · hex 인코딩 · 슬롯 배치

// ██████████████████████████████████████████████████████████████████████████
// █  ⚠  아래부터는 **고쳐도 된다. 다만 비용이 있다**                        █
// ██████████████████████████████████████████████████████████████████████████
//
// 🔴 **금지가 아니다.** 아래(와 🔒 헤더들)는 **링크 안정성이 걸린 코드**다 —
//   ESP 가 꼬였을 때 자력으로 돌아오는 경로가 여기서 나온다.
//   고치면 **안정성 기준선을 다시 재야 한다.** 기준선 수치는 `GUIDE.md` §5.
// 🔑 **"만지지 마라"가 아니라 "만지면 그 비용을 안다"이다.** 고쳤으면 **알려라.**
// ██████████████████████████████████████████████████████████████████████████
#include "Modules.h"           // 모듈 표를 쓰는 코드 · 명령 라우터
#include "FrameCodec2.h"       // ACK 발행·슬롯 배치
#include "Commands.h" // CODEX FIX: server decides reservation validity
#include "Session.h"           // 주기 처리(statusTick·cntTick)
#include "Runtime.h"        // begin()/tick() 정의 — **목록 맨 끝이어야 한다**
// ─────────────────────────────────────────────────────────────────────────
// ██████████████████████████████████████████████████████████████████████████
// █  🔓  **명령 수신 핸들러 — 자기 액추에이터를 여기 붙인다**                █
// ██████████████████████████████████████████████████████████████████████████
//   모양 :  `bool 이름(uint32_t arg)`   — **true = 성공**(ACK `result=0`) · false = 수행 불가(3)
//   등록 :  아래 `setup()` 에서 `node.actuator("이름").on(핸들러);`  — **한 줄이다**
//   🔑 **이름으로 등록한다** — 전선은 `idx`(적은 순서)로 오는데 이름은 순서를 바꿔도 안 밀린다.
//
// 🔴 **`arg` 의 뜻은 *네가* 정한다.** 서버도 프로토콜도 모른다 — 32비트 정수를 나를 뿐이다.
//   **같은 표를 서버 호출부에도 적어라.** 어긋나면 **조용히 다른 동작을 한다**(오류도 안 뜬다).
// 🔴 **거절할 때도 한 줄 남겨라.** 안 남기면 "거절했다"와 "안 불렸다"가 같은 모양이다.
//   판별자: *"내 콜백이 `false` 를 낼 수 있는 모든 자리에 로그가 있는가."*
// ⚠ 등록 안 한 모듈에 명령이 오면 `result=3` — 조용히 성공하지 않는다.
//
//
// 📖 세 가지 꼴의 본보기 · 에코 규약과 한계 · 묶음 하행 → `GUIDE.md` §3

// ═══════════════════════════════════════════════════════════════════════════
// 🔓 **본보기는 주석이 아니라 위의 실제 코드다** — 네 꼴이 다 켜져 있다:
//
//     readUltrasonic1  두 핀 센서(거리 → 문턱 판정)   `node.sensor("A1").on(readUltrasonic)`
//     readUltrasonic2  두 핀 센서 — 같은 꼴 두 번째      `node.sensor("B1").on(readB1)`
//     cmdLed          on/off 액추에이터               `node.actuator("LD").on(cmdLed)`
//     cmdL2           숫자를 받는 표시기(거절 있음)    `node.actuator("LC").on(cmdLCD)`
//
// 🔑 **넷이 모양이 같다.** 등록은 늘 *이름 ↔ 함수* 한 줄이고, 핀은 함수 안의 일이다.
//
// 🔑 **베낄 것을 주석에 두지 않는다.** 주석 예시는 컴파일을 안 거쳐서 **조용히 낡고**,
//   그것을 정답으로 베낀 사람이 안 되는 코드를 얻는다. 위 넷은 **매번 컴파일된다.**
//
// 🔴 **화면에 뜰 이름은 장치가 안 정한다.** 서버의 `lot.label(...)` 이 정한다:
//     lot.label("P1", "A1", "왼쪽 센서");     lot.label("A1", "1번 자리");
//   🔑 그래서 `kind` 에 "주차확인센서" 같은 뜻을 담지 마라 — **정본이 둘이 된다.**
//     `kind` 의 **첫 글자**(`I`=관측 / `O`=명령)만 서버가 본다.
//
// 📖 에코 규약 · 묶음 하행 · 안정성 기준선 → `GUIDE.md` §3·§4·§5
// ═══════════════════════════════════════════════════════════════════════════

// ██████████████████████████████████████████████████████████████████████████
// █  🔓  **여기가 네 자리다** — 자기 핀과 자기 모듈만 적는다                █
// ██████████████████████████████████████████████████████████████████████████
//
//   `node.begin()` : 링크 시작 · 슬롯 원점 · 난수 시드 · 센서 핀 모드 · 리셋선 · 워치독
//   `node.tick()`  : 한 박자 (수신 → 센서 훑기 → 송신 → 계수)
//
// 🔑 **그 안의 순서는 네가 바꿀 수 없다. 그래서 감춰 뒀다**(정의는 `Runtime.h`).
//   드러내 봐야 "지킬 의무" 만 생기고 얻는 것이 없다. 이 함수에는 **바꿀 수 있는 것만** 남아 있다.
// ██████████████████████████████████████████████████████████████████████████
void setup() {
  Serial.begin(115200);
  node.begin();

  // 🔓 **내 핀 — 아두이노 기본 설정.** 장치는 어느 모듈이 어느 핀인지 모른다
  pinMode(IR_1,INPUT_PULLUP);  pinMode(IR_2, INPUT_PULLUP);  pinMode(IR_3, INPUT_PULLUP);

  pinMode(US1_TRIG, OUTPUT);   pinMode(US1_ECHO, INPUT);
  pinMode(US2_TRIG, OUTPUT);   pinMode(US2_ECHO, INPUT);
  digitalWrite(US1_TRIG,LOW);  digitalWrite(US2_TRIG,LOW);  // 초기 Trigger LOW

  enterServo.attach(Servo_1);  exitServo.attach(Servo_2);
  enterServo.write(GATE_CLOSE);  exitServo.write(GATE_CLOSE);   // 부팅 시엔 닫힌 상태

  pinMode(LED_1,  OUTPUT);     pinMode(LED_2,  OUTPUT);   pinMode(LED_3,  OUTPUT);
  digitalWrite(LED_1,LOW);     digitalWrite(LED_2,LOW);     digitalWrite(LED_3,LOW);

  // 🔓 **내 모듈 — 이름 ↔ 함수, 한 줄에 하나.** 적은 순서가 전선 순서다(섞어 적어도 된다)
  node.sensor  ("A1").on(readIR1);    // CODEX FIX: match server parking slot A1    // P1
  node.sensor  ("A2").on(readIR2);    // CODEX FIX: match server parking slot A2
  node.sensor  ("A3").on(readIR3);    // CODEX FIX: match server parking slot A3

  node.sensor  ("U1").on(readUltrasonic1);
  node.sensor  ("U2").on(readUltrasonic2);

  node.actuator("ED").on(cmdEntryGate);
  node.actuator("XD").on(cmdExitGate);

  node.actuator("L1").on(cmdLed1);  // P1
  node.actuator("L2").on(cmdLed2);
  node.actuator("L3").on(cmdLed3);

  //node.actuator("LC").on(cmdLED);

  SAMPLE_EXTRA_MODULES                        // 회귀 시험만 쓴다. 평소엔 비어 있다
#if DEBUG
  Serial.println(F("\n=============================="));
  Serial.println(F("SMART PARKING NODE READY"));
  Serial.print(F("DEVICE ID : "));
  Serial.println(F(DEVICE_ID));
  Serial.println(F("------------------------------"));
  Serial.println(F("[SENSOR]"));
  Serial.println(F("A1 : Parking Slot 1"));
  Serial.println(F("A2 : Parking Slot 2"));
  Serial.println(F("A3 : Parking Slot 3"));
  Serial.println(F("U1 : Entry Vehicle"));
  Serial.println(F("U2 : Exit Vehicle"));
  Serial.println();
  Serial.println(F("[ACTUATOR]"));
  Serial.println(F("L1 : Guide LED 1"));
  Serial.println(F("L2 : Guide LED 2"));
  Serial.println(F("L3 : Guide LED 3"));
  Serial.println(F("ED : Entry Gate"));
  Serial.println(F("XD : Exit Gate"));
  Serial.println(F("LC : LCD"));
  Serial.println(F("=============================="));
#endif
}


// non-blocking detach 함수 추가
static void serviceServoDetach() {
    const unsigned long now = millis();

    if (enterDetachPending &&
        (unsigned long)(now - enterDetachAt) >= SERVO_MOVE_TIME_MS) {
      enterDetachPending = false;
      enterServo.detach();

  #if DEBUG
      Serial.println(F("[ED] ENTRY SERVO DETACHED"));
  #endif
    }

    if (exitDetachPending &&
        (unsigned long)(now - exitDetachAt) >= SERVO_MOVE_TIME_MS) {
      exitDetachPending = false;
      exitServo.detach();

  #if DEBUG
      Serial.println(F("[XD] EXIT SERVO DETACHED"));
  #endif
    }
  }

void loop() {
  node.tick();
  serviceServoDetach();
}
