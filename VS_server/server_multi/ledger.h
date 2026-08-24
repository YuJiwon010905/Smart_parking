// ledger.h — **노드 대장**. 서버가 "누가 있었는지"를 재기동을 건너 기억한다.
//            정본 명세: `docs/net/DESIGN-node-ledger.md` (온보딩 2단계)
// ═══════════════════════════════════════════════════════════════════════════
// 🔑 **이 클래스는 서버를 모른다.** 소켓도 전선도 모르고, 파일 경로조차 인자로 받는다.
//   `Server` 가 아는 것(언제·어떤 사건인가)과 대장이 아는 것(무엇을 기억하나)을 갈랐다.
//
// 🔴 **이 단계의 값은 *기억* 이지 *동작 변경* 이 아니다.**
//   결속은 여전히 이름 일치다. 대장은 아무 판정에도 관여하지 않는다.
//   → 그래서 굽기가 필요 없고, 관측 창을 안 건드린다.
//
// ⚠ **`assignments` 는 기록만 된다. 지금 아무것도 그것을 읽지 않는다.**
//   읽는 것은 4단계(관리자 화면)다. **적어 두지 않으면 다음 사람이 "할당이 동작한다"고 읽는다.**
// ═══════════════════════════════════════════════════════════════════════════
#ifndef LEDGER_H
#define LEDGER_H

// 🔑 **이 파일은 자립한다** — 몸통 조각들과 달리 진짜 클래스라 단독으로 열어도 성립한다.
//   그래서 필요한 표준 헤더를 스스로 들인다. IDE 가 열어도 오류가 안 난다.
#include <string>
#include <vector>
#include <fstream>
#include <cstdio>
#include <cstdlib>

class NodeLedger {
public:
    // 노드가 지금 어떤 상태인가 (명세 §4)
    enum State { NEW, UNASSIGNED, ASSIGNED, NEEDS_REVIEW };

    struct Mod {
        std::string name, kind;
        Mod() {}
        Mod(const std::string& n, const std::string& k) : name(n), kind(k) {}
        bool operator==(const Mod& o) const { return name == o.name && kind == o.kind; }
        bool operator!=(const Mod& o) const { return !(*this == o); }
    };
    struct Entry {
        std::string devid, label, last_peer, fp;
        long long   first_seen, last_seen;
        long long   sessions;
        State       state;
        std::vector<Mod> mods;
        Entry() : first_seen(0), last_seen(0), sessions(0), state(NEW) {}
    };
    // 🔴 **키는 `(devid, name)` 이다. `idx` 가 아니다.** (2026-08-19 · 루트가 정정을 받았다)
    //
    //   초안은 `(devid, idx)` 였다. 배제한 이유가 *"이름으로 걸면 지금 문제가 그대로 남는다"* 였는데,
    //   🔴 **지금 문제는 `name` 이 아니라 `name == 자리 id` 라는 *암묵 규칙* 이다.**
    //   명시적으로 사상하면(이름 `B7` → 자리 `A3`) 그 결합은 `name` 으로도 똑같이 끊긴다.
    //
    //   `idx` 를 안 쓰는 적극적 이유 둘:
    //     ① `idx` 는 **등록 순서**로 정해진다 → 순서가 바뀌면 **같은 idx 가 다른 물건을 가리킨다.**
    //        할당이 조용히 딴 데를 가리키고, 그래서 `mod_order_changed` 계기가 따로 있다
    //     ② web 이 REQ-0179 §① 에서 **`(devid, name)` 을 전역 신원으로 이미 쓴다.**
    //        키가 둘이면 화면이 매핑을 만들게 되고 **그 매핑은 등록이 바뀔 때마다 낡는다**
    //
    //   ⚠ **전선은 여전히 `idx` 로 주소를 정한다**(`G,<rid>,<idx>,<val>`).
    //     보낼 때 **현재 등록에서 name→idx 로 푼다.** 없으면 보내지 않고 사유를 준다.
    //   ⚠ 전제: **한 장치 안에서 `name` 이 고유해야 한다** → `mod_dup_name` 이 그것을 본다.
    //     **감시할 수 없는 것을 키로 쓰지 않는다.**
    struct Assign {
        std::string devid, name, zone, role;
    };

    NodeLedger() : persist_(false), dirty_(false), loads_(0), saves_(0),
                   save_fails_(0), lines_bad_(0), lines_unknown_(0) {}

    // ── 지문 (명세 §3) ────────────────────────────────────────────────────
    // 🔴 **순서를 포함한다.** `idx` 가 순서로 정해지므로 순서가 바뀌면 다른 구성이다.
    // ⚠ 32비트라 **충돌할 수 있다 — 충돌하면 "안 바뀐 것으로 보인다".**
    //   그래서 `mods` 전체를 같이 저장한다. **`fp` 는 빠른 비교용이고 정본은 `mods` 다.**
    static std::string fingerprint(const std::vector<Mod>& mods) {
        unsigned h = 2166136261u;                     // FNV-1a 32
        for (size_t i = 0; i < mods.size(); i++) {
            const std::string s = mods[i].name + ":" + mods[i].kind + "|";
            for (size_t k = 0; k < s.size(); k++) {
                h ^= (unsigned char)s[k];
                h *= 16777619u;
            }
        }
        char b[16];
        snprintf(b, sizeof(b), "%08x", h);
        return std::string(b);
    }

    // ── 조회 ──────────────────────────────────────────────────────────────
    Entry*       find(const std::string& devid) {
        for (size_t i = 0; i < nodes_.size(); i++)
            if (nodes_[i].devid == devid) return &nodes_[i];
        return 0;
    }
    const Entry* find(const std::string& devid) const {
        for (size_t i = 0; i < nodes_.size(); i++)
            if (nodes_[i].devid == devid) return &nodes_[i];
        return 0;
    }
    bool hasAssignment(const std::string& devid) const {
        for (size_t i = 0; i < assigns_.size(); i++)
            if (assigns_[i].devid == devid) return true;
        return false;
    }

    // ── 사건: 등록이 끝났다 (명세 §0.4 ①) ────────────────────────────────
    //
    // 🔴 **호출자는 `reg_done` 일 때만 불러야 한다.** 부분 목록으로 지문을 접으면
    //   **거짓 "구성 변경"** 이 된다(명세 §0.3). 그 판단은 호출자 몫이다 — 여기서는 못 본다.
    //
    // 반환: 이 등록으로 판정된 상태. 호출자가 로그에 남긴다.
    State onRegister(const std::string& devid, const std::string& peer,
                     const std::vector<Mod>& mods, long long now_epoch) {
        const std::string fp = fingerprint(mods);
        Entry* e = find(devid);
        if (!e) {
            Entry ne;
            ne.devid = devid; ne.last_peer = peer; ne.fp = fp; ne.mods = mods;
            ne.first_seen = now_epoch; ne.last_seen = now_epoch; ne.sessions = 1;
            ne.state = hasAssignment(devid) ? ASSIGNED : NEW;
            nodes_.push_back(ne);
            dirty_ = true;
            return ne.state;
        }
        e->sessions++;
        e->last_seen = now_epoch;
        e->last_peer = peer;
        if (e->fp != fp) {
            // 🔴 **자동 승격하지 마라.** 모듈이 빠졌는데 옛 할당이 남으면
            //   **없는 모듈을 가리키는 자리**가 생긴다. 사람이 봐야 한다.
            // ⚠ 그리고 원인이 "다른 보드"일 수 있다(명세 §0.2) — `last_peer` 를 같이 봐라.
            e->fp = fp; e->mods = mods;
            e->state = NEEDS_REVIEW;
        } else if (e->state != NEEDS_REVIEW) {
            e->state = hasAssignment(devid) ? ASSIGNED : UNASSIGNED;
        }
        dirty_ = true;
        return e->state;
    }

    // ── 사건: 세션이 끝났다 (명세 §0.4 ②) ────────────────────────────────
    void onSessionEnd(const std::string& devid, long long now_epoch) {
        Entry* e = find(devid);
        if (!e) return;
        e->last_seen = now_epoch;
        dirty_ = true;
    }

    // ── 파일 ──────────────────────────────────────────────────────────────
    // 경로가 비면 **영속하지 않는다**(메모리로만 돈다). 호출자가 그 사실을 크게 남긴다.
    void load(const std::string& path, const std::string& srv_id) {
        (void)srv_id;
        path_ = path;
        persist_ = !path.empty();
        if (!persist_) return;
        std::ifstream f(path.c_str(), std::ios::binary);
        if (!f) { loads_ = 0; return; }               // 없으면 빈 대장. 사고가 아니다
        std::string line;
        while (std::getline(f, line)) {
            if (!line.empty() && line[line.size()-1] == '\r') line.erase(line.size()-1);
            if (line.empty()) continue;
            std::vector<std::string> c = splitTab(line);
            if (c.empty()) continue;
            if (c[0] == "N") {
                if (c.size() < 9) { lines_bad_++; continue; }
                Entry e;
                e.devid = c[1]; e.label = c[2];
                e.first_seen = atoll(c[3].c_str());
                e.last_seen  = atoll(c[4].c_str());
                e.last_peer  = c[5]; e.fp = c[6];
                e.sessions   = atoll(c[7].c_str());
                e.state      = stateFromName(c[8]);
                nodes_.push_back(e);
            } else if (c[0] == "M") {
                if (c.size() < 5) { lines_bad_++; continue; }
                Entry* e = find(c[1]);
                if (!e) { lines_bad_++; continue; }   // N 없이 M 이 먼저 왔다
                e->mods.push_back(Mod(c[3], c[4]));
            } else if (c[0] == "A") {
                if (c.size() < 5) { lines_bad_++; continue; }
                Assign a;
                a.devid = c[1]; a.name = c[2];
                a.zone = c[3]; a.role = c[4];
                assigns_.push_back(a);
            } else if (c[0] == "v" || c[0] == "srv") {
                // 판·인스턴스 표시. 지금은 안 쓴다
            } else {
                // ⚠ **모르는 줄은 버리지 말고 무시한다** — 앞으로 늘 수 있다.
                //   그래도 **센다.** 조용히 무시하면 형식이 갈렸을 때 아무도 모른다
                lines_unknown_++;
            }
        }
        loads_ = (long long)nodes_.size();
        dirty_ = false;
    }

    // 임시파일 → rename. 중간에 죽어도 **반쪽 파일이 안 남는다**.
    bool save(const std::string& srv_id) {
        if (!persist_ || !dirty_) return true;
        const std::string tmp = path_ + ".tmp";
        {
            std::ofstream f(tmp.c_str(), std::ios::binary | std::ios::trunc);
            if (!f) { save_fails_++; return false; }
            f << "v\t1\n";
            f << "srv\t" << clean(srv_id) << "\n";
            for (size_t i = 0; i < nodes_.size(); i++) {
                const Entry& e = nodes_[i];
                f << "N\t" << clean(e.devid) << "\t" << clean(e.label) << "\t"
                  << e.first_seen << "\t" << e.last_seen << "\t"
                  << clean(e.last_peer) << "\t" << clean(e.fp) << "\t"
                  << e.sessions << "\t" << stateName(e.state) << "\n";
                for (size_t k = 0; k < e.mods.size(); k++)
                    f << "M\t" << clean(e.devid) << "\t" << k << "\t"
                      << clean(e.mods[k].name) << "\t" << clean(e.mods[k].kind) << "\n";
            }
            for (size_t i = 0; i < assigns_.size(); i++) {
                const Assign& a = assigns_[i];
                f << "A\t" << clean(a.devid) << "\t" << clean(a.name) << "\t"
                  << clean(a.zone) << "\t" << clean(a.role) << "\n";
            }
            if (!f.good()) { save_fails_++; return false; }
        }
        if (rename(tmp.c_str(), path_.c_str()) != 0) { save_fails_++; return false; }
        saves_++; dirty_ = false;
        return true;
    }

    static const char* stateName(State s) {
        switch (s) {
            case NEW:          return "new";
            case UNASSIGNED:   return "unassigned";
            case ASSIGNED:     return "assigned";
            case NEEDS_REVIEW: return "needs_review";
        }
        return "new";
    }

    // ── 관측 ──────────────────────────────────────────────────────────────
    // 🔑 **세는 것을 만들었으면 볼 자리도 만든다**(오늘 원장에서 다섯 번째로 나온 부류).
    //   호출자가 이 값들을 요약 줄에 싣는다. **분모를 같이 둔다** — `0/0` 은 최소한 `0/0` 이라고 말해 준다.
    size_t      size()        const { return nodes_.size(); }
    size_t      assignCount() const { return assigns_.size(); }
    bool        persistOn()   const { return persist_; }
    bool        dirty()       const { return dirty_; }
    long long   loaded()      const { return loads_; }
    long long   saves()       const { return saves_; }
    long long   saveFails()   const { return save_fails_; }
    long long   linesBad()    const { return lines_bad_; }
    long long   linesUnknown()const { return lines_unknown_; }
    const std::string& path() const { return path_; }
    const std::vector<Entry>& nodes() const { return nodes_; }

private:
    static std::vector<std::string> splitTab(const std::string& s) {
        std::vector<std::string> out;
        std::string cur;
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i] == '\t') { out.push_back(cur); cur.clear(); }
            else cur += s[i];
        }
        out.push_back(cur);
        return out;
    }
    // ⚠ 값 안의 탭·개행은 **칸을 밀어 버린다.** 공백으로 바꾼다.
    static std::string clean(const std::string& s) {
        std::string o = s;
        for (size_t i = 0; i < o.size(); i++)
            if (o[i] == '\t' || o[i] == '\n' || o[i] == '\r') o[i] = ' ';
        return o;
    }
    static State stateFromName(const std::string& s) {
        if (s == "unassigned")   return UNASSIGNED;
        if (s == "assigned")     return ASSIGNED;
        if (s == "needs_review") return NEEDS_REVIEW;
        return NEW;
    }

    std::vector<Entry>  nodes_;
    std::vector<Assign> assigns_;
    std::string         path_;
    bool                persist_, dirty_;
    long long           loads_, saves_, save_fails_, lines_bad_, lines_unknown_;
};

#endif  // LEDGER_H
