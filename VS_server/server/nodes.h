// nodes.h — 다중 노드 — 등록·승격·결속·라우팅 (REQ-0083). `struct Server` 의 몸통 조각.
    // ═══════════════════════════════════════════════════════════════════
    // 🔴 **단독으로 컴파일되지 않는다.** `struct Server` 의 **몸통 조각**이고
    //   `server.cpp` 안 그 자리에 include 된다. **위치가 곧 문법이다.**
    // 🔑 옮긴 것이지 고친 것이 아니다 — 전처리 결과가 같아 **`.o` 가 바이트 동일**해야 한다.
    //   대조가 0 이 아니면 이동이 아니라 재배치다. 되돌리고 보고한다(REQ-0272).
    // ═══════════════════════════════════════════════════════════════════

    // ---------- 다중 노드 (REQ-0083) ----------------------------------------
    // §2.3 `devid ::= 1*8( ALPHA / DIGIT / "_" / "-" )`
    static bool valid_devid(const std::string& d) {
        if (d.empty() || d.size() > 8) return false;
        for (size_t i = 0; i < d.size(); i++) {
            char c = d[i];
            bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '_' || c == '-';
            if (!ok) return false;
        }
        return true;
    }

    // 한 줄에서 device_id 를 꺼낸다. **체크섬을 통과한 `S` 프레임만 믿는다.**
    // 접속 직후에는 AT 잡음이 섞여 들어오므로(§6.2) 아무 줄이나 신뢰하면
    // **쓰레기가 device_id 가 되어** 그 이름으로 노드 자리를 하나 영구히 잡아먹는다.
    static bool peek_devid(const std::string& line, std::string& out) {
        std::vector<std::string> f;
        if (!verify_line(line, f)) return false;
        if (f.size() < 6 || f[0] != "S") return false;
        if (!valid_devid(f[5])) return false;
        out = f[5];
        return true;
    }

    // 주차 노드 자리를 넘겨받는다. **옛 소켓 대체는 같은 device_id 일 때만** 일어난다 —
    // 그것이 REQ-0083 이 고치는 핵심이다(옛 구조는 id 를 모른 채 무조건 대체했다).
    void adopt_as_parking(sock_t c, const std::string& dev) {
        if (ard != BAD_SOCK) {
            closesock(ard); conns.erase(ard);
            end_ard_session("같은 device_id(" + dev + ") 재접속으로 대체");
        }
        ard = c; ard_buf.clear();
        park_dev = dev;
        ard_sessions++;
        sess_start_ms = now_ms();
        sess_frames = 0; sess_last_line_ms = 0; sess_max_gap_ms = 0;
        // 창 계산에 쓰는 왕복 표본은 **세션마다 새로 쌓는다** — 옛 세션의 병리적 한 건이
        // 새 세션의 창을 영구히 조이면, 창이 좁아진 이유를 나중에 아무도 설명 못 한다.
        rtt_max_ms = 0; rtt_last_ms = 0; rtt_n = 0;
        if (link_down_since) { link_down_ms += now_ms() - link_down_since; link_down_since = 0; }
        reboot_by_conn++;
        park.reg_reset();          // 🔑 세션이 새로 서면 등록도 처음부터다
        ard_peer = peer_str(c);
        logf("+ARD", "주차 노드 접속 — 세션#" + std::to_string(ard_sessions)
                     + " · device=" + dev + " · 상대 " + ard_peer);
        ard_seq = -1; ard_uptime = -1;
        base_valid = false;                 // §7.5-1
        // 🔴 **재하달을 담기 전에 큐를 비운다.** `ard` 가 이미 BAD_SOCK 이었던 경로에서는
        // `end_ard_session()` 이 안 불렸을 수 있어 **옛 큐가 새 세션으로 넘어온다.**
        // 그 경우 장치는 자기가 모르는 rid 의 명령을 받는다(설계 §2).
        clear_downq("새 세션 시작");
        resync_reservations("새 연결");
    }

    // 보조 노드 자리에 넣는다(상행 전용).
    void adopt_as_aux(sock_t c, const std::string& dev) {
        std::map<std::string, AuxNode>::iterator it = aux.find(dev);
        if (it != aux.end() && it->second.fd != BAD_SOCK) {
            closesock(it->second.fd);
            logf("-AUX", "보조 노드 " + dev + " — 같은 device_id 재접속으로 대체 (프레임 "
                         + std::to_string(it->second.frames) + ")");
        }
        AuxNode& a = aux[dev];
        // 🔴 **자기 `devid` 를 채운다** (2026-08-19 · ②-b 가 드러냈다).
        //   맵의 **키**에만 id 가 있고 노드 자신의 필드는 비어 있었다. 아무도 안 읽어서 안 보였다 —
        //   ②-b 로 보조 노드의 `D` 가 파서에 들어가자 `bind_modules(n)` 이 그 빈 값을 썼고
        //   **지형에 `("", "A1")` 같은 결속이 생겼다.** 로그도 `노드  등록 결속` 으로 나왔다.
        //   🔑 **읽는 사람이 없던 필드는 틀려도 안 보인다.** 새 독자가 생기는 순간 드러난다.
        a.devid = dev;
        warn_example_devid(dev);       // 보조 노드도 같은 함정을 밟는다
        a.fd = c; a.buf.clear();
        a.connected_ms = now_ms();
        a.last_ms = now_ms();               // 유휴 마감 기준선. 0 이면 즉시 회수 대상이 된다
        a.online = false;
        logf("+AUX", "보조 노드 접속 — device=" + dev
                     + " · 현재 노드 " + std::to_string(aux.size() + (ard != BAD_SOCK ? 1 : 0))
                     + "/" + std::to_string(MAX_ARD_NODES) + " · **상행 전용**(하행 경로 없음)");
    }

    // 주차 노드 버퍼를 줄 단위로 비운다. 수신 경로와 **똑같은 규칙**이어야 하므로
    // 승격 직후에도 이걸 부른다(첫 프레임이 1초 늦게 처리되는 일이 없게).
    void drain_ard_buf() {
        size_t i;
        while ((i = ard_buf.find('\n')) != std::string::npos) {
            std::string line = ard_buf.substr(0, i);
            ard_buf.erase(0, i + 1);
            if (!line.empty() && line[line.size()-1] == '\r') line.erase(line.size()-1);
            // 🔑 **버릴 때도 길이를 남긴다.** 예전에는 "64B 초과" 라고만 적고 버려서
            // **몇 바이트였는지**를 못 봤다 — `AT+CIPSEND` 잘림 시험에서 그것이 측정값이다.
            if (line.size() + 1 > (size_t)MAX_LINE) {
                drop_overlong++;
                char b[96];
                snprintf(b, sizeof(b), "상한(%dB) 초과 줄 — 버림 · **rx=%zuB**",
                         MAX_LINE, line.size());
                logf("!", b);
                continue;
            }
            if (!line.empty()) on_ard_line(park, line);
        }
        if (ard_buf.size() > (size_t)MAX_LINE) {
            drop_noise++;
            logf("!", "LF 없이 64B 초과 — 버퍼 비움");
            ard_buf.clear();
        }
    }

    // id 미상 소켓 마감 + 보조 노드 유휴 회수 (REQ-0083).
    // 주차 노드의 유휴 마감(§3.5)과 **같은 근거·같은 상수**를 쓴다 — 노드마다 다른 규칙을 두면
    // "왜 저 노드만 안 끊기지"를 나중에 아무도 설명 못 한다.
    void reap_nodes() {
        long long t = now_ms();
        for (size_t k = 0; k < unknown.size(); ) {
            if (unknown[k].fd != BAD_SOCK && t - unknown[k].since_ms >= UNKNOWN_TIMEOUT_MS) {
                logf("✂", "id 미상 소켓 마감 — " + std::to_string(UNKNOWN_TIMEOUT_MS)
                          + "ms 안에 유효 프레임이 없었다(§3.4 판정의 2배)");
                closesock(unknown[k].fd);
                unknown.erase(unknown.begin() + k);
            } else k++;
        }
        std::vector<std::string> reap;
        for (std::map<std::string, AuxNode>::iterator it = aux.begin(); it != aux.end(); ++it) {
            AuxNode& a = it->second;
            if (a.fd == BAD_SOCK) continue;
            if (t - a.last_ms >= ARD_IDLE_CLOSE_MS) reap.push_back(it->first);
            // §3.4 오프라인 엣지 — 노드별로 따로 센다. 합치면 한 노드가 죽어도
            // 다른 노드에 가려 안 보인다(요청 항목 4).
            bool on = (t - a.last_ms) < OFFLINE_MS;
            if (on != a.online) {
                a.online = on;
                if (!on) { a.offline_episodes++;
                    logf("!", "보조 노드 " + it->first + " 오프라인(누적 "
                              + std::to_string(a.offline_episodes) + "회)"); }
                else logf("=", "보조 노드 " + it->first + " 온라인 복귀");
            }
        }
        for (size_t k = 0; k < reap.size(); k++) {
            zombie_reaps++;
            logf("✂", "보조 노드 " + reap[k] + " 회수 — "
                      + std::to_string(ARD_IDLE_CLOSE_MS / 1000) + "초 무프레임(유휴 마감)");
            closesock(aux[reap[k]].fd);
            aux.erase(reap[k]);
        }
    }

    // ── REQ-0203 4a: 지형 ───────────────────────────────────────────────────────
    // 🔴 **`epoch` 를 올리는 곳은 여기 셋뿐이다.** 지형을 바꾸는 줄 바로 옆이다.
    void bump_epoch(const std::string& why) {
        logf("=", "지형 판 " + std::to_string(lot.bumpEpoch()) + " (" + why + ")");
        // 🔑 **판이 오르면 곧바로 보낸다.** 안 보내면 화면은 `state.epoch` 불일치를 보고
        //   `get_map` 을 물어야 하고, **그 사이 "모른다"로 그리는 창이 생긴다.**
        //   보내는 쪽이 먼저 움직이면 그 창이 없다.
        push_map();
    }

    // 기본 지형. ⚠ **기본값이지 고정값이 아니다** — 설정 적재가 생기면 이 함수를 대체한다.
    // 🔴 조립 표가 주어지면 **그것이 지형이다**(REQ-0272). 없으면 종전 기본 지형.
    //   ⚠ 표는 `ParkingServer` 가 넣어 준다 — 자가검증은 표 없이 돌므로 기본값이 남아야 한다.
    const ParkingLot* lot_ = 0;   // 🔑 선언 자리에서 초기화한다 — 초기화 목록에 넣으면
                              //   선언 순서와 어긋나 `-Wreorder` 가 난다(실제로 났다)
    // 🔴🔴 **조립 표(사용 코드)가 말이 되는가** — 기동 때 한 번, 값으로 말한다 (2026-08-19)
    //
    //   사용자 요구: *"이 프로젝트가 나 혼자 진행하는 것이 아니다. 다른 인원의 작업을 반영하고 싶다.
    //   그러면 **작업 방식을 가이드해야 한다.**"*
    //   🔑 기여자가 만지는 면이 `lot.cpp` 의 조립 표다. **거기서 틀리면 여기서 말해야 한다.**
    //
    //   ⚠ **거절하지 않는다. 말만 한다.** 어느 것도 서버를 못 뜨게 할 만큼은 아니고,
    //     기동을 막으면 기여자가 *"내 것 때문에 서버가 안 뜬다"* 로 겁을 먹는다.
    //     🔑 **막는 것보다 보이는 것이 낫다** — 우리가 `--max-line` 범위 검사에서 반대로 한 것과
    //       다른 판단이고, 그건 **틀린 값이 조용히 엉뚱한 포트를 잡기 때문**이었다. 여기는 안 그렇다.
    void validate_assembly() {
        if (!lot_ || lot_->empty()) return;
        const std::vector<ParkingLot::Area>& as = lot_->areas();

        // ① **같은 센서 이름이 두 자리에** — `zoneOfModule` 은 **첫 자리를 돌려준다.**
        //    🔴 그래서 뒤엣 자리는 그 센서를 **영영 못 받는다.** 아무 오류도 안 난다.
        // ⚠ **`devid` 가 둘 다 비었거나 같을 때만 충돌이다.** 장치가 다르면 같은 이름이어도
        //   서로 다른 모듈이고, 그것을 갈라 주는 것이 2인자 형태의 목적이다(2026-08-19).
        for (size_t i = 0; i < as.size(); i++)
            for (size_t k = 0; k < as[i].modules.size(); k++)
                for (size_t j = i; j < as.size(); j++)
                    for (size_t m = (j == i ? k + 1 : 0); m < as[j].modules.size(); m++) {
                        const ParkingLot::Attach& x = as[i].modules[k];
                        const ParkingLot::Attach& y = as[j].modules[m];
                        if (x.name != y.name) continue;
                        // 둘 중 하나라도 "아무 장치나"면 겹친다. 둘 다 지정이면 같을 때만.
                        if (!x.devid.empty() && !y.devid.empty() && x.devid != y.devid) continue;
                        {
                            asm_warn_++;
                            logf("🔴", "조립 표 — 모듈 이름 `" + as[i].modules[k].name
                                       + "` 이 자리 " + as[i].id + " 와 " + as[j].id
                                       + " 에 둘 다 적혀 있다. **앞엣 자리(" + as[i].id
                                       + ")가 이기고 " + as[j].id + " 는 이 모듈을 영영 못 받는다.** "
                                         "`lot.cpp` 의 조립 표에서 한쪽을 고쳐라"
                                         + std::string(x.devid.empty() && y.devid.empty()
                                             ? " (둘 다 장치를 안 정했다 — `module(\"devid\",\"이름\")` 로 갈라라)"
                                             : ""));
                        }
                    }

        // ③ 🔴🔴 **모듈 이름은 정확히 2바이트다** — 장치의 표가 `char name[3]` 이다.
        //
        //   장치 쪽은 3글자 이상을 쓰면 **컴파일이 막는다.** 서버 쪽은 아무 문자열이나 받는다 —
        //   그래서 `"LED1"` 이나 `"왼쪽센서"` 라고 적으면 **그 모듈은 영영 안 붙는다.**
        //   🔴 증상은 `미결속모듈` 인데, 그 칸은 **"안 쓰기로 한 것"과 구분이 안 된다.**
        //     그래서 여기서 **이름을 지목해서** 말한다.
        //
        // ⚠ **바이트로 센다.** 한글 한 글자가 3바이트라 `"가"` 도 안 된다 — 전선은 ASCII 다.
        for (size_t i = 0; i < as.size(); i++)
            for (size_t k = 0; k < as[i].modules.size(); k++) {
                const std::string& nm = as[i].modules[k].name;
                bool okLen = (nm.size() == 2);
                bool okChr = okLen;
                for (size_t c = 0; c < nm.size() && okChr; c++)
                    if ((unsigned char)nm[c] < 0x21 || (unsigned char)nm[c] > 0x7E) okChr = false;
                if (okLen && okChr) continue;
                asm_warn_++;
                logf("🔴", "조립 표 — 모듈 이름 `" + nm + "` (자리 " + as[i].id
                           + ") 이 **쓸 수 없는 이름**이다. **정확히 2바이트 ASCII** 여야 한다"
                             " — 장치의 표가 `char name[3]` 이라 전선에 2글자만 나간다. "
                             "**이대로 두면 그 모듈은 영영 안 붙고 `미결속모듈` 로만 보인다.** "
                             "지금 " + std::to_string(nm.size()) + "바이트");
            }

        // 🔴 **② 검사가 여기 있었다 — 조립 시점에는 이제 못 한다** (v2 · 2026-08-20)
        //   `sensor()`/`actuator()` 를 `module()` 로 합치면서 **선언만 보고는 무엇이 센서인지 모른다.**
        //   센서 여부는 **장치 등록(`D`)의 `kind` 첫 글자**가 말한다.
        //   → 같은 검사를 `bind_modules()`(등록 뒤)로 옮겼다. **지우지 않았다.**
        //   🔑 늦어진 것이 아니라 **알 수 있는 가장 이른 시점으로 옮긴 것**이다.

        if (asm_warn_ == 0)
            logf("=", "조립 표 검사 — 자리 " + std::to_string(as.size()) + "개 · 문제 없음");
        else
            logf("!", "조립 표 검사 — **문제 " + std::to_string(asm_warn_)
                      + "건**. 서버는 뜬다(위 줄들을 봐라)");
    }

    void build_default_zones() {
        lot.clear();
        // 🔴 **주차 자리는 5개다** (사용자 확정 (A) · 명세 §9). 각 자리에 센서 둘(이중화).
        //   조립 표가 있으면 **그것이 지형이다**. 없으면 기본 지형(자가검증은 표 없이 돈다).
        if (lot_ && !lot_->empty()) {
            const std::vector<ParkingLot::Area>& as = lot_->areas();
            // 🔴 **격자 위치** (v2 · 2026-08-20)
            //   `at(행,열)` 을 준 자리는 그 자리에, **안 준 자리는 선언 순서대로 자동 배치**한다.
            //   ⚠ 자동 배치가 `at()` 이 이미 쓴 칸을 덮지 않게 **쓴 칸을 먼저 모아 두고 피한다** —
            //     안 그러면 `at()` 을 쓴 사람이 **자기가 안 만든 겹침**을 보게 된다.
            std::set<std::pair<int,int> > taken;
            for (size_t i = 0; i < as.size(); i++)
                if (as[i].row >= 0 && as[i].col >= 0)
                    taken.insert(std::make_pair(as[i].row, as[i].col));
            int auto_n = 0;
            for (size_t i = 0; i < as.size(); i++) {
                Zone z; z.id = as[i].id; z.kind = as[i].kind;
                int r, c;
                if (as[i].row >= 0 && as[i].col >= 0) { r = as[i].row; c = as[i].col; }
                else {
                    do { r = auto_n / grid_cols; c = auto_n % grid_cols; auto_n++; }
                    while (taken.count(std::make_pair(r, c)));
                    taken.insert(std::make_pair(r, c));
                }
                z.cells.push_back(std::make_pair(r, c));
                lot.add(z);
            }
            // 🔴 **격자 크기를 자리에서 계산한다.** 화면이 상수로 갖지 않게.
            {
                int mr = 0, mc = 0;
                for (size_t i = 0; i < lot.zones().size(); i++)
                    for (size_t k = 0; k < lot.zones()[i].cells.size(); k++) {
                        if (lot.zones()[i].cells[k].first  + 1 > mr) mr = lot.zones()[i].cells[k].first + 1;
                        if (lot.zones()[i].cells[k].second + 1 > mc) mc = lot.zones()[i].cells[k].second + 1;
                    }
                if (mr > 0) grid_rows = mr;
                if (mc > 0) grid_cols = mc;
            }
            // ⚠ **겹치면 말한다. 막지 않는다** — 겹침은 표시 문제이고 서버 동작은 멀쩡하다.
            //   🔴 다만 증상이 *가려짐* 이라 조용하다. 그래서 **두 자리 id 를 지목**한다.
            //   🔑 그리고 화면에서도 말한다(web) — **로그를 보는 사람과 화면을 보는 사람이 다르다.**
            {
                std::map<std::pair<int,int>, std::string> seen;
                for (size_t i = 0; i < lot.zones().size(); i++) {
                    const std::pair<int,int>& c = lot.zones()[i].cells[0];
                    std::map<std::pair<int,int>, std::string>::iterator it = seen.find(c);
                    if (it == seen.end()) { seen[c] = lot.zones()[i].id; continue; }
                    asm_warn_++;
                    char b[192];
                    snprintf(b, sizeof(b),
                             "조립 표 — 자리 `%s` 와 `%s` 가 **같은 칸 (%d,%d)** 에 있다. "
                             "화면에서 하나가 가려진다. `at(행,열)` 을 확인해라",
                             it->second.c_str(), lot.zones()[i].id.c_str(), c.first, c.second);
                    logf("🔴", b);
                }
            }
            validate_assembly();      // 🔴 선언이 틀렸으면 **여기서 말한다**
            bump_epoch("조립 표에서 지형 구성");
            return;
        }
        for (int i = 0; i < 5; i++) {
            Zone z; z.id = SLOT_ID[i]; z.kind = "parking";
            z.cells.push_back(std::make_pair(i / grid_cols, i % grid_cols));
            lot.add(z);
        }
        Zone e; e.id = "E1"; e.kind = "entrance";
        e.cells.push_back(std::make_pair(grid_rows - 1, 0));
        lot.add(e);
        Zone x; x.id = "X1"; x.kind = "exit";
        x.cells.push_back(std::make_pair(grid_rows - 1, grid_cols - 1));
        lot.add(x);
        // ⚠ 길이 1 **강제는 풀었다**(명세 §9.1). 다르면 로그에 남기고 **깎지는 않는다** —
        //   전에는 `resize(1)` 로 말없이 깎았다.
        for (size_t i = 0; i < lot.zones().size(); i++)
            if (lot.zones()[i].cells.size() != 1)
                logf("=", "자리 " + lot.zones()[i].id + " 의 칸 수 "
                          + std::to_string(lot.zones()[i].cells.size())
                          + " — 1이 아니다(허용한다. 화면이 순회한다)");
        bump_epoch("기본 지형 구성");
    }

    // 🔴 아래 셋은 **위임**이다 — 실물은 `lot.h` 에 있다.
    Zone* zone_find(const std::string& id) { return lot.find(id); }
    std::string zone_of_module_tbl(const std::string& nm) const { return lot.zoneOfModule(nm, lot_); }
    static std::string zone_of_module(const std::string& nm) { return Lot::nameRule(nm); }

    // 결속. 🔴 **규칙은 `Lot` 이 안다. 여기 남는 것은 "얼마나 크게 알리나" 뿐이다.**
    //   전에는 충돌 로그를 결속 안에서 찍었다 — 그러면 지형이 로그 형식을 알게 된다.
    // 🔴🔴 **한 장치 안에서 모듈 이름이 고유한가** (2026-08-19 · socket 이 찾았다)
    //
    //   위 `mod_name_conflict` 는 **노드 *사이*** 충돌만 본다(두 노드가 같은 자리를 주장).
    //   🔴 **한 노드 안의 중복은 아무도 안 봤다.** 그런데 web 은 REQ-0179 §① 에서
    //     *"모듈의 전역 신원은 `(devid, name)` 복합 키"* 라고 정하고 **이미 그렇게 구현했다.**
    //   → **중복이 있으면 그 키가 오늘 이미 애매하다.** 화면이 두 모듈 중 하나를 임의로 집는다.
    //   ⚠ **증상이 "가끔 엉뚱한 모듈이 보인다"라서 결함으로 안 보인다.**
    //
    //   🔑 §"감시할 수 없는 것을 조건으로 적지 마라" 의 역방향이다 —
    //     **남이 이미 조건으로 쓰고 있는 것을 내가 감시하지 않고 있었다.**
    void check_dup_names(const Node& n) {
        for (size_t i = 0; i < n.mods.size(); i++)
            for (size_t k = i + 1; k < n.mods.size(); k++)
                if (n.mods[i].first == n.mods[k].first) {
                    mod_dup_name++;
                    if (mod_dup_name <= 3 || mod_dup_name % 100 == 0)
                        logf("🔴", "장치 안 모듈 이름 중복 — 노드 "
                                   + (n.devid.empty() ? std::string("(미승격)") : n.devid)
                                   + " 의 idx " + std::to_string(i) + " 와 " + std::to_string(k)
                                   + " 가 둘 다 '" + n.mods[i].first + "' 이다. "
                                     "**`(devid,name)` 조회가 애매해진다**(web REQ-0179 §①). "
                                     "전선 주소는 `idx` 라 동작은 하지만 **화면이 둘 중 하나를 임의로 집는다**. "
                                     "누적 " + std::to_string(mod_dup_name));
                    return;   // 한 등록에 한 번만 알린다 — 로그를 덮지 않는다
                }
    }

    // 🔴🔴 **예시 `devid` 로 붙었는가** — 기여자가 기본값을 안 바꿨을 때 **로컬에서** 말한다
    //
    //   왜 여기서 말하나: 결함이 **생기는 단계**(② 각자 로컬 시험)와 **드러나는 단계**(③ 합류)가
    //   멀다. ②에서는 각자 혼자라 아무 증상이 없고, ③에서 전부 충돌한다.
    //   🔑 **그런데 ②에도 서버가 있다.** 그 서버가 말하면 **장치를 안 굽고 그 자리에서 보인다.**
    //
    // ⚠ **막지 않는다.** 혼자 시험할 때는 이 값으로도 정상 동작하고,
    //   기동을 막으면 기여자가 *"내 것 때문에 서버가 안 뜬다"* 로 겁을 먹는다.
    //   🔑 판별자 그대로다 — **증상이 보이면 말한다**(여기는 로그에 뜨고 사람이 본다).
    //
    // ⚠ 매 재접속마다 찍으면 로그를 덮는다 → 처음 셋과 이후 100회마다만 찍는다(다른 계수기와 같은 규율).
    void warn_example_devid(const std::string& dev) {
        for (size_t i = 0; i < EXAMPLE_DEVIDS_N; i++) {
            if (dev != EXAMPLE_DEVIDS[i]) continue;
            devid_example_++;
            if (devid_example_ <= 3 || devid_example_ % 100 == 0)
                logf("🔴", "devid 가 **예시값 `" + dev + "`** 이다 — 기여자가 자기 것으로 "
                           "안 바꿨을 수 있다. **혼자 시험할 때는 정상 동작하지만, 합치면 "
                           "같은 이름끼리 서로 쫓아낸다.** `client.ino` 의 `#define DEVICE_ID` 를 "
                           "자기 것으로 바꿔라(1~8자) · 누적 " + std::to_string(devid_example_));
            return;
        }
    }

    // 🔑 이 자리의 판정 방식. 조립 표가 안 정했으면 **서버 기본(OR)**.
    //   ⚠ 자리 id 로 찾는다 — 지형 `Zone` 과 조립 표 `Area` 가 같은 id 를 쓴다.
    const SpotBehavior& behavior_for(const std::string& zoneId) const {
        if (lot_) {
            const std::vector<ParkingLot::Area>& as = lot_->areas();
            for (size_t i = 0; i < as.size(); i++)
                if (as[i].id == zoneId && as[i].behavior) return *as[i].behavior;
        }
        return default_spot_;
    }

    void bind_modules(Node& n) {
        Lot::BindResult r = lot.bind(n.devid, n.mods, lot_);
        mod_seen_ += (long long)n.mods.size();   // 🔑 분모 — 시도한 만큼 센다
        for (size_t i = 0; i < r.conflicts.size(); i++) {
            mod_name_conflict++;
            if (mod_name_conflict <= 3 || mod_name_conflict % 100 == 0)
                logf("🔴", "모듈 이름 충돌 — 자리 " + r.conflicts[i].first + " 의 이름 '"
                           + r.conflicts[i].second + "' 을 노드 "
                           + (n.devid.empty() ? std::string("(미승격)") : n.devid)
                           + " 가 다시 주장한다. **먼저 잡은 노드를 유지하고 이것은 결속하지 않는다.**"
                             " 누적 " + std::to_string(mod_name_conflict)
                           + " · 자리 결속이 아직 이름 기반이라 생기는 한계다(REQ-0260)");
        }
        check_dup_names(n);
        // 🔴 **센서가 하나도 없는 주차 자리** — 조립 시점에서 여기로 **옮겨 온 검사**다(v2).
        //   `module()` 하나로 합치면서 **선언만 보고는 센서인지 알 수 없게 됐다.**
        //   장치가 `kind` 첫 글자로 말해 주는 **지금이 알 수 있는 가장 이른 시점**이다.
        //   ⚠ 점유를 영원히 모르면 화면에 자리는 보이는데 값이 안 채워진다 →
        //     *"센서가 고장났나"* 를 쫓게 된다. 그래서 **선언이 빈 것**이라고 말해 준다.
        for (size_t zi = 0; zi < lot.zones().size(); zi++) {
            const Zone& z = lot.zones()[zi];
            if (z.kind != "parking") continue;          // 일반영역은 센서가 없어도 된다
            bool anyMine = false, hasSensor = false;
            for (size_t m = 0; m < z.modules.size(); m++) {
                if (z.modules[m].first != n.devid) continue;
                anyMine = true;
                for (size_t k = 0; k < n.mods.size(); k++)
                    if (n.mods[k].first == z.modules[m].second
                        && !n.mods[k].second.empty() && n.mods[k].second[0] == 'I') hasSensor = true;
            }
            // 🔑 **이 노드의 모듈이 하나도 안 붙은 자리는 건너뛴다** — 다른 노드가 센서를
            //   대고 있을 수 있다. 안 건너뛰면 **노드가 늘 때마다 거짓 경고**가 뜬다.
            if (!anyMine || hasSensor) continue;
            asm_warn_++;
            logf("🔴", "자리 " + z.id + " 에 **센서가 하나도 없다**(노드 " + n.devid + " 기준). "
                       "화면에는 자리가 보이지만 점유는 **영원히 `unknown`** 이다 "
                       "— 고장이 아니라 선언이 빈 것이다");
        }
        // 🔴🔴 **어느 자리에도 안 붙은 모듈을 *말한다*.** (2026-08-19 · 루트 지시)
        //
        //   `Modules.h` 주석이 이미 경고하고 있었다: *"이름은 자리 id 와 같아야 한다 —
        //   다른 이름을 쓰면 등록은 성공하고 자리에는 아무것도 안 붙는다. **오류가 안 뜬다**"*
        //   🔴 **주석에 적혀 있다는 것은 아무 데도 없다는 뜻이다.** 기여자는 주석을 안 읽고,
        //     읽어도 자기가 그 경우인지 모른다. **값으로 말해야 한다.**
        //
        //   ⚠ **이건 거절이 아니다.** 노드는 정상 등록되고 하행도 그대로 간다 —
        //     그 모듈이 **어떤 자리에도 안 나타날 뿐**이다. 그래서 더 조용하다.
        for (size_t i = 0; i < r.unbound.size(); i++) {
            mod_unbound++;
            if (mod_unbound <= 5 || mod_unbound % 100 == 0) {
                std::string known;
                for (size_t z = 0; z < lot.zones().size() && z < 12; z++)
                    known += (z ? ", " : "") + lot.zones()[z].id;
                logf("🔴", "모듈 `" + r.unbound[i].second + "` (idx "
                           + std::to_string(r.unbound[i].first) + ", 노드 "
                           + (n.devid.empty() ? std::string("(미승격)") : n.devid)
                           + ") 은 **어떤 자리에도 안 붙는다** — 지형에 그 이름이 없다. "
                             "**등록은 성공했고 하행도 정상이지만 이 모듈은 화면에 안 나타난다.** "
                             "지금 지형의 자리: " + known
                           + " · 자리에 붙이려면 그 자리 id 와 같은 이름을 쓰거나(현행 규칙) "
                             "대장에 할당을 걸어라(4단계) · 누적 " + std::to_string(mod_unbound));
            }
        }
                if (r.changed) bump_epoch("노드 " + n.devid + " 등록 결속");
    }

    // ── REQ-0203 3a: **노드를 하나로 훑는 길** ──────────────────────────────────
    // 🔴 **색인을 저장하지 않는다. 부를 때마다 만든다.**
    //   저장하면 `park`·`aux` 가 바뀔 때마다 갱신해야 하고, **한 곳만 빠뜨리면 색인이 낡는다** —
    //   그건 "노드가 사라진 것처럼 보이는" 결함이고 로그에도 안 남는다.
    //   **낡을 수 없는 구조가 갱신을 잘 하는 것보다 낫다.**
    // ⚠ 지금은 **아무도 안 쓴다**(자가검증만). 다음 단계에서 라우팅이 이걸 탄다 —
    //   **먼저 길을 놓고 그 다음에 차를 올린다.** 한 단계에 둘을 하면 거동 변화 0 을 못 보인다.
    std::vector<Node*> all_nodes() {
        std::vector<Node*> v;
        if (!park.devid.empty() || park.fd != BAD_SOCK) v.push_back(&park);
        for (std::map<std::string, AuxNode>::iterator it = aux.begin(); it != aux.end(); ++it)
            v.push_back(&it->second);
        return v;
    }

    // 🔑 **`kind` 첫 글자가 "명령을 받는가"를 답한다**(설계 §5 · `O` = 받는다).
    // 🔴 **실패 방향을 못 박는다: `O` 가 아니면 명령을 보내지 않는다.**
    //    알 수 없는 글자도 **명령 금지 쪽으로 떨어진다** — 모르는 장치에 명령을 보내는 것이
    //    안 보내는 것보다 위험하다. **모르는 `kind` 를 거절하지는 않는다**(거절하면 새 모듈 하나가
    //    옛 서버에서 노드 전체를 미등록으로 만든다).
    // ── 🔴 `S` 의 비트필드를 **한 정의로** 읽는다 (2026-08-19 · 원장 §8.23-(66))
    //
    // **왜 함수인가**: 같은 프레임에 비트필드가 셋 있는데(`occ`·`res`·`ovr`) **각자 해독하고 있었다.**
    // hex 전환 때 `occ` 만 고쳤고 나머지 둘은 10진 전제로 남았다 —
    // `res` 는 **조건이 영영 거짓이 되어 자가 치유가 죽었고**, `ovr` 은 잠복이었다.
    // 🔑 **전선 형식이 바뀌면 그 형식을 읽는 자리를 전부 세야 한다. 정의가 하나면 셀 필요가 없다.**
    //
    // ⚠ **폭으로 형식을 가르지 않는다.** `n=10` 이면 hex 폭 3, 10진 폭 10 이라 지금은 갈리지만
    //   `n=40` 이면 hex 폭도 10 이 되어 **두 형식이 같은 폭을 갖는다.** 그래서 판별자는 폭이 아니라
    //   **등록 여부**다: 등록됐으면 `n` 을 아니까 hex, 아니면 옛 10진(폭 10)으로만 받는다.
    //
    // 반환 true = 해독했다 / false = 못 읽었다(`out` 은 전부 0).
    // 🔴 **모르면 0 을 채우고 false 를 낸다 — 짐작해서 풀지 않는다.** 짐작한 값은 폭도 체크섬도
    //   통과하고 자리만 어긋난다(그게 `49c07f6` 이 고친 고장이다).
    // 🔴 **10 에서 자르지 않는다.** `occ` 의 비트 `>= 10` 은 **액추에이터 상태**다
    //   (arduino REQ-0228 답변 · 명세 §5 "위험 다섯째"가 이미 그렇게 정했다):
    //     비트 0..9   = 주차 자리 점유 (`kind` 가 `I*`)
    //     비트 10..   = "지금 열려 있나" 같은 **출력 모듈의 현재 상태** (`kind` 가 `O*`)
    //   ⚠ **같은 비트열인데 의미가 다르고, 그 구분은 `kind` 에 있다.**
    //   🔑 그래서 이 값은 "자리 점유"가 아니라 **"모듈 상태"** 다. 10 에서 자르면
    //      **조작 완료를 판정할 값이 조용히 버려진다** — 화면은 영영 "진행 중"에 머문다.
    //
    // `out` 은 `REG_MODS_MAX` 칸. 반환 = 해독한 비트 수(0 = 못 읽음).
    int decode_mod_bits(const std::string& fld, int* out) const {
        for (int i = 0; i < REG_MODS_MAX; i++) out[i] = 0;
        if (park.reg_done && park.reg_n > 0) {
            const int n = park.reg_n;
            // ⚠ `REG_MODS_MAX` 가 32 라 `unsigned long`(32비트 보장)로는 아슬아슬하다.
            //   `strtoull` 로 받는다 — **폭이 상한에 닿아도 값이 안 잘린다.**
            unsigned long long v = strtoull(fld.c_str(), NULL, 16);
            for (int i = 0; i < n && i < REG_MODS_MAX; i++)
                out[i] = ((v >> (n - 1 - i)) & 1ULL) ? 1 : 0;
            return n;
        }
        if (fld.size() == 10) {                  // 미등록 + 폭 10 → 옛 10진 펌웨어(하위호환)
            for (int i = 0; i < 10; i++) out[i] = (fld[i] == '1') ? 1 : 0;
            return 10;
        }
        return 0;
    }
    static bool kind_commandable(const std::string& k) { return !k.empty() && k[0] == 'O'; }
    // ⚠ `atoi` 는 숫자가 아니면 **조용히 0 을 준다.** `D,*,IP,…`(이름이 `*` 인 모듈)이
    //   `drain=0` 으로 통과하면 유도식이 0 을 먹는다. **형식 검사를 값 변환 앞에 둔다.**
    static bool all_digits(const std::string& x) {
        if (x.empty() || x.size() > 5) return false;
        for (size_t i = 0; i < x.size(); i++) if (x[i] < '0' || x[i] > '9') return false;
        return true;
    }
    int reg_cmdable() const {
        int n = 0;
        for (size_t i = 0; i < park.mods.size(); i++)
            if (kind_commandable(park.mods[i].second)) n++;
        return n;
    }

    // id 미상 소켓 하나를 승격한다. 반환 false = 자리가 없어 거절했다(소켓은 닫힌다).
    bool promote_unknown(sock_t c, const std::string& dev) {
        // (0) 🔴 **잠금이 걸려 있으면 지정된 devid 만 주차 노드가 된다**(REQ-0217 ④).
        //     다른 devid 는 **거절이 아니라 보조 노드**로 들어간다 — 상행은 받되 하행은 안 준다.
        //     🔑 **그래야 그들이 로그에 보인다.** 끊어 버리면 다시 "안 보이게" 된다.
        if (!g_park_dev_pin.empty() && dev != g_park_dev_pin && park_dev.empty()) {
            logf("!", "주차 노드 잠금(" + g_park_dev_pin + ") — device=" + dev
                      + " (" + peer_str(c) + ") 는 보조 노드로 받는다. **하행 없음**");
        }
        // (1) 주차 노드가 아직 없다 → first-S-wins 로 이 장치가 주차 노드다
        // (2) 같은 device_id 의 재접속 → 자리를 물려받는다(옛 동작을 이 경우로 한정한 것)
        const bool pin_ok = g_park_dev_pin.empty() || dev == g_park_dev_pin;
        if ((park_dev.empty() && pin_ok) || park_dev == dev) {
            if (park_dev.empty())
                logf("=", "주차 노드 지정 — device=" + dev
                          + " (first-S-wins: 첫 S 프레임을 보낸 장치가 주차 노드다)");
            if (park_dev.empty()) warn_example_devid(dev);   // 🔑 **처음 아는 자리**에서 본다
            // 🔴🔴 **동시 접속 감지 — 1차 판별자는 시간이 아니라 IP 다** (REQ-0217)
            // 종전 규칙은 *"같은 devid = 같은 장치"* 를 전제했다. **조원들의 동일 카피 보드가
            // 전부 `P1` 이라 그 전제가 깨졌고, 그래서 이 경로가 조용히 통과했다.**
            // ⚠ **"최근 프레임이 있으면 침입자"로 갈라선 안 된다** — 실측 반증이 상수 주석에 있다
            //   (정상 재접속 85건 중 공백 0·1·2초가 실재한다).
            if (ard != BAD_SOCK) {
                const std::string np = peer_str(c);
                const std::string oh = peer_host(ard_peer), nh = peer_host(np);
                const bool known = (!oh.empty() && oh != "?" && !nh.empty() && nh != "?");
                // 🔴 **판별자가 없으면 막지 않는다 — 실패 방향을 고른 것이다.**
                // 주소를 못 얻는 경우(비 IPv4 소켓·`getpeername` 실패)에 거절 쪽으로 넘어지면
                // **우리 보드의 정상 재접속이 영영 막힌다.** 반대 방향의 손해는 "종전과 같다"뿐이다.
                // ⚠ 이 갈래는 자가검증 ⑬-(가)가 잡아 준 것이다 — 처음엔 거절 쪽으로 넘어졌다.
                if (!known) {
                    logf("!", "⚠ 같은 device_id(" + dev + ") 재접속인데 **주소를 못 얻어 판별 불가**"
                              " (기존 '" + ard_peer + "' → 새 '" + np + "') — 종전대로 교체한다");
                    adopt_as_parking(c, dev);
                    return true;
                }
                if (oh == nh) {
                    // 같은 IP = 같은 장치의 TCP 재접속. **공백을 묻지 않는다.**
                    adopt_as_parking(c, dev);
                    return true;
                }
                const long long quiet = ard_seen ? (now_ms() - ard_last_ms) : TAKEOVER_GRACE_MS;
                if (quiet < TAKEOVER_GRACE_MS) {
                    dup_devid_reject++;
                    logf("!!", "🔴 같은 device_id(" + dev + ") · **다른 IP** — 기존 " + ard_peer
                               + " 이 " + std::to_string(quiet)
                               + "ms 전까지 프레임을 보내는 중인데 " + np
                               + " 이 자리를 요구했다. **두 대다. 거절한다.** 누적 "
                               + std::to_string(dup_devid_reject) + "회");
                    // **확실한 것을 버리고 불확실한 것을 얻지 않는다**(MAX_ARD_NODES 와 같은 원칙).
                    // 🔑 관측에서 중요한 것은 이기는 것이 아니라 **누가 잡았는지 아는 것**이다.
                    logf("!!", "⚠ 같은 망에 동일 devid 보드가 있다 — 이 시각 전후의 장치 지표를 "
                               "우리 보드의 것으로 읽지 마라");
                    closesock(c);
                    return false;
                }
                takeover_grace++;
                logf("!", "⚠ 같은 device_id(" + dev + ") · 다른 IP(" + ard_peer + " → " + np
                          + ") 인데 기존이 " + std::to_string(quiet) + "ms 조용하다 — 교체 허용. "
                          "**우리 보드의 IP 가 바뀐 것일 수도, 남의 보드가 죽은 자리를 가져간 것일 수도 있다.** "
                          "누적 " + std::to_string(takeover_grace) + "회");
            }
            adopt_as_parking(c, dev);
            return true;
        }
        // (3) 이미 아는 보조 노드의 재접속
        if (aux.count(dev)) { adopt_as_aux(c, dev); return true; }
        // (4) 새 장치 — 상한 확인
        size_t total = aux.size() + (ard != BAD_SOCK ? 1 : 0);
        if (total >= MAX_ARD_NODES) {
            admit_rejects++;
            logf("!", "노드 상한(" + std::to_string(MAX_ARD_NODES) + ") 초과 — device=" + dev
                      + " 거절. **살아 있는 노드를 쫓아내지 않는다.** 누적 "
                      + std::to_string(admit_rejects) + "회");
            closesock(c);
            return false;
        }
        adopt_as_aux(c, dev);
        return true;
    }
    static int slot_index(const std::string& s) {
        for (int i = 0; i < 10; i++) if (s == SLOT_ID[i]) return i;
        return -1;
    }
    // ── 두 이름공간 (rid 를 §4.1 에서 나눈 것과 같은 구조) ─────────────────────
    //   브라우저 ↔ 서버 · 서버 내부 저장 : **번호판 전체** (UTF-8, JSON 이라 자유)
    //   서버 → 아두이노 (전선 userid)    : ASCII 0*8 (§2.3) — 못 담으면 **빈 값**
    //
    // 전선에 번호판을 태우려고 valid_userid() 를 느슨하게 만들면 REQ-0023 이 막은 버그
    // (잘린 값이 체크섬과 함께 형식상 유효한 라인으로 나가는 것)가 되살아난다.
    // 아두이노는 userid 를 쓰지 않는다(§2.4 — 무시해도 되지만 필드는 있어야 한다).
    // 그래서 담을 수 없으면 그냥 비운다. 잃는 것이 없다.
    static std::string wire_userid(const std::string& browser_user) {
        return valid_userid(browser_user) ? browser_user : std::string();
    }
    // 브라우저 쪽 값은 UTF-8 이라 문자 집합을 강제하지 않는다. 다만 저장이 무한히 커지지
    // 않게 길이만 막고, 제어문자는 거른다(로그·JSON 오염 방지).
    static bool valid_browser_user(const std::string& u) {
        if (u.size() > MAX_PLATE_BYTES) return false;
        for (size_t i = 0; i < u.size(); i++)
            if ((unsigned char)u[i] < 0x20) return false;
        return true;
    }

    // 명세 §2.3 `userid ::= 0*8( ALPHA / DIGIT / "_" / "-" )`
    // 검증하지 않으면 snprintf 가 조용히 잘라서 **형식상 유효하지만 내용이 잘린 라인**이 나간다.
    // 메모리 안전 문제는 없지만(잘린다) 64B 상한(§2.1)을 넘거나 엉뚱한 user_id 가 기록되고,
    // 증상은 "가끔 예약이 안 된다"(재전송 3회 후 ack_timeout)로 보여 원인을 찾기 어렵다.
    static bool valid_userid(const std::string& u) {
        if (u.size() > 8) return false;
        for (size_t i = 0; i < u.size(); i++) {
            char c = u[i];
            bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '_' || c == '-';
            if (!ok) return false;
        }
        return true;
    }

    // 모든 **접속된** 소켓에 전송 타임아웃을 건다.
    // 이게 없으면 상대가 안 빼갈 때 send() 가 무한정 막히고, 단일 스레드라 **서버 전체가 선다.**
    // 실제로 그렇게 죽었다 — 로그도 오류도 없이 멈춰서 단서가 0 이었다.
    // select() 의 "읽기 준비"는 쓰기에 대해 아무것도 보장하지 않는다는 점이 핵심이다.
    static void set_send_timeout(sock_t s) {
#ifdef _WIN32
        DWORD ms = (DWORD)SEND_TIMEOUT_MS;                  // 윈도우는 밀리초 DWORD
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&ms, sizeof(ms));
#else
        struct timeval tv;                                   // POSIX 는 timeval
        tv.tv_sec  = SEND_TIMEOUT_MS / 1000;
        tv.tv_usec = (SEND_TIMEOUT_MS % 1000) * 1000;
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
#endif
    }

