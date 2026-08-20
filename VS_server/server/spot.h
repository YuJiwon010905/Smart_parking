// spot.h — **자리의 동작 방식.** 서버가 센서 값을 모아 주고, 자리가 그것으로 무엇을 할지 정한다.
//          기여자는 서버 로직을 안 만지고 `occupied()` 하나만 구현한다.
//          쓰는 예 : `EXAMPLES.cpp` 의 `example_behavior()` (컴파일되는 예제다)
// ═══════════════════════════════════════════════════════════════════════════
// ⏳ **아직 없는 것**: `Tri`(YES/NO/UNKNOWN) 반환형. 지금은 `bool` 이라 *"모른다"* 를 못 나르고,
//   호출자가 `value_state` 로 따로 말한다.
// ⚠ 그 타입 변경을 다른 변경과 **한 커밋에 섞지 마라** — 옮기기와 타입 변경이 섞이면
//   산출물 차이를 무엇에 귀속할지 못 가른다.
// ═══════════════════════════════════════════════════════════════════════════
#ifndef SPOT_H
#define SPOT_H

#include <vector>
#include <cstddef>

// 센서 하나가 지금 무엇을 말하는가.
//
// 🔴 **`known` 과 `value` 는 다른 것이다.** `known=false` 는 *"그 센서 값을 아직 못 받았다"* 이지
//   *"비었다"* 가 아니다. **둘을 한 `bool` 로 합치면 모름이 거짓으로 무너진다** —
//   그래서 여기서부터 갈라 둔다(2단계에서 반환형도 `Tri` 로 간다).
struct SensorReading {
    bool known;   // 이 센서의 값을 받았나
    bool value;   // 받았다면 그 값 (`known == false` 면 **의미 없다**)
    SensorReading() : known(false), value(false) {}
    SensorReading(bool k, bool v) : known(k), value(v) {}
};

// 자리의 동작 방식. **기여자가 상속해서 구현한다.**
//
// ⚠ **기본 구현을 지우지 마라.** 자리가 자기 구현을 안 주면 이것이 쓰인다 —
//   지금 모든 자리가 그 경우이고, 그래서 이 단계의 거동 변화가 0 이다.
struct SpotBehavior {
    virtual ~SpotBehavior() {}

    // 🔴 **기본은 OR 다 — 아는 센서 중 하나라도 "찼다"면 찼다.**
    //
    //   왜 OR 인가: **두 오류의 대가가 대칭이 아니다.**
    //     빈 자리를 "찼다"고 하면 손해는 자리 하나이고,
    //     찬 자리를 "비었다"고 하면 **운전자가 가서 못 댄다.**
    //   ⚠ AND 로 하면 센서 하나가 죽었을 때 그 자리가 **영영 "비었다"** 로 보인다.
    //
    // ⚠ **모르는 센서는 안 센다**(`known == false` 는 건너뛴다).
    //   그래서 전부 모르면 `false` 가 나오는데, 그건 *"비었다"* 가 아니라 *"모른다"* 다.
    //   🔴 **그 구분은 지금 이 반환값이 못 나른다** — 호출자가 `value_state` 로 따로 말한다.
    //     2단계에서 `Tri` 로 바꾸면 이 주석이 사라진다.
    virtual bool occupied(const std::vector<SensorReading>& sensors) const {
        for (size_t i = 0; i < sensors.size(); i++)
            if (sensors[i].known && sensors[i].value) return true;
        return false;
    }
};

#endif  // SPOT_H
