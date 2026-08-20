// wsjson.h — 봉투 만들기 — map/state/snapshot JSON. `struct Server` 의 몸통 조각.
    // ═══════════════════════════════════════════════════════════════════
    // 🔴 **단독으로 컴파일되지 않는다.** `struct Server` 의 **몸통 조각**이고
    //   `server.cpp` 안 그 자리에 include 된다. **위치가 곧 문법이다.**
    // 🔑 옮긴 것이지 고친 것이 아니다 — 전처리 결과가 같아 **`.o` 가 바이트 동일**해야 한다.
    //   대조가 0 이 아니면 이동이 아니라 재배치다. 되돌리고 보고한다(REQ-0272).
    // ═══════════════════════════════════════════════════════════════════

    // ---------- JSON 만들기
    static std::string jstr(const std::string& s) {
        std::string o = "\"";
        for (size_t i = 0; i < s.size(); i++) {
            char c = s[i];
            if (c == '"' || c == '\\') { o += '\\'; o += c; }
            else if ((unsigned char)c < 0x20) { char b[8]; snprintf(b,sizeof(b),"\\u%04x",c); o += b; }
            else o += c;
        }
        return o + "\"";
    }
    // ── REQ-0203 4b: `map` 봉투 (설계 §6.8) ─────────────────────────────────────
    // 🔴 **`slots[]`(옛 `snapshot`)를 대체하지 않는다. 더한다.**
    //   화면이 `map` 을 받은 적 없으면 옛 경로로 그리고, 받으면 격자로 바꾼다(web 의 이중 경로).
    //   **그래야 이 배포가 화면을 안 깨뜨리고, 화면 배포도 서버를 안 기다린다.**
    // ⚠ **한 번에 완결해서 보낸다. 조각내지 않는다** — 화면이 "모른다"로 그리는 창을 짧게 하려는 것이다.
    std::string map_json() {
        std::ostringstream o;
        o << "{\"type\":\"map\",\"srv_id\":" << jstr(srv_id) << ",\"epoch\":" << lot.epoch()
          << ",\"grid\":{\"rows\":" << grid_rows << ",\"cols\":" << grid_cols << "}"
          << ",\"zones\":[";
        for (size_t i = 0; i < lot.zones().size(); i++) {
            const Zone& z = lot.zones()[i];
            if (i) o << ",";
            o << "{\"id\":" << jstr(z.id) << ",\"kind\":" << jstr(z.kind);
            // 🔴 **선언했을 때만 나간다**(존재/부재 규칙). 없으면 화면이 `id` 를 쓴다.
            emit_label(o, "", z.id);
            // 🔴 **`active` — 모듈이 하나도 안 붙은 주차 자리** (REQ-0292 · 2026-08-20)
            //
            //   계산 주체는 **서버**다. 조립 표를 가진 것이 서버이므로 화면이 모듈 수를 세게
            //   두지 않는다 — 세게 두면 같은 규칙이 두 곳에 생기고, 두 곳이 갈리면 아무도 모른다.
            //
            //   🔑 **`value_state:"unknown"` 을 대체하지 않는다.** 그 자리의 점유는 여전히
            //     모르는 것이 맞다. 이 필드는 **그 무지의 *이유*** 를 말한다 —
            //     *"센서가 고장난 게 아니라 애초에 아무것도 안 붙였다."*
            //
            //   ⚠ **모듈 0개**(조립 시점에 안다)와 **모듈은 있는데 센서가 없다**(등록 뒤에 안다)는
            //     **다른 시점의 다른 사실**이다. 뒤엣것은 `bind_modules()` 가 따로 말한다.
            //     같은 이름으로 부르지 않는다.
            //
            //   🔑 일반영역(`area`)은 모듈이 없어도 정상이다 — 점유를 보고할 의무가 없다.
            //     그래서 `parking` 에만 이 판정을 건다.
            //   🔴 **모든 자리에 항상 싣는다.** 선택적이면 화면이 안 본다(같은 파일 `value_state` 규칙).
            //     모양은 `actions.*` 와 같은 `{ok, reason}` 이다 — 화면이 이미 아는 꼴이다.
            {
                // 🔴🔴 **`z.modules` 로 세면 안 된다** — 그것은 *결속된* 모듈이라
                //   **장치가 등록하기 전에는 비어 있다.** 그걸로 판정하면 자리가 부팅 직후
                //   "비활성"이었다가 등록되면 살아난다 — **없는 결함을 화면에 만든다.**
                //   ✅ 선언 수는 **조립 표(`lot_->areas()`)** 에서 센다. 그것이 조립 시점 사실이다.
                //   (구현하면서 시험이 잡았다: 모듈 하나 붙인 A1 이 `no_modules` 로 나왔다)
                //
                //   `declared == -1` = **조립 표가 없다**(기본 지형 · 자가검증 경로).
                //   그때는 판정하지 않는다 — 셀 것이 없는데 "0개"라고 말하면 지어내는 것이다.
                int declared = -1;
                if (lot_)
                    for (size_t a = 0; a < lot_->areas().size(); a++)
                        if (lot_->areas()[a].id == z.id) {
                            declared = (int)lot_->areas()[a].modules.size();
                            break;
                        }
                const bool no_mods = (z.kind == "parking" && declared == 0);
                o << ",\"active\":{\"ok\":" << (no_mods ? "false" : "true")
                  << ",\"reason\":" << (no_mods ? "\"no_modules\"" : "null") << "}";
            }
            o << ",\"cells\":[";
            for (size_t c = 0; c < z.cells.size(); c++) {
                if (c) o << ",";
                o << "[" << z.cells[c].first << "," << z.cells[c].second << "]";
            }
            o << "],\"modules\":[";
            for (size_t m = 0; m < z.modules.size(); m++) {
                if (m) o << ",";
                const std::string& dv = z.modules[m].first;
                const std::string& nm = z.modules[m].second;
                // `kind` 와 `idx` 는 **그 모듈을 등록한 노드**가 갖고 있다 — 이름만으로 찾으면 안 된다(복합 키).
                std::string kind; int idx = -1;
                std::vector<Node*> ns = all_nodes();
                for (size_t k = 0; k < ns.size(); k++) {
                    if (ns[k]->devid != dv) continue;
                    for (size_t j = 0; j < ns[k]->mods.size(); j++)
                        if (ns[k]->mods[j].first == nm) { kind = ns[k]->mods[j].second; idx = (int)j; }
                }
                o << "{\"devid\":" << jstr(dv) << ",\"name\":" << jstr(nm)
                  << ",\"kind\":" << jstr(kind) << ",\"idx\":" << idx;
                // 🔴 **모듈 표시 이름** (2026-08-20). 선언 안 했으면 **키가 없다** —
                //   화면이 자기 폴백 표(`MOD_KIND_LABEL`)를 쓸 수 있게 남겨 둔다.
                emit_label(o, dv, nm);
                emit_control(o, dv, nm);
                o << "}";
            }
            o << "]}";
        }
        o << "]}";
        return o.str();
    }
    // 🔴 **빈 지형을 `map` 으로 내보내지 않는다** (2026-08-19).
    //   첫 배포에서 `build_default_zones()` 가 기동 경로에 없어 `lot.zones()` 가 빈 채 나갔다.
    //   그때 화면은 **"자리가 0개인 주차장"** 을 정상 상태로 그렸다 —
    //   🔑 **빈 지형은 답이 아니라 고장이다. 답인 척하면 아무도 안 본다.**
    //   ⚠ 그래서 **안 보내고 시끄럽게 남긴다.** 화면은 지형이 없으면 옛 것을 유지하거나
    //     "아직 못 받았다"로 남는데, **둘 다 "0개"라고 그리는 것보다 낫다.**
    // 🔑 **선언 자리에서 초기화한다** — 생성자 목록에 넣으면 선언 순서에 묶여
    //   (`-Wreorder`) 다음 사람이 자리를 옮길 때 조용히 경고가 난다.
    // 🔑 **선언 자리에서 초기화한다**(C++11 NSDMI). 생성자 목록에 넣으면 선언 순서에
    //   묶여(`-Wreorder`) 다음 사람이 이 멤버를 옮길 때마다 경고가 난다.
    bool map_empty_warned = false;
    // 🔑 `mutable` 을 쓰지 않는다 — **요약을 만드는 함수가 상태를 바꾸면 안 된다.**
    //   갱신은 `ws_upgrade` 에서 하고 여기서는 읽기만 한다.
    int  ws_peak = 0;      // 이 인스턴스에서 동시에 붙었던 **WS 소켓 수**의 최대
                           //   ⚠ 사람이 보는 화면 수가 아니다 — 탐침·하니스·주입기도 센다
    void push_map() {
        if (lot.zones().empty()) {
            if (!map_empty_warned) {
                map_empty_warned = true;
                logf("!", "🔴 지형이 비어 있다 — `map` 을 보내지 않는다. "
                          "**기동 경로가 build_default_zones() 를 안 불렀을 수 있다**");
            }
            return;
        }
        ws_broadcast(map_json());
    }

    // ── REQ-0203 4c: `state` 봉투 + `actions` (설계 §6.5·§6.8·§6.9) ──────────────
    // 🔴 **`actions` 는 서버가 계산해 *값으로* 준다.** 화면이 "모듈 목록 + 노드 생사"로 조합하지 않는다 —
    //   조합 규칙이 화면에도 생기면 두 답이 갈리고 **관대한 쪽이 이기면 죽은 노드에 명령이 나간다.**
    // 🔑 **키의 존재/부재가 한 비트를 나른다**: 없으면 "그 조작은 지금 이 자리에 없다"(버튼을 안 그린다),
    //   있는데 `ok:false` 면 "뜻은 있는데 막혔다". **뜻 없는 것을 `ok:false` 로 보내면 화면이
    //   영영 안 풀리는 막힌 버튼을 그린다.**
    Node* node_of(const std::string& devid) {
        std::vector<Node*> ns = all_nodes();
        for (size_t i = 0; i < ns.size(); i++) if (ns[i]->devid == devid) return ns[i];
        return NULL;
    }
    // 자리 하나의 막힌 이유. 빈 문자열 = 안 막혔다. **코드 다섯 중에서만 고른다**(§6.5).
    std::string zone_block_reason(const Zone& z) {
        // 🔴 **2026-08-19 — "모듈이 없다"와 "아직 등록을 안 받았다"는 다른 상태다.**
        //   모듈은 등록(`D`)이 끝나야 자리에 붙는다(`bind_modules`). 그래서 **등록 전에는
        //   모든 자리가 `module_absent` 로 보인다** — 화면은 그것을 *"이 자리엔 장비가 없다"*(영구)
        //   로 읽는데 실제로는 **곧 붙는다**(일시). 🔑 **영구와 일시를 같은 코드로 답하면
        //   화면이 영영 안 그리는 자리를 만든다.**
        //   ⚠ `reg_giveup` 은 "형성 중"이 아니다 — 포기한 뒤에는 `module_absent` 가 정직하다.
        if (z.modules.empty()) {
            std::vector<Node*> ns = all_nodes();
            bool any_unreg = false;
            for (size_t i = 0; i < ns.size(); i++)
                if (!ns[i]->reg_done && !ns[i]->reg_giveup) any_unreg = true;
            if (ns.empty())  return "node_offline";        // 노드가 아예 없다
            if (any_unreg)   return "node_unregistered";   // 형성 중이다
            return "module_absent";                        // 등록이 끝났는데 이 자리엔 안 붙었다
        }
        for (size_t i = 0; i < z.modules.size(); i++) {
            Node* n = node_of(z.modules[i].first);
            if (!n) return "module_absent";
            // ⚠ **주차 노드만 생사 판정이 있다**(`device_online`). 보조 노드는 `online` 필드를 쓴다.
            bool up = (n == &park) ? device_online() : n->online;
            if (!up) return "node_offline";
            if (!n->reg_done) return "node_unregistered";
        }
        // 같은 자리에 이미 전선/큐에 나간 명령이 있으면 사용자 조작을 겹치지 않는다
        for (std::map<uint16_t, Pending>::iterator it = pend.begin(); it != pend.end(); ++it)
            if (it->second.slot == z.id) return "pending";
        return "";
    }
    // 🔴 **기여자가 선언한 조작 UI 를 찾는다** (2026-08-20). 없으면 0 — 화면은 UI 를 안 그린다.
    const ControlDecl* control_of(const std::string& devid, const std::string& name) const {
        if (!lot_) return 0;
        const std::vector<ControlDecl>& cs = lot_->controls();
        for (size_t i = 0; i < cs.size(); i++)
            if (cs[i].devid == devid && cs[i].name == name) return &cs[i];
        return 0;
    }
    // 🔴 **표시 이름을 싣는다** — 모듈이면 `devid` 를 주고, 자리면 빈 `devid` 에 자리 id 를 준다.
    //   ⚠ **선언 안 했으면 키를 아예 안 만든다.** 빈 문자열을 실으면 화면이
    //     *"이름이 있는데 비었다"* 로 읽고 폴백을 안 쓴다.
    void emit_label(std::ostringstream& o, const std::string& devid, const std::string& key) {
        if (!lot_) return;
        if (devid.empty()) {                      // 자리
            for (size_t i = 0; i < lot_->areas().size(); i++)
                if (lot_->areas()[i].id == key && !lot_->areas()[i].label.empty()) {
                    o << ",\"label\":" << jstr(lot_->areas()[i].label);
                    return;
                }
            return;
        }
        std::map<std::string, std::string>::const_iterator it =
            lot_->labels().find(devid + "\t" + key);
        if (it != lot_->labels().end() && !it->second.empty())
            o << ",\"label\":" << jstr(it->second);
    }
    // `map` 의 모듈에 붙는다. **선언된 것에만 키가 생긴다**(존재/부재 규칙).
    void emit_control(std::ostringstream& o, const std::string& devid, const std::string& name) {
        const ControlDecl* c = control_of(devid, name);
        if (!c) return;
        // 🔴 `"label"` 이 여기 있었다. **이름은 모듈의 `label` 키가 나른다**(2026-08-20).
        o << ",\"control\":{\"widget\":\"" << c->widget_name() << "\"";
        if (c->widget == ControlDecl::NUMBER) o << ",\"min\":" << c->vmin << ",\"max\":" << c->vmax;
        if (c->widget == ControlDecl::CHOICE) {
            o << ",\"options\":[";
            for (size_t i = 0; i < c->options.size(); i++) {
                if (i) o << ",";
                o << "{\"value\":" << c->options[i].first
                  << ",\"label\":" << jstr(c->options[i].second) << "}";
            }
            o << "]";
        }
        o << "}";
    }
    void emit_action(std::ostringstream& o, bool& first,
                     const char* name, const std::string& reason) {
        if (!first) o << ",";
        first = false;
        o << jstr(name) << ":{\"ok\":" << (reason.empty() ? "true" : "false")
          << ",\"reason\":" << (reason.empty() ? std::string("null") : jstr(reason)) << "}";
    }
    std::string state_json() {
        std::ostringstream o;
        int split_now = 0;                     // 이번 판의 갈린 자리 수 — 끝에서 계기에 옮긴다
        o << "{\"type\":\"state\",\"srv_id\":" << jstr(srv_id)
          // 🔑 **화면이 상수로 갖지 마라.** 장치의 ACK 배출률에서 유도되므로 판마다 바뀐다.
          << ",\"max_per_batch\":" << max_per_batch()
          << ",\"epoch\":" << lot.epoch() << ",\"ts_ms\":" << epoch_ms() << ",\"zones\":[";
        for (size_t i = 0; i < lot.zones().size(); i++) {
            const Zone& z = lot.zones()[i];
            if (i) o << ",";
            const std::string blk = zone_block_reason(z);
            int si = slot_index(z.id);                 // 예약 상태는 여전히 slots[] 가 원본이다
            o << "{\"id\":" << jstr(z.id);
            // ── 🔴 `occupied` 를 **그 자리의 센서들에서 유도한다** (명세 §9.2)
            //   한 자리에 센서가 둘이므로 두 값에서 하나를 만들어야 한다. **서버가 계산한다** —
            //   화면이 조합하게 두면 규칙이 두 곳에 생긴다(§"파생 값은 원본을 가진 쪽이 계산한다").
            // 🔴 **서버는 값을 모으기만 하고, 무엇으로 읽을지는 자리가 정한다** (`spot.h` · 1단계)
            //   전에는 이 자리에서 바로 `occ = (v_ones > 0)` 을 계산했다.
            //   이제 **판단은 `SpotBehavior::occupied()`** 가 하고 여기서는 **입력만 만든다.**
            //   🔑 그래야 기여자가 자기 자리의 판정을 갈아끼울 수 있다(2단계).
            //   ⚠ 지금은 모든 자리가 기본 구현(=지금과 같은 OR)을 쓰므로 **거동 변화가 0 이다.**
            std::vector<SensorReading> readings;
            int v_known = 0, v_total = 0, v_ones = 0;
            for (size_t m = 0; m < z.modules.size(); m++) {
                const Node* mn = node_by_devid(z.modules[m].first);
                int mi = -1;
                if (mn)
                    for (size_t k = 0; k < mn->mods.size(); k++)
                        if (mn->mods[k].first == z.modules[m].second) { mi = (int)k; break; }
                if (mi < 0) continue;
                // 🔑 **점유 센서만 센다.** 명령 모듈(`O…`)은 자리 점유를 말하지 않는다(명세 §8.1).
                // 🔴 **첫 글자만 본다** (v2 · 2026-08-20). 전에는 `!= "IP"` 로 **통째로** 비교했다 —
                //   그러면 기여자가 `IQ`(관측·다른 종류)를 써도 **점유에서 조용히 빠진다.**
                //   계약은 **첫 글자 `I`/`O`** 뿐이고 나머지는 기여자 자유다.
                if (!mn || mn->mods[mi].second.empty() || mn->mods[mi].second[0] != 'I') continue;
                v_total++;
                const bool known = (mi < mn->mod_bits_n);
                const bool val   = known && mn->mod_bits[mi];
                readings.push_back(SensorReading(known, val));
                if (known) { v_known++; if (val) v_ones++; }
            }
            if (z.kind == "parking") {
                // 🔴 **OR 다.** 두 오류의 대가가 대칭이 아니다 —
                //   빈 자리를 "찼다"고 하면 손해는 자리 하나이고,
                //   찬 자리를 "비었다"고 하면 **운전자가 가서 못 댄다.**
                //   ⚠ AND 로 하면 센서 하나가 죽었을 때 그 자리가 영영 "비었다"로 보인다.
                // 🔴 **여기가 기여자가 갈아끼우는 자리다.** 지금은 자리 전부가 기본 구현을 쓴다.
                //   ⚠ 반환값 `false` 는 *"비었다"* 가 아닐 수 있다 — 전부 모르는 경우도 `false` 다.
                //     그 구분은 아래 `value_state` 가 나른다. **2단계에서 `Tri` 로 합친다.**
                const bool occ = behavior_for(z.id).occupied(readings);
                const char* vs = (v_total == 0 || v_known == 0) ? "unknown"
                               : (v_known == v_total ? "known" : "partial");
                // 🔴 `value_state` 는 **모든 자리에 항상 싣는다.** 선택적이면 화면이 안 본다.
                //   ⚠ `unknown` 일 때 `occupied:false` 는 **"비었다"가 아니라 "모른다"** 이다.
                o << ",\"occupied\":" << (occ ? "true" : "false")
                  << ",\"value_state\":" << jstr(vs)
                  << ",\"value_known\":" << v_known << ",\"value_total\":" << v_total;
                if (si >= 0) o << ",\"reserved\":" << (slots[si].reserved ? "true" : "false");
                // 🔴 **둘 다 아는데 값이 갈리면 센다.** 이중화의 목적이 고장 감지인데
                //   세지 않으면 이중화가 아무 일도 안 한다(명세 §9.2).
                if (v_known >= 2 && v_ones > 0 && v_ones < v_known) split_now++;
            } else if (si >= 0) {
                o << ",\"occupied\":" << (slots[si].occupied ? "true" : "false")
                  << ",\"reserved\":" << (slots[si].reserved ? "true" : "false");
            }
            o << ",\"actions\":{";
            bool first = true;
            if (z.kind == "parking" && si >= 0) {
                // 🔑 **뜻이 있는 것만 넣는다.** 예약이 없는 자리의 `cancel` 은 **키 자체를 안 보낸다**
                if (!slots[si].reserved) {
                    // 🔴 **차가 있는 자리는 예약할 수 없다** (REQ-0235 · web 이 찾음 · 2026-08-19).
                    //   장치가 그렇게 판정한다 — `client.ino`: `if (occMask & bit) result = 1;`
                    //   그리고 설계 의도가 그 위 주석에 있다: **`occupied=1 ∧ reserved=1` 은
                    //   "예약하고 나서 주차한" 성공 상태**다. **반대 순서는 없다.**
                    //
                    //   ⚠ **여는 근거와 막는 근거가 다르면 반드시 창이 생긴다.**
                    //     여기서 `ok:true` 를 주면 화면이 버튼을 그리고, 누르면 장치가 `result=1` 로
                    //     거절한다 — **사용자에게는 "눌렀는데 안 되는 버튼"이다.**
                    //   🔑 `actions` 가 있는 이유가 **실패할 것을 안 내놓는 것**이다.
                    //
                    //   ⚠ **키를 빼지 않고 `ok:false` 로 준다**: 차가 나가면 예약이 가능해지므로
                    //     *"뜻이 없다"* 가 아니라 *"뜻은 있는데 막혔다"* 다(§6.5 의 존재/부재 규칙).
                    const std::string rr = (blk.empty() && slots[si].occupied) ? "occupied" : blk;
                    emit_action(o, first, "reserve", rr);
                } else {
                    emit_action(o, first, "cancel",  blk);
                }
            }
            // 🔴 v2 : 자리 종류가 **`"parking"` / `"area"`** 둘뿐이다.
            //   `area` 에는 자리 단위 조작이 없다 — 조작은 **모듈의 `control` 선언**이 나른다.
            (void)blk;
            o << "}";
            // 🔴 **자리의 `completion` 이 여기 있었다. 2026-08-20 에 없앴다.**
            //   게이트 내장 조작을 없앤 뒤로 **`gate_want` 에 값을 쓰는 곳이 하나도 없다** →
            //   `settled`·`mismatch` 가 **도달 불가**가 되어 `pending`/`unknown` 만 남았다.
            //   실기 실측도 `unknown` 뿐이었다.
            //   🔴 **닫힌 집합 넷이라고 적어 둔 계약이 일어날 수 없는 상태 둘을 계속 말하고 있었다.**
            //
            // 🔑 그리고 애초에 **자리 단위로 답할 수 없는 물음**이었다 —
            //   자리는 모듈이 여럿인데 `completion` 은 **하나**였다.
            //   답은 이제 **모듈마다의 `confirmed`** 가 나른다.
            o << ",\"modules\":[";
            for (size_t m = 0; m < z.modules.size(); m++) {
                if (m) o << ",";
                // 🔴 **이제 값이 있다** — `S` 의 비트열에서 뽑는다(비트 `idx`).
                //   ⚠ **모르면 여전히 `known:false` 다.** 등록 전이거나 폭을 못 읽었으면 `mod_bits_n == 0` 이고,
                //     그때 `value:false` 를 내면 **화면이 "닫혀 있다"를 사실로 그린다.** 모름과 거짓은 다르다.
                // 🔴 ②-d — **그 모듈의 노드**에서 색인과 비트를 읽는다(주 노드 고정을 걷어낸다).
                const Node* mn = node_by_devid(z.modules[m].first);
                int mi = -1;
                if (mn)
                    for (size_t k = 0; k < mn->mods.size(); k++)
                        if (mn->mods[k].first == z.modules[m].second) { mi = (int)k; break; }
                const bool known = (mn && mi >= 0 && mi < mn->mod_bits_n);
                // 🔴 **`known:false` 에는 사유를 붙인다** (명세 §8.10 · 2026-08-19)
                //   전에는 원인 셋이 **같은 모양**으로 나왔다 — 원인이 다른데 표시가 같으면 아무도 못 고친다.
                //   ⚠ 어휘는 §6.5 의 기존 코드를 먼저 쓰고 없는 것만 새로 만들었다.
                //   ⚠ `reason` 은 **`known:false` 일 때만** 싣는다(존재/부재 규칙).
                const char* why = 0;
                if (!known) {
                    const bool off = mn && (mn == &park ? !device_online() : !mn->online);
                    if (!mn)                      why = "node_unregistered";
                    else if (mi < 0)              why = "module_absent";
                    else if (off)                 why = "node_offline";
                    else if (mn != &park && mn->mod_bits_n == 0)
                        // 🔑 보조 노드는 `S` 가 아직 파서에 안 들어간다(②-b 가 `D` 만 넣었다).
                        //   **구조적으로 값 경로가 없다** — "해독 실패"와 다른 사건이다.
                        why = "bits_unavailable";
                    else if (mn->mod_bits_n == 0) why = "bits_undecoded";
                    else                          why = "bits_out_of_range";
                }
                o << "{\"devid\":" << jstr(z.modules[m].first)
                  << ",\"name\":" << jstr(z.modules[m].second)
                  << ",\"idx\":" << mi
                  << ",\"value\":" << (known ? (mn->mod_bits[mi] ? "true" : "false") : "null")
                  << ",\"known\":" << (known ? "true" : "false");
                if (why) o << ",\"reason\":" << jstr(why);
                emit_module_cmd(o, z.modules[m].first, z.modules[m].second, mn, mi, known);
                o << "}";
            }
            o << "]}";
        }
        o << "]}";
        sensor_split_now = split_now;      // 🔑 누적이 아니라 **지금 값**으로 덮는다
        return o.str();
    }
    // 🔴🔴 **`requested` · `confirmed` · `cmd`** (2026-08-20 · SPEC-web-control §3.2/§4)
    //
    //   web 이 찾은 빈 자리: *"`value` 가 bool 이라 7654321 을 보냈는데 그렇게 됐나를 못 잰다."*
    //   **맞다. 그리고 넓힐 수 없다** — 상행 `S` 의 모듈 상태는 **비트마스크**다(`decode_mod_bits`).
    //   **모듈당 1비트.** 숫자를 실을 자리가 **전선에 없다.**
    //
    //   🔑 그래서 `value` 를 숫자로 **안 넓혔다.** 넓히면 `known:false`(모른다)를
    //     `known:true`(안다·틀림)로 바꾸는 것이고, **값 경로가 없는데 값을 만들면 지어내는 것**이다.
    //     대신 **세 값을 갈랐다**: 에코 비트 · 내가 보낸 값 · 판정.
    //
    // 🔴 **판정을 한 키에 몬다.** 초안은 `completion` + `echo_proves` **둘을 읽어야**
    //   거짓 완료를 피하는 형태였다 — **두 키를 읽어야 안전한 계약은 언젠가 한 키만 읽힌다.**
    void emit_module_cmd(std::ostringstream& o, const std::string& devid,
                         const std::string& name, const Node* mn, int mi, bool known) {
        const ControlDecl* c = control_of(devid, name);
        // ── `cmd` : 지금 보낼 수 있나. 🔑 **사유를 붙인다** — 원인이 다르면 고칠 곳도 다르다.
        const char* blk = 0;
        if (!c)                        blk = "not_declared";
        else if (mi < 0)               blk = "module_absent";
        else if (!mn || !mn->reg_done) blk = "node_unregistered";
        else if (!device_online())     blk = "device_offline";
        o << ",\"cmd\":{\"ok\":" << (blk ? "false" : "true")
          << ",\"reason\":" << (blk ? jstr(blk) : std::string("null")) << "}";

        std::map<std::string, long>::const_iterator ri = mod_req.find(devid + "\t" + name);
        if (ri == mod_req.end()) {
            // **보낸 적이 없으면 키를 안 보낸다**(존재/부재 규칙). `unknown` 이 옳은 답이다.
            o << ",\"confirmed\":\"unknown\"";
            return;
        }
        const long req = ri->second;
        o << ",\"requested\":" << req;

        // 떠 있는 명령이 있나 — 🔑 **색인으로 본다.** `Pending::slot` 은 경로마다 뜻이 다르다
        //   (`dispatch_gate` 는 자리 id, `send_to_module` 은 모듈 이름). **겸한 칸을 믿지 않는다.**
        bool pending = false;
        for (std::map<uint16_t, Pending>::const_iterator it = pend.begin(); it != pend.end(); ++it)
            if (it->second.kind == 'G' && it->second.mod_idx == mi) pending = true;
        if (pending)      { o << ",\"confirmed\":\"pending\""; return; }
        if (!known)       { o << ",\"confirmed\":\"unknown\""; return; }

        const bool bit = mn->mod_bits[mi] != 0;
        // 🔴🔴 **에코 비트는 장치의 *상태* 다. 우리가 보낸 *인자* 가 아니다.**
        //   그 둘의 관계를 아는 것은 **기여자뿐이다.** 실측이 보여 줬다:
        //     `DR 1`(열기) → 비트 **1**   ·   `DR 2`(닫기) → 비트 **0**
        //   → 인자 **2** 를 보냈는데 비트가 **0** 이다. **성공인데 값이 다르다.**
        //
        // 🔴 처음엔 *"요청이 0/1 이면 증명된다"* 로 짰다. **그러면 위 닫기가 `mismatch` 가 된다** —
        //   **정확히 성공한 명령을 실패라고 부른다.** 오늘 게이트 경로가 저지른 것과 **같은 가정**이다:
        //   *"서버가 값의 뜻을 안다"*. **같은 날 같은 실수를 두 번 했고, 시험을 고치다 두 번째를 잡았다.**
        //
        // ✅ **판별자는 위젯이다** — `toggle` 선언은 **"값이 곧 상태다"** 라는 **기여자의 선언**이다.
        //   그 밖의 위젯에서는 **서버가 arg 와 비트의 관계를 모른다** → `partial` 이 유일한 참이다.
        if (c && c->widget == ControlDecl::TOGGLE && (req == 0 || req == 1))
            o << ",\"confirmed\":\"" << ((bit ? 1L : 0L) == req ? "settled" : "mismatch") << "\"";
        else
            // `number`·`choice` — 비트가 0 이든 1 이든 **그 값이 됐다는 증거가 아니다.**
            o << ",\"confirmed\":\"partial\"";
    }
    void push_state() { ws_broadcast(state_json()); }

    std::string snapshot_json() {
        std::ostringstream o;
        o << "{\"type\":\"snapshot\",\"ts\":" << epoch_ms()
          << ",\"device\":{\"online\":" << (device_online() ? "true" : "false")
          << ",\"device_id\":" << jstr(ard_dev)
          << ",\"uptime\":" << (ard_uptime < 0 ? 0 : ard_uptime)
          << ",\"seq\":" << (ard_seq < 0 ? 0 : ard_seq);
        // §9.1 — **파일 폴백(`data_log.json`)과 같은 키·같은 단위·같은 정의**로 낸다(REQ-0132).
        // 값은 `ard_last_epoch_ms` = **마지막 유효 S 프레임을 받은 서버 시각(epoch ms)** 이고,
        // "스냅샷을 만든 시각"(위 `ts`)과 **다른 것**이다. 둘이 갈리는 게 신선도 표시의 요점이다.
        //
        // ⚠ 전에는 이 키가 **WS 에만 없고 파일에만 있었다.** 그래서 신선도 표시가
        // **폴백일 때만 동작하고 정상 운영 중에는 영영 "알 수 없음"** 이었다 —
        // 기능이 가장 필요한 경로에서만 빠져 있던 것이다(web-engineer 크롬 실측).
        //
        // 한 번도 프레임을 못 받았으면 **`null`** 이다. `0` 을 내보내면 화면이 그것을
        // epoch 로 읽어 **1970년으로부터의 나이**를 그린다 — 누락보다 나쁘다.
        o << ",\"last_frame_ts\":";
        if (ard_last_epoch_ms > 0) o << ard_last_epoch_ms; else o << "null";
        o << "}";
        // §5.3 test_mode — 출처는 S 의 tmask 다(§12A.4). 서버가 T 를 보냈다는 사실이 아니다.
        int novr = 0;
        for (int i = 0; i < 10; i++) if (test_armed && test_ovr[i]) novr++;
        o << ",\"test_mode\":{\"armed\":" << (test_armed ? "true" : "false")
          << ",\"override_count\":" << novr << "}";
        // 🔴🔴 **지형의 자리만 낸다.** 전에는 `SLOT_ID[10]` 을 고정으로 돌았다.
        //
        //   화면은 새 격자(`map.zones`)를 못 쓰면 **이 목록으로 옛 격자를 만든다**(폴백).
        //   지형이 자리 하나인데 여기서 열을 내면 **없는 자리 아홉이 화면에 그려진다.**
        //   🔴 값이 `00` 이라 고장으로 안 보이고 **"빈 자리 아홉"** 으로 보인다 —
        //     **운전자가 없는 자리를 보고 간다.**
        //   ⚠ 실제로 났다: 전선 키가 깨져 `mapUsable()` 이 false 가 됐을 때 화면이
        //     이 폴백으로 떨어졌고, 그때 열 칸 중 아홉이 지금 지형에 없는 자리였다.
        //
        // 🔑 **폴백은 안전장치인데, 없는 자리를 그리면 안전장치가 아니라 거짓 정보다.**
        // 🔑 그리고 두 봉투가 **같은 출처**가 되므로 "두 경로 일치" 대조가 다시 성립한다 —
        //   분모가 다른 두 값을 대조하는 검사는 **항상 불일치이거나 아무 말도 안 한다.**
        //
        // ⚠ **봉투에서 빼지 않는다.** 빼면 폴백 경로가 죽고, 새 격자가 깨진 날 화면이 통째로 빈다.
        o << ",\"slots\":[";
        {
            bool first_slot = true;
            for (size_t z = 0; z < lot.zones().size(); z++) {
                const Zone& zn = lot.zones()[z];
                if (zn.kind != "parking") continue;        // 입출구는 옛 격자의 자리가 아니다
                const int si = slot_index(zn.id);          // 옛 자리 배열에 있으면 그 값을 쓴다
                if (!first_slot) o << ",";
                first_slot = false;
                o << "{\"id\":" << jstr(zn.id)
                  << ",\"occupied\":" << (si >= 0 ? slots[si].occupied : 0)
                  << ",\"reserved\":" << (si >= 0 ? slots[si].reserved : 0)
                  << ",\"user_id\":";
                if (si < 0 || slots[si].user_id.empty()) o << "null"; else o << jstr(slots[si].user_id);
                o << ",\"reserved_at\":";
                if (si < 0 || slots[si].reserved_at == 0) o << "null"; else o << slots[si].reserved_at;
                o << ",\"overridden\":" << ((si >= 0 && test_armed && test_ovr[si]) ? 1 : 0);
                o << "}";
            }
        }
        o << "]}";
        return o.str();
    }
    void push_snapshot() {
        ws_broadcast(snapshot_json());
        // 🔑 **옛 봉투와 같은 순간에 새 봉투를 낸다** — 같은 서버 상태에서 파생시키므로
        //   두 경로가 서로 다른 말을 할 수 없다(web §1.2 의 "한 화면이 두 진실" 방지).
        ws_broadcast(state_json());
    }

