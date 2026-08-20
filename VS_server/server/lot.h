// lot.h — 🔴 **주차장 지형이 스스로 답한다** (REQ-0272 3단계 · 2026-08-19)
//
// 사용자 확정: *"주차자리와 디바이스노드는 **개별구조다. 연결되지 않는다.**"*
// 이 파일이 그 문장의 코드 판본이다 — 여기에는 소켓도, 프레임도, 전송도 없다.
//
// 🔑 무엇을 안에 넣나 : 지형 목록 · 판(epoch) · **결속 규칙**(어느 센서가 어느 자리인가)
// 🔑 무엇을 밖에 남기나 : **로그와 방송.** 판이 올랐다는 것을 *알리는* 일은 전송이고 서버의 몫이다
//   ⚠ 전에는 `bump_epoch()` 안에서 `logf` 와 `push_map()` 을 같이 불렀다.
//     그대로 옮기면 이 클래스가 **다시 서버를 알게 된다.** 그래서 갈랐다.
//
// ⚠ **전선에 안 닿는다** — 프레임 형식·타이밍을 하나도 안 건드린다.
//   그래서 잠금 계약(`firmware-esp-link`)과 무관하고, 그 축부터 쪼개는 순서에 맞는다.
#ifndef LOT_H
#define LOT_H

#include <string>
#include <vector>
#include <utility>

class Lot {
public:
    Lot() : epoch_(0) {}

    // ── 지형 ────────────────────────────────────────────────────────────────
    void clear()                        { zones_.clear(); }
    void add(const Zone& z)             { zones_.push_back(z); }
    std::vector<Zone>&       zones()       { return zones_; }
    const std::vector<Zone>& zones() const { return zones_; }
    int  epoch() const                  { return epoch_; }
    int  bumpEpoch()                    { return ++epoch_; }   // 🔑 **순수 증가.** 로그·방송은 부르는 쪽

    Zone* find(const std::string& id) {
        for (size_t i = 0; i < zones_.size(); i++)
            if (zones_[i].id == id) return &zones_[i];
        return 0;
    }

    // ── 결속 규칙 — **어느 센서가 어느 자리인가** ───────────────────────────
    // 표(조립 API)가 있으면 **표가 답한다.** 없으면 이름 규칙으로 떨어진다.
    // 🔑 표가 생기면 *"B3 은 A3 의 둘째 센서"* 라는 **추측이 사실로 바뀐다.**
    // 🔴 **`devid` 를 같이 받는다** (2026-08-19). 조립 표가 장치를 못 박을 수 있게 됐다.
    //   `Attach::devid` 가 비어 있으면 **아무 장치나** — 1인자 선언이 그 뜻이다.
    // ⚠ **첫 번째로 맞는 것을 돌려준다.** 같은 이름이 두 자리에 있으면 뒤엣 자리는 못 받는다 —
    //   그래서 `validate_assembly()` 가 기동 때 그것을 말한다.
    std::string zoneOfModule(const std::string& devid, const std::string& nm,
                             const ParkingLot* table) const {
        if (table && !table->empty()) {
            const std::vector<ParkingLot::Area>& as = table->areas();
            for (size_t i = 0; i < as.size(); i++)
                for (size_t k = 0; k < as[i].modules.size(); k++) {
                    const ParkingLot::Attach& at = as[i].modules[k];
                    if (at.name != nm) continue;
                    // 🔑 devid 가 비면 아무 장치나. 있으면 **그 장치일 때만**.
                    if (!at.devid.empty() && at.devid != devid) continue;
                    return as[i].id;
                }
            return nm;                       // 표에 없는 이름 — 자기 이름으로 찾아본다
        }
        return nameRule(nm);
    }
    // 옛 형태 — devid 를 모르는 자리에서 부른다. **"아무 장치나"로 읽는다.**
    std::string zoneOfModule(const std::string& nm, const ParkingLot* table) const {
        return zoneOfModule(std::string(), nm, table);
    }

    // 이름 규칙(표가 없을 때) : `B3` → 자리 `A3` 의 둘째 센서
    static std::string nameRule(const std::string& nm) {
        if (nm.size() == 2 && nm[0] == 'B' && nm[1] >= '1' && nm[1] <= '5')
            return std::string("A") + nm[1];
        return nm;
    }

    // 노드의 모듈들을 자리에 결속한다.
    // 🔴 **로그를 여기서 찍지 않는다** — 충돌을 *결과로 돌려주고* 문구는 부르는 쪽이 만든다.
    //   그래야 이 클래스가 로그 형식을 모르고, 부르는 쪽이 얼마나 크게 알릴지 정한다.
    struct BindResult {
        bool changed;                                        // 지형이 바뀌었나(판을 올릴 이유)
        std::vector<std::pair<std::string, std::string> > conflicts;  // (자리 id, 이름)
        // 🔴🔴 **어느 자리에도 안 붙은 모듈** — (idx, 이름). 2026-08-19 신설
        //   전에는 `if (!z) continue;` 로 **조용히 건너뛰었다.** 그래서 기여자가 이름을
        //   잘못 쓰면 **등록은 성공하고 자리에는 아무것도 안 붙고 아무도 안 알려 줬다.**
        //   ⚠ 다인원 협업에서 가장 나쁜 형태다 — **코드는 도는데 결과가 없다.**
        //   🔑 여기서는 *사실만* 실어 보낸다. 얼마나 크게 알릴지는 부르는 쪽이 정한다.
        std::vector<std::pair<size_t, std::string> > unbound;
        BindResult() : changed(false) {}
    };
    BindResult bind(const std::string& devid,
                    const std::vector<std::pair<std::string, std::string> >& mods,
                    const ParkingLot* table) {
        BindResult r;
        for (size_t i = 0; i < mods.size(); i++) {
            Zone* z = find(zoneOfModule(devid, mods[i].first, table));
            if (!z) { r.unbound.push_back(std::make_pair(i, mods[i].first)); continue; }
            std::pair<std::string, std::string> key(devid, mods[i].first);
            bool dup = false, takenByOther = false;
            for (size_t k = 0; k < z->modules.size(); k++) {
                if (z->modules[k] == key) dup = true;
                else if (z->modules[k].second == key.second) takenByOther = true;
            }
            // 🔴 **먼저 잡은 노드를 유지한다.** 둘 다 받으면 그 자리 값이 어느 노드 것인지 못 가르고,
            //   조용히 덮으면 뒤엣것이 앞엣것을 지워 원인을 못 찾는다. `first-S-wins` 와 같은 규율.
            if (takenByOther) { r.conflicts.push_back(std::make_pair(z->id, key.second)); continue; }
            if (!dup) { z->modules.push_back(key); r.changed = true; }
        }
        return r;
    }

private:
    std::vector<Zone> zones_;
    int epoch_;
};

#endif  // LOT_H
