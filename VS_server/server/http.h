// http.h — HTTP 요청·WebSocket 업그레이드·정적 파일 서빙. `struct Server` 의 몸통 조각.
    // ═══════════════════════════════════════════════════════════════════
    // 🔴 **단독으로 컴파일되지 않는다.** `struct Server` 의 **몸통 조각**이고
    //   `server.cpp` 안 그 자리에 include 된다. **위치가 곧 문법이다.**
    // 🔑 옮긴 것이지 고친 것이 아니다 — 전처리 결과가 같아 **`.o` 가 바이트 동일**해야 한다.
    //   대조가 0 이 아니면 이동이 아니라 재배치다. 되돌리고 보고한다(REQ-0272).
    // ═══════════════════════════════════════════════════════════════════

    // ---------- HTTP / WS 업그레이드
    static std::string header(const std::string& req, const char* name) {
        std::string low, ln = name;
        for (size_t i = 0; i < req.size(); i++) low += (char)tolower((unsigned char)req[i]);
        for (size_t i = 0; i < ln.size(); i++) ln[i] = (char)tolower((unsigned char)ln[i]);
        size_t i = low.find("\n" + ln + ":");
        if (i == std::string::npos) return "";
        i = req.find(':', i + 1) + 1;
        size_t e = req.find('\r', i);
        if (e == std::string::npos) e = req.find('\n', i);
        std::string v = req.substr(i, e - i);
        while (!v.empty() && (v[0]==' '||v[0]=='\t')) v.erase(0,1);
        while (!v.empty() && (v[v.size()-1]==' '||v[v.size()-1]=='\r')) v.erase(v.size()-1);
        return v;
    }
    void serve_file(sock_t fd, std::string path) {
        // 🔴 **쿼리를 가장 먼저 떼어낸다. 이 순서가 이 함수의 요점이다.**
        // 전에는 `"/"` 판정이 앞에 있고 쿼리 제거가 뒤에 있어서 `GET /?demo=1` 이 404 였다:
        //   `/?demo=1` 은 `"/"` 와 다르므로 index.html 로 **안 바뀌고**, 그 뒤 `?` 앞을 자르면
        //   `fn` 이 **빈 문자열**이 되어 열기에 실패했다.
        //   (web-engineer 가 크롬으로 실측 · REQ-0132. `/index.html?x` 는 200 인데 `/?x` 만 404 였다)
        // **쿼리는 경로가 아니다.** 경로에 관한 어떤 판정보다도 앞에서 떼는 것이 맞고,
        // 그래야 `/?x` · `/index.html?x` · `/data_log.json?t=…` 가 **한 규칙**으로 처리된다.
        size_t q = path.find('?');
        if (q != std::string::npos) path = path.substr(0, q);
        if (path.empty() || path == "/") path = "/index.html";
        // 경로 탈출 차단 — 데모여도 디렉터리를 서빙하는 코드에 이건 기본이다
        if (path.find("..") != std::string::npos || path.find('\\') != std::string::npos) {
            const char* r = "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send_raw(fd, r, strlen(r), "HTTP 클라이언트");
            return;
        }
        std::string fn = path.substr(1);

        std::ifstream f(fn.c_str(), std::ios::binary);
        if (!f) {
            const char* r = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send_raw(fd, r, strlen(r), "HTTP 클라이언트");
            return;
        }
        std::ostringstream ss; ss << f.rdbuf();
        std::string body = ss.str();

        // ── 🔴 **무엇을 내줬는지 로그에 남긴다** (REQ-0248 · 2026-08-19)
        //
        //   2026-08-19 에 `:9900` 이 **08-17 판 화면을 이틀간** 내줬다. 서버는 아무 잘못이 없었고
        //   로그에도 흔적이 없었다 — **`serve_file()` 이 cwd 상대경로로 열기 때문**이다.
        //   ⚠ `ls` 도 `curl 200` 도 둘 다 통과했다. **존재형 검사는 유물을 통과시킨다.**
        //   → 화면이 자기 판본을 `<meta name="screen-build">` 로 싣기로 했으므로(web) **그것을 찍는다.**
        //   🔑 **한 번만 찍는다** — 요청마다 찍으면 로그가 그것으로 덮이고, 판본은 파일이 바뀔 때만 바뀐다.
        if (fn.size() > 5 && fn.substr(fn.size()-5) == ".html") {
            size_t mp = body.find("name=\"screen-build\"");
            std::string tag = "(표지 없음)";
            if (mp != std::string::npos) {
                size_t cs = body.find("content=\"", mp);
                if (cs != std::string::npos) {
                    cs += 9;
                    size_t ce = body.find('"', cs);
                    if (ce != std::string::npos) tag = body.substr(cs, ce - cs);
                }
            }
            if (tag != last_screen_build_) {
                last_screen_build_ = tag;
                logf("=", "화면 판본 — " + tag + " · " + std::to_string(body.size()) + "B · "
                          + abs_path(fn));
            }
        }

        const char* ct = "application/octet-stream";
        if (fn.size() > 5 && fn.substr(fn.size()-5) == ".html") ct = "text/html; charset=utf-8";
        else if (fn.size() > 5 && fn.substr(fn.size()-5) == ".json") ct = "application/json; charset=utf-8";
        else if (fn.size() > 3 && fn.substr(fn.size()-3) == ".js") ct = "application/javascript";
        else if (fn.size() > 4 && fn.substr(fn.size()-4) == ".css") ct = "text/css";

        std::ostringstream h;
        h << "HTTP/1.1 200 OK\r\nContent-Type: " << ct
          << "\r\nContent-Length: " << body.size()
          << "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
        std::string head = h.str();
        send_raw(fd, head.data(), head.size(), "HTTP 클라이언트");
        send_raw(fd, body.data(), body.size(), "HTTP 클라이언트");
    }
    // 반환 false = 이 연결을 닫아라. **여기서 직접 erase 하지 않는다** —
    // 호출부가 conns 를 순회 중이라 안에서 지우면 반복자가 무효화된다(실제로 SIGSEGV 를 냈다).
    bool on_http(sock_t fd, Conn& c) {
        size_t end = c.inbuf.find("\r\n\r\n");
        if (end == std::string::npos) return true;
        std::string req = c.inbuf.substr(0, end);
        c.inbuf.erase(0, end + 4);

        size_t sp1 = req.find(' ');
        size_t sp2 = req.find(' ', sp1 + 1);
        std::string path = (sp1 != std::string::npos && sp2 != std::string::npos)
                         ? req.substr(sp1 + 1, sp2 - sp1 - 1) : "/";

        std::string key = header(req, "Sec-WebSocket-Key");
        if (!key.empty() && path.substr(0, 3) == "/ws") {
            std::string acc = ws_accept(key);
            std::string r = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                            "Connection: Upgrade\r\nSec-WebSocket-Accept: " + acc + "\r\n\r\n";
            send_raw(fd, r.data(), r.size(), "WS 업그레이드");
            c.kind = Conn::WS;
            // 🔑 최대치는 **승격되는 자리**에서 갱신한다. 요약을 만드는 함수는 `const` 이고
            //   **읽는 함수가 상태를 바꾸면 안 된다**(그래서 `mutable` 을 안 썼다).
            {
                int ws_n = 0;
                for (std::map<sock_t, Conn>::const_iterator it2 = conns.begin();
                     it2 != conns.end(); ++it2)
                    if (it2->second.kind == Conn::WS) ws_n++;
                if (ws_n > ws_peak) ws_peak = ws_n;
            }
            logf("+WS", "업그레이드 완료");
            ws_send(fd, snapshot_json());          // 접속 즉시 현재 상태(옛 봉투)
            // 🔴 **접속 즉시 새 봉투도 보낸다**(REQ-0203 4b/4c · web 실기 대조가 이 구멍을 찾았다).
            //   전에는 `state` 가 `push_snapshot()` 안에서만 방송돼서 **장치가 스냅샷을 밀 때까지
            //   새 클라이언트가 못 받았고, 장치가 없으면 영영 못 받았다.**
            //   ⚠ **화면에 우회가 없다** — `get_map` 은 있는데 `get_state` 는 없다.
            // 🔑 **`map` 을 먼저, `state` 를 뒤에 보낸다.** 지형이 먼저 서면 화면이 상태를 바로 그린다 —
            //   반대 순서면 화면이 `state` 를 들고 지형을 기다려야 하고, **그 대기 규칙이 화면에 생긴다.**
            //   **순서를 보내는 쪽이 지키면 받는 쪽에 규칙이 안 생긴다.**
            ws_send(fd, map_json());
            ws_send(fd, state_json());
            return true;
        }
        serve_file(fd, path);
        return false;                              // 정적 응답은 Connection: close
    }
    // WS 프레임 파싱 — 클라이언트 프레임은 **항상 마스킹**돼 있다(§5.2)
    bool ws_pump(sock_t fd, Conn& c) {
        while (true) {
            if (c.inbuf.size() < 2) return true;
            const unsigned char* p = (const unsigned char*)c.inbuf.data();
            int opcode = p[0] & 0x0F;
            bool masked = (p[1] & 0x80) != 0;
            uint64_t len = p[1] & 0x7F;
            size_t off = 2;
            if (len == 126) {
                if (c.inbuf.size() < 4) return true;
                len = (uint64_t(p[2]) << 8) | p[3]; off = 4;
            } else if (len == 127) {
                if (c.inbuf.size() < 10) return true;
                len = 0;
                for (int i = 0; i < 8; i++) len = (len << 8) | p[2+i];
                off = 10;
            }
            if (!masked) { logf("!", "마스킹 안 된 클라이언트 프레임 — 연결 종료"); return false; }
            // 선언된 길이를 그대로 믿으면 inbuf 가 무한히 커진다. 브라우저→서버 메시지는
            // 예약/취소 JSON 뿐이라 수백 바이트면 충분하다.
            if (len > WS_MAX_FRAME) {
                logf("!", "WS 프레임 길이 " + std::to_string((unsigned long long)len)
                          + " 바이트 — 상한 초과, 연결 종료");
                return false;
            }
            if (c.inbuf.size() < off + 4 + len) return true;
            unsigned char mask[4];
            for (int i = 0; i < 4; i++) mask[i] = p[off + i];
            std::string payload;
            payload.reserve((size_t)len);
            for (uint64_t i = 0; i < len; i++)
                payload += char(p[off + 4 + i] ^ mask[i % 4]);
            c.inbuf.erase(0, (size_t)(off + 4 + len));

            if (opcode == 0x8) return false;                       // close
            if (opcode == 0x9) {                                   // ping → pong
                // ping 페이로드는 125바이트 이하만 정상이다(제어 프레임 규칙). 넘으면 잘라 답한다.
                if (payload.size() > 125) payload.resize(125);
                std::string f; f += char(0x8A); f += char((unsigned char)payload.size()); f += payload;
                send_raw(fd, f.data(), f.size(), "WS pong");
                continue;
            }
            if (opcode == 0x1) on_ws_message(fd, payload);
        }
    }

    // ================= 폰(digitcam) 수신 · 번호판 매칭 =========================
    // 수신부는 net/test_server.py 에서 들어왔다 — 바이트 버퍼에 모으고 LF 로 자른 뒤
    // **그 다음에** 디코딩한다. 한글이 recv 경계에서 갈리기 때문이고, 그 파일이
    // 그 경우를 실측으로 검증해 뒀다(digitcam 명세 §8.3, §10.6).
    std::map<std::string, std::string> plate_slot;   // 번호판 → 배정된 자리

    int find_slot_by_user(const std::string& plate) const {
        for (int i = 0; i < 10; i++)
            if (slots[i].reserved && slots[i].user_id == plate) return i;
        return -1;
    }
    // 빈자리 선택 규칙: **§1 의 전선 인덱스 순서(A1..A5,B1..B5)에서 가장 앞.**
    // 근거 — 결정적이라 같은 입력이면 항상 같은 결과가 나오고(재현 가능한 시험),
    // 명세가 이미 정한 유일한 자리 순서라 새 순서를 발명하지 않는다.
    // 입구에서 가까운 순 같은 물리 배치 기준은 **지금 아무도 갖고 있지 않다** —
    // 배치도가 생기면 이 함수 하나만 바꾸면 된다.
    int pick_free_slot() const {
        for (int i = 0; i < 10; i++)
            if (!slots[i].occupied && !slots[i].reserved) return i;
        return -1;
    }

    void on_plate(const std::string& plate, const std::string& dev) {
        logf("←폰", "번호판 " + plate + " (device=" + dev + ")");

        // (1) 같은 번호판이 또 왔다 — 이미 배정된 자리가 살아 있으면 새 입차가 아니다.
        //     fresh 는 상승 엣지지만 인식이 끊겼다 다시 잡히면 새 엣지가 뜬다.
        //     **시간 창을 두지 않고 상태로 판정한다** — 타이머는 근거 없는 숫자를 만든다.
        std::map<std::string, std::string>::iterator it = plate_slot.find(plate);
        if (it != plate_slot.end()) {
            int i = slot_index(it->second);
            if (i >= 0 && slots[i].user_id == plate && (slots[i].reserved || slots[i].occupied)) {
                logf("=", "중복 수신 — " + plate + " 는 이미 " + it->second
                          + " 에 배정돼 있다. 새로 배정하지 않는다");
                return;
            }
            plate_slot.erase(it);   // 그 사이 은퇴/취소됐다 → 다시 배정 가능
        }

        // (2) 이미 주차된 차의 번호판이 게이트에 나타났다.
        //     오인식일 수도, 실제로 나갔다 다시 온 것일 수도 있다 — 서버는 구분할 수 없다.
        //     **새 자리를 배정하지 않는다.** 배정하면 같은 차가 두 칸을 먹는다.
        //     사람이 볼 수 있게 로그로 올린다.
        for (int i = 0; i < 10; i++) {
            if (slots[i].occupied && slots[i].user_id == plate) {
                logf("!", "이미 주차된 번호판이 게이트에서 인식됨 — " + plate + " (" + SLOT_ID[i]
                          + "). 오인식이거나 재진입이다. 새 배정 없음");
                return;
            }
        }

        // (3) 예약이 있으면 그 자리로 확정한다. 키는 번호판이다.
        int i = find_slot_by_user(plate);
        if (i >= 0) {
            plate_slot[plate] = SLOT_ID[i];
            logf("✓", "예약 확인 — " + plate + " → " + SLOT_ID[i] + " (안내 대상 자리)");
            push_snapshot();
            return;
        }

        // (4) 예약이 없으면 빈자리를 배정한다(사용자 요구: "안 하면 빈자리").
        int f = pick_free_slot();
        if (f < 0) {
            // 조용히 지나가지 않는다. 브라우저로 올리려면 새 WS 메시지가 필요한데
            // 그건 명세 변경이라 하지 않았다(REQ-0037 은 명세 무변경). 루트에 보고했다.
            logf("!", "빈자리 없음 — " + plate + " 배정 실패. 차단기를 열 자리가 없다");
            return;
        }
        if (!device_online()) {
            logf("!", "아두이노 미연결 — " + plate + " 배정 보류(예약을 내릴 수 없다)");
            return;
        }
        plate_slot[plate] = SLOT_ID[f];
        logf("✓", "예약 없음 → 빈자리 배정 " + plate + " → " + SLOT_ID[f]);
        dispatch('R', BAD_SOCK, "", SLOT_ID[f], plate);   // 예약의 주인은 서버다(§7.4)
    }

    void on_phone_line(const std::string& line) {
        // digitcam 명세 §5: plain 모드는 번호판 문자열 한 줄, json 모드는 오브젝트 하나.
        std::string plate, dev;
        if (!line.empty() && line[0] == '{') {
            plate = jget(line, "value");
            dev = jget(line, "device");
        } else {
            plate = line;
        }
        if (plate.empty()) {
            // §5.2 — 송신 측은 빈 값을 보내지 않지만 수신 측은 견딘다.
            logf("!", "폰: 빈 값 — 무시");
            return;
        }
        if (plate.size() > MAX_PLATE_BYTES) {
            logf("!", "폰: 번호판이 너무 길다(" + std::to_string(plate.size()) + "B) — 무시");
            return;
        }
        // **신뢰도 임계값을 두지 않는다.** 폰이 이미 min_confidence 로 걸렀고,
        // conf 는 문자별 최소값이라 문자 수가 늘수록 낮아진다(digitcam 명세 §4.4 경고).
        // 서버가 절대값으로 자르면 **긴 번호판만 유독 거절된다.**
        on_plate(plate, dev.empty() ? std::string("?") : dev);
    }

    // 기동 시 data_log.json 이 없으면 **빈 배열로 만들어 둔다.**
    //
    // 왜: 서버는 §9.4 대로 상태가 바뀔 때만 파일을 쓴다. 아두이노가 안 붙으면 한 번도 안 쓴다.
    // 그런데 화면은 WS 가 붙기 전부터 폴백 폴링을 돌리므로 **아무 문제 없는 첫 실행에서
    // 404 를 받아 빨간 오류를 띄운다.** 이런 것이 쌓이면 진짜 오류를 무시하게 된다.
    //
    // `[]` 는 §9.1 형태에서 "아직 기록이 없다"를 정직하게 표현한다 —
    // 없는 파일보다 낫고, 지어낸 값을 넣는 것보다도 낫다.
    //
    // **이미 있으면 절대 덮지 않는다.** 덮으면 재시작할 때마다 직전 2건이 날아간다.
    void ensure_log_exists() {
        if (no_disk) return;                 // 시험용 인스턴스는 파일을 만들지도 않는다
        std::ifstream f("data_log.json", std::ios::binary);
        if (f.good()) {
            logf("=", "data_log.json 이 이미 있다 — 그대로 둔다(직전 기록 보존)");
            return;
        }
        atomic_write_log("[]\n");     // 쓰기 경로는 원자적 교체 하나뿐이다
        logf("=", "data_log.json 이 없어 빈 배열로 만들었다");
    }

