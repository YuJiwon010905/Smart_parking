#pragma once
// ═════════════════════════════════════════════════════════════════════════
// Modules.h — 모듈 표(자리 유동화의 단일 원천) · 가상 차단봉   (REQ-0273 리팩터링 · 2026-08-19)
//   원본 `조별과제샘플/ardu/client.ino` 의 **1520~1789 행을 원문 그대로** 옮긴 것이다.
//   ⚠ 행 번호는 **이 분리 이전 판(2980줄) 기준**이다.
// ═════════════════════════════════════════════════════════════════════════
// 🔴🔴 **위치를 옮기지 마라. 순서 보존이 `hex` 차이 0 의 조건이다.**
//   이 파일은 `client.ino` 의 **원래 그 자리에서** `#include` 된다. 다른 자리로 옮기거나
//   헤더들의 순서를 바꾸면 **그 순간 거동 변경이 되고 굽기 축이 하나 는다**(원장 §19·§31).

// ─────────────────────────────────────────────────────────────────────────
// 🔴 **모듈 표 — 자리 유동화의 단일 원천** (2026-08-18 · 축 3)
//
// 왜: 이름·종류·핀이 **세 곳에 흩어져** 있었다. 모듈을 하나 더 달려면 세 곳을 맞춰 고쳐야 하고
//     **하나만 어긋나도 길이·체크섬은 통과하고 자리만 틀린다** — 오늘 `node.occMask` 에서 잡은 그 부류다.
//     표로 모으면 **한 줄만 고친다.**
//
// ⚠ **이 단계의 기대는 "거동 변화 0"** 이다(PLAN-axes 축 3). 값이 하나라도 달라지면 그것이 결함이다.
// ⚠ **표를 런타임에 바꾸지 않는다.** N:1 도 지금 안 만든다 —
//    🔑 **쓰이지 않는 일반화는 검증되지 않은 코드다**(socket 의 `cells` 규율과 같다).
// 🔴🔴 **`name` 을 바꾸지 마라 — 서버의 자리 결속이 이 이름에 걸려 있다**
//
//   서버 조립 표가 모듈을 자리에 **명시로** 붙인다:
//   ```
//   lot.spot("A1").module("P1","A1").module("P1","B1")     ← lot.cpp
//                          ↑ devid    ↑ 이 이름과 글자 그대로 같아야 한다
//   ```
//   ⚠ 이름이 자리 id 와 같아야 할 이유는 **없다.** 우리가 그렇게 쓰고 있을 뿐이다 —
//     서버가 보는 것은 `.module()` 에 적힌 이름이다.
//
// 🔴 **갈리면 조용히 끊긴다: 등록은 성공하고(`등록 완료` 오름) 자리에는 아무것도 안 붙는다.**
//   **화면에 모듈이 안 보이고, 모듈 기반 조작이 생기면 "조작 불가"가 된다. ⚠ 오류로 안 뜬다.**
//   ⚠ **장치 쪽에서는 이걸 볼 수 없다** — 결속은 서버에서 일어난다.
//     그래서 **시험 [34] 가 이름 열 개를 리터럴로 못 박는다.** 바꾸면 시험이 깨진다.
//
// ⚠ 명세는 *"`name` 은 고유값이 아니라 명칭"* 이라 했는데 **지금 구현은 그보다 강하게 쓴다** —
//   설정 파일이 없어서 **이름을 결속으로 쓰는 부트스트랩**이다. socket 이 그 간극을 밝혔다.
// 🔮 **설정 적재가 들어오면 이 종속이 사라진다.** 그 전에 이름을 바꿀 계획이 생기면
//   **socket 에 먼저 말해라** — 설정을 먼저 넣으면 안전해진다.
// ✏️ `ModuleDef`·`MODULE_TABLE`·`MODULE_N` 은 **`client.ino` 로 옮겼다** (2026-08-19 · 사용자 지시).
//   🔑 **자리는 *성질* 이 정한다**: 모듈 표는 **그 장치의 자기 구성**이고, 자기 구성은
//   **스케치가 담는 것**이 맞다(`DEVICE_ID`·망 설정과 같은 부류다).
//   ⚠ *"헤더를 안 만지려고"* 가 아니다 — **헤더는 고쳐도 된다.** 근거를 그렇게 적으면
//     다음 사람이 그 잘못된 근거로 다른 것까지 옮긴다.
//   `client.ino` 가 이 파일보다 **먼저** 정의한다.

// 🔴 **상한을 컴파일 시점에 박는다.** `node.occMask`·`node.resMask`·`node.ovrActive` 가 전부 `uint16_t` 다.
//   표에 17번째가 들어오는 순간 `1u << 16` 이 **아무 일도 안 하고** 그 자리가 조용히 사라진다 —
//   **길이도 체크섬도 통과한다.** 표를 만든 이득("한 줄만 고치면 된다")이
//   그대로 결함의 배달 경로가 되는 자리라, 여기서 빌드를 깨는 것이 유일한 방어다.
static_assert(MODULE_CAP <= 16,
              "마스크가 uint16_t 다 — 17번째 모듈은 조용히 사라진다. 마스크 폭을 먼저 늘려라");
// 🔴 **센서(`I*`)는 표의 앞쪽에 연속으로 둔다.** 센서 수를 그 연속 구간으로 세기 때문에,
//   중간에 액추에이터가 끼면 그 뒤 센서가 **센서 수 밖으로 밀려 조용히 안 읽힌다.**
//   ⚠ 그리고 인덱스는 전선의 `idx` 이자 `occMask` 비트 위치다 — 앞에 끼워 넣으면
//     비트 위치·서버 자리 결속이 **한꺼번에 어긋난다.** 늘릴 때는 끝에 붙인다.

// 🔴 **등록 배치가 한 슬롯에 들어가야 한다.** `buildRegistration` 은 넘치면 **통째로 0 을 돌려주고**,
//   그러면 **등록이 영영 안 된다**(잘린 등록을 내보내지 않는 것이 옳지만, 대안이 없으면 굶는다).
//   실측 계산: 머리 `D,*,<drain>,<n>,<ck>` ≈ 11B · 모듈 줄 `D,A1,IP,<ck>` + LF = 11B
//     n=13 → 154B (OK)   ·   🔴 n=14 → 165B (BATCH_CAP 160 초과)
//   ⚠ **`MODULE_N <= 16`(마스크 폭)보다 이쪽이 먼저 걸린다.** 둘 다 필요하다.
//   🔮 `n > 13` 이 필요해지면 **등록을 두 슬롯에 나눠 보내는 구현**이 먼저다. 지금은 안 만든다 —
//      쓰이지 않는 일반화는 검증되지 않은 코드다.
static_assert(11 * MODULE_CAP + 11 <= 160,
              "등록이 한 배치(BATCH_CAP)에 안 들어간다 — n<=13 이거나 두 슬롯 분할이 먼저다");

// ★ **표 하나가 `n` 의 원천이다.** 폭·뒤집기 축·등록 줄 수가 전부 이 값을 따라간다.
// `moduleCount()` 는 `Module.h` 에 있다 — 표와 같은 자리여야 한다.


static void moduleNameOf(uint8_t i, char* out4) {
  out4[0] = modName0(i);
  out4[1] = modName1(i);
  out4[2] = '\0';
}

// 슬롯 i 의 종류. 지금은 전부 주차확인센서다.
//   ⚠ **출력 모듈도 이 비트열에 들어가야 한다**(명세 위험 다섯째) — 차단봉·안내등이 생기면
//     여기서 종류를 돌려주고 `moduleCount()` 가 그만큼 커진다. **`n` 은 입력+출력 합이다.**
//   🔑 이유: ACK 은 "받았다"이지 "됐다"가 아니다. **도달 확인은 다음 `S` 의 마스크 변화로 한다.**
// ⚠ `out4` 는 **4바이트**여야 한다 — 가상 접미(`OBV`)가 3글자다.
static void moduleKindOf(uint8_t i, char* out4) {
  // 🔴 표는 `isAct` 한 비트만 갖는다. 전선 `kind` 두 글자는 여기서 만든다.
  //   ⚠ 서버·화면은 **첫 글자만** 동작에 쓴다(`kind[0]=='O'`). 둘째 글자는 화면 라벨의
  //     **폴백**일 뿐이고 정본은 서버 조립 표의 `lot.label(...)` 이다.
  //   그래서 기여자에게 종류를 다섯 고르게 하지 않는다 — `sensor`/`actuator` 둘이다.
  out4[0] = MODULE_TABLE[i].isAct ? 'O' : 'I';
  out4[1] = MODULE_TABLE[i].isAct ? 'G' : 'P';
  out4[2] = 0;
  out4[3] = 0;
}

// ═════════════════════════════════════════════════════════════════════════
// 🔴 `CommandRouter` — **모듈별 명령 수신 콜백** (2026-08-19 · 사용자 지시)
//
// 왜 지금 만드나: 서버는 `actuator(devid, name)` 로 **기여자가 조작 모듈을 선언할 수 있는데**,
//   장치에는 그 명령을 받을 자리가 없었다(`G` 처리가 가상 차단봉을 **하드코딩**하고 있었다).
//   🔴 **서버에서 붙일 수는 있는데 장치가 못 받는다 — 기여자는 왜 안 되는지 모른다.**
//
// 🔑 **가상 차단봉을 이 경로로 옮긴다.** 그러면 **지금 실제로 돌아가는 경로**가 되고,
//   나중에 가상을 지우고 실물을 붙일 때 **등록 한 줄만 바꾸면 된다.**
//   ⚠ 시그니처만 정해 두면 **그 경로는 한 번도 안 돌아간다** —
//     §"시험이 실기가 안 하는 준비를 대신해 주면 실기만 빈다" 의 **반대 활용**이다.
//
// ── AVR 제약 ──
//   ✅ **함수 포인터만**(모듈당 2B · 표 전체 32B). `std::function`·캡처 람다는 힙·RAM 을 먹는다
//   ⚠ **`MODULE_TABLE` 에 넣지 않는다** — **선언은 자료, 동작은 코드**다.
//     표는 `PROGMEM` 이고 핸들러는 RAM 이라 물리적으로도 갈린다
//   ⚠ **등록은 이름으로, 전선은 `idx` 로 온다.** 등록할 때 이름→idx 를 여기서 푼다 —
//     기여자가 `idx`(등록 순서)를 셀 필요가 없다. **표를 고치면 idx 가 밀리는데 이름은 안 밀린다.**
// ═════════════════════════════════════════════════════════════════════════
// 🔴 **인자는 32비트다** (2026-08-19). 이진 on/off 도, LCD 의 7자리 숫자도 같은 통로로 온다.
//   ⚠ **값의 뜻은 프로토콜이 모른다.** 서버는 숫자를 나르기만 하고 **뜻은 기여자가 정한다.**
// `CommandFn` typedef 는 모듈 표 앞(`client.ino`)에 있다 — 표가 그 타입을 쓰기 때문이다.

class CommandRouter {
 public:
  // 🔴 **등록표가 없다.** 표에 함수를 적는 것이 등록이고, 이 `on()` 은 **그 표를 고친다** —
  //   `node.actuator("LD").on(cmdLed)` 와 같은 자리에 쓴다. 진실이 하나다.
  //   ⚠ 표에 없는 이름이면 false. **조용히 무시하지 않는다.**
  //   🔴 액추에이터에만 붙는다 — 센서에 붙이면 **에코 비트가 그 자리의 점유 비트와 겹쳐**
  //     명령 한 번이 "차가 들어왔다"로 보고된다. 조용히 틀린 자료를 만든다.
  bool on(const char* name, CommandFn fn) {
    for (uint8_t i = 0; i < MODULE_N; i++) {
      char n4[4];
      moduleNameOf(i, n4);
      if (strcmp(n4, name) == 0) {
        if (!isActuator(i)) return false;      // 명령 대상이 아니다
        MODULE_TABLE[i].cmd = fn; return true;
      }
    }
    return false;
  }
  // 전선에서 온 `idx` 로 부른다. 핸들러가 없으면 false → 호출부가 `result=3` 으로 답한다.
  // 🔴 **성공했을 때만 에코를 갱신한다.** 거절된 명령은 상태를 안 바꾼다.
  bool dispatch(uint8_t idx, uint32_t arg) {
    CommandFn f = cmdOf(idx);                  // 🔑 종류 확인이 이 접근자 안에 있다
    if (f == 0) return false;
    curEcho_ = (arg != 0);                 // 기본값 — 핸들러가 `echoIs()` 로 덮을 수 있다
    if (!f(arg)) return false;             // 🔴 거절된 명령은 상태를 안 바꾼다
    const uint16_t bit = (uint16_t)1 << idx;
    if (curEcho_) echo_ |= bit; else echo_ &= (uint16_t)~bit;
    return true;
  }

  // 🔴 **핸들러 안에서 부른다 — "이 명령 뒤 내 모듈은 켜진 상태인가"를 직접 정한다.**
  //   기본값은 `arg != 0` 이다. 그것과 다른 모듈(예: 닫기가 1)이 이 함수를 쓴다.
  void echoIs(bool on) { curEcho_ = on; }
  bool has(uint8_t idx) const { return cmdOf(idx) != 0; }

  // ═══ 🔴 **액추에이터 에코** — `S` 의 자리 비트열에 실려 나간다 ═══════════════
  //   서버는 **`ACK` 이 아니라 이 비트**로 "명령이 먹었나"를 안다.
  //   `ACK result=0` 은 *"콜백이 true 를 냈다"* 이고, 이 비트는 *"장치가 그 값을 갖고 있다"* 다.
  //   ⚠ **로컬 조작 API 를 열면 이 비트가 거짓이 된다** — 그래서 안 열었다(원장 §84).
  uint16_t echoMask() const { return echo_; }

 private:
  uint16_t  echo_;           // 비트 i = 모듈 i 가 지금 "켜진" 상태인가 (에코)
  bool      curEcho_;
};
static CommandRouter router;

// ═════════════════════════════════════════════════════════════════════════
// 🔓 **센서 읽기 등록표** — `CommandRouter` 의 입력 쪽 짝
//
//   `router.on("LD", cmdLed)`    ← 명령을 **받는다**
//   `sensors.on("A1", myRead)`   ← 값을 **읽는다**
//   🔑 **이름·인자·반환의 규약이 같다.** 한 번만 배우면 양쪽에 쓴다.
//
// ⚠ 생성자 없음 · NSDMI 없음 — 전역은 `.bss` 로 0(=미등록) 시작이다.
// ⚠ 비용: 함수 포인터 `MODULE_N` 개 = 2B × 모듈 수. 지금 구성(5)에서 **10B**.
// ═════════════════════════════════════════════════════════════════════════
class SensorRouter {
 public:
  // 🔴 **등록표가 없다.** 표의 `sense` 칸을 고친다 — `node.sensor("A1").on(myRead)` 와 같은 자리.
  //   반환 false = **표에 그 이름이 없다.** 조용히 무시하지 않는다.
  bool on(const char* name, SensorFn fn) {
    for (uint8_t i = 0; i < MODULE_N; i++) {
      char n4[4]; moduleNameOf(i, n4);
      if (strcmp(n4, name) == 0) {
        if (!isSensor(i)) return false;        // 센서가 아니다
        MODULE_TABLE[i].sense = fn; return true;
      }
    }
    return false;
  }
  SensorFn at(uint8_t idx) const { return senseOf(idx); }   // 🔑 종류 확인이 접근자 안에 있다
};
static SensorRouter sensors;

// `Slots.h` 가 선언만 해 둔 것의 **정의**. 자리 인덱스 = 모듈 인덱스다(센서가 표 앞쪽에 온다).
// `sensorFnOf` 는 없앴다 — `senseOf(idx)`(`Module.h`)가 그 일을 한다.

// 등록 배치를 만든다. 성공하면 길이, 실패하면 0.
//   ⚠ **`D` 여러 줄 + `S` 는 상한을 넘는다.** 그래서 명세가 *"첫 슬롯은 `D` 만"* 으로 정했다.
//     여기서도 `BATCH_CAP` 을 넘으면 **만들다 말고 0 을 돌려준다** — 잘린 등록을 내보내지 않는다.
//   🔴 잘린 등록은 **서버가 `n` 개를 못 채워 미완료로 두고**, 그 상태는 `Q` 로 복구된다.
//     하지만 **잘린 줄이 유효 프레임처럼 보이면** 그 복구조차 안 걸린다. 그래서 통째로 버린다.
static uint16_t buildRegistration(char* buf, uint16_t cap) {
  const uint8_t mn = moduleCount();
  uint16_t used = 0;

  // ① 배출률 선언 — **맨 앞.** 서버가 `n` 개를 다 받기 전에 유도식을 세울 수 있다
  // 🔴 `n`(모듈 수)을 같이 싣는다 — **명세가 `n` 으로 완료를 판정하는데 전선에 없었다.**
  //   ⚠ `S` 의 hex 폭에서 유도할 수 없다: 폭 `w` 는 **`n ∈ [4w−3, 4w]`** 만 준다(ceil 때문).
  //   ⚠ "다음 `S` 가 오면 완료"도 안 된다: `n` 이 크면 `D` 가 두 슬롯에 걸릴 수 있어
  //     **첫 슬롯에서 거짓 완료**가 된다.
  //   🔑 그리고 `n` 이 있으면 **①선언값 ②실제 D 줄 수 ③hex 폭** 셋이 서로를 *정확히* 못 박는다.
  //     ③만으로는 ±3 여유가 있어 ②를 못 잡는다. **다중 표현의 일치**가
  //     "오류 신호 없이 값만 틀리는" 부류에 가장 잘 듣는 방어다.
  int w = snprintf(buf, cap, "D,*,%u,%u,", (unsigned int)DRAIN_DECL, (unsigned int)mn);
  if (w <= 0 || (uint16_t)w + 3 > cap) return 0;
  appendChecksum(buf, (uint8_t)w);
  used = (uint16_t)strlen(buf);

  // ② 모듈들 — 전부 같은 필드 수
  for (uint8_t i = 0; i < mn; i++) {
    char nm[4];
    moduleNameOf(i, nm);
    char line[24];
    char kd[4];
    moduleKindOf(i, kd);
    int lw = snprintf(line, sizeof line, "D,%s,%s,", nm, kd);
    if (lw <= 0 || (unsigned)lw + 3 > sizeof line) return 0;
    appendChecksum(line, (uint8_t)lw);
    const uint16_t ll = (uint16_t)strlen(line);
    // ⚠ `+1` 은 줄 사이 LF. **넘치면 통째로 버린다 — 잘린 등록을 내보내지 않는다.**
    if (used + 1 + ll + 1 > cap || used + 1 + ll > BATCH_CAP) return 0;
    buf[used++] = '\n';
    memcpy(buf + used, line, ll + 1);
    used += ll;
  }
  return used;
}

// 🔑 폭 = ceil(n/4). 왼쪽 0 채움 고정폭 — **가변이면 길이로 n 을 검증할 수 없다.**
static uint8_t hexWidthFor(uint8_t n) { return (uint8_t)((n + 3) / 4); }
// mask 가 uint16_t 라 n ≤ 16 이고, 그때 폭은 4다. **버퍼를 이 값으로 잡는다.**
static const uint8_t HEX_W_MAX = 4;
static_assert(HEX_W_MAX >= (16 + 3) / 4,
              "HEX_W_MAX 는 n=16 의 폭 이상이어야 한다 — mask 가 uint16_t 이므로 n 의 상한이 16 이다");

// mask(슬롯 i = 비트 i) → 대문자 hex 고정폭. out 은 최소 hexWidthFor(n)+1 바이트.
static void bitsToHex(uint16_t mask, uint8_t n, char* out) {
  uint16_t v = 0;
  for (uint8_t i = 0; i < n; i++)
    if (mask & (uint16_t)(1u << i)) v |= (uint16_t)(1u << (n - 1 - i));   // ★ 뒤집기
  const uint8_t w = hexWidthFor(n);
  static const char HEXD[] = "0123456789ABCDEF";                          // ① 대문자만
  for (uint8_t k = 0; k < w; k++)
    out[w - 1 - k] = HEXD[(v >> (4 * k)) & 0x0F];
  out[w] = '\0';
}

// 전선 hex → mask. 실패하면 false — **받는 쪽도 검사한다**(명세 ④: 한쪽만 검사하면 그쪽 버그만 잡힌다).
//   ⚠ 거부 사유 셋: 폭이 다르다 · hex 가 아니다 · **상위 패딩 비트가 0 이 아니다.**
//   셋째가 특히 중요하다 — **길이도 체크섬도 통과하는데 값만 틀리는 경로**다.
static bool hexToBits(const char* in, uint8_t n, uint16_t* out) {
  const uint8_t w = hexWidthFor(n);
  uint16_t v = 0;
  for (uint8_t k = 0; k < w; k++) {
    const char c = in[k];
    uint8_t d;
    if      (c >= '0' && c <= '9') d = (uint8_t)(c - '0');
    else if (c >= 'A' && c <= 'F') d = (uint8_t)(c - 'A' + 10);
    else return false;                       // ① 소문자도 거부한다 — 정본은 대문자 하나다
    v = (uint16_t)((v << 4) | d);
  }
  if (in[w] != '\0' && in[w] != ',') return false;   // ② 폭이 더 길다
  // ④ 남는 상위 비트는 반드시 0
  if (n < 16 && (v >> n) != 0) return false;
  uint16_t m = 0;
  for (uint8_t i = 0; i < n; i++)
    if (v & (uint16_t)(1u << (n - 1 - i))) m |= (uint16_t)(1u << i);      // ★ 되뒤집기
  *out = m;
  return true;
}

// buf[64] 인 근거 (§2.1-6 · §2.5):
//   전선 한 줄은 LF 포함 최대 64B → 문자열은 63자 + NUL = 64. 즉 이 버퍼가 규격 상한과 정확히 같다.
//   실제 최장은 tmask 를 실은 S 프레임이고, devid 8자 기준 61B(LF 포함) = 60자 + NUL = **61바이트**.
//   → 여유 3바이트. 우리 devid 는 "P1"(2자)이라 실제로는 55B(LF 포함)까지만 나간다.
//   넘칠 일은 없지만 snprintf 반환값을 검사해 넘치면 프레임을 버린다(잘린 줄을 내보내지 않는다).
// S 프레임 **본문만** 만든다(체크섬 포함, LF 없음). 배치가 이것을 첫 줄로 쓴다.
//   길이를 돌려준다. 0 이면 만들지 못한 것이다.
static uint8_t buildStatus(char* buf, uint8_t cap) {
  // 🔴 2026-08-18 — **hex 로 바꿨다** (socket 명세 §5 확정). `n=10` 에서 폭 10 → 3.
  //   ⚠ **셋을 같이 바꾼다.** `occ`·`res`·`tm` 중 하나만 바꾸면 **같은 슬롯을 두고 두 필드가
  //     어긋나고**, 길이도 체크섬도 통과한다 — 명세가 경고한 바로 그 부류다.
  //   ⚠ 버퍼는 `HEX_W_MAX + 1`. `n ≤ 16` 이므로 폭은 최대 4다.
  const uint8_t mn = moduleCount();                 // ★ 여기 하나가 n 의 원천이다
  char occ[HEX_W_MAX + 1], res[HEX_W_MAX + 1];
  // 🔴 **출력 모듈도 비트열에 들어간다**(명세 위험 다섯째 · 설계문서 §6).
  //   `occ` 비트 0~9 = 자리 점유 · 비트 10·11 = 차단봉이 **열려 있다**.
  //   ⚠ **같은 비트열인데 의미가 다르다** — 앞은 "차가 있다", 뒤는 "열려 있다". `kind` 로 구분한다.
  //   🔑 **완료 판정이 이 에코로 이뤄진다.** `ACK` 은 "받았다"이지 "됐다"가 아니다 —
  //     서버는 다음 `S` 에서 그 비트가 바뀌는 것으로 조작 성공을 안다.
  uint16_t occOut = node.occMask;
  // 🔴 **액추에이터 에코** — 명령이 먹은 것을 서버가 이 비트로 안다(전선 비용 0B).
  //   센서 비트와 안 겹친다: `router.on` 이 `O*` 모듈에만 붙기 때문이다.
  occOut |= router.echoMask();
  bitsToHex(occOut, mn, occ);
  bitsToHex(node.resMask, mn, res);

  int n;
  if (node.testArmed) {
    // §2.4 tmask — 무장 중에만 붙는 선택 필드. 각 비트 = 그 칸의 occupied 가 주입된 값인가.
    // occupied 에는 주입값이 이미 반영돼 있고, tmask 는 "그게 진짜인가"만 알려준다.
    char tm[HEX_W_MAX + 1];
    bitsToHex(node.ovrActive, mn, tm);                   // ★ 셋째도 같은 변환 — 빠뜨리면 어긋난다
    n = snprintf(buf, cap, "S,%u,%s,%s,%lu,%s,%s,",
                 (unsigned int)seqNo, occ, res,
                 (unsigned long)(millis() / 1000UL), DEVICE_ID, tm);
  } else {
    // 해제 상태면 필드를 통째로 생략한다(옛 형식과 같다 — §2.4 "필드 없음 = 해제").
    n = snprintf(buf, cap, "S,%u,%s,%s,%lu,%s,",
                 (unsigned int)seqNo, occ, res,
                 (unsigned long)(millis() / 1000UL), DEVICE_ID);
  }
  if (n <= 0 || (unsigned)n + 3 > cap) return 0;
  appendChecksum(buf, (uint8_t)n);
  return (uint8_t)strlen(buf);
}

static bool sendStatus(void) {
  char buf[64];
  const uint8_t n = buildStatus(buf, sizeof(buf));
  if (n == 0) return false;
  return espWrite(buf, n);
}
