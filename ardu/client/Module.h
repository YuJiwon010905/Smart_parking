#pragma once
// ═════════════════════════════════════════════════════════════════════════
// Module.h — 🔓 **모듈 등록**: `node.sensor("A1").on(내함수)` · `node.actuator("LD").on(내함수)`
//   ⚙ 응용 성질 — 잠금 대상이 아니다
//
// 🔓 **기여자는 이 파일을 열지 않아도 된다.** `setup()` 에서 위 한 가지 모양만 쓴다.
//
// 🔴 **왜 `client.ino` 가 아니라 여기인가**: 아두이노는 `.ino` 의 함수 정의마다 프로토타입을
//   파일 머리에 자동 삽입한다. 타입·빌더가 `.ino` 에 있으면 그 선언이 `struct` 앞에 끼어들어
//   `'Mod' does not name a type` 이 난다. **타입과 빌더는 헤더에.**
// ═════════════════════════════════════════════════════════════════════════
// 🔴🔴 **위치를 옮기지 마라.** `client.ino` 의 `#include` 목록 **맨 앞**이다 —
//   `Slots.h`(`ParkingNode`)와 `Modules.h`(라우터)가 이 표를 읽는다.

// 🔓 모듈이 하는 일. 이름에 함수를 붙이면 그것이 등록이다.
// 🔴 **핀은 여기 없다.** 등록은 *이름 ↔ 함수* 의 짝이고, 핀은 그 함수 안의 일이다 —
//   기여자가 `setup()` 에서 `pinMode` 로 잡고 함수에서 `digitalRead(9)` 로 읽는다.
//   🔑 그래서 핀이 하나든 둘이든(초음파) **등록 모양이 같다.**
typedef bool (*CommandFn)(uint32_t arg);   // 액추에이터: 반환 true → ACK `result=0` · false → `3`
typedef bool (*SensorFn)(void);            // 센서: 반환 true = 찼다

// ─────────────────────────────────────────────────────────────────────────
// 🔴 **모듈 상한 8** — 실측으로 정했다(2026-08-20)
//   `ramLow`(실행 중 최저 여유) **602B** 대비 : 8모듈 **10.6%** · 16모듈 21.2%
//   ⚠ 컴파일러가 말하는 정적 여유 834B 는 **예산이 아니다** — 스택이 232B 내려간다
//   상한을 늘리려면 **`ramLow` 를 다시 재라**(부팅 70초 뒤 `[RAM]` 줄).
// ─────────────────────────────────────────────────────────────────────────
// ⚠ `#ifndef` 인 이유: **회귀 시험이 더 많은 모듈로 지형을 만든다.** 호스트에는 RAM 제약이 없다.
//   🔴 실기 빌드에서는 이 값을 바꾸지 마라 — 위 실측이 8 을 정했다.
//
// 🔴 **RAM 말고 두 번째 상한이 있다 — 센서 블로킹이다.**
//   센서 함수가 블로킹하면(초음파 `pulseIn` 6.96ms) 그동안 `espRead()` 가 안 돈다.
//   게이트는 **함수마다** 따로 만료되고 🔴 **부팅 직후에는 전부 만료 상태**라
//   첫 `readSensors()` 에서 N개가 **한꺼번에** 막힌다. 그건 우연이 아니라 늘 일어난다.
//     실측(시험 [45]) : 8개 전부 초음파 → 55.7ms · 유입 **53B / 링버퍼 64B**
//     🔴 넘치는 경계  : **10개**
//   ⚠ 그래서 이 값을 10 이상으로 올리면 **모듈이 다 초음파일 때 링버퍼가 넘친다.**
//     RAM 이 남아도 그렇다. **두 상한 중 작은 쪽이 답이다.**
//   🔑 타임아웃을 늘리면 경계가 그만큼 내려간다 — `client.ino` 가 타임아웃을
//     **문턱에서 유도**하는 이유가 이것이다(4m 로 두면 경계가 3개가 된다).
#ifndef MODULE_CAP
#define MODULE_CAP 8
#endif

// 회귀 시험만 쓰는 확장점. 기본은 **비어 있다** — 샘플에는 아무 영향이 없다.
//   시험 하네스가 이것을 정의해 자기 모듈을 더 등록한다. 그래야 `client.ino` 에
//   시험용 `#if` 를 두지 않고도 명령 경로를 계속 밟을 수 있다.
//   ⚠ **샘플 코드에서는 이 이름을 쓰지 마라.** 자기 모듈은 `setup()` 에 직접 적는다.
#ifndef SAMPLE_EXTRA_MODULES
#define SAMPLE_EXTRA_MODULES
#endif

struct Mod {
  char      name[2];   // 전선에 나가는 두 글자
  uint8_t   isAct;     // 1 = 액추에이터(전선 kind `OG`) · 0 = 센서(`IP`)
  CommandFn cmd;       // 액추에이터만 쓴다
  SensorFn  sense;     // 센서만 쓴다
};
// 🔴 **표는 RAM 이다**(런타임 등록이므로). 전역이라 `.bss` 에서 0 으로 시작한다 —
//   생성자를 두지 않는 이유는 AVR 전역 생성자가 `main()` 전에 돌기 때문이다.
// 🔴 **핀 칸이 되살아나면 여기서 막힌다.** AVR 은 정렬이 1바이트라 패딩이 없어
//   필드 하나가 그대로 모듈당 1바이트다(실측: 8 → 7, 8모듈에서 RAM 8B).
//   ⚠ 호스트 시험은 이것을 못 잡는다 — 64비트에서는 포인터 정렬 패딩이 그 자리를 채워
//     `sizeof` 가 24 로 같다. **그래서 이 검사는 실기 빌드에만 있다.**
#ifdef __AVR__
static_assert(sizeof(Mod) == 7, "sizeof(Mod) 가 바뀌었다 — 필드를 더했나? 모듈당 RAM 이 는다");
#endif

static Mod     MODULE_TABLE[MODULE_CAP];
static uint8_t MODULE_N;        // 등록된 수. `D,*,7,<n>` 의 `n` 이고 `occ` 비트열 폭이다
static uint8_t modOverflowed;   // 상한을 넘겨 버린 등록 수 — 부팅에서 문장으로 말한다

// ── 접근자 — 🔴 **`cmd`/`sense` 를 만지는 곳은 이 둘뿐이다** ────────────────
//   센서 칸의 `cmd` 나 액추에이터 칸의 `sense` 는 **늘 0** 이지만(전역 0 시작),
//   그래도 여기서 종류를 확인한다. **판정이 한 곳에 있으면 두 곳이 갈릴 일이 없다.**
static inline bool isSensor  (uint8_t i) { return i < MODULE_N && !MODULE_TABLE[i].isAct; }
static inline bool isActuator(uint8_t i) { return i < MODULE_N &&  MODULE_TABLE[i].isAct; }
static inline CommandFn cmdOf (uint8_t i) { return isActuator(i) ? MODULE_TABLE[i].cmd   : (CommandFn)0; }
static inline SensorFn  senseOf(uint8_t i) { return isSensor(i)  ? MODULE_TABLE[i].sense : (SensorFn)0; }
static inline char modName0(uint8_t i) { return (i < MODULE_N) ? MODULE_TABLE[i].name[0] : 0; }
static inline char modName1(uint8_t i) { return (i < MODULE_N) ? MODULE_TABLE[i].name[1] : 0; }
static inline uint8_t moduleCount(void) { return MODULE_N; }

// ─────────────────────────────────────────────────────────────────────────
// 🔓 빌더 — `node.sensor("A1").on(내함수)` 의 `.on(...)` 을 받는 임시 객체
//   🔑 **정적 RAM 을 안 쓴다.** 내부가 인덱스 하나이고 스택에 산다.
//   ⚠ `on()` 은 **오버로드**다 — 센서에는 `SensorFn`, 액추에이터에는 `CommandFn` 이 붙는다.
//     기여자는 `on` 하나만 기억하면 되고 **틀린 종류를 주면 컴파일이 막는다.**
// ─────────────────────────────────────────────────────────────────────────
class ModRef {
 public:
  explicit ModRef(uint8_t i) : i_(i) {}
  ModRef& on(CommandFn f) { if (i_ < MODULE_N) MODULE_TABLE[i_].cmd   = f; return *this; }
  ModRef& on(SensorFn  f) { if (i_ < MODULE_N) MODULE_TABLE[i_].sense = f; return *this; }
  uint8_t idx() const { return i_; }
 private:
  uint8_t i_;
};

// 등록 — 🔴 **호출 순서가 전선 `idx` 이고 `occ` 비트 위치다.**
//   ⚠ 센서·액추에이터를 섞어 적어도 된다(순서 불변식이 없다).
static ModRef modAdd(const char (&name)[3], uint8_t isAct) {
  if (MODULE_N >= MODULE_CAP) { if (modOverflowed < 255) modOverflowed++; return ModRef(0xFF); }
  const uint8_t i = MODULE_N++;
  MODULE_TABLE[i].name[0] = name[0];
  MODULE_TABLE[i].name[1] = name[1];
  MODULE_TABLE[i].isAct   = isAct;
  MODULE_TABLE[i].cmd     = 0;
  MODULE_TABLE[i].sense   = 0;
  return ModRef(i);
}
