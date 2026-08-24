// cmdresult.h — **명령의 결과를 나중에 받는다** (콜백)
// ═══════════════════════════════════════════════════════════════════════════
// 🔑 사용자 요구:
//   *"서버는 아두이노에 명령을 보내면 **리턴을 바로 받는 게 아니라** 아두이노 사이클에
//   일괄로 들어오는 정보를 보고 확인해야 한다. **그 콜백을 말한 것이다.**"*
//
//   `srv.send()` 는 **큐에 넣었다**까지만 말한다. 실제 결과는 장치가 자기 주기에 실어 보내고,
//   그것이 도착할 때 이 콜백이 불린다. **비동기 수신 구조가 요구된 것이다.**
//
// 🔴 **갈래가 셋이고, 셋을 반드시 갈라야 한다.**
//   하나로 뭉치면 기여자가 **장치 고장을 자기 콜백 로직으로 쫓는다.**
// ═══════════════════════════════════════════════════════════════════════════
#ifndef CMDRESULT_H
#define CMDRESULT_H

#include <string>

struct CmdResult {
    // 🔴 **셋은 서로 다른 사건이다. 고치는 곳도 다르다.**
    enum Kind {
        OK,          // 장치가 받았고 **콜백이 true 를 냈다**
                     //   ⚠ *"물리적으로 그렇게 됐다"* 가 **아니다.**
                     //     차단봉이 걸려도 콜백은 true 를 낼 수 있다.
                     //     그것까지 알려면 장치가 **읽어서 보고하는 값**(에코)이 필요하다.
        REJECTED,    // 장치가 받았고 **거절했다**(`result=3`). 🔑 **장치가 답했다**
                     //   → 재시도는 뜻이 없다. 값이나 등록을 고쳐야 한다.
        NO_ANSWER    // 🔴 **ACK 이 안 왔다.** 재전송을 다 쓰고도 답이 없다
                     //   → 장치·링크 문제다. **콜백 로직을 고쳐도 안 낫는다.**
    };

    Kind        kind;
    std::string devid;
    std::string module;     // 모듈 이름(2바이트)
    long        value;      // 보낸 인자
    int         deviceResult;   // 장치가 준 `result`. `NO_ANSWER` 면 -1
    unsigned    rid;        // 전선 rid — 로그와 대조할 때 쓴다

    CmdResult() : kind(NO_ANSWER), value(0), deviceResult(-1), rid(0) {}

    // 사람이 읽는 이름. 로그에 그대로 쓸 수 있다.
    const char* kindName() const {
        switch (kind) {
            case OK:        return "성공";
            case REJECTED:  return "거절";
            case NO_ANSWER: return "무응답";
        }
        return "?";
    }
};

// 기여자가 구현하는 것. 등록 안 하면 아무 일도 안 일어난다(로그는 그대로 남는다).
//
// ⚠ **이 함수 안에서 오래 걸리는 일을 하지 마라.** 서버의 한 박자 안에서 불린다 —
//   여기서 지연되면 **다음 하행 창을 놓친다.**
typedef void (*CmdResultFn)(const CmdResult&);

#endif  // CMDRESULT_H
