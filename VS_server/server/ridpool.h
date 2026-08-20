// ridpool.h — 🔴 `rid` 발행·격리·영속을 **한 클래스로** (REQ-0272 3단계 · 2026-08-19)
//
// 전에는 이 상태 열 개와 함수 넷이 `struct Server`(약 4,000줄) 안에 흩어져 있었다.
// **자료를 옮기는 것이 아니라 책임을 옮긴다** — 바깥은 `alloc()` / `release()` 두 개만 안다.
//
// 🔑 무엇을 감추나: 커서·격리표·해제 순번·블록 예약·디스크 경로·계수 여섯.
// 🔑 무엇을 드러내나: **계수는 그대로 낸다**(요약 줄). 감출 것은 복잡함이고 드러낼 것은 상태다.
//
// ⚠ **전선에 안 닿는다.** `rid` 값의 폭·형식은 그대로이고 프레임도 안 바뀐다 —
//   그래서 잠금 계약(`firmware-esp-link`)과 무관하다. 그 축부터 먼저 쪼갠 이유다.
#ifndef RIDPOOL_H
#define RIDPOOL_H

#include <string>
#include <map>
#include <fstream>

class RidPool {
public:
    RidPool() : cursor_(1), reserved_to_(0), persist_on_(false), rel_seq_(0),
                alloc_n_(0), skips_(0), forced_(0), exhausted_(0), no_disk_(false) {}

    // ── 발행 ────────────────────────────────────────────────────────────────
    // `inUse(rid)` 가 참이면 **절대 발행하지 않는다**(하드 규칙 ①).
    // 🔑 술어를 인자로 받는 이유: 그래야 이 클래스가 `Pending`·`Server` 를 몰라도 된다.
    //   **못 쓰는 rid 가 무엇인지는 부르는 쪽이 안다.** 여기서 정의하면 그 지식이 두 곳에 생긴다.
    // ⚠ **시계를 인자로 받는다.** 처음엔 `now_ms()` 를 직접 불렀는데 **헤더가 서버의 시계를
    //   알게 된다** — 그러면 이 클래스만 따로 시험할 수도 없고 include 순서에도 묶인다(실제로 깨졌다).
    //   🔑 **의존을 없애는 가장 싼 방법은 인자로 올리는 것이다.**
    template <class InUse>
    uint16_t alloc(const InUse& inUse, long long now) {
        const long long t = now;
        for (uint16_t i = 0; i < RID_SPACE; i++) {
            uint16_t cand = (uint16_t)(cursor_ % RID_SPACE);
            cursor_++;
            if (cursor_ >= reserved_to_) reserveBlock();
            if (inUse(cand)) { skips_++; continue; }
            std::map<uint16_t, Q>::iterator q = quar_.find(cand);
            if (q != quar_.end()) {
                if (t < q->second.until_ms) { skips_++; continue; }
                quar_.erase(q);
            }
            alloc_n_++;
            return cand;
        }
        // 한 바퀴가 다 막혔다 — **격리만** 조기 해제한다. 🔴 하드 규칙 ①은 안 푼다.
        // 해제 **순번**이 가장 작은 것을 고른다. 시각으로 고르면 같은 밀리초 동률에서
        // 같은 칸이 반복해 뽑혀 재사용 간격이 1까지 떨어진다(자가검증이 잡았다).
        uint16_t best = RID_NONE; long long best_seq = 0;
        for (std::map<uint16_t, Q>::iterator it = quar_.begin(); it != quar_.end(); ++it) {
            if (inUse(it->first)) continue;
            if (best == RID_NONE || it->second.seq < best_seq) { best = it->first; best_seq = it->second.seq; }
        }
        if (best == RID_NONE) { exhausted_++; return RID_NONE; }
        quar_.erase(best);
        forced_++; alloc_n_++;
        return best;
    }

    // `pend` 를 떠난 rid 를 격리에 넣는다. 🔴 **모든 해제 지점에서 부른다** —
    // 한 곳이라도 빠지면 격리 없이 재사용되고 **그 누락은 아무 증상도 안 낸다.**
    void release(uint16_t rid, long long now) {
        if (rid >= RID_SPACE) return;
        Q q; q.until_ms = now + RID_QUARANTINE_MS; q.seq = ++rel_seq_;
        quar_[rid] = q;
    }

    // ── 영속 ────────────────────────────────────────────────────────────────
    // 기동 때 한 번. 🔴 읽은 값을 **로그로 낸다** — 못 읽은 것은 사고인데
    //   값이 안 찍히면 정상 기동과 구별이 안 된다.
    //   ⚠ 본문은 `server.cpp` 에 있다 — 로그·디렉터리 생성 도우미가 거기 있고,
    //     그것들을 헤더로 끌어오면 이 클래스가 다시 서버를 알게 된다.
    void loadCursor(const std::string& path, long long seed_when_missing, bool no_disk);

    // ── 관측 (감출 것은 복잡함이고 드러낼 것은 상태다) ──────────────────────
    long long allocN()   const { return alloc_n_; }
    long long skips()    const { return skips_; }
    long long forced()   const { return forced_; }
    long long exhausted()const { return exhausted_; }
    size_t    quarSize() const { return quar_.size(); }
    unsigned  nextWire() const { return (unsigned)(cursor_ % RID_SPACE); }
    bool      persistOn()const { return persist_on_; }
    long long cursor()   const { return cursor_; }
    long long reservedTo() const { return reserved_to_; }

private:
    struct Q { long long until_ms; long long seq; Q() : until_ms(0), seq(0) {} };
    void reserveBlock();

    long long   cursor_;        // 단조. 전선 값은 cursor_ % RID_SPACE
    long long   reserved_to_;   // 디스크에 **미리 적어 둔** 상한
    bool        persist_on_;
    std::string file_;
    std::map<uint16_t, Q> quar_;
    long long   rel_seq_;
    long long   alloc_n_, skips_, forced_, exhausted_;
    bool        no_disk_;
};

#endif  // RIDPOOL_H
