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
class ParkingServer;

// 🔴 **센서가 잰 값.** 점유 비트(`occupied`)와 **다른 축**이다 —
//   비트는 *"문턱을 넘었나"*, 이것은 *"얼마인가"* 다. 한 칸에 합치지 마라.
// 🔴 **`has` 와 `value` 는 붙어 다녀야 한다.** 갈라 두면 `has` 를 안 보고 `value` 를 쓰는 코드가
//   반드시 나오고, 그때 **"못 쟀다"가 조용히 `0` 이 된다.**
// ⚠ 단위는 **기여자의 것**이다 — 서버도 프로토콜도 모른다(초음파는 cm · 조도는 lux).
//   단위를 알려면 라벨에 담아라: `lot.label("P1","A1","앞쪽 거리(cm)")`
struct SensorMeasure {
    bool has;     // 값이 있나. false 면 아래는 **뜻이 없다**
    long value;   // 장치가 준 정수
    SensorMeasure() : has(false), value(0) {}
    SensorMeasure(bool h, long v) : has(h), value(v) {}
};

// 🔴 **자리 점유가 바뀌면 불린다.** `onCommandResult` 와 같은 계열이다 —
//   기여자는 훅 하나를 더 배우는 것이 아니라 **같은 모양을 한 번 더 쓴다.**
//   `spot`     : 자리 id · `module` : **그 값을 말한 센서 모듈 이름**
//   `occupied` : 바뀐 **뒤**의 값. 상승(비었다→찼다)과 하강(찼다→비었다) **둘 다** 온다
//
// 🔴 **모듈 단위로 불린다.** 한 자리에 센서가 둘이면 **각각** 온다 —
//   자리 하나로 합쳐서 한 번 부르지 않는다. 합칠지 말지는 **기여자가 정한다**:
//     `if (module != "A1") return;`  ← 한 센서만 쓰겠다
//   🔑 그래서 **콜백은 하나이고 파라미터만 하나 늘었다.** 배우는 것이 안 는다.
// 🔑 **센서(`I*`)만 온다.** 명령 모듈의 에코는 안 온다 — 안 그러면 자기 명령이 자기를 부른다.
// ⚠ 서버의 한 박자 안에서 불린다. 오래 걸리는 일을 하지 마라.
// 🔴 **값이 올 때마다 불린다** — 점유가 안 바뀌어도 온다.
//   `onOccupancy` 는 **상태가 바뀔 때**의 콜백이고 이것은 **값이 흐를 때**의 콜백이다.
//   차가 60 → 40 → 20cm 로 들어와도 점유는 계속 1 이라 `onOccupancy` 가 안 불린다 —
//   🔑 **주차 유도(거리 표시·근접 경고)가 이 콜백의 존재 이유다.**
//
// 🔑 **`has` 가 없다.** 값이 있을 때만 부르기 때문이다 — *"못 쟀다"* 는 사건이 아니다.
// ⚠ 단위는 **기여자의 것**이다(§`SensorMeasure`).
// ⚠ **자주 불린다** — 슬롯당 센서 수만큼(초당 약 k/1.2회). 여기서 무거운 일을 하면 박자를 먹는다.
//   거르고 싶으면 **기여자가 거른다.** 서버가 문턱을 정하면 그 문턱을 누가 정하는지가 또 빈 자리가 된다.
typedef void (*SensorValueFn)(ParkingServer& srv, const std::string& spot,
                              const std::string& module, long value);

typedef void (*OccupancyFn)(ParkingServer& srv, const std::string& spot,
                            const std::string& module, bool occupied,
                            const SensorMeasure& measure);

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
