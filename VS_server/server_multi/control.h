// control.h — **조작 UI 를 기여자가 선언한다** (2026-08-20 · REQ-0281)
// ═══════════════════════════════════════════════════════════════════════════
// 사용자 요구:
//   *"모든 센서에 활용가능하도록 만들자. 혹시 **기여자 코드에서 작성된 코드를 기준으로
//   호출하는 방식**이 이용가능한가?"*
//
// 🔴 **이것이 우리가 막혀 있던 자리를 푼다.**
//   우리 계약은 *"값의 뜻을 서버도 프로토콜도 모른다"* 이다. 그래서 화면이 UI 를 정할 수 없었다:
//     ❌ 화면이 `kind` 로 정한다  → kind 가 늘 때마다 **화면을 고쳐야 한다**
//     ❌ 숫자 칸 하나로 통일한다  → `DR` 에 1/2/3/4 를 **외워서 쳐야 한다**
//     ✅ **뜻을 아는 사람이 선언한다** — 기여자다
//
// > **`choice` 선언이 곧 명령표다.**
//   지금은 그 표를 **서버 주석과 장치 주석 두 곳**에 똑같이 적으라고 했다(GUIDE-sample §명령).
//   **선언으로 만들면 화면이 그것을 그리고, 표가 한 곳이 된다.**
//
// 🔑 화면이 아는 것은 **입력 형태**뿐이고 **뜻은 라벨로만** 온다.
//   그래서 `kind` 가 늘어도 화면을 안 고친다 — 위 ❌ 첫째를 구조적으로 피한다.
// ═══════════════════════════════════════════════════════════════════════════
#ifndef CONTROL_H
#define CONTROL_H

#include <string>
#include <vector>
#include <utility>

// 모듈 하나의 조작 선언. **선언 안 한 모듈은 화면에 조작 UI 가 없다**
// (`sensor`/`actuator` 의 *"선언 안 한 것은 꺼진 것"* 과 같은 규칙).
struct ControlDecl {
    // ⚠ **위젯은 셋뿐이다. 늘리지 마라.**
    //   넷째를 만들고 싶어지면 대개 *"뜻"* 을 화면에 넣으려는 것이다 — 그건 라벨의 일이다.
    enum Widget {
        NONE,     // 🔴 선언은 했는데 위젯을 안 정했다 — 기동 로그가 지목해 말한다
        TOGGLE,   // 0 / 1
        NUMBER,   // min ~ max 숫자
        CHOICE    // 정해진 값 목록 — **이것이 명령표다**
    };
    std::string devid;
    std::string name;      // 모듈 이름(2바이트)
    // 🔴 **`label` 필드가 여기 있었다. 2026-08-20 에 뺐다.**
    //   이름의 정본은 `lot.label(devid, module, "…")` 하나다 —
    //   같은 모듈에 이름이 둘이면 **우선순위 규칙**이 생기고, 규칙이 생기면 사람이 틀린다.
    //   ⚠ `choice` 의 `.option(1,"열기")` 는 **버튼 글자**라 다른 것이다. 그건 남는다.
    Widget      widget;
    long        vmin, vmax;                              // NUMBER 전용
    std::vector<std::pair<long, std::string> > options;  // CHOICE 전용

    ControlDecl() : widget(NONE), vmin(0), vmax(0) {}

    // 🔴 **값이 이 선언에 맞나** — 서버가 판정한다. 화면이 아니다.
    //   화면이 먼저 막아도 좋지만 **믿지는 않는다**(§"양쪽이 각자 확인하는 것이 낫다").
    //   반환: 0 = 통과, 그 외 = 사유 코드 문자열
    const char* reject_reason(long v) const {
        switch (widget) {
            case TOGGLE:
                return (v == 0 || v == 1) ? 0 : "out_of_range";
            case NUMBER:
                return (v >= vmin && v <= vmax) ? 0 : "out_of_range";
            case CHOICE: {
                for (size_t i = 0; i < options.size(); i++)
                    if (options[i].first == v) return 0;
                return "out_of_range";
            }
            case NONE:
            default:
                // 🔑 **선언은 있는데 위젯이 없다.** `not_declared` 로 뭉치지 않는다 —
                //   원인이 다르면 고칠 곳도 다르다(*"안 적었다"* 와 *"위젯을 안 골랐다"*).
                return "not_declared";
        }
        // (도달하지 않음)
    }

    const char* widget_name() const {
        switch (widget) {
            case TOGGLE: return "toggle";
            case NUMBER: return "number";
            case CHOICE: return "choice";
            case NONE:
            default:     return "none";
        }
    }
};

#endif  // CONTROL_H
