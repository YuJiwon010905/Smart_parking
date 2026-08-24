#pragma once
// ═════════════════════════════════════════════════════════════════════════
// Runtime.h — 🔴 **프레임워크의 시작(`begin`)과 한 박자(`tick`)**
//   ⚙ 응용 성질 — 잠금 대상이 아니다
//
// 🔓 **기여자는 이 파일을 열지 않는다.** `client.ino` 의 `setup()`/`loop()` 가 여기로 위임한다.
//   그래서 기여자 파일에는 **바꿀 수 있는 것만** 남는다 — 자기 핀과 자기 모듈.
//
// 🔴 **왜 여기인가**: `begin()` 은 `espInit()` 을, `tick()` 은 `espRead()`·`statusTick()` 을
//   부른다. 그 함수들이 `Slots.h`(클래스 정의) **뒤에** 오므로 클래스 안에서는 본문을 쓸 수 없다.
//   그래서 선언은 `Slots.h`, 정의는 **모든 헤더 뒤인 이 파일**이다.
// ═════════════════════════════════════════════════════════════════════════
// 🔴🔴 **위치를 옮기지 마라.** `client.ino` 의 **정해진 자리에서**(목록 맨 끝) `#include` 된다.
//   헤더 순서가 바뀌면 선언·초기화 순서가 같이 바뀌어 **산출물이 달라진다.**

// ─────────────────────────────────────────────────────────────────────────
// 시작 — 🔴 **이 순서는 호출자가 바꿀 수 없다. 그래서 감춘다**
//
// 원래 이 줄들은 `setup()` 에 기여자 줄과 섞여 있었고, 그 위험을 설명하는 주석이 길어져
// 코드가 더 안 읽혔다. **드러내 봐야 "지킬 의무" 만 생기고 얻는 것이 없다.**
// ─────────────────────────────────────────────────────────────────────────
void ParkingNode::begin() {
  beginDone = true;
  espInit();                        // UART 를 열고 접속 사다리를 시작만 한다 (여기서 기다리지 않는다)

  // 슬롯 위상의 원점. 🔴 **`espInit()` 바로 뒤여야 한다** — 0 으로 두면 첫 `statusTick` 에서
  //   `now - 0` 만큼의 슬롯을 한꺼번에 소진하느라 while 이 헛돈다.
  //   ⚠ 그리고 `espInit()` 이 잡는 `netStepAt` 과 **이 값의 선후가 뒤집히면** 안 된다.
  slotStart = millis();

  // 난수 시드는 **아무 데도 안 물린 아날로그 핀**에서 뽑는다 — 물려 있으면 노이즈가 안 나온다.
  // ⚠ 자기 센서에 A1 을 쓰면 그 핀을 여기 쓰지 마라 — 값이 노이즈가 아니게 된다.
  randomSeed((unsigned long)analogRead(A2) ^ micros()); //A1 Default

  // 🔑 **핀 모드는 여기서 안 잡는다.** 기여자가 `setup()` 에서 `pinMode` 로 직접 잡는다 —
  //   장치는 어느 모듈이 어느 핀에 붙었는지 **모른다.** 알 필요가 없다.

  // 재부팅하면 테스트 오버라이드는 사라진다 — 서버가 다시 내려보내지 않는다(예약과 정반대).
  // 전역이라 어차피 0 이지만, **"여기서 버린다"를 코드로 남겨 둔다.**
  testArmed = false;
  slotOverrideClearAll();

  // ESP 리셋선은 **놓은 상태(하이임피던스)로 시작**한다.
  //   전원 인가 직후 AVR 핀은 원래 INPUT 이라 이미 떠 있지만, **"여기서 명시적으로 놓는다"**를
  //   코드로 남긴다. 🔴 실수로 OUTPUT LOW 로 두면 ESP 가 영원히 리셋에 잡혀 아무 일도 안 난다.
#if ESP_RST_WIRED
  pinMode(PIN_ESP_RST, INPUT);
#endif

#if ENABLE_WDT
  // 8초. SoftwareSerial 비트뱅잉과 waitForPrompt(300ms)를 넉넉히 덮는다.
  // (가장 긴 정지는 drainSerial 의 120ms 다 — 8초와는 두 자릿수 차이라 오발이 없다.)
  wdt_enable(WDTO_8S);
#endif
}

// ─────────────────────────────────────────────────────────────────────────
// 주기 진단 — 🔴 **여기 있는 이유**: `netOnline`·`netStep`·`rung`·`espRstHeld` 를 읽는다.
//   그 변수들이 링크 계층 헤더에 있어 `Diag.h`(앞쪽)에서는 안 보인다. 성질은 관측이고
//   자리는 "모든 헤더 뒤" 여야 하는 것이다.
// ─────────────────────────────────────────────────────────────────────────

#if DEBUG
// 오프라인인 동안 3초마다 한 줄. 🔴 **이 한 줄이 원인을 셋으로 가른다:**
//   rx=0                 → ESP→Uno 로 바이트가 아예 안 온다. 배선(D7)·레벨·모듈 전원을 봐라
//   rx>0, lines=0        → 바이트는 오는데 줄이 안 끊긴다. 줄 종단이 LF 가 아닐 수 있다
//   lines>0, online=0    → 줄은 오는데 접속 문구를 못 알아본다. [AT] 로그에서 실제 문구를 봐라
// 셋 중 무엇인지 모르는 채로 고치면 또 빗나간다.
static void diagTick(unsigned long now) {
  if (netOnline) return;
  if (now - dbgLastDiag < DIAG_PERIOD_MS) return;
  dbgLastDiag = now;
  Serial.print(F("[DIAG] offline step="));  Serial.print(netStep);
  // 사다리의 현재 칸을 같이 찍는다. 없으면 3초마다 같은 줄이 흘러갈 뿐
  //   **"지금 무엇을 하며 기다리는 중인가"**를 로그에서 알 수 없다.
  Serial.print(F(" 사다리="));              Serial.print(rung);
  Serial.print('/');                        Serial.print(rungFails);
  if (espRstHeld) Serial.print(F(" [ESP리셋유지중]"));
  Serial.print(F(" rx="));                  Serial.print(dbgRxBytes);
  Serial.print(F(" lines="));               Serial.print(dbgLineCnt);
  Serial.print(F(" up="));                  Serial.print(now / 1000UL);
  Serial.println(F("s"));
}
#endif

#if DEBUG
// RAM 최저 여유를 1분에 한 줄. **온라인일 때도 찍는다** — [DIAG] 는 오프라인 전용이라
// 정작 2시간 소크(=계속 온라인) 동안 아무것도 안 보이기 때문이다.
static unsigned long lastRamReport = 0;
static void ramTick(unsigned long now) {
  if (now - lastRamReport < 60000UL) return;
  lastRamReport = now;
  Serial.print(F("[RAM] 최저 여유 "));
  Serial.print(ramLow);
  Serial.print(F(" B · 프롬프트 재동기 "));
  Serial.print(promptResyncs);
  Serial.println(F("회 (서버의 '버린줄' 과 맞아야 한다)"));
}
#endif


#if DEBUG
// ─────────────────────────────────────────────────────────────────────────
// 부팅 배너 — 🔴 **첫 `tick()` 에서 한 번.** `begin()` 이 아니다
//   `[SENS]` 줄이 등록된 함수를 보고 있음/없음을 가르므로 **등록 뒤여야 참이다.**
// ─────────────────────────────────────────────────────────────────────────
static void printBootBanner(void) {
  // 🔴 **이 보드가 어느 서버를 보는가** — 포트 세트가 둘이라 자주 물어지는 것이다.
  //   ⚠ 소스를 읽어 짐작하지 마라. **빌드 시점에 덮일 수 있다.** 이 줄이 칩의 진실이다.
  Serial.print(F("\n[NET] 대상 " SERVER_IP ":" SERVER_PORT));
  Serial.println();

  // 🔴 **내 센서가 실제로 읽히는가** — 이 줄이 그 답이다.
  Serial.print(F("\n[SENS] "));
  for (uint8_t i = 0; i < MODULE_N; i++) {
    if (!isSensor(i)) continue;
    char nm[4]; moduleNameOf(i, nm);
    Serial.print(nm); Serial.print('=');
    // 🔴 함수가 없으면 그 센서는 **영원히 0** 이다. 화면에는 "비었다"로 보여 조용하다 —
    //   그래서 이 줄이 그것을 지목한다. `.on(...)` 을 안 부른 것이 유일한 원인이다.
    Serial.print(senseOf(i) ? F("함수있음") : F("🔴함수없음"));
    Serial.print(' ');
  }
  Serial.println();

  // 🔴 **둘은 다른 값이다. 하나만 찍으면 오독된다.**
  //     센서 수         = 등록된 것 중 `sensor()` 로 붙인 것
  //     `moduleCount()` = **모듈 수**  — 센서 + 액추에이터. `D,*,<drain>,<n>` 의 `n` 이 이것이다
  //   ⚠ **자리 수는 안 찍는다.** 장치는 자리 배치를 모른다 — 서버 조립 표가 정한다.
  Serial.print(F("\n[PARKING NODE] proto v1 / "));
  { uint8_t sn = 0; for (uint8_t i = 0; i < MODULE_N; i++) if (isSensor(i)) sn++;
    Serial.print(sn); }             Serial.print(F(" sensors / "));
  Serial.print(moduleCount());     Serial.println(F(" modules / dev=" DEVICE_ID));

  // 🔴 상한(MODULE_CAP)을 넘겨 버린 등록이 있으면 **문장으로 말한다.**
  //   ⚠ 조용히 버리면 기여자는 "내 모듈이 왜 안 뜨지" 를 서버·화면에서 찾는다.
  if (modOverflowed) {
    Serial.print(F("[CFG] 🔴 모듈 상한 "));
    Serial.print(MODULE_CAP);
    Serial.print(F(" 을 넘겨 "));
    Serial.print(modOverflowed);
    Serial.println(F(" 개를 버렸다 — setup() 의 등록 줄을 줄여라"));
  }
  // 🔴 `begin()` 을 안 불렀으면 링크가 안 선다. **누락은 경고로 잡는다**(순서 의존이 아니다).
  //   ⚠ `slotStart == 0` 으로 보면 **거짓 경보**다 — 부팅 직후 `millis()` 가 0 이라
  //     세워도 0 이다. 그래서 별도 플래그를 본다.
  if (!node.beginDone) Serial.println(F("[CFG] 🔴 setup() 에서 node.begin() 을 먼저 불러라"));

  // ── 부팅 원인 — **추측을 사실로 바꾸는 한 줄**. 왜 재부팅했는지는 여기서만 알 수 있다 ──
  Serial.print(F("[BOOT] 리셋 원인: "));
  if (mcusrMirror == 0) {
    // 🔴 이 보드의 부트로더(optiboot 4.4)는 MCUSR 을 지우고 넘어온다.
    //   `.init3` 에서 가장 먼저 읽어도 이미 0 이다 — **알 수 있는 방법이 없다.**
    //   ⚠ "불명"이라고만 쓰면 가끔은 알 수 있을 것처럼 읽혀 다음 사람이 기다린다.
    Serial.println(F("알 수 없음 — 이 부트로더가 MCUSR 을 지우고 넘어온다"));
  } else {
    if (mcusrMirror & _BV(PORF))  Serial.print(F("전원인가(POR) "));
    if (mcusrMirror & _BV(EXTRF)) Serial.print(F("외부리셋(버튼/DTR) "));
    if (mcusrMirror & _BV(BORF))  Serial.print(F("**브라운아웃(전원부족)** "));
    if (mcusrMirror & _BV(WDRF))  Serial.print(F("워치독 "));
    Serial.println();
  }
  // 🔴 **"미배선" 이 아니라 "배제" 다** — 둘은 다른 진술이다(CLAUDE.md §아직 안 했다 / 안 하기로 했다).
  //   사용자 확정: 송수신 재설계로 펌웨어 hang 이 사라졌고, 하드 리셋으로만 풀리는 고장이
  //   그 hang 뿐이었으므로 **필요가 없어졌다.** 옛 문구는 *"A2 를 물리고 1 로 바꿔라"* 였는데
  //   그것은 **할 일 목록**으로 읽혀 다음 사람이 되살리게 만든다.
  //   ⚠ 되살릴 조건: **hang 이 다시 관측되면** 그때 다시 본다.
  Serial.print(F("[BOOT] 사다리 4단(ESP 하드리셋선) "));
  Serial.print(ESP_RST_WIRED ? F("배선됨(A2)") : F("배제(설계) — hang 을 없애 불필요"));
  Serial.print(F(" · 6단(워치독) "));
  Serial.println(ENABLE_WDT ? F("켬") : F("끔"));
}
#endif

// ─────────────────────────────────────────────────────────────────────────
// 🔓 모듈 등록 — `node.sensor("A1").on(내함수)` · `node.actuator("LD").on(내함수)`
//   🔴 **호출 순서가 전선 `idx` 이고 `occ` 비트 위치다.** 센서·액추에이터를 섞어도 된다 —
//     `occ` 비트를 센서는 값으로, 액추에이터는 에코로 세우므로 **겹치지 않는다.**
// ─────────────────────────────────────────────────────────────────────────
ModRef ParkingNode::sensor  (const char (&name)[3]) { return modAdd(name, 0); }
ModRef ParkingNode::actuator(const char (&name)[3]) { return modAdd(name, 1); }

// ─────────────────────────────────────────────────────────────────────────
// 한 박자 — 🔴 **네 단계의 순서는 고정이다.** 호출자가 바꿀 수 있는 것이 없다
//   수신 → 센서 훑기 → 송신(슬롯 배치) → 계수.
//   ⚠ **보류 ACK 를 따로 내보내지 마라.** 슬롯 배치가 같이 싣는다 —
//     따로 내보내면 슬롯당 1거래 규칙이 깨지고 수신 창을 침범한다.
// ─────────────────────────────────────────────────────────────────────────
void ParkingNode::tick() {
#if DEBUG
  // 🔴 **맨 앞이다.** 그래야 배너가 어떤 AT 로그보다 먼저 나가고 부팅 로그 첫 줄이 그대로다.
  if (!bannerDone) { bannerDone = true; printBootBanner(); }
#endif
#if ENABLE_WDT
  wdt_reset();                      // 여기 못 오면(=행) 8초 뒤 AVR 이 스스로 리셋된다
#endif
  unsigned long now = millis();
  espReset(now);
  espRead();
  drainPending();
  readSensors();                    // 자리 상태를 훑는다
  statusTick(now);
  cntTick(now);                     // 🔴 DEBUG 밖이다 — 운영 빌드에서도 관측이 남아야 한다
#if DEBUG
  diagTick(now);
  ramTick(now);
#endif
}
