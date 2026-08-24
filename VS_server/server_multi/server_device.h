/* server_device.h — **디바이스 계층: 도메인을 모르는 것들** (REQ-0096 단계 A)
 *
 * 판별 기준(요청이 준 것): **"주차장이 아니라 온실·창고였어도 그대로 쓰이는가?"**
 * 여기 있는 것은 전부 "그렇다" 다 — WebSocket 핸드셰이크와 전선 체크섬은
 * 무엇을 실어 나르는지 모른다.
 *
 * ⚠ **왜 .cpp 가 아니라 헤더 전용인가**
 *   사용자가 **윈도우에서 자기 명령으로 빌드**하고 있고 나는 그 명령을 모른다.
 *   `.cpp` 를 추가하면 그 명령이 그 파일을 안 넘겨서 **링크 에러로 깨진다.**
 *   "지금 도는 것을 깨지 마라"가 이 작업의 절대 조건이므로, 지금은 헤더로 가른다 —
 *   **파일은 갈라지고 빌드 명령은 그대로**다.
 *   `.cpp` 전환은 빌드 스크립트가 생긴 뒤(REQ-0086 PAL)가 맞다. 그때 한 줄씩 옮기면 된다.
 */
#ifndef SERVER_DEVICE_H
#define SERVER_DEVICE_H

#include <string>
#include <vector>
#include <cstdio>
#include <cstdint>
#include <cstring>

static const char* WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

// ---------------------------------------------------------------- SHA-1
// RFC 3174. 흔한 실수 셋: 길이는 **비트 수**를 빅엔디안 64비트로, 패딩은 0x80 뒤 0 을
// 채워 56 mod 64, 회전은 전부 32비트. --selftest 가 이 셋을 한 번에 잡는다.
struct SHA1 {
    uint32_t h[5];
    uint64_t len;
    uint8_t  buf[64];
    size_t   n;

    SHA1() { reset(); }
    void reset() {
        h[0]=0x67452301u; h[1]=0xEFCDAB89u; h[2]=0x98BADCFEu; h[3]=0x10325476u; h[4]=0xC3D2E1F0u;
        len = 0; n = 0;
    }
    static uint32_t rol(uint32_t v, int b) { return (v << b) | (v >> (32 - b)); }

    void block(const uint8_t* p) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++)
            w[i] = (uint32_t(p[i*4]) << 24) | (uint32_t(p[i*4+1]) << 16) |
                   (uint32_t(p[i*4+2]) << 8) | uint32_t(p[i*4+3]);
        for (int i = 16; i < 80; i++)
            w[i] = rol(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);

        uint32_t a=h[0], b=h[1], c=h[2], d=h[3], e=h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if      (i < 20) { f = (b & c) | (~b & d);          k = 0x5A827999u; }
            else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
            else             { f = b ^ c ^ d;                   k = 0xCA62C1D6u; }
            uint32_t t = rol(a,5) + f + e + k + w[i];
            e = d; d = c; c = rol(b,30); b = a; a = t;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e;
    }
    void update(const uint8_t* p, size_t sz) {
        len += uint64_t(sz) * 8;                    // ← 비트 수
        while (sz--) {
            buf[n++] = *p++;
            if (n == 64) { block(buf); n = 0; }
        }
    }
    void final(uint8_t out[20]) {
        uint64_t bits = len;
        uint8_t pad = 0x80;
        update(&pad, 1);
        uint8_t z = 0;
        while (n != 56) update(&z, 1);
        uint8_t lb[8];
        for (int i = 0; i < 8; i++) lb[i] = uint8_t((bits >> (56 - 8*i)) & 0xFF);  // 빅엔디안
        // update() 가 len 을 또 더하지 않도록 직접 넣는다
        for (int i = 0; i < 8; i++) { buf[n++] = lb[i]; if (n == 64) { block(buf); n = 0; } }
        for (int i = 0; i < 5; i++) {
            out[i*4]   = uint8_t((h[i] >> 24) & 0xFF);
            out[i*4+1] = uint8_t((h[i] >> 16) & 0xFF);
            out[i*4+2] = uint8_t((h[i] >> 8)  & 0xFF);
            out[i*4+3] = uint8_t( h[i]        & 0xFF);
        }
    }
};


static std::string base64(const uint8_t* p, size_t n) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = uint32_t(p[i]) << 16;
        if (i+1 < n) v |= uint32_t(p[i+1]) << 8;
        if (i+2 < n) v |= uint32_t(p[i+2]);
        o += T[(v >> 18) & 63];
        o += T[(v >> 12) & 63];
        o += (i+1 < n) ? T[(v >> 6) & 63] : '=';
        o += (i+2 < n) ? T[v & 63]        : '=';
    }
    return o;
}

static std::string ws_accept(const std::string& key) {
    std::string s = key + WS_GUID;
    SHA1 sh; sh.update((const uint8_t*)s.data(), s.size());
    uint8_t d[20]; sh.final(d);
    return base64(d, 20);
}

// ---------------------------------------------------------------- 시간
// ⚠ now_ms() 와 epoch_ms() 는 **원점이 다르다. 절대 섞어 쓰지 마라.**
//   now_ms()   : 단조 증가하는 상대 시각. 윈도우에서는 **부팅 후 경과 ms**.
//                타이머 계산(ACK 타임아웃·offline 판정)에만 쓴다.
//   epoch_ms() : Unix epoch 기준 절대 시각. 바깥으로 나가는 값(ts, reserved_at)에만 쓴다.
//   POSIX 에서는 둘 다 epoch 라 섞어도 티가 안 나지만, 윈도우에서는 그 순간
//   수십 년짜리 값이 나온다. **윈도우에서만 틀리는 버그**라 여기서 못 잡는다.



// ---------------------------------------------------------------- 라인 체크섬 (§2.2)
// 대상: 첫 바이트부터 체크섬 앞 쉼표까지(그 쉼표 포함). 대문자 2자리 hex.
static std::string cksum(const std::string& prefix) {
    unsigned char x = 0;
    for (size_t i = 0; i < prefix.size(); i++) x ^= (unsigned char)prefix[i];
    char b[3]; snprintf(b, sizeof(b), "%02X", x);
    return std::string(b);
}
static std::string build_line(const std::string& prefix) { return prefix + cksum(prefix) + "\n"; }

static bool verify_line(const std::string& line, std::vector<std::string>& out) {
    size_t cut = line.rfind(',');
    if (cut == std::string::npos) return false;
    if (cksum(line.substr(0, cut + 1)) != line.substr(cut + 1)) return false;
    out.clear();
    std::string body = line.substr(0, cut);
    std::string cur;
    for (size_t i = 0; i < body.size(); i++) {
        if (body[i] == ',') { out.push_back(cur); cur.clear(); }
        else cur += body[i];
    }
    out.push_back(cur);
    return true;
}

// ---------------------------------------------------------------- §7.4 재부팅 판정 (개정 8)
// **전선의 카운터는 둘 다 순환한다.** uptime 은 millis() 가 32비트라 약 49.7일마다,
// seq 는 uint16 이라 1Hz 하트비트에서 **18.2시간마다** 0 으로 돌아간다.
// 옛 규칙("줄었으면 재부팅")은 그래서 **정상 연속가동 중에 참이 됐다** — 연 480회 오탐.
// 오탐은 예약을 쓸데없이 재하달할 뿐 아니라 §7.5-1 경로를 타서 **그 프레임의 진짜
// occupied 1→0 은퇴를 삼킨다.** 장시간 돌려야만 나오는 종류라 벤치에서는 안 보인다.
//
// 고친 규칙: 순환을 접은 **전진량**으로 본다. 되돌아감과 순환은 뺄셈으로 구별되지 않지만,
// 접고 나면 정상 전진은 작고 재부팅은 거의 한 바퀴다. 경계 특례가 필요 없다.
//   fwd = (up - prev_up) mod WRAP  →  fwd > MAX_FWD 이면 재부팅
// **seq 는 판정에 쓰지 않는다**(§2.4). 18.2시간마다 합법적으로 0 을 지나가므로
// 어떤 식으로 섞어도 방금 없앤 오탐이 되돌아온다.
static const long long UPTIME_WRAP    = 4294968;  // uptime 이 가질 수 있는 값의 개수(0..4294967)
// MAX_FWD 는 "정상 전진"과 "한 바퀴"를 가르는 분리선이다(§7.4). 진짜 재부팅의 fwd 는 400만대라
// 이 선을 1시간에 두든 하루에 두든 판정은 같다 — 예민한 값이 아니다. 다만 **아래로는 못 내린다**:
// OFFLINE_MS(3.5초)는 오프라인 표시만 할 뿐 **연결을 닫지 않으므로** 한 연결 안 침묵에 상한이 없고
// (호스트 절전·ESP 멈춤·링크 복구 지연), 그동안에도 uptime 은 전진한다. 100분 침묵이면 fwd=6000 이라
// 3600 에 두면 그 자리에서 오탐이 난다. 크게 잡을수록 놓치는 창(§7.4)이 넓어지므로 24시간에 세운다.
//
// ⚠ **위 문단의 전제가 REQ-0072 로 바뀌었다. 숫자는 그대로 두되 이유를 갱신해 둔다.**
// ARD_IDLE_CLOSE_MS(60초)가 생기면서 **한 연결 안 침묵에 상한이 생겼다** — 60초를 넘으면
// 소켓을 닫으므로 "100분 침묵" 은 이제 한 연결 안에서 일어날 수 없다. 즉 fwd 는 한 연결 안에서
// 60 을 넘지 못한다(재연결하면 ard_uptime 이 -1 로 초기화되어 애초에 판정하지 않는다).
// 원리적으로는 MAX_FWD 를 크게 내려 §7.4 가 놓치는 창을 좁힐 수 있다.
// **그래도 내리지 않는다**: (1) REQ-0072 의 범위 밖이고 판정 상수를 곁다리로 바꾸지 않는다,
// (2) 내려서 얻는 이득이 실측으로 증명된 바 없는데 오탐 위험은 실측으로 겪은 적이 있다(연 480회).
// 내릴 거라면 그것만을 위한 요청과 근거가 따로 있어야 한다.
static const long long UPTIME_MAX_FWD = 86400;


#endif /* SERVER_DEVICE_H */
