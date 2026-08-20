// metrics.h — 소크 관측(REQ-0065) · 복구 계측(REQ-0072) · 지표 문장. `struct Server` 의 몸통 조각.
    // ═══════════════════════════════════════════════════════════════════
    // 🔴 **단독으로 컴파일되지 않는다.** `struct Server` 의 **몸통 조각**이고
    //   `server.cpp` 안 그 자리에 include 된다. **위치가 곧 문법이다.**
    // 🔑 옮긴 것이지 고친 것이 아니다 — 전처리 결과가 같아 **`.o` 가 바이트 동일**해야 한다.
    //   대조가 0 이 아니면 이동이 아니라 재배치다. 되돌리고 보고한다(REQ-0272).
    // ═══════════════════════════════════════════════════════════════════

    // ---------- 소크 관측 (REQ-0065)
    // "2시간 안 끊겼다"는 **로그가 증명해야** 한다. 접속·종료 순간만 찍히면 그 사이의 침묵을
    // 아무도 증언하지 않는다 — 조용한 로그와 죽은 링크가 똑같이 보인다.
    // 핵심 지표는 **프레임 수신 간 최대 공백**이다. 평균은 링크가 반쯤 죽어도 예쁘게 나온다.
    long long soak_start_ms;      // 프로세스 기동(now_ms)
    int  ard_sessions;            // ARD 접속 횟수
    long long sess_start_ms;      // 현 세션 시작. 0 = 세션 없음
    long long sess_frames;        // 현 세션 수신 줄
    long long sess_last_line_ms;  // 현 세션 마지막 줄 수신 시각
    long long sess_max_gap_ms;    // 현 세션 최대 공백
    long long all_frames;         // 누적 수신 줄
    long long all_max_gap_ms;     // 누적 최대 공백 — **세션 경계는 넘지 않는다**(끊긴 시간은 공백이 아니다)
    long long all_max_gap_at;     // 그 공백이 끝난 시각(epoch_ms)
    long long link_down_ms;       // ARD 연결이 없던 누적 시간
    long long link_down_since;    // 0 = 연결돼 있음
    // 재부팅 감지를 **원인별로** 센다. §7.4 는 "새 연결이 1차 신호, uptime 추론은 2차 방어선"이라고
    // 정했는데, 그 주장이 실기에서 맞는지는 이 두 숫자의 비율로만 확인된다.
    // (불변식: reboot_by_conn + reboot_by_uptime == resync_count)
    int  reboot_by_conn;
    int  reboot_by_uptime;
    int  offline_episodes;        // 3.5초 무프레임 판정 횟수(§3.4)
    // ⚠ 아래 셋은 **모순 대조용**이다(죽은 탐지기 규칙 5). 상행만 세면
    // "조용한 링크"와 "시끄럽지만 전부 버려지는 링크"(AT 잡음 §6.2)가 요약에서 똑같아 보이고,
    // 상행만 보면 **하행이 통째로 죽어도 요약이 멀쩡하다**(실기에서 +IPD 0건이던 바로 그 경우).
    // 버린 줄은 **원인별로** 센다. 합만 세면 "전선에서 깨졌다"와 "장치가 프로토콜 아닌 것을
    // 흘린다"가 같은 숫자가 된다 — 진단이 정반대인데도. 특히 `모름` 이 오르면 AT 응답 같은
    // 비프로토콜 텍스트가 소켓에 새고 있다는 뜻이라 체크섬 손상과 완전히 다른 사건이다.
    long long drop_cksum;         // 체크섬 불일치 — 전선/장치에서 바이트가 깨졌다
    long long drop_overlong;      // 64B 초과 줄(§2.1)
    long long drop_unknown;       // 모르는 타입 문자 — 비프로토콜 텍스트 유입 의심
    long long drop_noise;         // LF 없이 64B 초과 → 버퍼 비움(잡음)
    // 승격 전(id 미상) 소켓에서 버린 줄 (REQ-0118 (F)).
    // **왜 필요한가**: 이 경로의 줄은 지금까지 **어느 카운터도 안 올리고 사라졌다.**
    // 그래서 "안 왔다"와 "왔는데 못 셌다"가 구별되지 않았다 — 08-16 에 `TX-RESYNC 4 : 버린줄 0`
    // 을 두고 팀이 판정을 못 내린 원인이 이것이다. 그리고 **마감된 소켓 56건이 전부 이 경로**라
    // 지금 그 소켓들이 무엇을 받았는지 아무도 모른다. 0 이 "해당 없음"인지 "못 셈"인지 가른다.
    long long drop_prepromo;      // 승격 전 소켓에서 버린 줄(체크섬 실패·비S프레임 포함)
    long long drop_prepromo_buf;  // 승격 전 소켓 버퍼가 상한을 넘겨 통째로 비운 횟수
    long long retx_count;         // 하행 재전송 횟수(§7.3)
    long long ack_fail_count;     // 하행 ACK 최종 실패 횟수
    bool ard_online;              // 온·오프라인 **엣지** 판정용 직전 상태(§3.4). 아래 루프 주석 참조
    long long last_report_ms;     // 주기 보고 시각

    // ---------- 복구 계측 (REQ-0072)
    // 왜 "최대공백"으로 부족한가 — 공백은 **얼마나 나빴나**만 말하고 **스스로 돌아왔나**를
    // 말하지 않는다. 사용자 요구가 "오류에서 정상으로 복구하는 구조"로 바뀌었으므로
    // 판정 지표도 바뀌어야 한다: **끊긴 뒤 몇 초 만에 저절로 프레임이 다시 왔는가.**
    // 이 숫자가 없으면 장치 쪽 복구 사다리(REQ-0071)가 듣는지를 사람이 로그를 눈으로 세어
    // 판정하게 된다 — 실제로 오늘 그렇게 했다.
    long long offline_since_ms;   // 오프라인이 **시작된** 시각(now_ms). 0 = 온라인
    int  offline_at_session;      // 그 순간의 ard_sessions — 같은 연결/재연결 복구를 가른다
    std::vector<long long> recov_ms;  // 복구시간 표본(중앙값용). RECOV_SAMPLE_MAX 에서 멈춘다
    long long recov_dropped;      // 상한을 넘겨 못 담은 표본 수 — 조용히 버리지 않는다
    int  recov_same_conn;         // 같은 TCP 연결에서 프레임이 되돌아왔다(링크가 살아 있었다)
    int  recov_reconn;            // 재연결한 뒤에야 되돌아왔다(사다리가 소켓을 다시 세웠다)
    long long recov_worst_ms;     // 최악 복구시간
    int  zombie_reaps;            // 유휴 마감으로 회수한 소켓 수 — **앱 경로**
    int  keepalive_reaps;         // ETIMEDOUT 으로 죽은 소켓 수 — **OS 경로**

    // ⚠ `srv_id` 는 **기동마다 달라야 한다.** 벽시계 + 소스 식별자를 쓴다 —
    //   시각만 쓰면 같은 초에 두 번 뜨면 겹치고, 소스 식별자만 쓰면 재기동이 구분 안 된다.
    void init_srv_id() {
        char b[96];
        snprintf(b, sizeof(b), "%s-%lld", BUILD_ID, (long long)epoch_ms());
        srv_id = b;
    }

    // 🔑 `park` 의 필드들은 **`Node` 의 생성자가 초기화한다.** 여기 목록에 다시 적지 않는다 —
    //   적으면 참조에 임시값을 묶으려는 것이 되어 컴파일이 안 된다(그게 이 설계의 안전장치다).
    Server() : grid_rows(5), grid_cols(5),   // 선언 순서(-Wreorder)
               getmap_win_ms(0), getmap_in_win(0), getmap_rejects(0),
               lsn_ard(BAD_SOCK), lsn_http(BAD_SOCK), lsn_phone(BAD_SOCK),
               reg_ok(0), reg_bad(0), reg_qsent(0), reg_giveups(0), reg_widthbad(0),
               occ_undecoded(0), occ_undecoded_warned(false),
               mod_order_changed(0),
               gate_q(0), gate_sent(0), gate_ans(0), dev_reject(0),
               heal_checks(0), heal_fires(0), res_undecoded(0), res_undecoded_warned(false),
               dup_devid_reject(0), takeover_grace(0),  // 선언 순서와 일치시킨다(-Wreorder)
               aux_conflicts(0), admit_rejects(0),
               // 🔴 이 여섯(+다섯)은 **선언만 돼 있고 초기화 목록에 없었다.** 쓰기 시작하는 순간
               // 쓰레기값이 지표로 나간다 — 원장이 통째로 경고하는 그 형태다. 여기서 닫는다.
               downq_bytes(0), batch_count(0), batch_lines(0),
               q_rejected(0), q_full_n(0), s_max_b(0), s_worst_warned(false),
           q_nodev(0), q_dup(0),
               q_deferred(0), q_dropped_link(0),
               rtt_max_ms(0), rtt_last_ms(0), rtt_n(0), win_skips(0), dmax_flushes(0),
               last_dmax_ms(0), dmax_armed(true),
               ard_uptime(-1), ard_seq(-1),
               ard_dev("?"),
               xs_uptime(-1), xs_last_ms(0), xs_dev(""),
               xs_reconnect_reboot(0), xs_reconnect_link(0),
               xs_reconnect_unknown(0),
               rid_cursor(1), rid_reserved_to(0), rid_persist_on(false),
               rid_rel_seq(0),
               rid_alloc_n(0), rid_skips(0), rid_forced(0), rid_exhausted(0),
               ack_unknown_rid(0), ack_slot_mismatch(0), mod_name_conflict(0), mod_dup_name(0), mod_unbound(0), mod_seen_(0), asm_warn_(0), batch_seq_(0), batch_deferred(0), devid_example_(0), sensor_split_now(0), not_reservable_n(0),
               base_valid(false), test_armed(false),
               cmd_cb_(0), cb_ok_(0), cb_rejected_(0), cb_noanswer_(0),
               ledger_new_(0), ledger_review_(0),
               resync_count(0), no_disk(false),
               soak_start_ms(0), ard_sessions(0), sess_start_ms(0), sess_frames(0),
               sess_last_line_ms(0), sess_max_gap_ms(0), all_frames(0), all_max_gap_ms(0),
               all_max_gap_at(0), link_down_ms(0), link_down_since(0),
               reboot_by_conn(0), reboot_by_uptime(0), offline_episodes(0),
               drop_cksum(0), drop_overlong(0), drop_unknown(0), drop_noise(0),
               drop_prepromo(0), drop_prepromo_buf(0),
               retx_count(0), ack_fail_count(0),                  // 선언 순서와 일치시킨다(-Wreorder)
               ard_online(false), last_report_ms(0),
               offline_since_ms(0), offline_at_session(0), recov_dropped(0),
               recov_same_conn(0), recov_reconn(0), recov_worst_ms(0),
               zombie_reaps(0), keepalive_reaps(0) {
        for (int i = 0; i < 10; i++) { base_occ[i] = 0; test_ovr[i] = 0; base_ovr[i] = 0; }
    }

    // ---------- 소크 관측 보조 (REQ-0065)
    static std::string hms(long long ms) {
        if (ms < 0) ms = 0;
        long long s = ms / 1000;
        char b[32];
        snprintf(b, sizeof(b), "%02lld:%02lld:%02lld", s / 3600, (s % 3600) / 60, s % 60);
        return std::string(b);
    }
    static std::string secs(long long ms) {
        char b[32];
        snprintf(b, sizeof(b), "%.1f초", ms / 1000.0);
        return std::string(b);
    }
    // 요약 안에서 "언제"를 가리키는 값(최대공백@… 등)도 **줄 앞머리와 같은 완전 형식**으로 쓴다.
    // v0.1 에서 연도를 뺐다가 monitor 지적으로 되돌렸다: "경계 줄이 연도를 확정해 준다"는
    // **파일을 처음부터 순서대로 읽을 때만** 참인데, 관측자는 5MB 를 통째로 못 올려서
    // **항상 잘라 읽는다.** 잘린 조각에서 `@08-16 16:52:47` 은 다시 연도가 없다.
    // 이 계약의 요지는 **부분 문자열만 봐도 절대시각이 확정되는 것**이므로 예외를 두지 않는다.
    // 비용은 요약이 분당 1줄이라 줄 앞머리의 1/60 이다.
    static std::string clock_at(long long ep_ms) {
        if (ep_ms <= 0) return std::string("-");
        time_t tt = (time_t)(ep_ms / 1000);
        char b[32]; strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", localtime(&tt));
        return std::string(b);
    }

    // ⚠ **아직 닫히지 않은 침묵도 공백이다.** 공백을 "줄과 줄 사이"로만 세면
    // 장치가 조용히 멈춘 바로 그 사고가 최대공백에 **안 잡힌다** — 다음 줄이 영영 안 오기 때문이다.
    // (실제로 겪었다: 22분간 무프레임이었는데 세션 최대공백이 3.3초로 찍혔다.)
    long long live_gap_ms() const {
        if (!sess_start_ms || !sess_last_line_ms) return 0;
        return now_ms() - sess_last_line_ms;
    }
    void fold_live_gap() {                       // 진행 중이던 침묵을 확정한다
        long long g = live_gap_ms();
        if (g > sess_max_gap_ms) sess_max_gap_ms = g;
        if (g > all_max_gap_ms) { all_max_gap_ms = g; all_max_gap_at = epoch_ms(); }
    }

    // ---------- 복구 계측 보조 (REQ-0072)
    // **평균이 아니라 중앙값을 쓴다.** 복구시간은 한쪽으로 길게 끌리는 분포다(대부분 몇 초,
    // 가끔 재연결로 수십 초). 평균은 그 꼬리 하나에 끌려가 "보통 얼마나 걸리나"를 못 말한다.
    // 최악값은 따로 낸다 — 중앙값과 최악을 **같이** 봐야 "대체로 빠른데 가끔 나쁘다"가 읽힌다.
    long long recov_median_ms() const {
        if (recov_ms.empty()) return -1;                  // -1 = 표본 없음(0 과 구별한다)
        std::vector<long long> v = recov_ms;              // 정렬이 원본을 흔들면 안 된다
        size_t mid = v.size() / 2;
        std::nth_element(v.begin(), v.begin() + mid, v.end());
        if (v.size() % 2) return v[mid];
        long long hi = v[mid];
        std::nth_element(v.begin(), v.begin() + (mid - 1), v.end());
        return (v[mid - 1] + hi) / 2;
    }

    // 아두이노 소켓의 유휴를 재는 기준 시각.
    // ⚠ **`ard_last_ms`(체크섬을 통과한 S 프레임)를 쓴다.** 이유는 ARD_IDLE_CLOSE_MS 주석에 있다 —
    // 모든 수신 줄(`sess_last_line_ms`)로 재면 잡음을 흘리는 장치가 영원히 회수되지 않는다.
    // `sess_start_ms` 와 큰 쪽을 쓰는 이유는 둘이다:
    //   · 붙기만 하고 **한 줄도 안 보내는** 연결도 회수 대상이다(그 자체로 누수 경로다).
    //   · `ard_last_ms` 는 세션 경계에서 리셋되지 않는다. 새 연결 직후 옛 값으로 재면
    //     **갓 붙은 정상 연결을 그 자리에서 끊는다.** sess_start_ms 가 그 사고를 막는다.
    long long ard_idle_base_ms() const {
        return (ard_last_ms > sess_start_ms) ? ard_last_ms : sess_start_ms;
    }

    // 아두이노 소켓에만 keepalive 를 건다. **모든 accept 에 일괄로 걸지 않는다** —
    // HTTP/WS/폰 연결의 수명 정책은 이 요청의 범위가 아니고, 조용히 바꿀 일도 아니다.
    //
    // 반환값은 **OS 가 실제로 받아들인 값을 되읽은 것**이다(요청한 값이 아니다).
    // setsockopt 는 조용히 무시되거나 값이 잘릴 수 있고, 그러면 "켰다고 믿는데 안 켜진"
    // 상태가 된다 — 로그에 요청값을 찍으면 그 거짓말을 그대로 기록하게 된다.
    // 되읽어 찍으면 켜졌는지 아닌지가 로그만 보고 판정된다.
    static std::string set_keepalive(sock_t s) {
        int on = 1;
        if (setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, (const char*)&on, sizeof(on)) != 0)
            return "keepalive 설정 실패(errno=" + std::to_string(sockerr()) + ")";
#ifdef _WIN32
        // ⚠ 윈도우는 SO_KEEPALIVE 만으로는 **기본 유휴가 2시간**이라 사실상 안 걸린다.
        // 세부 조정은 SIO_KEEPALIVE_VALS(mstcpip.h)가 필요한데 **이 기기에서 컴파일 검증을
        // 할 수 없어 넣지 않았다.** 그래서 윈도우에서는 OS 경로가 하중을 받지 못하고
        // 유휴 마감(ARD_IDLE_CLOSE_MS)이 **유일한** 회수 장치가 된다 — 명세에도 그렇게 적었다.
        // 안 되는 것을 된다고 적는 것보다, 못 한 것을 못 했다고 적는 편이 낫다.
        return "keepalive on(윈도우 기본 유휴 2시간 — 세부조정 없음, 유휴 마감이 주 장치)";
#else
        int idle = KEEPALIVE_IDLE_S, intvl = KEEPALIVE_INTVL_S, cnt = KEEPALIVE_CNT;
  #ifdef TCP_KEEPIDLE                        // 리눅스
        setsockopt(s, IPPROTO_TCP, TCP_KEEPIDLE, (const char*)&idle, sizeof(idle));
        const int IDLE_OPT = TCP_KEEPIDLE;
  #elif defined(TCP_KEEPALIVE)               // macOS/BSD — 같은 뜻의 다른 이름이다
        setsockopt(s, IPPROTO_TCP, TCP_KEEPALIVE, (const char*)&idle, sizeof(idle));
        const int IDLE_OPT = TCP_KEEPALIVE;
  #endif
  #ifdef TCP_KEEPINTVL
        setsockopt(s, IPPROTO_TCP, TCP_KEEPINTVL, (const char*)&intvl, sizeof(intvl));
  #endif
  #ifdef TCP_KEEPCNT
        setsockopt(s, IPPROTO_TCP, TCP_KEEPCNT, (const char*)&cnt, sizeof(cnt));
  #endif
        // ---- 되읽기. 여기서 찍는 숫자는 **커널이 들고 있는 값**이다.
        int ron = 0, ridle = -1, rintvl = -1, rcnt = -1;
        socklen_t sl = sizeof(int);
        getsockopt(s, SOL_SOCKET, SO_KEEPALIVE, (char*)&ron, &sl);
  #if defined(TCP_KEEPIDLE) || defined(TCP_KEEPALIVE)
        sl = sizeof(int); getsockopt(s, IPPROTO_TCP, IDLE_OPT, (char*)&ridle, &sl);
  #endif
  #ifdef TCP_KEEPINTVL
        sl = sizeof(int); getsockopt(s, IPPROTO_TCP, TCP_KEEPINTVL, (char*)&rintvl, &sl);
  #endif
  #ifdef TCP_KEEPCNT
        sl = sizeof(int); getsockopt(s, IPPROTO_TCP, TCP_KEEPCNT, (char*)&rcnt, &sl);
  #endif
        char b[160];
        snprintf(b, sizeof(b),
                 "keepalive %s · 유휴 %ds · 간격 %ds · 횟수 %d → 탐지 약 %ds (커널 되읽기)",
                 ron ? "on" : "**off**", ridle, rintvl, rcnt,
                 (ridle > 0 && rintvl > 0 && rcnt > 0) ? ridle + rintvl * rcnt : -1);
        return std::string(b);
#endif
    }

    // recv 가 -1 을 준 이유가 **keepalive 가 죽였기 때문인지**를 가른다.
    // 안 가르면 "수신 오류" 한 문자열에 전부 섞여 keepalive 가 한 번이라도 일했는지
    // 증명할 길이 없어진다 — 켜 놓고 안 켜진 것과 구별이 안 되는 계측은 계측이 아니다.
    static bool err_is_timeout(int e) {
#ifdef _WIN32
        return e == WSAETIMEDOUT;
#else
        return e == ETIMEDOUT;
#endif
    }

    // 한 줄로 소크 전체를 재구성할 수 있게 한다. 주기 보고와 종료 요약이 같은 문장을 쓴다.
    std::string soak_line() const {
        long long t = now_ms();
        long long gap_max = all_max_gap_ms;
        long long live = live_gap_ms();
        if (live > gap_max) gap_max = live;       // 진행 중인 침묵도 포함해서 보고한다
        long long down = link_down_ms + (link_down_since ? t - link_down_since : 0);
        std::string s = "소크 " + hms(t - soak_start_ms)
            + " · 세션 " + std::to_string(ard_sessions) + "회";
        if (sess_start_ms) s += "(현재 #" + std::to_string(ard_sessions)
                                + " 연결중 " + hms(t - sess_start_ms) + ")";
        else               s += "(현재 끊김)";
        s += " · 프레임 " + std::to_string(all_frames)
           + " · 최대공백 " + secs(gap_max) + (live > all_max_gap_ms ? "(진행중)" : "@" + clock_at(all_max_gap_at))
           + " · 재부팅감지 " + std::to_string(reboot_by_conn + reboot_by_uptime)
           + "(새연결 " + std::to_string(reboot_by_conn)
           + " / uptime추론 " + std::to_string(reboot_by_uptime) + ")"
           // ⚠ 위 `재부팅감지` 는 **새 연결을 전부 재부팅으로 센다.** 그게 과대집계라는 것이
           // 08-16 에 밝혀졌고(36건 중 35건이 링크 재접속), 아래가 실제로 갈라 놓은 값이다.
           // 위 필드는 옛 집계 도구 호환을 위해 **형태를 그대로 두었다** — 이름 정정은
           // 계약 변경이라 monitor 와 합의 후 별도 교체로 한다(REQ-0118 (A) 2단계).
           + " · 재연결내역(재부팅 " + std::to_string(xs_reconnect_reboot)
           + "/링크재접속 " + std::to_string(xs_reconnect_link)
           + "/미상 " + std::to_string(xs_reconnect_unknown) + ")"
           + " · 오프라인 " + std::to_string(offline_episodes) + "회"
           + " · 링크없음 " + hms(down)
           // 상행만 보면 하행이 통째로 죽어도 요약이 멀쩡하다. 네 숫자를 나란히 둔다.
           // 버린줄은 합과 원인을 함께 — 원인이 갈리면 진단이 갈린다(잡음 유입 vs 바이트 손상).
           + " · 버린줄 " + std::to_string(drop_cksum + drop_overlong + drop_unknown + drop_noise)
           + "(체크섬 " + std::to_string(drop_cksum)
           + "/과길이 " + std::to_string(drop_overlong)
           + "/모름 " + std::to_string(drop_unknown)
           + "/잡음 " + std::to_string(drop_noise) + ")"
           // 승격 전 소켓에서 버린 줄 — 위 4칸 어디에도 안 잡히던 것이다(REQ-0118 (F)).
           // **0 이면 "안 왔다"가 확정된다.** 전에는 0 이 "못 셌다"일 수도 있었다.
           + " · 승격전버림 " + std::to_string(drop_prepromo)
           + "(버퍼비움 " + std::to_string(drop_prepromo_buf) + ")"
           + " · 재전송 " + std::to_string(retx_count)
           + " · ACK실패 " + std::to_string(ack_fail_count)
           // ── 하행 슬롯 큐. **분모를 같이 찍는다** — `거절 0` 이 "건강"인지 "한 번도 안 돌았다"인지
           // 갈리려면 `배치` 가 옆에 있어야 한다(원장 §5.2). 배치 0 이면 하행이 아예 없었던 것이다.
           + " · 배치 " + std::to_string(batch_count)
           + "(줄 " + std::to_string(batch_lines)
           + "/미룬창 " + std::to_string(q_deferred)
           + "/창포기 " + std::to_string(dmax_flushes)
           + "/창놓침 " + std::to_string(win_skips) + ")"
           // 🔴 **네 칸을 갈라 둔다** — 합치면 `큐거절 3` 을 보고 cap 을 올리려 하는데
           // 실제로는 장치가 없었던 것일 수 있다. 원인이 갈리면 진단이 갈린다.
           + " · 큐넘침 " + std::to_string(q_rejected)
           + "(건수축 " + std::to_string(q_full_n)
           + "/장치없음 " + std::to_string(q_nodev)
           + "/중복 " + std::to_string(q_dup)
           + "/링크버림 " + std::to_string(q_dropped_link) + ")"
           // 🔑 **한 번 뜨는 경고는 놓친다.** 주기 요약에 계속 보이게 둔다 —
           // 이 칸이 가정을 넘으면 하행 건수 상한의 근거가 이미 깨진 것이다.
           // 🔴 **0 이 아니면 그 창의 장치 지표는 읽지 마라** — 두 보드가 섞였다는 뜻이다.
           // 계측 신뢰도 지표라서 다른 칸보다 앞에 둔다.
           // 🔑 **등록은 지금 관측 전용이다**(하행 경로를 안 바꾼다). 그래서 계수만 낸다.
           // `완료 0` 이 "옛 펌웨어라 등록을 안 한다"인지 "새 펌웨어인데 실패한다"인지는
           // **`질의`·`포기` 가 옆에 있어야 갈린다** — 분모를 같이 찍는 그 규율이다.
           + " · 등록 완료 " + std::to_string(reg_ok)
           + "(형식오류 " + std::to_string(reg_bad)
           + "/질의 " + std::to_string(reg_qsent)
           + "/포기 " + std::to_string(reg_giveups)
           + "/폭불일치 " + std::to_string(reg_widthbad)
           + "/미해독 " + std::to_string(occ_undecoded)
           + (reg_widthbad ? "🔴" : "") + ")"
           + " · devid거절 " + std::to_string(dup_devid_reject)
           + (dup_devid_reject ? "🔴" : "")
           + "/유예교체 " + std::to_string(takeover_grace)
           + " · S최대 " + std::to_string(s_max_b) + "B/가정 "
           + std::to_string(DEV_S_WORST_ASSUMED_B) + "B"
           + (s_worst_warned ? "🔴" : "")
           + " · 대기 " + std::to_string(downq.size())
           + "줄(" + std::to_string(downq_bytes) + "B/" + std::to_string(downq_max_b()) + "B)"
           // 🔴 `RTT` 는 **실측이고 표본 수를 같이 낸다.** `n=0` 이면 그 값은 "0ms"가 아니라
           // **"안 쟀다"** 이고, 그때 `W_srv` 는 상한(600−여유)에 그대로 앉아 있다.
           + " · 왕복 최대 " + std::to_string(rtt_max_ms) + "ms"
           + "(마지막 " + std::to_string(rtt_last_ms)
           + "/n=" + std::to_string(rtt_n) + ")"
           + " · W_srv " + std::to_string(w_srv()) + "ms"
           + " · " + recovery_phrase()
           + " · 회수 " + std::to_string(zombie_reaps + keepalive_reaps)
           + "(유휴 " + std::to_string(zombie_reaps)
           + "/keepalive " + std::to_string(keepalive_reaps) + ")";
        // 🔴 **분모를 찍는다** — `치유 0/0` 과 `치유 0/1200` 은 완전히 다른 문장이다.
        //   앞은 **검사가 안 돈 것**(2026-08-19 에 7시간 그랬다)이고 뒤는 **건강한 것**이다.
        //   ⚠ 분자만 찍으면 둘이 똑같이 `0` 으로 보인다. 그래서 검사 수를 같이 낸다.
        // 🔑 **이름을 붙여 낸다.** `M − N` 은 성공이 아니다 — 무응답이 그 안에 섞인다.
        s += " · G 띄움 " + std::to_string(gate_q)
           + "·도달 " + std::to_string(gate_sent)
           + "·응답 " + std::to_string(gate_ans)
           + "·거절 " + std::to_string(dev_reject);
        // 🔴 **계수를 만들고 요약에 안 내보냈다**(2026-08-19 발견). monitor 에게 *"`순서변경` 을 봐라"* 라고
        //   알려 둔 상태였다 — **없는 칸을 보라고 한 것이다.** 그쪽 파서는 표지 기준이라 조용히 못 찾는다.
        //   🔑 §"조건을 적었으면 그것을 보는 감시를 같은 자리에 만들어라" 의 **한 걸음 뒤 판본**이다:
        //   감시는 만들었는데 **그 결과를 볼 자리를 안 만들었다.** 세는 것과 보이는 것은 다른 일이다.
        s += " · 순서변경 " + std::to_string(mod_order_changed);
        // 🔴 계수를 만들었으면 **볼 자리도 만든다**(원장 규칙 셋째). 분모는 `등록 완료` 다.
        s += " · 비자리예약 " + std::to_string(not_reservable_n)
           + (not_reservable_n > 0 ? " 🔴" : "");
        s += " · 센서갈림 " + std::to_string(sensor_split_now) + "자리"
           + (sensor_split_now > 0 ? " 🔴" : "");
        s += " · 이름중복 " + std::to_string(mod_dup_name)
           + (mod_dup_name > 0 ? " 🔴" : "");
        s += " · 미결속모듈 " + std::to_string(mod_unbound)
           + "/" + std::to_string(mod_seen_)
           + (mod_unbound > 0 ? " 🔴" : "");
        // 🔑 갈래를 갈라 센다 — 뭉치면 "장치가 이상한가 내 로직이 이상한가"를 못 가른다
        s += " · 명령결과 성공 " + std::to_string(cb_ok_)
           + "/거절 " + std::to_string(cb_rejected_)
           + "/무응답 " + std::to_string(cb_noanswer_)
           + (cb_noanswer_ > 0 ? " 🔴" : "");
        s += " · 묶음미룸 " + std::to_string(batch_deferred)
           + (batch_deferred > 0 ? " 🔴" : "");
        s += " · 조립표문제 " + std::to_string(asm_warn_)
           + (asm_warn_ > 0 ? " 🔴" : "");
        s += " · 예시devid " + std::to_string(devid_example_)
           + (devid_example_ > 0 ? " 🔴" : "");
        s += " · 이름충돌 " + std::to_string(mod_name_conflict)
           + (mod_name_conflict > 0 ? " 🔴" : "");
        // 🔴 **화면 수를 찍는다** (2026-08-19). `S` 처리 안에서 `push_snapshot`·`state` 방송이 돌고
        //   그 비용은 **붙어 있는 화면 수에 비례**한다. 창 M 에서 하행 송신 지연이
        //   `≥2ms 9.4% → 40.9%` 로 늘었는데 **그 축이 아무 데도 안 남아서 원인을 못 갈랐다.**
        //   ⚠ **없는 축은 사후에 복원할 수 없다.** 지금부터 남긴다.
        //   🔑 최대값을 같이 낸다 — 평균만 내면 **한 창에 몰린 접속이 안 보인다.**
        {
            int ws_n = 0;
            for (std::map<sock_t, Conn>::const_iterator it = conns.begin(); it != conns.end(); ++it)
                if (it->second.kind == Conn::WS) ws_n++;
            // 🔴 **이름을 `화면` 에서 `WS접속` 으로 바꿨다** (2026-08-19 · REQ-0248)
            //   루트가 이 값을 **"화면에 끊김이 표시된 수"** 로 읽어 없는 결함을 사용자에게
            //   보고할 뻔했다. monitor 가 창 O 실측(주입기 8개를 띄우자 `1→9`)으로 반증해 멈췄다.
            //   🔑 **`화면` 은 세는 대상이 이름에 없다** — 사람이 보는 화면인지, WS 소켓인지.
            //     실제로 세는 것은 **`Conn::WS` 소켓 수**이고 탐침·하니스·주입기도 전부 포함된다.
            //   ⚠ §"두 뜻을 겸한 이름은 값이 갈리기 전까지 안 보인다" 와 같은 자리다 —
            //     사람이 화면 하나만 열어 두는 동안에는 두 뜻이 같은 값이었다.
            s += " · WS접속 " + std::to_string(ws_n) + "(최대 " + std::to_string(ws_peak) + ")";
        }
        s += " · 치유 " + std::to_string(heal_fires) + "/" + std::to_string(heal_checks)
           + (heal_checks == 0 ? " 🔴검사0" : "")
           + " · 예약미해독 " + std::to_string(res_undecoded);
        // 🔴 A[1] — **세는 것과 보이는 것은 다른 일이다.** `mod_order_changed` 를 만들어 놓고
        //   요약에 안 내보내 **monitor 에게 없는 칸을 보라고 한** 사고가 있었다. 같은 것을 반복하지 않는다.
        //   🔑 **분모를 같이 낸다** — `발행` 이 없으면 `건너뜀 0` 은 건강이 아니라 **표본 0** 이다.
        //   각 칸이 낮아지는 *다른* 이유는 `docs/net/DESIGN-rid-width-and-quarantine.md` §5 표에 있다.
        s += " · rid 발행 " + std::to_string(ridpool_.allocN())
           + "(다음 " + std::to_string(ridpool_.nextWire())
           + "/" + std::to_string((unsigned)RID_SPACE)
           + (ridpool_.persistOn() ? " 영속" : " 🔴영속꺼짐") + ")"
           + " 격리 " + std::to_string((long long)ridpool_.quarSize())
           + " 건너뜀 " + std::to_string(ridpool_.skips())
           + " 강제 " + std::to_string(ridpool_.forced())
           + (ridpool_.forced() > 0 ? " 🔴" : "")
           + (ridpool_.exhausted() > 0 ? (" 고갈 " + std::to_string(ridpool_.exhausted()) + " 🔴") : "")
           + " · ACK 미상rid " + std::to_string(ack_unknown_rid)
           + " 자리불일치 " + std::to_string(ack_slot_mismatch)
           + (ack_slot_mismatch > 0 ? " 🔴" : "");

        // ── 노드 대장 (온보딩 2단계) ────────────────────────────────────────
        // 🔴 **분모를 같이 둔다.** `0` 혼자 서 있으면 건강처럼 보인다 —
        //   `0/0` 은 최소한 "표본이 없다"고 말해 준다(monitor 가 세운 규칙).
        // 🔑 그리고 **영속 여부를 매번 찍는다.** 대장이 메모리로만 돌고 있는데
        //   요약이 정상으로 보이면, 재기동 뒤에야 "다 사라졌다"를 알게 된다.
        s += " · 대장 " + std::to_string(ledger_.size()) + "노드"
           + "(신규 " + std::to_string(ledger_new_)
           + "/재확인 " + std::to_string(ledger_review_)
           + (ledger_review_ > 0 ? " 🔴" : "") + ")"
           + " 할당 " + std::to_string(ledger_.assignCount())
           + " 저장 " + std::to_string(ledger_.saves())
           + (ledger_.saveFails() > 0
                ? ("/실패 " + std::to_string(ledger_.saveFails()) + " 🔴") : "")
           + (ledger_.persistOn() ? "" : " **영속꺼짐** 🔴")
           + (ledger_.linesBad() > 0
                ? (" 깨진줄 " + std::to_string(ledger_.linesBad()) + " 🔴") : "");
        return s;
    }

    // ---------- 복구 지표 문장 (REQ-0072)
    // **"끊긴 뒤 몇 초 만에 스스로 돌아왔는가"** 를 한 구절로 말한다.
    //
    // ⚠ `미복구` 는 정의상 **0 아니면 1**이다. 다음 오프라인 에피소드가 열리려면 그 전에
    // 온라인으로 올라와야 하고(엣지 판정), 온라인으로 올라왔다는 것이 곧 복구이기 때문이다.
    // 그래서 "미복구 Z회" 를 여러 건으로 부풀리지 않고 **지금 몇 초째 못 돌아오고 있는지**를
    // 같이 찍는다 — 요약에서 눈에 띄어야 할 것은 횟수가 아니라 그 진행 시간이다.
    // (진짜로 여러 번 셀 수 있는 "못 돌아왔다" 지표는 위의 `회수`(유휴 마감) 쪽이다.)
    std::string recovery_phrase() const {
        std::string s = "복구 " + std::to_string(recov_same_conn + recov_reconn) + "회";
        if (recov_same_conn + recov_reconn) {
            s += "(같은연결 " + std::to_string(recov_same_conn)
               + "/재연결 " + std::to_string(recov_reconn) + ")";
            long long med = recov_median_ms();
            s += " 중앙 " + (med < 0 ? std::string("-") : secs(med))
               + " 최악 " + secs(recov_worst_ms);
            if (recov_dropped) s += "(표본상한 초과 " + std::to_string(recov_dropped) + "건 제외)";
        }
        // 진행 중인 미복구는 **가장 시끄럽게** 적는다. 이번 사고에서 요약만 보고는 안 보였고
        // 프레임 수가 멈춘 것을 사람이 눈치채야 했다 — 그 실패를 되풀이하지 않으려는 칸이다.
        if (offline_since_ms)
            s += " · 🔴미복구 1회(진행 " + secs(now_ms() - offline_since_ms) + ")";
        else
            s += " · 미복구 0";
        return s;
    }

    // 세션이 끝나는 길은 두 갈래다(recv 실패, send 실패). 둘 다 이리로 모은다 —
    // 한쪽만 계측하면 "끊겼는데 세션이 계속 열려 있는" 장부가 만들어진다.
    // (why 를 const char* 에서 std::string 으로 넓혔다 — 끊긴 이유에 errno 를 실어야
    //  keepalive 가 죽인 것과 그냥 수신 오류를 로그에서 가를 수 있다. REQ-0072)
    void end_ard_session(const std::string& why) {
        if (sess_start_ms) {
            fold_live_gap();                     // 끝나지 않은 침묵을 공백으로 확정하고 닫는다
            logf("-ARD", "세션#" + std::to_string(ard_sessions) + " 종료(" + why + ") — 지속 "
                         + hms(now_ms() - sess_start_ms)
                         + " · 프레임 " + std::to_string(sess_frames)
                         + " · 최대공백 " + secs(sess_max_gap_ms)
                         + " · 상대 " + (ard_peer.empty() ? std::string("?") : ard_peer));
            sess_start_ms = 0;
            // 명세 §0.4 ② — 세션 종료에 `last_seen` 을 굳힌다.
            // ⚠ **SIGKILL 로 죽으면 이게 안 남는다.** 받아들인 대가다 —
            //   대장의 목적은 *누가 있었나*이지 *언제까지 있었나*가 아니다.
            if (!park.devid.empty()) {
                ledger_.onSessionEnd(park.devid, epoch_ms());
                ledger_save("세션종료");
            }
        }
        if (!link_down_since) link_down_since = now_ms();
        // 설계 §2 — **세션이 끝나면 하행 큐를 비운다.** 옛 큐를 새 세션에 쏘면 장치가 모르는
        // 상태에 명령이 떨어진다. 비운 항목은 `device_offline` 로 **반드시 답한다**(§4-B 보장).
        // 🔴 **장치가 사라지면 모듈 상태를 모른다.** 안 지우면 `state` 가 끊긴 뒤에도
        //   `known:true` 로 낡은 값을 **사실로 주장한다** — 차단봉이 열려 있다고 그려 놓고
        //   실제로는 볼 수 없는 상태다. **모르면 덜 주장한다**(같은 블록에 적어 둔 규칙이다).
        //   ⚠ `z.modules` 는 남는다(지형은 유지) — **없어지는 것은 값이지 구조가 아니다.**
        park.mod_bits_n = 0;
        clear_downq("세션 종료");
    }

    bool device_online() const {
        return ard != BAD_SOCK && ard_seen && (now_ms() - ard_last_ms) < OFFLINE_MS;
    }

