// serve.h — 한 박자 — 수신·자리 판정·하행 송신·화면 방송 (serveOneTick). `struct Server` 의 몸통 조각.
    // ═══════════════════════════════════════════════════════════════════
    // 🔴 **단독으로 컴파일되지 않는다.** `struct Server` 의 **몸통 조각**이고
    //   `server.cpp` 안 그 자리에 include 된다. **위치가 곧 문법이다.**
    // 🔑 옮긴 것이지 고친 것이 아니다 — 전처리 결과가 같아 **`.o` 가 바이트 동일**해야 한다.
    //   대조가 0 이 아니면 이동이 아니라 재배치다. 되돌리고 보고한다(REQ-0272).
    // ═══════════════════════════════════════════════════════════════════

    // 한 박자 : 수신 → 자리 판정 → 하행 송신 → 화면 방송. **false 면 그만 돈다**
    //
    // 🔴 **여기에 코드를 더할 때 알아야 할 것** (REQ-0272 · 2026-08-19)
    //   이 본문은 원래 `while (!g_stop) { … }` 의 몸통이었다. 기계적으로 옮겼고,
    //   옮기기 전에 **최상위 `continue`/`break`/`return` 이 하나도 없는 것**을 확인했다.
    //   그 확인이 이 분할의 안전망 전부였다.
    //
    //   ✅ **그런데 옮긴 뒤로 둘은 *구조적으로* 막혔다** — 실험으로 확인했다:
    //      최상위 `continue;` → `error: 'continue' statement not in loop statement` (컴파일 실패)
    //      `break;` 도 같다. **루프가 아니므로 컴파일러가 잡는다.**
    //
    //   🔴 **남는 위험은 `return` 하나다.** 최상위에 `return false;` 를 쓰면 **컴파일된다.**
    //      그리고 그 뜻은 "이 박자를 건너뛴다"가 아니라 **"서버를 멈춘다"** 이다.
    //      ⚠ 옛 코드에서 `continue` 였을 자리에 무심코 `return` 을 쓰면 **서버가 조용히 죽는다.**
    //      → 이 박자를 건너뛰려면 **`return true;`** 다. 멈추는 것은 `g_stop` 이 정한다.
    //
    //   🔴 **`false` 를 내는 경로는 지금 *둘* 이고 둘 다 `g_stop` 이 정한다** (2026-08-19 실측):
    //      ① 들머리 `if (g_stop) return false;`   ② 끝의 `return !g_stop;`
    //      **`return true` 는 0곳이다** — 아직 아무도 박자를 건너뛰지 않는다.
    //      🔑 **이 수가 늘면 신호다.** 셋째 `false` 는 `g_stop` 이 아닌 이유로 서버를 멈춘다는 뜻이고,
    //        그건 **호출자가 모르는 정지 조건**이 생겼다는 것이다. 늘릴 거면 명세 §11.6 에 이유를 적어라.

    bool serveOneTick() {
        if (g_stop) return false;
            fd_set rd; FD_ZERO(&rd);
            sock_t mx = 0;
            FD_SET(lsn_ard, &rd);  if (lsn_ard  > mx) mx = lsn_ard;
            FD_SET(lsn_http, &rd); if (lsn_http > mx) mx = lsn_http;
            FD_SET(lsn_phone, &rd); if (lsn_phone > mx) mx = lsn_phone;
            for (std::map<sock_t, std::string>::iterator it = phones.begin(); it != phones.end(); ++it) {
                FD_SET(it->first, &rd);
                if (it->first > mx) mx = it->first;
            }
            if (ard != BAD_SOCK) { FD_SET(ard, &rd); if (ard > mx) mx = ard; }
            // ⚠ REQ-0083 — **여기에 하나라도 빠뜨리면 그 노드는 조용히 귀머거리가 된다.**
            // 오류도 로그도 없이 영영 readable 이 안 될 뿐이라, 방화벽 신원 사고와 같은 형태다.
            // 아래 둘(보조 노드·id 미상 소켓)은 select 대상에 **반드시** 들어가야 한다.
            for (std::map<std::string, AuxNode>::iterator it = aux.begin(); it != aux.end(); ++it) {
                if (it->second.fd == BAD_SOCK) continue;
                FD_SET(it->second.fd, &rd);
                if (it->second.fd > mx) mx = it->second.fd;
            }
            for (size_t k = 0; k < unknown.size(); k++) {
                if (unknown[k].fd == BAD_SOCK) continue;
                FD_SET(unknown[k].fd, &rd);
                if (unknown[k].fd > mx) mx = unknown[k].fd;
            }
            for (std::map<sock_t, Conn>::iterator it = conns.begin(); it != conns.end(); ++it) {
                FD_SET(it->first, &rd);
                if (it->first > mx) mx = it->first;
            }
            // 타이머(재전송·offline)를 돌려야 하므로 NULL 을 주면 안 된다.
            // 소켓이 조용해도 이 주기로 깨어나 tick() 을 돈다.
            timeval tv; tv.tv_sec = 0; tv.tv_usec = SELECT_TICK_MS * 1000;
            int n = select((int)mx + 1, &rd, NULL, NULL, &tv);

            if (n > 0) {
                if (FD_ISSET(lsn_ard, &rd)) {
                    sock_t c = accept(lsn_ard, NULL, NULL);
                    if (c != BAD_SOCK) {
                        // ⚠ REQ-0083 — **여기서 옛 소켓을 끊지 않는다. 그게 조원 배포의 차단 요인이었다.**
                        // 이 시점엔 device_id 를 모른다(첫 프레임을 받아야 안다). 그런데 옛 구조는
                        // 모르는 채로 기존 연결을 끊었고, 그래서 **노드 2대가 서로 밀어냈다.**
                        // 이제는 대기열에 넣고 **첫 유효 프레임에서 승격**한다(promote_unknown).
                        set_send_timeout(c);
                        std::string ka = set_keepalive(c);
                        // 상한 초과는 **거절**한다. 살아 있는 노드를 쫓아내지 않는 이유는
                        // MAX_ARD_NODES 주석에 있다(확실한 것을 버리고 불확실한 것을 얻지 않는다).
                        if (unknown.size() >= MAX_UNKNOWN_SOCKS) {
                            admit_rejects++;
                            logf("!", "id 미상 소켓이 상한(" + std::to_string(MAX_UNKNOWN_SOCKS)
                                      + ")에 찼다 — 새 연결 거절. 누적 " + std::to_string(admit_rejects) + "회");
                            closesock(c);
                        } else {
                            UnknownSock u; u.fd = c; u.since_ms = now_ms();
                            u.peer = peer_str(c);
                            unknown.push_back(u);
                            logf("+?", "연결 수락 — 상대 " + u.peer
                                       + " · device_id 대기 중(" + std::to_string(UNKNOWN_TIMEOUT_MS / 1000)
                                       + "초 안에 유효 프레임 없으면 끊는다) · " + ka);
                        }
                    }
                }
                if (FD_ISSET(lsn_http, &rd)) {
                    sock_t c = accept(lsn_http, NULL, NULL);
                    if (c != BAD_SOCK) { set_send_timeout(c); conns[c] = Conn(); }
                }
                if (FD_ISSET(lsn_phone, &rd)) {
                    sock_t c = accept(lsn_phone, NULL, NULL);
                    if (c != BAD_SOCK) {
                        set_send_timeout(c);
                        phones[c] = std::string(); logf("+폰", "digitcam 접속");
                    }
                }
                // 폰 연결들 — 순회 중 map 을 건드리지 않도록 fd 를 먼저 모은다
                {
                    std::vector<sock_t> pr, pdrop;
                    for (std::map<sock_t, std::string>::iterator it = phones.begin(); it != phones.end(); ++it)
                        if (FD_ISSET(it->first, &rd)) pr.push_back(it->first);
                    for (size_t k = 0; k < pr.size(); k++) {
                        sock_t fd = pr[k];
                        char b[2048];
                        int r = (int)recv(fd, b, sizeof(b), 0);
                        if (r <= 0) { pdrop.push_back(fd); continue; }
                        std::string& pb = phones[fd];
                        pb.append(b, r);
                        size_t i;
                        while ((i = pb.find('\n')) != std::string::npos) {
                            std::string line = pb.substr(0, i);
                            pb.erase(0, i + 1);
                            if (!line.empty() && line[line.size()-1] == '\r') line.erase(line.size()-1);
                            if (!line.empty()) on_phone_line(line);
                        }
                        if (pb.size() > MAX_PHONE_LINE) {
                            logf("!", "폰: LF 없이 상한 초과 — 버퍼 비움");
                            pb.clear();
                        }
                    }
                    for (size_t k = 0; k < pdrop.size(); k++) {
                        // LF 없이 남은 조각은 완성된 줄이 아니다 — 값으로 쓰지도, 조용히 버리지도 않는다
                        if (phones.count(pdrop[k]) && !phones[pdrop[k]].empty())
                            logf("!", "폰 해제 — 미종단 잔여 "
                                      + std::to_string(phones[pdrop[k]].size()) + "B 버림");
                        else logf("-폰", "digitcam 연결 종료");
                        closesock(pdrop[k]); phones.erase(pdrop[k]);
                    }
                }
                if (ard != BAD_SOCK && FD_ISSET(ard, &rd)) {
                    char b[2048];
                    int r = (int)recv(ard, b, sizeof(b), 0);
                    if (r <= 0) {
                        // ⚠ 이유를 **원인별로** 남긴다(REQ-0072). keepalive 가 죽인 소켓은
                        // recv 가 ETIMEDOUT 으로 돌아오는데, 그걸 "수신 오류" 한 문자열에
                        // 섞으면 keepalive 가 한 번이라도 일했는지 증명할 수 없다 —
                        // 켠 것과 안 켠 것이 로그에서 똑같아 보이는 계측은 계측이 아니다.
                        std::string why;
                        if (r == 0) why = "상대가 닫음";
                        else {
                            int e = sockerr();
                            if (err_is_timeout(e)) {
                                keepalive_reaps++;
                                why = "keepalive 시간초과(errno=" + std::to_string(e) + ")";
                            } else {
                                why = "수신 오류(errno=" + std::to_string(e) + ")";
                            }
                        }
                        end_ard_session(why);
                        closesock(ard); ard = BAD_SOCK; ard_buf.clear();
                        emit_dev(DEV_DISCONNECT, park_dev, why);   // 단계 C
                    } else {
                        ard_buf.append(b, r);
                        size_t i;
                        while ((i = ard_buf.find('\n')) != std::string::npos) {
                            std::string line = ard_buf.substr(0, i);
                            ard_buf.erase(0, i + 1);
                            if (!line.empty() && line[line.size()-1] == '\r') line.erase(line.size()-1);
                            // 🔑 **버릴 때도 길이를 남긴다.** 예전에는 "64B 초과" 라고만 적고 버려서
            // **몇 바이트였는지**를 못 봤다 — `AT+CIPSEND` 잘림 시험에서 그것이 측정값이다.
            if (line.size() + 1 > (size_t)MAX_LINE) {
                drop_overlong++;
                // 🔴 이름이 `b` 였다 — **바깥의 수신 버퍼 `b` 를 가리고 있었다**(`-Wshadow`).
                //   여기서 누가 `recv(fd, b, sizeof(b))` 를 쓰면 **96바이트짜리로 읽는다.**
                //   ⚠ 컴파일은 통과한다. **가려진 이름은 틀렸을 때 조용하다.**
                char msg[96];
                snprintf(msg, sizeof(msg), "상한(%dB) 초과 줄 — 버림 · **rx=%zuB**",
                         MAX_LINE, line.size());
                logf("!", msg);
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
                }

                // ---------- id 미상 소켓 (REQ-0083) — 첫 유효 프레임에서 승격
                {
                    std::vector<size_t> gone;
                    for (size_t k = 0; k < unknown.size(); k++) {
                        sock_t fd = unknown[k].fd;
                        if (fd == BAD_SOCK || !FD_ISSET(fd, &rd)) continue;
                        char b[1024];
                        int r = (int)recv(fd, b, sizeof(b), 0);
                        if (r <= 0) {
                            logf("-?", "id 미상 소켓이 승격 전에 끊겼다");
                            closesock(fd); unknown[k].fd = BAD_SOCK; gone.push_back(k);
                            continue;
                        }
                        unknown[k].buf.append(b, r);
                        std::string dev, rest;
                        bool found = false;
                        size_t i;
                        while (!found && (i = unknown[k].buf.find('\n')) != std::string::npos) {
                            std::string line = unknown[k].buf.substr(0, i);
                            unknown[k].buf.erase(0, i + 1);
                            if (!line.empty() && line[line.size()-1] == '\r') line.erase(line.size()-1);
                            if (peek_devid(line, dev)) {
                                found = true;
                                // **승격해도 이 프레임을 잃지 않는다.** 되돌려 놓고 넘긴다 —
                                // 첫 프레임은 §7.4 기준선을 세우는 줄이라 버리면 안 된다.
                                rest = line + "\n" + unknown[k].buf;
                            } else if (!line.empty()) {
                                // (REQ-0118 (F)) 여기서 버려지는 줄을 **이제 센다.**
                                // 전에는 아무 카운터도 안 올리고 사라져서, 이 소켓이 침묵했는지
                                // 말했는데 못 알아들었는지 구별할 방법이 없었다.
                                drop_prepromo++;
                            }
                        }
                        if (found) {
                            unknown[k].fd = BAD_SOCK; gone.push_back(k);
                            if (promote_unknown(fd, dev)) {
                                if (ard == fd)                      { ard_buf = rest; drain_ard_buf(); }
                                else if (aux.count(dev) && aux[dev].fd == fd) aux[dev].buf = rest;
                            }
                        } else if (unknown[k].buf.size() > (size_t)MAX_LINE * 4) {
                            // 잡음만 흘리는 소켓이 메모리를 먹지 않게 한다(§6.2)
                            // (REQ-0118 (F)) 통째로 버리는 것도 **말없이 하지 않는다.**
                            drop_prepromo_buf++;
                            logf("!", "승격 전 소켓 버퍼 상한 초과 — 통째로 비움("
                                      + std::to_string(unknown[k].buf.size()) + "B)");
                            unknown[k].buf.clear();
                        }
                    }
                    for (size_t j = gone.size(); j > 0; j--) unknown.erase(unknown.begin() + gone[j-1]);
                }

                // ---------- 보조 노드 (REQ-0083) — **상행 전용**
                {
                    std::vector<std::string> dead_devs;
                    for (std::map<std::string, AuxNode>::iterator it = aux.begin(); it != aux.end(); ++it) {
                        AuxNode& a = it->second;
                        if (a.fd == BAD_SOCK || !FD_ISSET(a.fd, &rd)) continue;
                        char b[1024];
                        int r = (int)recv(a.fd, b, sizeof(b), 0);
                        if (r <= 0) { dead_devs.push_back(it->first); continue; }
                        a.buf.append(b, r);
                        size_t i;
                        while ((i = a.buf.find('\n')) != std::string::npos) {
                            std::string line = a.buf.substr(0, i);
                            a.buf.erase(0, i + 1);
                            if (!line.empty() && line[line.size()-1] == '\r') line.erase(line.size()-1);
                            if (line.empty()) continue;
                            std::vector<std::string> f;
                            if (!verify_line(line, f)) { a.drops++; continue; }
                            a.frames++; a.last_ms = now_ms(); a.last_epoch_ms = epoch_ms();
                            // ── 🔴 ②-b — **보조 노드의 등록(`D`)을 파서에 넣는다** (REQ-0262/0263)
                            //
                            // 전에는 이 줄들이 **계수만 되고 버려졌다.** 그래서 보조 노드의 모듈은
                            // `map`/`state` 에 값이 없었고 `state_json()` 이 `known:false` 로 냈다.
                            //
                            // 🔴 **`D` 만 넣는다. `S` 는 아직 아니다.** 이유가 구조적이다:
                            //   `S` 분기는 `n.mod_bits`(노드별 ✅) 말고도 **`slots[]`·`base_occ`·
                            //   `resync_reservations`·하행 flush** 를 건드리는데 **그건 전부 서버 공유다.**
                            //   보조 노드의 `S` 를 넣으면 **그 노드의 점유가 주차 자리를 덮는다** —
                            //   ②-c 로 비트열은 갈랐지만 **자리는 아직 안 갈렸다.**
                            //   🔑 ①→② 에서 막았던 것과 같은 형태다: **값 경로가 없는데 값을 만들어 낸다.**
                            //   → `S` 는 효과를 이음매(`DEV_SENSORS`)로 뺀 뒤에(설계 §8.13.2) 넣는다.
                            //
                            // ⚠ `D` 는 안전하다 — 그 분기는 `n.mods`·`n.reg_*` 와 `bind_modules(n)` 만
                            //   건드리고 **전부 그 노드의 것**이다(②-a 로 인자화됐다).
                            if (f[0] == "D") { on_ard_line(a, line); continue; }
                            // 🔴 보조 노드가 `S` 를 보낸다 = **주차 노드가 둘이라는 뜻**이다.
                            // first-S-wins 가정이 깨지는 순간이므로 조용히 넘기지 않는다.
                            // (조용히 둘 중 하나를 고르면 "예약이 가끔 엉뚱한 데로 간다"가 되고,
                            //  그건 며칠 뒤에 원인을 못 찾는 종류의 사고다.)
                            if (f[0] == "S") {
                                aux_conflicts++;
                                if (aux_conflicts <= 3 || aux_conflicts % 200 == 0)
                                    logf("🔴", "역할 충돌 — 보조 노드 " + it->first
                                               + " 도 S 를 보낸다(주차 노드는 " + park_dev
                                               + "). **이 노드에는 하행을 주지 않는다.** 누적 "
                                               + std::to_string(aux_conflicts)
                                               + " · 전선에 역할 필드가 없어 생기는 한계다(명세 §2.4-D 참조)");
                            }
                        }
                        if (a.buf.size() > (size_t)MAX_LINE * 2) { a.drops++; a.buf.clear(); }
                    }
                    for (size_t k = 0; k < dead_devs.size(); k++) {
                        logf("-AUX", "보조 노드 " + dead_devs[k] + " 연결 종료 — 프레임 "
                                     + std::to_string(aux[dead_devs[k]].frames)
                                     + " · 버린줄 " + std::to_string(aux[dead_devs[k]].drops));
                        if (aux[dead_devs[k]].fd != BAD_SOCK) closesock(aux[dead_devs[k]].fd);
                        aux.erase(dead_devs[k]);
                    }
                }
                // 준비된 fd 를 먼저 모은 뒤 처리한다. 순회 중에 conns 를 건드리면
                // 반복자가 무효화된다 — 그대로 두면 SIGSEGV 다(실제로 겪었다).
                std::vector<sock_t> ready, drop;
                for (std::map<sock_t, Conn>::iterator it = conns.begin(); it != conns.end(); ++it)
                    if (FD_ISSET(it->first, &rd)) ready.push_back(it->first);

                for (size_t k = 0; k < ready.size(); k++) {
                    sock_t fd = ready[k];
                    if (!conns.count(fd)) continue;
                    Conn& c = conns[fd];               // map 의 참조는 erase 전까지 안정적이다
                    char b[4096];
                    int r = (int)recv(fd, b, sizeof(b), 0);
                    if (r <= 0) { drop.push_back(fd); continue; }
                    c.inbuf.append(b, r);
                    if (c.kind == Conn::HTTP) {
                        if (!on_http(fd, c)) { drop.push_back(fd); continue; }
                    }
                    if (c.kind == Conn::WS && !ws_pump(fd, c)) drop.push_back(fd);
                }
                for (size_t k = 0; k < drop.size(); k++) {
                    if (conns.count(drop[k])) { closesock(drop[k]); conns.erase(drop[k]); }
                }
            }

            reap_dead();                              // 전송 실패로 표시된 연결을 여기서 정리
            reap_nodes();                             // id 미상 마감 + 보조 노드 회수 (REQ-0083)
            tick();                                   // 소켓이 조용해도 매 주기 돈다

            // ---------- 좀비 아두이노 소켓 회수 — 유휴 마감 (REQ-0072)
            // 장치가 FIN 없이 죽으면 이 fd 는 영원히 안 돌아온다. keepalive 는 상대 커널이
            // 살아 ACK 를 보내면 **원리적으로 못 잡는다**(ESP 가 행에 걸린 바로 그 경우).
            // 그래서 앱이 직접 마감한다. 기준 시계는 ard_idle_base_ms() — 주석에 이유가 있다.
            if (ard != BAD_SOCK && sess_start_ms) {
                long long idle = now_ms() - ard_idle_base_ms();
                if (idle >= ARD_IDLE_CLOSE_MS) {
                    zombie_reaps++;
                    logf("✂", "아두이노 소켓 회수 — " + secs(idle) + " 무프레임(유휴 마감 "
                              + std::to_string(ARD_IDLE_CLOSE_MS / 1000) + "초) · 누적 "
                              + std::to_string(zombie_reaps) + "회");
                    // 닫는 순서는 recv 실패 경로(위)와 **똑같이** 간다. 새 정리 경로를 만들면
                    // 한쪽만 고쳐지는 장부가 생긴다.
                    end_ard_session("유휴 마감 " + std::to_string(ARD_IDLE_CLOSE_MS / 1000) + "초");
                    closesock(ard); ard = BAD_SOCK; ard_buf.clear();
                    emit_dev(DEV_DISCONNECT, park_dev,             // 단계 C
                             "유휴 마감 " + std::to_string(ARD_IDLE_CLOSE_MS / 1000) + "초");
                }
            }
            // ---------- 온·오프라인 **엣지** 판정 (§3.4)
            // ⚠ 옛 코드는 `was_online = device_online()` 을 루프 맨 위에서 재고 맨 아래에서
            // 다시 재 비교했다. 두 시점의 간격이 마이크로초라 **3.5초 경계는 언제나 두 반복
            // '사이'에서 넘어간다** — 즉 엣지가 잡히는 반복이 존재하지 않았다.
            // 그래서 "링크는 열려 있는데 프레임만 끊긴" 경우(ESP 가 조용히 멈춘 바로 그 경우)
            // 오프라인 판정이 **한 번도 뜨지 않았다.** 소켓이 닫힐 때만 떴고, 그때는
            // device_online() 이 ard==BAD_SOCK 때문에 뒤집힌 것이라 판정이 아니라 부작용이었다.
            // 피해는 로그만이 아니다 — 이 자리의 push_snapshot() 이 브라우저에 "센서 끊김"을
            // 알리는 유일한 경로인데, 장치가 조용하면 다른 이벤트도 없어서
            // **화면이 마지막 상태를 계속 진짜처럼 보여 준다.**
            // 상태를 멤버로 들고 비교하는 평범한 엣지 검출로 바꾼다. 복귀도 대칭으로 찍는다.
            bool now_online = device_online();
            if (now_online != ard_online) {
                ard_online = now_online;
                if (now_online) {
                    // ---------- 복구시간 확정 (REQ-0072)
                    // 복구시간 = **오프라인이 시작된 순간 → 다음 프레임이 실제로 도착한 순간.**
                    // 끝점을 now_ms() 가 아니라 `ard_last_ms`(그 프레임의 수신 시각)로 잡는다 —
                    // 엣지는 select 주기(200ms) 뒤에 잡히므로 now 로 재면 매번 그만큼 부풀려진다.
                    std::string extra;
                    if (offline_since_ms) {
                        long long r = ard_last_ms - offline_since_ms;
                        if (r < 0) r = 0;
                        // 같은 연결에서 되살아났나, 재연결해서 되살아났나 — **이 구분이
                        // 장치 쪽 복구 사다리(REQ-0071)가 듣는지를 판정한다.** 합쳐 세면
                        // "링크가 잠깐 조용했다" 와 "소켓을 새로 세워야 했다" 가 같아 보인다.
                        bool same = (ard_sessions == offline_at_session);
                        if (same) recov_same_conn++; else recov_reconn++;
                        if (r > recov_worst_ms) recov_worst_ms = r;
                        if (recov_ms.size() < RECOV_SAMPLE_MAX) recov_ms.push_back(r);
                        else recov_dropped++;             // 조용히 버리지 않는다 — 요약에 적힌다
                        extra = " — 복구 " + secs(r) + (same ? "(같은 연결)" : "(재연결)");
                        offline_since_ms = 0;
                    }
                    logf("=", "아두이노 온라인 복귀" + extra);
                } else {
                    offline_episodes++;                   // 소크 관측(REQ-0065)
                    // 오프라인이 **시작된** 시각을 잡는다. 엣지가 잡힌 시각이 아니다.
                    //   · 3.5초 무프레임으로 뒤집힌 경우 → 마지막 프레임 + OFFLINE_MS 가 그 순간
                    //   · 소켓이 닫혀 뒤집힌 경우       → 지금이 그 순간(미래 값을 쓰면 안 되므로 min)
                    long long t = now_ms();
                    long long off_at = ard_last_ms ? (ard_last_ms + OFFLINE_MS) : t;
                    offline_since_ms = (off_at < t) ? off_at : t;
                    offline_at_session = ard_sessions;
                    logf("!", "아두이노 오프라인 판정(" + std::to_string(OFFLINE_MS)
                              + "ms 무프레임) — 누적 " + std::to_string(offline_episodes) + "회");
                }
                // ⚠ **파일 쓰기를 여기서 반드시 해야 한다**(§9.4 개정 9). 다른 두 호출 지점은
                // 둘 다 on_ard_line 안이라 **장치가 조용하면 아예 실행되지 않는다.**
                // 이 줄이 없으면 `device.online` 은 키에 넣어도 영영 false 로 기록되지 못한다 —
                // 필드가 **가장 필요한 순간에** 거짓말을 한다.
                // 단계 C: 직접 호출 → 이벤트. **소비자가 같은 틱에 같은 일을 한다**(아래 drain).
                emit_dev(now_online ? DEV_ONLINE : DEV_OFFLINE, park_dev,
                         now_online ? "프레임 복귀" : "3.5초 무프레임");
            }

            // ---------- 이음매 소비 (REQ-0096 단계 C)
            // **같은 틱 안에서** 소비한다 — 디바이스가 이번 반복에 낸 이벤트는 이번 반복에
            // 도메인이 처리한다. 다음 틱으로 미루면 최대 200ms 가 밀리고, 그건 옮기기 전과
            // 다른 동작이다. 단계 C 는 구조만 바꾸고 동작은 안 바꾼다.
            drain_dev_events();

            // 주기 보고 — **조용한 로그와 죽은 서버를 구별할 수 있게** 한다.
            // 이 줄이 없으면 "2시간 동안 아무 일 없었다"와 "1분 만에 멈췄다"가 같은 모양이다.
            if (now_ms() - last_report_ms >= SOAK_REPORT_MS) {
                last_report_ms = now_ms();
                // 🔴 dev 는 60초 소크 요약(1,300자)을 **안 찍는다.** 운영 관측이다.
                //   콘솔에서 그 줄은 못 읽고, 개발자가 보려던 프레임 로그를 밀어낸다.
            }
        return !g_stop;
    }

