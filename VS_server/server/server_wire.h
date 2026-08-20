/* server_wire.h — 전선 프레임의 **유일한 정의**. 서버와 장치가 같은 코드를 쓴다.
 *
 * 명세: docs/net/parking-protocol.md §2.2(체크섬) · §2.4-W(일반 하행 프레임)
 *
 * ─────────────────────────────────────────────────────────────────────────────
 * ⚠ **왜 헤더 하나에 몰아넣는가**
 *   서버와 장치가 각자 손으로 파싱하면 **반드시 어긋난다** — 필드 순서, 패딩 문자,
 *   체크섬 범위에서. 프로토콜 버그의 고전적 발생원이다. 그래서 encode/decode 를
 *   여기 한 곳에만 둔다. 양쪽이 이 파일을 그대로 쓴다.
 *
 * ⚠ **freestanding 이다.** AVR(8비트·RAM 2KB)에서 컴파일된다:
 *   `std::string`·`iostream`·동적 할당·예외를 쓰지 않는다. `char[]` 와 정수만 쓴다.
 *   `snprintf` 도 decode 경로에서는 쓰지 않는다(AVR 에서 1.5KB 가량을 끌어온다).
 * ─────────────────────────────────────────────────────────────────────────────
 */
#ifndef SERVER_WIRE_H
#define SERVER_WIRE_H

#include <stdint.h>
#include <string.h>

/* ⚠ 드리프트 감지 — 막을 수 없으면 **드러나게** 한다.
 *
 * Arduino IDE 는 스케치 폴더 안의 파일만 자동 컴파일하므로 장치 쪽은 이 파일의
 * **복사본**이 될 가능성이 높다. 복사하는 순간 갈라진다 — 저장소 파일과 실제 칩이
 * 다른 상태는 오늘 실제로 겪었다.
 *
 * 규약: 장치는 부팅 배너에 `wire=N` 을 찍는다. 서버는 기동 로그에 자기 값을 찍는다.
 *
 * ⚠ **서버가 장치의 값을 자동으로 비교할 수는 아직 없다.** 상행 프레임에 그 필드가
 *   없기 때문이다. 지금은 **두 로그를 사람이 대조**해야 한다.
 *   자동 비교는 §2.4-D 진단 프레임이 들어올 때 그 프레임에 실으면 공짜로 된다 —
 *   그때까지는 "자동으로 잡힌다"고 말하지 않는다. 안 되는 것을 된다고 적지 않는다.
 */
#define WIRE_VERSION 1

/* 페이로드는 **8바이트 고정**이다(§2.4-W).
 * 릴레이처럼 1바이트만 쓰는 경우에도 8을 채운다 — 하행은 이벤트성이라 낭비가 무의미하고,
 * 대신 **파서가 하나로 끝난다.** 기능마다 프레임 타입을 늘리지 않기 위한 값이다. */
#define WIRE_DATA_LEN 8

/* ⚠ **패딩은 공백이 아니라 `_` 다. 이건 취향이 아니다.**
 * 2026-08-15 에 `AT+CIPSTART="TCP"," 192.168.35.21"` 의 **앞 공백 한 칸**이
 * 하루를 날렸다. 공백은 눈에 안 보이고, 트림 여부가 구현마다 달라 조용히 갈린다.
 * `_` 는 로그에서 보이고 트림 대상이 아니다. */
#define WIRE_PAD '_'

/* 한 줄 최대 — §2.1 의 64B(LF 포함) 안에 있어야 한다.
 * W 프레임 최악은 `W,65535,255,________,XX\n` = 24B 라 여유 40B. */
#define WIRE_MAX_LINE 64

/* 수신 측에서 **의미가 드러나게** 하는 보관 자료형.
 * 파싱 결과를 `char*` 조각으로 들고 다니면 어느 필드가 무엇인지 호출부마다 다시 세게 된다. */
typedef struct {
    char     type;                    /* 'W' 등 프레임 타입                     */
    uint16_t rid;                     /* 요청 id — ACK 로 되돌려 준다(§4.1)     */
    uint8_t  ch;                      /* **그 장치의 출력 채널 번호.**          */
                                      /* 무엇을 뜻하는지는 서버 설정에 있다     */
    char     data[WIRE_DATA_LEN + 1]; /* 8바이트 + NUL                          */
    uint8_t  valid;                   /* 1 = 체크섬·문법 통과. 0 이면 나머지를  */
                                      /*     **쓰면 안 된다**                   */
} WireMsg;

/* ── §2.2 체크섬 ────────────────────────────────────────────────────────────
 * 대상: 첫 바이트부터 **체크섬 앞 쉼표까지(그 쉼표 포함)**. 대문자 2자리 hex.
 * 범위를 한 글자라도 다르게 잡으면 양쪽이 영원히 안 맞는다 — 그래서 여기 한 번만 적는다. */
static inline uint8_t wire_cksum(const char* p, uint16_t len) {
    uint8_t x = 0;
    for (uint16_t i = 0; i < len; i++) x ^= (uint8_t)p[i];
    return x;
}

static inline char wire_hex(uint8_t v) {
    return (char)(v < 10 ? ('0' + v) : ('A' + (v - 10)));
}

static inline int wire_hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;                        /* 소문자는 받지 않는다 — §2.2 가 대문자로 못 박았다 */
}

/* 페이로드에 쓸 수 있는 문자.
 * **쉼표와 LF 는 절대 안 된다**(필드·줄 구분자다). 공백도 뺀다(WIRE_PAD 주석 참조). */
static inline uint8_t wire_data_char_ok(char c) {
    return (uint8_t)((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                     (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.');
}

/* ── encode ────────────────────────────────────────────────────────────────
 * `out` 에 `W,<rid>,<ch>,<8바이트>,<cksum>\n` 을 쓴다.
 * 반환: 쓴 길이(LF 포함). 0 = 실패(버퍼 부족 / 잘못된 페이로드).
 * **실패를 0 으로 돌려주는 이유**: 잘린 줄을 전선에 내보내는 것이 최악이다.
 * 형식상 유효해 보이는데 내용이 잘린 프레임은 원인 추적이 가장 어렵다(REQ-0023 이 겪었다). */
static inline uint16_t wire_encode(char* out, uint16_t outsz,
                                   char type, uint16_t rid, uint8_t ch,
                                   const char* data8) {
    if (!out || !data8 || outsz < 24) return 0;
    for (uint8_t i = 0; i < WIRE_DATA_LEN; i++)
        if (!wire_data_char_ok(data8[i])) return 0;   /* NUL 이면 여기서 걸린다 */

    uint16_t n = 0;
    out[n++] = type;
    out[n++] = ',';
    /* uint16 을 손으로 찍는다 — snprintf 를 안 쓰려는 것이다(AVR 코드 크기) */
    char tmp[6]; uint8_t t = 0; uint16_t v = rid;
    do { tmp[t++] = (char)('0' + (v % 10)); v /= 10; } while (v);
    while (t) out[n++] = tmp[--t];
    out[n++] = ',';
    uint8_t c8 = ch; t = 0;
    do { tmp[t++] = (char)('0' + (c8 % 10)); c8 /= 10; } while (c8);
    while (t) out[n++] = tmp[--t];
    out[n++] = ',';
    for (uint8_t i = 0; i < WIRE_DATA_LEN; i++) out[n++] = data8[i];
    out[n++] = ',';
    if ((uint16_t)(n + 3) > outsz) return 0;          /* cksum 2 + LF 1 */
    uint8_t k = wire_cksum(out, n);                   /* 쉼표까지 포함 — §2.2 */
    out[n++] = wire_hex((uint8_t)(k >> 4));
    out[n++] = wire_hex((uint8_t)(k & 0x0F));
    out[n++] = '\n';
    return n;
}

/* ── decode ────────────────────────────────────────────────────────────────
 * 한 줄(LF 없이, `\r` 도 제거된 상태)을 받아 `WireMsg` 를 채운다.
 * 반환: 1 = 유효. 0 = 버림.
 *
 * ⚠ **실패해도 out 을 부분적으로 채우지 않는다.** 반쯤 채워진 구조체를 쓰면
 * "가끔 이상한 값이 나온다"가 되고, 그건 원인을 못 찾는 종류다. valid 로만 판단하게 한다. */
static inline uint8_t wire_decode(const char* line, uint16_t len, WireMsg* out) {
    if (!line || !out) return 0;
    memset(out, 0, sizeof(*out));
    if (len < 10 || len > WIRE_MAX_LINE) return 0;

    /* 체크섬은 맨 뒤 2자리, 그 앞은 쉼표여야 한다 */
    if (line[len - 3] != ',') return 0;
    int hi = wire_hexval(line[len - 2]), lo = wire_hexval(line[len - 1]);
    if (hi < 0 || lo < 0) return 0;
    uint8_t want = (uint8_t)((hi << 4) | lo);
    if (wire_cksum(line, (uint16_t)(len - 2)) != want) return 0;   /* 쉼표 포함 */

    uint16_t i = 0;
    char type = line[i++];
    if (line[i++] != ',') return 0;

    uint32_t rid = 0; uint8_t nd = 0;
    while (i < len && line[i] >= '0' && line[i] <= '9') { rid = rid * 10 + (uint32_t)(line[i++] - '0'); nd++; }
    if (!nd || nd > 5 || rid > 65535u || line[i] != ',') return 0;
    i++;

    uint32_t ch = 0; nd = 0;
    while (i < len && line[i] >= '0' && line[i] <= '9') { ch = ch * 10 + (uint32_t)(line[i++] - '0'); nd++; }
    if (!nd || nd > 3 || ch > 255u || line[i] != ',') return 0;
    i++;

    /* 페이로드는 **정확히 8바이트**여야 한다. 짧아도 길어도 버린다 —
     * 고정폭이 이 설계의 전제이고, 어기면 파서가 하나로 안 끝난다. */
    if ((uint16_t)(i + WIRE_DATA_LEN) != (uint16_t)(len - 3)) return 0;
    for (uint8_t k = 0; k < WIRE_DATA_LEN; k++) {
        char c = line[i + k];
        if (!wire_data_char_ok(c)) return 0;
        out->data[k] = c;
    }
    out->data[WIRE_DATA_LEN] = '\0';
    out->type  = type;
    out->rid   = (uint16_t)rid;
    out->ch    = (uint8_t)ch;
    out->valid = 1;
    return 1;
}

/* ── LCD 번호판 7자리 (§2.4-W 의 첫 사용자) ────────────────────────────────
 * `front` 는 2 또는 3자리, `back` 은 4자리. 결과는 8바이트 페이로드다.
 *
 *   123가4567 → front="123" back="4567" → "1234567_"
 *    12가3456 → front="12"  back="3456" → "_123456_"
 *
 * **앞자리 폭이 흔들리므로 3칸 고정·오른쪽 정렬**로 잡는다. 위치가 고정이면
 * 장치 파싱이 "0~2 앞자리 · 3~6 뒷자리"로 끝난다.
 *
 * ⚠⚠ **표시 전용이다. 식별에 쓰면 안 된다.**
 * 한글이 빠졌으므로 `123가4567` 과 `123나4567` 이 **같은 값**이 된다. 유일하지 않다.
 * 칸 매칭·중복 판정·예약은 **항상 전체 번호판**으로 해야 한다. 이 구분이 흐려지면
 * "가끔 엉뚱한 차가 배정된다"가 되고 며칠 뒤에 원인을 못 찾는다.
 *
 * ⚠ **공백은 전선에 싣지 않는다.** 전선 `1234567_` → LCD 표시 `123 4567`.
 * 보기 좋게 띄우는 것은 **장치가** 한다. 전선에 공백을 실으면 트림 사고가 난다. */
static inline uint8_t wire_lcd_plate(char* out9, const char* front, const char* back) {
    if (!out9 || !front || !back) return 0;
    uint16_t fl = (uint16_t)strlen(front), bl = (uint16_t)strlen(back);
    if ((fl != 2 && fl != 3) || bl != 4) return 0;
    uint8_t n = 0;
    if (fl == 2) out9[n++] = WIRE_PAD;                /* 3칸 오른쪽 정렬 */
    for (uint16_t k = 0; k < fl; k++) {
        if (front[k] < '0' || front[k] > '9') return 0;
        out9[n++] = front[k];
    }
    for (uint16_t k = 0; k < bl; k++) {
        if (back[k] < '0' || back[k] > '9') return 0;
        out9[n++] = back[k];
    }
    out9[n++] = WIRE_PAD;                             /* 7자리 + 패딩 1 = 8 */
    out9[n] = '\0';
    return 1;
}

/* LCD 지우기 — 8바이트 전부 `-`. 빈 문자열이나 공백을 쓰지 않는 이유는 위와 같다. */
static inline void wire_lcd_clear(char* out9) {
    for (uint8_t i = 0; i < WIRE_DATA_LEN; i++) out9[i] = '-';
    out9[WIRE_DATA_LEN] = '\0';
}

#endif /* SERVER_WIRE_H */
