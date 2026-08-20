// listen.h — 소켓 준비 — 세 포트 바인드·리슨·기동 로그 (openPorts). `struct Server` 의 몸통 조각.
    // ═══════════════════════════════════════════════════════════════════
    // 🔴 **단독으로 컴파일되지 않는다.** `struct Server` 의 **몸통 조각**이고
    //   `server.cpp` 안 그 자리에 include 된다. **위치가 곧 문법이다.**
    // 🔑 옮긴 것이지 고친 것이 아니다 — 전처리 결과가 같아 **`.o` 가 바이트 동일**해야 한다.
    //   대조가 0 이 아니면 이동이 아니라 재배치다. 되돌리고 보고한다(REQ-0272).
    // ═══════════════════════════════════════════════════════════════════

    // ---------- 소켓 준비
    sock_t listen_on(int port) {
        sock_t s = socket(AF_INET, SOCK_STREAM, 0);
        if (s == BAD_SOCK) return BAD_SOCK;
        int opt = 1;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
        sockaddr_in a; memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET; a.sin_addr.s_addr = INADDR_ANY; a.sin_port = htons((u_short)port);
        if (bind(s, (sockaddr*)&a, sizeof(a)) != 0) {
            std::cerr << "바인드 실패 포트 " << port << " (err " << sockerr() << ")\n";
            closesock(s); return BAD_SOCK;
        }
        if (listen(s, 8) != 0) { closesock(s); return BAD_SOCK; }
        return s;
    }

    // ── 🔴 `run()` 을 셋으로 갈랐다 (REQ-0272 1단계 · 2026-08-19)
    //   호출자가 **바꿀 수 있는 것**(언제 열지 · 어떻게 돌지)을 밖으로 낸다.
    //   한 박자 **안**의 순서는 못 바꾸므로 안에 남는다 — 우리 판별자 그대로다.
    //   ⚠ 기계적 분할이다. 루프 몸통에 최상위 `continue`/`break`/`return` 이 **하나도 없는 것**을
    //     먼저 확인하고 옮겼다 — 있으면 의미가 조용히 바뀐다.
    // 🔑 **`offset=` 을 대신한다** — monitor 가 보던 것은 *"이게 시험 인스턴스인가"* 였다.
    //   포트를 손으로 지정했는지로 그것을 답한다.
    static bool ports_are_default() {
        return g_port_ardu == PORT_ARDUINO && g_port_web == PORT_HTTP && g_port_cam == PORT_PHONE;
    }
    bool openPorts() {
        lsn_ard   = listen_on(g_port_ardu);
        lsn_http  = listen_on(g_port_web);
        lsn_phone = listen_on(g_port_cam);
        // 🔴 포트를 못 잡았으면 **그 사실을 로그에 남기고** 죽는다.
        // 08-16 에 `프레임 0` 짜리 짧은 인스턴스가 여럿 있었는데, 관측자가 그것이
        // "장치가 안 붙은 것"인지 "포트를 못 잡은 것"인지 **가를 수 없어서** 한참 헤맸다.
        // 0 이 "나쁨"인지 "해당 없음"인지 가르는 것 — 그게 관측의 절반이다.
        if (lsn_ard == BAD_SOCK || lsn_http == BAD_SOCK || lsn_phone == BAD_SOCK) {
            // 🔴 dev 는 기계용 경계 줄을 안 찍는다. **왜 못 떴는지만** 사람 말로 말한다.
            std::cout << "\n🔴 포트를 못 잡았다. 이미 누가 쓰고 있거나 권한이 없다.\n"
                      << "   웹 " << g_port_web << " · 아두이노 " << g_port_ardu
                      << " · 카메라 " << g_port_cam << "\n"
                      << "   확인 : lsof -nP -iTCP:" << g_port_web << " -sTCP:LISTEN\n"
                      << "   다른 포트로 : ./srv --port-web=<값> --port-ardu=<값> --port-cam=<값>\n\n";
            return 1;
        }

        // 🔴 **dev 는 기계용 경계 줄(`=== INSTANCE … logfmt=`)을 안 찍는다.**
        //   그건 monitor 가 한 파일에 쌓인 인스턴스를 자르려고 읽는 줄이다.
        //   개발자는 터미널 하나를 보고 있고 **자를 것이 없다.**
        if (!ports_are_default())
            std::cout << "  ⚠ 포트를 손으로 지정했다 (dev 기본: 웹 " << PORT_HTTP
                      << " · 아두이노 " << PORT_ARDUINO << " · 카메라 " << PORT_PHONE << ")\n";
        std::cout << "\n  개발용 주차 서버  (pid " << cur_pid() << ")\n"
                  << "  화면 http://127.0.0.1:" << g_port_web << "/\n"
                  << "  아두이노 TCP " << g_port_ardu << "  ·  카메라 " << g_port_cam << "\n"
                  << "  ─────────────────────────────────────────\n";
        std::cout.flush();
        ensure_log_exists();

        // 소크 관측(REQ-0065) — 기동 시각과 "아직 링크 없음" 상태를 장부에 연다
        soak_start_ms = now_ms();
        last_report_ms = soak_start_ms;
        link_down_since = soak_start_ms;
        // 🔑 **서버가 자기 계약값을 기동 시 찍는다** (web 지적 2026-08-18)
        // web: *"서버 값 변화를 잡을 수 있는 검사가 팀에 없다"* — 하니스가 값을 주입하고
        // 같은 값으로 단언하므로 **서버가 바뀌어도 빨간불이 안 된다.** 그렇다고 특정 값을 박으면
        // **고친 쪽이 벌을 받는 검사**가 된다.
        // → **검사로 막지 말고 관측 가능하게 만든다.** 그러면 "지금 서버가 보내는 값"이
        //   남의 원장 사본이 아니라 **로그에 남는 사실**이 된다.
        {
            // 🔴 운영판은 여기서 **계약값 덤프 두 줄(289자·188자)** 을 찍는다.
            //   arduino·monitor 가 계산에 쓰는 값이다. **개발자는 안 쓴다.**
            std::cout << "  묶음 상한 " << max_per_batch() << "건  ·  ACK 대기 "
                      << (ACK_TIMEOUT_MS / 1000.0) << "초 × " << ACK_MAX_TRIES << "회\n";
            init_srv_id();
            // 🔴 **여기서 부르지 않으면 자리가 비어 있고 `map` 이 빈 배열로 나간다.**
            //   첫 배포에서 실제로 그랬다 — **자가검증에서만 부르고 있었다.**
            //   ⚠ 자가검증이 스스로 지형을 만들어 쓰기 때문에 **시험은 전부 통과한다.**
            //   **"시험 경로 ≠ 실기 경로"의 가장 조용한 형태다.**
            build_default_zones();
            if (false) logf("=", "서버 인스턴스 id — " + srv_id
                      + " (🔑 `epoch` 는 **이 id 안에서만** 단조다. id 가 바뀌면 판을 비교하지 마라)");
        }
        // 🔴 소크 관측(60초 요약)은 **운영 관측**이다. 콘솔에서는 잡음이라 안 켠다.

        return true;
    }

    void closeDown() {

        // ---------- 소크 종료 요약 (REQ-0065) — 한 줄로 끝난다
        if (sess_start_ms) end_ard_session("서버 종료");
        // 🔴 소크 총평(1,329자)은 **운영 관측**이다. 콘솔에서 못 읽는다.
        std::cout << "\n  서버를 닫는다 — 장치 세션 " << ard_sessions
                  << "회 · 프레임 " << sess_frames << "\n";

        // ---------- 인스턴스 종료 경계 (REQ-0111 로그 계약 §2.3)
        // ⚠ **이 줄이 없다고 "아직 살아 있다"로 읽으면 안 된다.** SIGKILL·정전·패닉은
        // 이 줄을 남길 기회를 주지 않는다. 실제로 08-16 에 pid 36998 이 이 줄 없이 죽었다.
        // **생존 판정은 로그가 아니라 pid 로 해야 한다.** 이 줄은 "정상 종료였다"만 증명한다.
        // 누계를 이 줄에 싣는다 — **로그 뒤쪽이 잘려도 이 한 줄로 인스턴스 총계가 복원된다.**
        // 🔴 dev 는 기계용 종료 줄을 안 찍는다.
    }
