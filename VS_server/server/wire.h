// wire.h — 송신 helper · WebSocket 프레임 (§5.2). `struct Server` 의 몸통 조각.
    // ═══════════════════════════════════════════════════════════════════
    // 🔴 **단독으로 컴파일되지 않는다.** `struct Server` 의 **몸통 조각**이고
    //   `server.cpp` 안 그 자리에 include 된다. **위치가 곧 문법이다.**
    // 🔑 옮긴 것이지 고친 것이 아니다 — 전처리 결과가 같아 **`.o` 가 바이트 동일**해야 한다.
    //   대조가 0 이 아니면 이동이 아니라 재배치다. 되돌리고 보고한다(REQ-0272).
    // ═══════════════════════════════════════════════════════════════════

    // ---------- 송신 helper
    // 반환 false = 밀어 넣지 못했다(타임아웃 또는 오류) → **호출자가 그 연결을 끊어야 한다.**
    // 부분 전송 뒤 타임아웃이면 전선에 **잘린 줄**이 남는다. 붙들고 있으면 상태가 어긋나므로 버린다.
    bool send_raw(sock_t fd, const char* p, size_t n, const char* who) {
        size_t off = 0;
        // **전체 마감시각을 따로 둔다.** SO_SNDTIMEO 는 `send()` **한 번**에 걸리는 것이지
        // 이 루프 전체에 걸리는 것이 아니다. 상대가 아주 조금씩만 빼가면 매 호출이 조금씩
        // 진전해 루프가 계속 돌고, 총 정지 시간은 얼마든지 길어진다(느린 클라이언트 정체).
        // 실측으로 확인했다 — 1초 타임아웃인데 send() 를 여러 번 부르느라 1.93초가 걸렸다.
        const long long deadline = now_ms() + SEND_TIMEOUT_MS;
        while (off < n) {                       // 부분 write 는 정상이다
            if (now_ms() > deadline) {
                char b[192];
                snprintf(b, sizeof(b),
                         "%s 로 전송이 %d ms 를 넘겼다 — %zu/%zu 바이트. 연결을 끊는다",
                         who, SEND_TIMEOUT_MS, off, n);
                logf("!", b);
                return false;
            }
            int w =
#ifdef _WIN32
                ::send(fd, p + off, (int)(n - off), 0);
#else
                (int)::send(fd, p + off, n - off, 0);
#endif
            if (w <= 0) {
                char b[192];
                snprintf(b, sizeof(b),
                         "%s 로 전송 실패 — %zu/%zu 바이트만 밀어넣음 (err %d). "
                         "상대가 안 빼가는 것으로 보고 연결을 끊는다",
                         who, off, n, sockerr());
                logf("!", b);
                return false;
            }
            off += (size_t)w;
        }
        return true;
    }
    // 아두이노 송신 실패 뒷정리. **두 경로(`send_ard`·`flush_downq`)가 여기로 모인다** —
    // 한쪽만 정리하면 "끊겼는데 세션이 열려 있는" 장부가 만들어진다(§899 와 같은 이유).
    void ard_send_failed() {
        closesock(ard); ard = BAD_SOCK; ard_buf.clear();
        end_ard_session("송신 실패");        // 세션 장부를 여기서도 닫는다(REQ-0065)
        // 단계 C: 직접 호출 → 이벤트. ⚠ **이 자리는 복구된 이음매 표에 없었다** —
        // 표는 5곳이라 했지만 실제로는 여기가 여섯 번째다. 표를 지도로 믿지 마라.
        emit_dev(DEV_DISCONNECT, park_dev, "송신 실패");
    }
    void send_ard(const std::string& line) {
        if (ard == BAD_SOCK) return;
        if (!send_raw(ard, line.data(), line.size(), "아두이노")) {
            ard_send_failed();
            return;
        }
        // ---------- 착지 위상 계측 (DESIGN-downlink-window.md §3.5)
        //
        // **하행이 장치 주기의 어디에 떨어졌는가**를 ms 로 남긴다. 값을 바꾸지 않고 찍기만 한다.
        //
        // 🔴 **왜 로그 본문에 넣나**: 줄 앞머리 시각이 **1초 해상도**라(§433 `%H:%M:%S`)
        // 조용한 창(약 1.0초)과 장치 활성 구간(약 113ms)을 **구별할 수 없다.**
        // `ard_last_ms` 는 ms 단위로 이미 있으므로, 그 차이를 본문에 적으면 그 순간 잴 수 있다.
        // → 이것 없이 `W`(송신 허용 창)를 정하면 **추측이다.**
        //
        // ⚠ **`S` 를 한 번도 못 받은 상태를 따로 표시한다.** `ard_seen` 이 false 면
        // `ard_last_ms` 는 **의미 없는 0** 이고, 그때 차이를 찍으면 **거대한 가짜 값**이 된다.
        // 승격 직후 `resync_reservations()` 가 쏘는 자리가 정확히 그 경우라
        // **그 구분이 `resync` 게이트 면제 판단의 입력**이 된다(설계 §3.4).
        //
        // 🔑 `wire_rid` 를 같이 찍는 이유: 장치 쪽에서 **`+IPD` 도착 위상**을 따로 재는데,
        // **짝지을 식별자가 없으면 양쪽이 각각 반쪽**이다("언제 쐈나" 대 "언제 받았나").
        {
            char ph[64];
            if (!ard_seen)
                snprintf(ph, sizeof(ph), " (S미수신)");
            else
                snprintf(ph, sizeof(ph), " (S+%lldms)", (long long)(now_ms() - ard_last_ms));
            logf("→ARD", line.substr(0, line.size() - 1) + ph);
            return;
        }
    }

    // ═══════ 하행 슬롯 큐 (docs/net/DESIGN-server-slot-queue.md) ═══════════════
    //
    // 🔑 **이 구조는 위상을 추정하지 않는다.** `S` 도착이 트리거이므로 장치가 알려준다.
    // 스케줄이면 병리 구간(`SEND OK` 가 2~5초)에서 예측이 가장 크게 틀리는데,
    // **가장 크게 틀리는 때가 하필 가장 위험한 때다.** 트리거는 그 문제가 없다.

    // 내 창의 실제 폭. `W_srv = 600 − RTT − 여유`.
    // 🔴 **`RTT` 는 상수가 아니라 실측이다.** 값을 박으면 Wi-Fi 가 붐빌 때 조용히 깨진다.
    // `rtt_max_ms` 는 ACK 왕복의 최대값이고 그건 `RTT + 장치 처리시간` 이라 **RTT 의 상한**이다 —
    // 창을 좁히는 방향이므로 **보수적 = 안전**하다.
    long long w_srv() const {
        long long w = (long long)DOWN_WIN_MS - rtt_max_ms - DOWN_WIN_MARGIN_MS;
        if (w < DOWN_WIN_MIN_MS) w = DOWN_WIN_MIN_MS;
        return w;
    }
    // 큐 깊이(바이트). **유도값이다** — "마감 안에 나갈 수 있는 양".
    long long downq_max_b() const {
        return (long long)DOWN_BATCH_CAP_B * (DOWNQ_WAIT_CAP_MS / DOWN_SLOT_MS);
    }

    // ── 🔴 **큐를 떠난 뒤의 최악 예산** (REQ-0166 · web 지적으로 추가)
    //
    // `expires_ms` 가 재는 것은 **큐 대기까지**다. 그러면 **이탈 후를 덮는 값이 화면 쪽에만
    // 있게 되는데**, web 의 그 값(6000)이 내 실제 최악보다 작았다. 화면이 서버보다 먼저
    // 롤백하면 사용자가 다시 누르고 **새 rid 로 큐에 한 건 더** 쌓인다 — 우리가 닫으려던 고리다.
    // → **서버가 이 값을 같이 준다.** 화면은 받은 값을 쓰고 짐작하지 않는다.
    //
    // 🔑 **리터럴을 쓰지 않고 상수에서 계산한다.** 그래야 `ACK_TIMEOUT_MS`·`ACK_MAX_TRIES`·
    // `DOWN_DMAX_MS` 를 나중에 누가 바꿔도 **이 값이 조용히 틀릴 수 없다.**
    // (web 이 제시한 (다)안 — 합의 상수를 화면에 박기 — 의 약점이 정확히 "값이 바뀌면
    //  조용히 틀린다" 였다. 계산으로 두면 그 약점이 구조적으로 사라진다.)
    //
    // 시도 한 번의 최악 = `ACK_TIMEOUT_MS` + **틱 해상도**(`SELECT_TICK_MS`).
    //   ⚠ 내가 처음 6900 이라 적을 때 **이 틱 해상도를 안 셌다.** 타임아웃은 `tick()` 이
    //   발견하는데 그건 최대 200ms 늦게 돈다. 3회면 600ms 가 빠져 있었다.
    // 시도 사이의 창 대기 최악 = `DOWN_DMAX_MS`.
    //   ⚠ 평시엔 한 슬롯(1200)이지만 **장치가 침묵하면 창 포기 경로(2400)가 상한**이다.
    //   그 경로를 안 세면 하필 병리 구간에서 예산이 모자란다.
    long long ack_budget_ms() const {
        return (long long)ACK_MAX_TRIES * (ACK_TIMEOUT_MS + SELECT_TICK_MS)
             + (long long)(ACK_MAX_TRIES - 1) * DOWN_DMAX_MS;
    }

    // 큐에 있던 항목을 **전선에 못 내보낸 채** 끝낼 때.
    // 🔴 설계 §4-B 의 보장: **`queued` 를 보냈으면 그 뒤에 반드시 `ack` 또는 `error` 가 간다.**
    // 그 보장이 없으면 화면의 중간 상태가 영구가 된다.
    // ⚠ 코드는 `device_offline` 이다 — **전선에 안 나갔다는 뜻**이고 원장 §8.16 이 요구하는 구분이다.
    // `ack_timeout` 을 쓰면 "나갔는데 응답이 없다"는 **거짓 문장**이 로그에 남는다.
    void fail_down_item(const DownQ& q, const char* code, const char* msg) {
        std::map<uint16_t, Pending>::iterator it = pend.find(q.wire_rid);
        if (it != pend.end()) { pend.erase(it); rid_release(q.wire_rid); }
        if (q.ws_fd != BAD_SOCK) send_err(q.ws_fd, q.brid, code, msg);
    }

    // 세션이 끝나면 큐를 비운다(설계 §2). **옛 큐를 새 세션에 쏘면 안 된다** —
    // 장치는 자기 번호를 새로 시작하고, 그 사이 상태도 우리가 모른다.
    void clear_downq(const char* why) {
        if (downq.empty()) return;
        char b[192];
        snprintf(b, sizeof(b), "하행 큐 비움(%s) — %zu줄 · %lldB. **전선에 나가지 않았다**",
                 why, downq.size(), downq_bytes);
        logf("✂", b);
        q_dropped_link += (long long)downq.size();
        // 🔴 **먼저 떼어낸다.** `send_err` 가 `dead_conns` 를 건드리고, 실패 경로가 다시
        // `end_ard_session` → `clear_downq` 로 들어올 수 있다. 비운 뒤에 답하면 재진입이 안전하다.
        std::vector<DownQ> tmp;
        tmp.swap(downq);
        downq_bytes = 0;
        for (size_t i = 0; i < tmp.size(); i++)
            fail_down_item(tmp[i], "device_offline", "센서 연결이 끊겨 요청이 취소되었습니다");
    }

    // 하행 한 줄을 큐에 넣는다. **여기서 전송은 일어나지 않는다.**
    //   announce : 브라우저에 `queued` 를 보내나 (재전송은 안 보낸다 — §expires_ms 단조성)
    //   force    : 깊이 상한을 무시하나 (재전송은 이미 약속한 것이므로 거절하지 않는다)
    // 반환값 false = 거절했다(호출자가 `pend` 에서 지워야 한다).
    bool enqueue_down(Pending& p, const std::string& line, bool announce, bool force,
                      long long batch_id = 0) {
        // 🔴 **장치가 없으면 큐에 담지 않는다.** 담으면 아무도 빼가지 않아 다음 세션까지
        // 살아남고, 그러면 **장치가 모르는 상태에 옛 명령이 떨어진다**(설계 §2 가 금지한 것).
        // 옛 거동은 `send_ard` 가 조용히 no-op 해서 **아무 말 없이 사라졌다** — 그건 더 나쁘다.
        // (폰 경로 `on_plate()` 가 장치 없이도 `dispatch` 를 부를 수 있어 실제로 도달한다.
        //  selftest 에서 큐가 쌓이는 것을 보고 찾았다 — 소켓 없는 경로가 그 모양이다.)
        if (ard == BAD_SOCK) {
            q_nodev++;                 // 🔴 `q_rejected`(큐 넘침)와 **다른 칸**이다
            logf("!", "장치 연결이 없다 — 하행 거절 rid=" + std::to_string(p.wire_rid));
            if (p.ws_fd != BAD_SOCK)
                send_err(p.ws_fd, p.browser_rid, "device_offline", "센서가 연결되어 있지 않습니다");
            return false;
        }
        // 옛 거동 — **착지 위상을 의도적으로 겨냥하는 시험 전용**(원장 §8.17 의 미해결 물음).
        if (DOWN_IMMEDIATE) {
            p.queued = false; p.tries = (p.tries < 1 ? 1 : p.tries); p.sent_ms = now_ms();
            send_ard(line);
            return true;
        }
        // 한 줄 상한 감시선. `RX_CAP=96` 에서 `+IPD` 접두를 뺀 87B 다. 우리 줄은 30B 안쪽이라
        // 정상적으로는 안 걸린다 — **걸리면 그건 새 프레임 종류가 들어온 신호다.**
        if ((long long)line.size() > (long long)DOWN_LINE_MAX_B + 1) {
            char b[128];
            snprintf(b, sizeof(b), "하행 한 줄이 %zuB — 장치 RX_CAP(96, 접두 포함) 위험. 그대로 보낸다",
                     line.size());
            logf("!", b);
        }
        const bool important = (p.ws_fd == BAD_SOCK);   // 서버 내부 발생 = 중요(§분류)

        // 🔴 **사용자 계열만 건수로 막는다. 중요는 거절하지 않는다.**
        // 사용자 조작은 **거절하면 사용자가 안다**(다시 누르면 된다). 중요는 버리면
        // **아무도 모르게 서버와 장치가 갈린 채 남는다** — 그래서 문턱을 안 건다.
        // 깊이는 유도값이다: **한 창에 `DOWN_BATCH_MAX_N` 이 나가므로 마감 안에 나갈 수 있는 양**
        //   = 4건/창 × (4800ms ÷ 1200ms) = **16건**
        if (!force && !important) {
            int user_n = 0;
            for (size_t k = 0; k < downq.size(); k++) if (!downq[k].important) user_n++;
            const int user_cap = DOWN_BATCH_MAX_N * (DOWNQ_WAIT_CAP_MS / DOWN_SLOT_MS);
            if (user_n >= user_cap) {
                q_rejected++; q_full_n++;
                char b[160];
                snprintf(b, sizeof(b), "사용자 계열 큐 상한(%d건) 초과 — 거절 rid=%u",
                         user_cap, p.wire_rid);
                logf("!", b);
                // 🔑 **침묵이 아니라 거절이다**(§8.20) — 조용히 버리면 사용자가 더 세게 누른다
                if (p.ws_fd != BAD_SOCK)
                    send_err(p.ws_fd, p.browser_rid, "queue_full",
                             "요청이 밀려 있습니다. 잠시 후 다시 눌러 주세요");
                return false;
            }
        }
        if (!force && downq_bytes + (long long)line.size() > downq_max_b()) {
            q_rejected++;
            char b[160];
            snprintf(b, sizeof(b), "하행 큐 상한(%lldB) 초과 — 거절 rid=%u (대기 %zu줄 %lldB)",
                     downq_max_b(), p.wire_rid, downq.size(), downq_bytes);
            logf("!", b);
            // 🔴 **조용히 버리지 않는다.** 버림과 거절은 다르다(원장 §8.20) —
            // 버림은 아무도 모르게 사라지고, **거절은 사용자에게 말한다.**
            if (p.ws_fd != BAD_SOCK)
                send_err(p.ws_fd, p.browser_rid, "queue_full", "요청이 밀려 있습니다. 잠시 후 다시 눌러 주세요");
            return false;
        }
        DownQ q;
        q.line = line; q.ws_fd = p.ws_fd; q.brid = p.browser_rid; q.slot = p.slot;
        q.wire_rid = p.wire_rid;
        // 마감은 **enqueue 때 한 번만 박는다.** 그래야 `expires_ms = deadline − now` 가
        // 같은 rid 에 대해 **단조 비증가**가 된다(설계 §4-B). `ahead` 로 다시 계산하면
        // 앞이 밀릴 때 값이 늘어 단조성이 깨진다.
        q.batch_id = batch_id;
        q.deadline_ms = now_ms() + DOWNQ_WAIT_CAP_MS;
        q.important = important;
        downq.push_back(q);
        downq_bytes += (long long)line.size();
        // 🔴 **`tries` 를 건드리지 않는다.** 재전송이 이 함수를 다시 타므로 여기서 0 으로 밀면
        // 재시도 횟수가 영구히 리셋되어 **ACK_MAX_TRIES 가 무력해지고 무한 재전송이 된다.**
        // "전선에 나갔나"는 `queued` 하나로 표현한다 — 표시자를 두 개 두면 반드시 갈린다.
        p.queued = true;
        if (announce && p.ws_fd != BAD_SOCK) send_queued(q);
        return true;
    }

    // 창이 열렸다(= `S` 가 도착했다). **큐 전부를 한 거래로 묶어** 내보낸다.
    //   ignore_window : `S` 가 안 와서 창을 포기하는 경로(설계 §3)
    // `max_n` = 이번 창에 전선에 올릴 **최대 건수**. 기본은 유도값이고, 증거 없는
    // 경로(창 포기)는 `DOWN_PROBE_N` 을 넘긴다 — **송신 지점이 한 곳이라 우회가 없다.**
    void flush_downq(const char* why, bool ignore_window, int max_n = DOWN_BATCH_MAX_N) {
        if (downq.empty() || ard == BAD_SOCK) return;
        long long t = now_ms();
        if (!ignore_window && ard_seen) {
            // 창 안으로 얼마나 들어왔나. 지났으면 **다음 `S` 를 기다린다** —
            // 늦게 쏘면 장치의 송신 구간을 침범하고, 그게 §8.15 의 UART 혼재다.
            long long into = t - ard_last_ms;
            if (into >= w_srv()) {
                win_skips++;
                char b[160];
                snprintf(b, sizeof(b), "창을 지났다(S+%lldms ≥ W_srv %lldms) — 다음 S 를 기다린다"
                                       " · 대기 %zu줄 (%s)", into, w_srv(), downq.size(), why);
                logf("…", b);
                return;
            }
        }
        // 바이트 상한까지만 담는다. **남은 것은 버리지 않고 다음 창으로 미룬다.**
        std::string payload;
        std::vector<DownQ> batch;
        // 이번 창에서 통째로 미룬 배치 / 담기로 한 배치
        std::set<long long> skipped_batch, accepted_batch;
        // 🔴 **중요 계열을 먼저 담는다.** 각 계열 안에서는 FIFO 다(예약/취소가 뒤집히면 안 된다).
        // **계열 사이에는 순서 보장이 필요 없다** — 상태 정정과 새 의도는 서로 독립이다.
        for (int pass = 0; pass < 2; pass++) {
            const bool want = (pass == 0);
            for (size_t k = 0; k < downq.size(); ) {
                const DownQ& q = downq[k];
                if (q.important != want) { k++; continue; }
                // 🔴🔴 **창 원자성** — 배치는 **통째로 들어가거나 통째로 미뤄진다.**
                //
                //   전에는 큐에 있는 것을 순서대로 담았다. 그러면 **내 셋을 넣기 전에 남의 것이
                //   큐에 있으면 내 셋 중 하나가 다음 창으로 밀리고, 호출자는 그것을 모른다.**
                //   🔑 **"같이 나가더라"는 관찰이고 "같이 나간다"는 보장이다.** 여기가 그 차이다.
                //
                // ⚠ 이건 **창 원자성**이지 **실행 원자성**이 아니다 — 나간 뒤에 하나가 거절돼도
                //   나머지는 수행된다. 되돌릴 수단이 없으므로 그건 약속하지 않는다.
                if (q.batch_id != 0 && !skipped_batch.count(q.batch_id)) {
                    size_t need_b = 0; int need_n = 0;
                    for (size_t j = 0; j < downq.size(); j++)
                        if (downq[j].batch_id == q.batch_id) { need_b += downq[j].line.size(); need_n++; }
                    const bool fitsB = payload.empty()
                                    || payload.size() + need_b <= (size_t)DOWN_BATCH_CAP_B;
                    const bool fitsN = (int)batch.size() + need_n <= max_n;
                    if (!fitsB || !fitsN) {
                        // 이번 창에는 이 배치가 통째로 안 들어간다 — **한 줄도 안 담는다**
                        skipped_batch.insert(q.batch_id);
                        batch_deferred++;
                        k++; continue;
                    }
                    accepted_batch.insert(q.batch_id);
                }
                if (q.batch_id != 0 && skipped_batch.count(q.batch_id)) { k++; continue; }
                // ⚠ 첫 줄은 상한을 넘어도 담는다 — 안 그러면 큰 줄 하나가 큐를 영구히 막는다.
                if (!payload.empty() &&
                    payload.size() + q.line.size() > (size_t)DOWN_BATCH_CAP_B) break;
                // 🔴 **건수 상한 — 바이트와 다른 축이다.** 장치의 ACK 배출률에서 유도됐다.
                if ((int)batch.size() >= max_n) break;
                payload += q.line;
                batch.push_back(q);
                downq_bytes -= (long long)q.line.size();
                downq.erase(downq.begin() + k);
            }
            if ((int)batch.size() >= max_n) break;
        }
        if (downq_bytes < 0) downq_bytes = 0;
        if (batch.empty()) return;

        if (!send_raw(ard, payload.data(), payload.size(), "아두이노")) {
            // 🔴 떼어낸 배치는 이미 큐에 없다 — **여기서 따로 닫아야** 화면의 중간 상태가 안 남는다.
            // (실패 경로가 `clear_downq` 로 나머지를 닫는다. 순서상 이 둘이 겹치지 않는다.)
            for (size_t i = 0; i < batch.size(); i++)
                fail_down_item(batch[i], "device_offline", "센서로 보내지 못했습니다");
            ard_send_failed();
            return;
        }
        batch_count++;
        batch_lines += (long long)batch.size();

        // 🔑 **줄마다 따로 찍는다.** 배치 요약만 남기면 장치 쪽 `+IPD` 도착 위상과
        // **짝지을 식별자가 사라진다**(어느 rid 가 언제 나갔나). 그 짝이 반쪽이면 위상 계측이 죽는다.
        for (size_t i = 0; i < batch.size(); i++) {
            std::map<uint16_t, Pending>::iterator it = pend.find(batch[i].wire_rid);
            if (it != pend.end()) {
                it->second.queued = false;
                it->second.sent_ms = t;                       // ACK 시계는 **여기서** 시작한다
                // 🔑 **도달**은 여기서 센다 — 큐에 든 것(`띄움`)과 다르다.
                //   세션이 끊기면 큐의 것은 버려지므로 **띄움 > 도달** 인 구간이 생긴다.
                //   ⚠ 그 차이가 "큐에서 죽었다"이고, `도달 − 응답` 이 "나갔는데 답이 없다"다.
                //     **둘은 다른 고장이라 한 칸에 못 넣는다.**
                if (it->second.kind == 'G') gate_sent++;
                if (it->second.tries < 1) it->second.tries = 1;
            }
            char ph[64];
            if (!ard_seen) snprintf(ph, sizeof(ph), " (S미수신)");
            else           snprintf(ph, sizeof(ph), " (S+%lldms)", (long long)(t - ard_last_ms));
            std::string l = batch[i].line;
            if (!l.empty() && l[l.size()-1] == '\n') l.erase(l.size()-1);
            logf("→ARD", l + ph);
        }
        {
            char b[192];
            snprintf(b, sizeof(b), "배치 %zu줄 · %zuB · 한 거래 (%s) · cap %dB · 남은 큐 %zu줄",
                     batch.size(), payload.size(), why, DOWN_BATCH_CAP_B, downq.size());
            logf("⇉", b);
        }
        // 미룬 것을 **조용히 미루지 않는다** — 안 적으면 지연이 아무 데도 안 보인다(원장 §5.2).
        if (!downq.empty()) {
            q_deferred++;
            char b[160];
            snprintf(b, sizeof(b), "바이트 상한에 걸려 %zu줄(%lldB)을 다음 창으로 미뤘다",
                     downq.size(), downq_bytes);
            logf("…", b);
        }
    }

    // ---------- WebSocket 프레임 (§5.2)
    void ws_send(sock_t fd, const std::string& payload) {
        std::string f;
        f += char(0x81);                                  // FIN + text
        size_t n = payload.size();
        if (n < 126) {
            f += char((unsigned char)n);                  // 서버→클라이언트는 마스킹 금지 (n<126 확정)
        } else if (n <= 0xFFFF) {
            f += char(126);                               // 16비트 확장 길이 — 스냅샷이 여기 온다
            f += char((n >> 8) & 0xFF);
            f += char(n & 0xFF);
        } else {
            f += char(127);
            for (int i = 7; i >= 0; i--) f += char((n >> (8*i)) & 0xFF);
        }
        f += payload;
        // 실패하면 여기서 erase 하지 않는다 — broadcast 순회 중이면 반복자가 무효화된다
        // (예전에 SIGSEGV 를 냈던 바로 그 실수다). 표시만 해 두고 루프 끝에서 거둔다.
        if (!send_raw(fd, f.data(), f.size(), "WS 클라이언트")) dead_conns.push_back(fd);
    }
    void ws_broadcast(const std::string& payload) {
        for (std::map<sock_t, Conn>::iterator it = conns.begin(); it != conns.end(); ++it)
            if (it->second.kind == Conn::WS) ws_send(it->first, payload);
    }
    // 🔴 **이름이 `dead` 였다.** `-Wshadow` 가 짚었다 — 바깥 `dead` 를 가리고 있었다.
    //   같은 이름이 **셋**이었다: 이 멤버(연결) · `downlink.h` 의 지역(rid) · `serve.h` 의 지역(장치id)
    //   🔴 셋 다 **정수 계열 컨테이너**라 잘못 써도 **컴파일이 통과한다.**
    //   ⚠ 그때도 코드는 맞았다 — 지역이 이겨서 의도대로 돌았다. **위험은 *다음 편집* 이었다.**
    //   🔑 이 부류의 네 번째다(`n` · `kind` · `slot` · `dead`).
    //     **두 뜻을 겸한 이름은 값이 갈리기 전까지 안 보인다.**
    std::vector<sock_t> dead_conns;      // 전송 실패로 끊어야 할 연결. 루프 끝에서 거둔다
    void reap_dead() {
        for (size_t i = 0; i < dead_conns.size(); i++) {
            sock_t fd = dead_conns[i];
            if (conns.count(fd)) { closesock(fd); conns.erase(fd); logf("-WS", "전송 실패로 연결 종료"); }
            if (phones.count(fd)) { closesock(fd); phones.erase(fd); }
        }
        dead_conns.clear();
    }

