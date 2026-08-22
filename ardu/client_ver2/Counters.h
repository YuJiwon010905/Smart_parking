#pragma once
// ═════════════════════════════════════════════════════════════════════════
// Counters.h — 운영 계수기 출력([CNT]) — DEBUG 와 무관하게 항상 나간다
//   👁 **관측 성질** — 🔴 **잠그지 마라**
//     계수기 추가는 정상 작업이다. 잠그면 그때마다 경보가 뜬다(오늘 `hwrst` 를 여기 넣었다).
// ═════════════════════════════════════════════════════════════════════════
// 🔴🔴 **위치를 옮기지 마라.** `client.ino` 의 **정해진 자리에서** `#include` 된다.
//   자리를 바꾸거나 헤더 순서를 바꾸면 **선언·초기화 순서가 같이 바뀌고**,
//   **그 순간 거동이 바뀌고 산출물이 달라진다.**

// ── 🔴 운영 계수기 출력 — **`DEBUG` 와 무관하게 항상 나간다** ────────────────
//
// 왜 `#if DEBUG` 밖인가 (루트 지시 · 2026-08-16):
//   계수기 **변수**는 원래도 DEBUG 밖이었다. 문제는 **찍는 줄이 전부 안에 있었던 것**이다.
//   → `DEBUG=0` 으로 구우면 3칸이 1칸으로 주는 게 아니라 **관측이 0칸**이 된다.
//     장치가 **자기 ESP 리셋에 대해 눈이 먼 채로** 시연장에 서게 된다.
//   **셀 수만 있고 못 읽으면 안 뺀 것과 같다.** 그래서 읽는 경로를 같이 둔다.
//
// 왜 S 프레임(전선)이 아니라 시리얼인가:
//   전선에 실으면 서버 로그에 영구 보관되어 더 낫다. **그러나 프로토콜 변경이라
//   socket·web 이 걸리고**, 펌웨어 수정과 같은 굽기에 섞으면 변수가 둘이 된다(§6.3).
//   → **별건으로 올린다.** 지금은 도메인 안에서 닫히는 방법을 쓴다.
//
// 비용: 60초에 한 줄(약 60바이트). 115200bps 에서 약 5ms. 플래시도 수십 바이트다.
//   ⚠ `[AT]` 원문 로깅과 달리 **대역을 먹지 않는다** — 그래서 이건 밖에 둬도 된다.
//     경계는 "정수 카운터는 밖 · 장문 진단은 안"이다. 이 경계를 흐리지 마라.
//
// ⚠ 형식을 바꾸면 monitor 파서가 깨진다. 칸을 **추가**하되 기존 칸 이름·순서는 유지해라.
static uint32_t      cntLastAt = 0;
static const uint32_t CNT_PERIOD_MS = 60000;

static void cntTick(uint32_t now) {
  if ((uint32_t)(now - cntLastAt) < CNT_PERIOD_MS) return;

  // 🔴 2026-08-18 (REQ-0187 ②) — **슬롯 규율을 디버그 출력에도 적용한다.**
  //
  // 왜: 이 줄이 **손실원이었다.** 60초마다 하행 프레임 하나가 깨졌고, 창 B·C·D 에서
  //   **`[CKSUM NG]` 시각이 `[CNT]` 시각과 1~2초 안에 정합**했다(monitor 전수 대조).
  //     창 B 5건 · 창 C 4건(위상 `:56` 고정) · 창 D 2건 — **셋 다 정합.**
  //
  // 기전: `[CNT]` 는 약 142B 인데 하드웨어 UART TX 링버퍼는 **64B**(`SERIAL_TX_BUFFER_SIZE`)다.
  //   → 넘치는 78B 만큼 **블로킹**하고, 그동안 `espRead()` 가 안 돌아
  //     **도착 중인 하행 바이트가 SoftwareSerial 64B 링버퍼에서 사라진다.**
  //
  // ★ **UART 를 쓰는 모든 것이 슬롯 규율의 대상이다** — 프레임 송신만이 아니라 진단 출력도.
  //   지금까지 "송신"을 프로토콜 프레임으로만 좁게 봤다(원장 §11.2).
  //
  // ⚠ **60초 주기에서 최대 1.2초 지연은 무해하다.** 다음 송신 창까지 미룰 뿐이고
  //   `cntLastAt` 을 **찍을 때** 갱신하므로 주기가 밀리지 않는다(§3.4 의 재장전 교훈).
  //
  // ❌ **줄을 쪼개는 것은 대책이 아니다** — 총 바이트가 같아 점유가 안 줄고,
  //   오히려 **여러 창에 흩어져 더 많은 하행과 겹친다.**
  if (!netOnline) {
    // 오프라인이면 하행이 없다 → 겹칠 대상이 없으므로 창을 기다리지 않는다.
    // (안 그러면 링크가 죽은 동안 [CNT] 가 통째로 멈춰 진단이 사라진다)
  } else if ((uint32_t)(now - slotStart) + SLOT_TX_RESERVE_CNT_MS > TX_WINDOW_MS) {
    return;                      // 우리 송신 창이 아니거나 남은 시간이 부족하다 → 다음 슬롯에
  }

  cntLastAt = now;
  // 전부 **부팅 이후 누적**이다. 구간값이 아니다 — 창을 잡으려면 두 줄을 빼서 써라.
  Serial.print(F("[CNT] up="));      Serial.print(now / 1000);
  Serial.print(F(" drop="));         Serial.print(linkDrops);
  Serial.print(F(" esprst="));       Serial.print(espResets);
  Serial.print(F(" hwrst="));        Serial.print(hwRstAsserts);
  Serial.print('/');                 Serial.print(hwRstOk);        // 4단 실행/성공 — 분모를 같이 둔다
  Serial.print(F(" resync="));       Serial.print(promptResyncs);
  Serial.print(F(" sendfail="));     Serial.print(sendFails);
  Serial.print(F(" okto="));         Serial.print(sendOkTimeouts);
  // ★ 2026-08-17 신설 — ⚠ **칸을 뒤에 붙인다. 기존 이름·순서는 그대로 둔다**(monitor 파서).
  //   ⚠ 옛 로그에는 이 칸들이 없다. **굽기 전후를 같은 표에 넣지 마라** —
  //     그 칸의 0 은 "안 났다"가 아니라 "그 칩엔 없었다"다.
  Serial.print(F(" stuck="));        Serial.print(sendOkGiveups);   // T2 초과 → 링크 재수립
  Serial.print(F(" ackq="));         Serial.print(ackQ.pending());       // 지금 보류 중인 ACK
  Serial.print(F(" ackdrop="));      Serial.print(ackQ.drops());       // 큐가 넘쳐 버린 ACK(유입 초과)
  // ★ REQ-0204 — 캐시에서 밀려 버린 ACK. **대책이 다르므로 칸을 가른다.**
  //   ⚠ 칸을 더해 이 줄이 길어지는 것은 이제 안전하다 — `cntTick` 이 송신 창 안에서만 나간다(§11.2-2).
  Serial.print(F(" ackstale="));     Serial.print(ackQ.stale());
  // ★ 2026-08-17 슬롯 — **정수 계수는 `DEBUG` 밖**이다(원장 §8.4-2 의 경계).
  //   `[SLOT]` 줄은 매 슬롯 나가 대역을 먹으므로 `DEBUG` 안에 두지만, **누적은 여기 남긴다.**
  //   그래야 `DEBUG=0` 시연 빌드에서도 슬롯이 지켜졌는지 셀 수 있다.
  Serial.print(F(" slot="));         Serial.print(slotNo);          // 지난 슬롯 수 = **분모**
  Serial.print(F(" oow="));          Serial.print(slotOow);         // 수신 창 밖 하행 = 설계 위반
  Serial.print(F(" smiss="));        Serial.print(slotMissed);      // 송신 기회를 놓친 슬롯
  // ★ REQ-0167 — SoftwareSerial 링버퍼(64B) 넘침. **하한이다**(읽으면 플래그가 지워진다).
  Serial.print(F(" ssovf="));        Serial.print(ssOverflows);
  // ★ REQ-0174 — 체크섬 불일치(하행 프레임 파괴). 서버 계수로는 안 보이는 손실이다.
  Serial.print(F(" cksumng="));      Serial.print(cksumNg);
  // ★ REQ-0218 — 바이트 흐름으로 잡은 `SEND OK` 수. **줄 경로가 놓치던 양**이다.
  // 🔴 2026-08-18 — **okstream 계열은 스냅샷으로 찍는다.**
  //   이 줄을 찍는 도중 `espRead()` 가 돈다. `cntTick` 중에 `inSend` 는 false 지만
  //   **`awaitingSendOk` 는 흔히 true 다**(CIPSEND 직후의 정상 상태).
  //   그 pump 가 `SEND OK` 를 매칭하면 `okstream`/`bigokst` 가 **출력 도중에 늘어나서**
  //   먼저 찍힌 `okstream` 은 옛 값, 나중 찍힌 `bigokst` 는 새 값이 된다.
  //   → **`bigokst > okstream` 이라는 불가능한 출력**이 나오고, 읽는 쪽은 펌웨어 결함으로 읽는다.
  //   ⚠ `pendfill`/`bigfill`/`bigdrop` 은 `inSend` 가 필요해서 이 문제가 없다.
  //     **이 계열만 해당한다** — 그래서 여기만 스냅샷한다.
  // 🔴 **출력 도중에는 `espRead()` 를 부르지 않는다.** 한 번 넣었다가 뺐다.
  //   이유: pump 가 `[AT] "..."` 를 찍어 **`[CNT]` 줄 한가운데로 끼어든다.**
  //   시험에서 실제로 그렇게 나왔다: `… pendfill=0[AT] "SEND OK" (7)\n bigfill=0 …`
  //   **monitor 의 파서가 그 줄을 못 읽는다.** 계측기를 고치려다 계측기를 깬 꼴이다.
  //   ⚠ 그러면 블로킹은 어떻게 되나 — **계산으로 안전하다:**
  //     `[CNT]` 약 220B @115200 ≈ 19ms · TX 링버퍼 64B 를 넘는 156B 만큼 ≈ 13.5ms 블로킹.
  //     그동안 들어올 하행은 9600bps 에서 **약 13B** 이고, SoftwareSerial 링버퍼 64B 에 여유가 있다.
  //   🔑 그리고 `cntTick` 은 **송신 창 안에서만** 나간다(§11.2-2) — 하행이 오는 시간대가 아니다.
  //   ⚠ 줄이 이보다 크게 길어지면 이 계산을 다시 해라. **300B 시험이 그 경보다**(시험 [28]).
  //
  // 🔴 **여기 말고 블로킹하는 곳이 하나 더 있다 — 센서 함수다.**
  //   `pulseIn`(초음파 샘플)은 타임아웃만큼 막는다. 위 13B 는 **그것을 안 센 값이다.**
  //   실측(시험 [45]): 타임아웃 6,960µs · 게이트 200ms → 슬롯당 **6회 · 41ms = 3.4%**
  //     그 6.96ms 동안 유입은 9600bps 에서 **약 6.7B** → 링버퍼 64B 에 여유가 남는다.
  //   ⚠ **타임아웃을 늘리면 이 값이 그대로 커진다.** 4m(25,000µs)로 두면 24B 가 되고
  //     위 13B 와 겹치면 37B 다 — 64B 의 절반을 넘는다. **문턱에서 유도하는 이유가 이것이다.**
  //   🔑 겹침 자체는 구조가 막는다: `readSensors()` 와 `cntTick()` 은 `tick()` 안에서
  //     **순차**이고 단일 스레드다. 두 블로킹이 동시에 일어날 수 없다.
  const uint16_t okStreamSnap = sendOkByStream;
  const uint16_t okLostSnap   = okLostByLine;
  const uint16_t okStreamBigSnap = okStreamBig;
  const uint16_t okLostBigSnap   = okLostBig;
  Serial.print(F(" okstream="));     Serial.print(okStreamSnap);
  // ★ monitor 요청 — `okstream` 과 **짝으로 읽는다**(위 feedRxChar 주석의 판정표).
  Serial.print(F(" penddrop="));     Serial.print(pendDrops);
  // 🔴 2026-08-18 — **`penddrop` 의 분모.** 이것이 0 이면 위 `penddrop=0` 은
  //   "안 넘쳤다"가 아니라 **"셀 일이 없었다"** 이고, 그 둘은 다른 결론으로 이어진다.
  Serial.print(F(" pendfill="));     Serial.print(pendFills);
  // ⚠ monitor 요청 — **대역(`>=64B`)별.** 누적 총계로는 창 G 의 갈림(`48~63` 0/43 · `>=64` 9/34)이 안 보인다.
  //   🔑 `64` 는 `_SS_MAX_RX_BUFF` = **기전 경계**다. monitor 의 `48` 은 표본 추출 기준이라 **다른 축이다.**
  Serial.print(F(" bigfill="));      Serial.print(pendFillsBig);
  Serial.print(F(" bigdrop="));      Serial.print(pendDropsBig);
  Serial.print(F(" bigokst="));      Serial.print(okStreamBigSnap);
  // ★ **대책 ②의 진짜 분자.** `okstream` 이 아니다 — 바이트 매처는 줄 완성 **전**에 발화해
  //   항상 줄 경로를 이기므로, `okstream` 은 그냥 탐지 총수다.
  //   **`oklost` 는 "줄 경로였으면 영영 잃었을 SEND OK"** 이고 그것이 곧 T2 였다.
  Serial.print(F(" oklost="));       Serial.print(okLostSnap);
  Serial.print(F(" bigoklost="));    Serial.print(okLostBigSnap);
  Serial.print(F(" skip="));         Serial.print(sendSkips);
  Serial.print(F(" online="));       Serial.println(netOnline ? 1 : 0);
}
