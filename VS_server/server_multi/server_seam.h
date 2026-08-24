/* server_seam.h — **주차장 도메인 ↔ 디바이스 계층의 유일한 접점** (REQ-0096 단계 B)
 *
 * ─────────────────────────────────────────────────────────────────────────────
 * ⚠ **이 파일이 이 리팩터링의 전부다.** 나머지는 코드를 옮기는 일이고, 설계는 여기 있다.
 *
 * 규칙 셋 — 하나라도 어기면 나중에 프로세스로 못 가른다:
 *
 *   1. **도메인은 소켓·프레임·체크섬·레지스트리를 모른다.**
 *      `device_id` 도 **문자열 값**으로만 받는다. 소켓 핸들이나 노드 포인터가 넘어오면
 *      그 순간 도메인이 전송 계층에 묶인다.
 *
 *   2. **디바이스는 주차공간·예약·번호판을 모른다.** 센서 인덱스와 채널 번호만 안다.
 *      그래서 `sensors` 는 **"칸이 찼다"가 아니라 "내 센서 N번이 눌렸다"** 이다.
 *      (이 의미 재정의가 계획서 §5 의 핵심이고, 매핑은 이 경계 위에 얹힌다.)
 *
 *   3. **값만 오간다. 포인터로 서로의 내부를 만지지 않는다.**
 *      이 구조체들이 그대로 바이트로 직렬화될 수 있어야 나중에 루프백 TCP(계획서 §4)로
 *      갈아탈 수 있다. 그래서 `std::string`·컨테이너·가상함수를 쓰지 않는다.
 *
 * ⚠ **지금은 한 프로세스 안이다.** 프로세스 분리는 이번 범위가 아니다(요청 명시).
 *   다만 그때가 오면 **이 파일만 그대로 두고 양쪽을 떼면 된다** — 그게 이음매의 값이다.
 * ─────────────────────────────────────────────────────────────────────────────
 */
#ifndef SERVER_SEAM_H
#define SERVER_SEAM_H

#include <stdint.h>
#include <string.h>

#define SEAM_DEVID_MAX 8            /* §2.3 devid ::= 1*8(...) */
#define SEAM_SENSORS   10           /* §2.4 bits10 — 인덱스 0..9 */

/* ── 디바이스 → 도메인 ─────────────────────────────────────────────────────
 * "장치에서 이런 일이 있었다" 만 말한다. **무엇을 뜻하는지는 도메인이 정한다.** */
enum DeviceEventKind {
    DEV_CONNECT = 1,    /* 장치가 붙었다 (device_id 확정 시점 = 승격) */
    DEV_DISCONNECT,     /* 끊겼다. reason 에 사유 문자열 */
    DEV_ONLINE,         /* §3.4 프레임이 돌아왔다 */
    DEV_OFFLINE,        /* §3.4 3.5초 무프레임 */
    DEV_SENSORS,        /* 유효한 상태 프레임을 받았다 */
    DEV_ACK             /* 하행 명령에 대한 응답 */
};

struct DeviceEvent {
    uint8_t  kind;                        /* DeviceEventKind */
    char     device_id[SEAM_DEVID_MAX+1]; /* NUL 종단. **값이다** */

    /* kind == DEV_SENSORS */
    uint16_t sensors;      /* 비트 0..9 = 센서 0..9. ⚠ **"칸"이 아니다** */
    uint16_t tmask;        /* §12A 오버라이드 비트. 0xFFFF = 없음(무장 해제) */
    uint32_t uptime_s;     /* §2.4 초 단위 */
    uint16_t seq;
    uint8_t  reboot;       /* §7.4 판정 결과. **도메인은 판정법을 모른다** */

    /* kind == DEV_ACK */
    uint16_t rid;
    uint8_t  result;       /* §2.4 A 프레임 result */

    /* kind == DEV_DISCONNECT */
    char     reason[40];   /* "keepalive 시간초과(errno=60)" 등. 표시·기록용 */
};

/* ── 도메인 → 디바이스 ─────────────────────────────────────────────────────
 * "이 장치에 이걸 보내라" 만 말한다. **어떻게 보내는지(프레임·재전송·ACK 대기)는
 * 디바이스가 정한다.** 도메인은 wire_rid 도 체크섬도 모른다. */
enum DeviceCommandKind {
    CMD_RESERVE = 1,    /* §2.4 R */
    CMD_CANCEL,         /* §2.4 C */
    CMD_TEST,           /* §12A T */
    CMD_SIMSTEP,        /* §12B M */
    CMD_WRITE           /* §2.4-W 일반 하행(LCD·차단기·부저) */
};

struct DeviceCommand {
    uint8_t  kind;                        /* DeviceCommandKind */
    char     device_id[SEAM_DEVID_MAX+1];

    /* CMD_RESERVE / CMD_CANCEL
     * ⚠ **과도기 필드다.** 지금은 도메인이 "칸 이름"을 그대로 넘긴다 —
     *   현재 장치가 칸을 알기 때문이다(계획서 §5 가 없애려는 그 구조).
     *   매핑이 들어오면 이 자리가 `(ch, 값)` 으로 바뀌고 도메인은 space_id 만 다루게 된다.
     *   **지금 지우면 동작이 깨진다.** REQ-0096 은 경계만 만들고 매핑은 안 건드린다.
     *
     *   ⏳ **이 과도기가 끝나는 시점 — 명시한다(임시가 영구가 되는 것을 막는 유일한 방법이다):**
     *      `docs/net/plan-windows-and-mapping.md` §5.7 의 **B 단계**
     *      (= 서버 내부를 `space_id` 기준으로 전환하는 단계)에서 이 필드를 지운다.
     *      그때 `slot[3]`·`user_id[9]` 가 사라지고 `ch`+`data[9]` 만 남는다 —
     *      즉 **`CMD_RESERVE`/`CMD_CANCEL` 이 `CMD_WRITE` 로 흡수된다.**
     *      **B 단계를 하면서 이 주석이 남아 있으면 그것이 곧 미완료 신호다.** */
    char     slot[3];      /* "A1".."B5" 또는 "??" */
    char     user_id[9];   /* 전선에 나가는 값(§2.3 0*8) */

    /* CMD_TEST */
    char     top;          /* 'A'|'D'|'S'|'X' */
    char     tval;         /* '0'|'1'|'-' */

    /* CMD_WRITE (§2.4-W) */
    uint8_t  ch;           /* 출력 채널. **의미는 서버 설정에 있다** */
    char     data[9];      /* 8B 고정 + NUL */
};

/* 값 채우기 도우미 — 양쪽이 같은 방식으로 채우게 한다.
 * `strncpy` 를 직접 쓰면 NUL 종단을 빠뜨리는 실수가 반복된다. */
static inline void seam_set_dev(char* dst, const char* src) {
    size_t n = 0;
    while (src && src[n] && n < SEAM_DEVID_MAX) { dst[n] = src[n]; n++; }
    dst[n] = '\0';
}

static inline void seam_clear_event(DeviceEvent* e) {
    memset(e, 0, sizeof(*e));
    e->tmask = 0xFFFF;          /* 기본은 "무장 없음" — 0 은 "전부 해제"라 뜻이 다르다 */
}

static inline void seam_clear_command(DeviceCommand* c) {
    memset(c, 0, sizeof(*c));
    c->top = '-';
    c->tval = '-';
}

#endif /* SERVER_SEAM_H */
