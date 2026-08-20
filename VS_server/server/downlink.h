// downlink.h — 아두이노 하행(서버 → 장치) 발행·재전송·회수. `struct Server` 의 몸통 조각.
    // ═══════════════════════════════════════════════════════════════════
    // 🔴 **이 파일은 단독으로 컴파일되지 않는다.**
    //   `struct Server` 의 **몸통 조각**이고, `server.cpp` 안에서 그 자리에 include 된다.
    //   include 문을 클래스 밖으로 옮기면 컴파일이 깨진다 — 위치가 곧 문법이다.
    //
    // 🔑 **왜 이 형태인가**: 코드를 *옮긴 것*이지 *고친 것*이 아님을 증명하려고.
    //   전처리 결과가 원본과 같으므로 **`.o` 가 바이트 동일**해야 한다.
    //   그 대조가 0 이 아니면 이동이 아니라 재배치다 — 그때는 되돌리고 보고한다(REQ-0272).
    // ═══════════════════════════════════════════════════════════════════

    // ---------- 아두이노로 요청 내리기
    void dispatch(char kind, sock_t ws_fd, const std::string& brid,
                  const std::string& slot, const std::string& uid) {
        uint16_t rid = alloc_rid();
        if (rid == RID_NONE) {
            logf("!", "rid 공간 고갈 — 하행 발행 포기 (pend=" + std::to_string(pend.size()) + ")");
            return;
        }
        Pending p;
        p.wire_rid = rid; p.ws_fd = ws_fd; p.browser_rid = brid;
        p.slot = slot;
        p.plate = uid;                       // 서버가 기억할 원본(번호판일 수 있다)
        p.user_id = wire_userid(uid);        // 전선에 나갈 값 — ASCII 0*8 아니면 빈 값
        p.kind = kind;
        p.sent_ms = now_ms(); p.tries = 1;
        pend[rid] = p;

        char buf[64];
        // **p.user_id 를 쓴다. 인자 uid 를 쓰면 안 된다** — uid 는 번호판일 수 있고
        // 그러면 UTF-8 이 그대로 전선에 나가 §2.1(ASCII 전용)과 §2.3 을 위반한다.
        // 실제로 그 버그를 냈다: `R,1,B2,980가4568,F7` 이 나가 아두이노가 죽었다.
        if (kind == 'R') snprintf(buf, sizeof(buf), "R,%u,%s,%s,", rid, slot.c_str(), p.user_id.c_str());
        else             snprintf(buf, sizeof(buf), "C,%u,%s,", rid, slot.c_str());
        // 🔑 **전송하지 않는다. 큐에 담는다.** 나가는 것은 다음 창(= 다음 `S` 도착)이다.
        if (!enqueue_down(pend[rid], build_line(buf), true, false)) { pend.erase(rid); rid_release(rid); }
    }

    // §2.4 `T` — 테스트 모드 제어. R/C 와 같은 pend 표·재전송·타임아웃을 그대로 탄다.
    // Pending.user_id 를 tval 보관에 재사용하고, slot 에 "??" 가 들어갈 수 있다.
    void dispatch_test(sock_t ws_fd, const std::string& brid,
                       char op, const std::string& slot, const std::string& tval) {
        uint16_t rid = alloc_rid();
        if (rid == RID_NONE) { logf("!", "rid 공간 고갈 — T 발행 포기"); return; }
        Pending p;
        p.wire_rid = rid; p.ws_fd = ws_fd; p.browser_rid = brid;
        p.slot = slot; p.user_id = tval; p.kind = 'T';
        p.top = op;
        p.sent_ms = now_ms(); p.tries = 1;
        pend[rid] = p;
        // ⚠ **`T` 도 같은 큐를 탄다** — 하행이면 규율을 지킨다(설계 §7 은 "안 정했다"였고
        // 여기서 정한다). 대가가 있다: `T` 는 §8.7·§8.17 에서 **착지 타이밍을 재는 도구**였는데
        // 큐에 태우면 안전한 창으로 강제되어 **"착지 시점을 의도적으로 맞춘 시험"의 수단이 사라진다.**
        // → 그 시험이 필요하면 `--down-immediate` 로 옛 거동을 쓴다. **도구가 바뀐 사실은 원장 §8.23.**
        if (!enqueue_down(pend[rid], build_line(test_prefix(p)), true, false)) { pend.erase(rid); rid_release(rid); }
    }
    // §12B — 시뮬레이터 한 걸음. **무장 여부로 막지 않는다**(테스트 모드와 별개).
    void dispatch_sim(sock_t ws_fd, const std::string& brid) {
        uint16_t rid = alloc_rid();
        if (rid == RID_NONE) { logf("!", "rid 공간 고갈 — M 발행 포기"); return; }
        Pending p;
        p.wire_rid = rid; p.ws_fd = ws_fd; p.browser_rid = brid;
        p.kind = 'M'; p.top = 0;
        p.sent_ms = now_ms(); p.tries = 1;
        pend[rid] = p;
        if (!enqueue_down(pend[rid], build_line(sim_prefix(p)), true, false)) { pend.erase(rid); rid_release(rid); }
    }
    // ── 🔴 자리 조작 `G` — **전선 형식은 arduino 의 파서에서 읽어 맞췄다**(REQ-0228 · 말로 받지 않았다)
    //
    //   `G,<rid>,<idx>,<op>,<ck>`
    //     idx : **모듈 인덱스**(등록 `D` 순서). 장치는 `idx >= SLOT_N && idx < moduleCount()` 만 받는다
    //     op  : **0 이 아니면 열기, 0 이면 닫기**
    //   ACK  : `A,<rid>,G<d>,<result>,<ck>`  (d = idx % 10) · result 0=수행 · **3=수행 불가**
    //
    // ⚠ **ACK 의 둘째 칸이 자리 이름이 아니다**(`G0` 같은 값). 서버의 ACK 경로가 이미
    //   `slot_index(slot) < 0` 이면 `p.slot` 으로 되돌리므로 그대로 동작한다 — **상관 키는 `rid`** 다.
    // 🔴 **완료는 이 ACK 이 아니라 다음 `S` 의 비트 `idx` 로 판정한다.**
    //   ACK 은 *"명령이 도착해 적용됐다"* 까지이고, **실제로 열렸는지는 장치가 매 슬롯 에코한다.**
    static std::string gate_prefix(const Pending& p) {
        char buf[64];
        // 🔴 **인자를 그대로 싣는다** (2026-08-19). 전에는 `(p.top=='1') ? 1 : 0` 이라
        //   **0/1 밖에 못 보냈다** — 차단봉 전용 경로였기 때문이다.
        //   장치는 이미 `parseU32` 로 받는다(`EspLink_at.h`) → **전선 개정이 아니라 API 확장이다.**
        // ⚠ 최악 길이 실측: `G,999,255,4294967295,` + 체크섬 = **23B** (MAX_LINE 64 · 여유 충분)
        snprintf(buf, sizeof(buf), "G,%u,%d,%ld,", p.wire_rid, p.mod_idx, p.g_arg);
        return std::string(buf);
    }
    // 자리에 붙은 **명령 가능한 모듈**의 인덱스. 없으면 -1.
    // 🔑 **자리 → 모듈 라우팅은 여기 한 곳에만 있다**(설계 §6.8) — 화면은 자리만 지목한다.
    // ── 🔴 ②-d — **그 모듈을 등록한 노드를 찾는다** (REQ-0262 · 2026-08-19)
    //   전에는 `state_json()` 이 **주 노드만** 보고 나머지는 `known:false` 로 냈다.
    //   그건 **버그가 아니라 옳은 답**이었다 — 값 경로가 없었으니까(설계 §8.9).
    //   ②-c 로 비트열이 노드별이 됐고 ②-b 로 보조 노드 등록이 들어오므로 **이제 채울 수 있다.**
    const Node* node_by_devid(const std::string& dev) const {
        // ⚠ **빈 `devid` 를 걸러내지 않는다.** 처음에 `if (dev.empty()) return 0;` 을 넣었다가
        //   **자가검증 셋이 깨졌다**(E1·X1 값, 세션 종료 뒤 known). 이유:
        //   승격 전 주차 노드는 `devid` 가 비어 있고 그 상태로 등록하면 지형에 `("", "E1")` 이 들어간다.
        //   옛 코드는 `z.modules[m].first == park.devid` 로 **빈 값끼리 맞춰** 동작했다.
        //   🔑 방어를 넣으면서 **거동을 같이 바꿨다** — 시험이 아니었으면 화면에서 값이 조용히 사라졌다.
        //   보조 노드의 빈 `devid` 는 `adopt_as_aux()` 에서 이미 고쳤으므로 여기서 또 막을 필요가 없다.
        if (dev == park.devid) return &park;
        std::map<std::string, AuxNode>::const_iterator it = aux.find(dev);
        return (it != aux.end()) ? &it->second : 0;
    }

    // ⚠ **이 함수는 주차 노드만 본다. 그대로 둔다** — 보조 노드는 **상행 전용**이라
    //   하행 경로가 없다. 없는 노드에 조작 색인을 만들어 주면 **누를 수 있는 버튼**이 생기고
    //   눌러도 아무 일이 안 난다. 🔑 §"조용히 성공으로 답하지 않는 것이 지금의 정답" 그대로다.
    // 🔴 `gate_index_of()` 가 여기 있었다. **2026-08-20 에 없앴다** —
    //   유일한 호출자였던 자리 `completion` 이 사라지면서 **호출자가 0 이 됐다.**
    //   ⚠ 호출자 없는 코드는 **있는 것처럼 읽힌다.** 남기지 않는다.
    // 🔴 **명령 결과를 알린다** — 갈래 셋을 여기 한 곳에서 만든다.
    //   🔑 한 곳에서 만들어야 세 갈래가 **같은 모양**으로 나간다. 흩어지면 칸이 갈린다.
    void notify_cmd(const Pending& p, CmdResult::Kind k, int devResult) {
        if (k == CmdResult::OK)            cb_ok_++;
        else if (k == CmdResult::REJECTED) cb_rejected_++;
        else                               cb_noanswer_++;
        // 🔴 **화면이 시킨 명령이면 결과를 화면으로 돌려준다** (2026-08-20)
        //   ⚠ **`ack` 에 얹지 않는다.** 옛 화면은 `type==='ack'` 만 보고 pending 을 지우며
        //     "예약되었습니다" 를 띄운다 — 모르는 타입은 조용히 무시하므로 새 타입이 안전하다.
        //     (seam.h §4-B 가 같은 함정에 같은 답을 냈다. 그것을 그대로 쓴다.)
        if (p.web_cmd && p.ws_fd != BAD_SOCK && conns.count(p.ws_fd)) {
            const char* out = (k == CmdResult::OK) ? "ok"
                            : (k == CmdResult::REJECTED) ? "rejected" : "no_answer";
            const char* msg = (k == CmdResult::OK) ? "명령이 적용되었습니다"
                            : (k == CmdResult::REJECTED) ? "장치가 이 명령을 거절했습니다"
                            : "장치가 응답하지 않습니다";
            // 🔴 **사유 코드** (web 이 찾은 빈 자리 · 2026-08-20)
            //   ⚠ **`error` 봉투의 코드와 뜻이 겹치지 않는다.** 갈래가 다르다:
            //     `error`      — **전선에 나가기 전** 거절(선언 없음·범위 밖·묶음 초과). **안 나갔다**
            //     `cmd_result` — **나갔고** 장치가 답했다(또는 안 답했다)
            //   🔑 화면이 이 둘을 다르게 그려야 한다 — 사람이 할 일이 다르다.
            //     앞은 *"내 요청을 고쳐라"* 이고 뒤는 *"장치·선을 봐라"* 다.
            const char* why = (k == CmdResult::OK) ? 0
                            : (k == CmdResult::REJECTED) ? "device_refused"   // **새 코드**
                            : "ack_timeout";                                   // 기존 코드
            std::ostringstream o;
            o << "{\"type\":\"cmd_result\",\"rid\":" << jstr(p.browser_rid)
              << ",\"devid\":" << jstr(park.devid)
              << ",\"module\":" << jstr(p.slot)
              << ",\"value\":" << p.g_arg
              << ",\"outcome\":\"" << out << "\""
              << ",\"result\":" << devResult;
            // **없으면 키를 안 보낸다**(존재/부재 규칙) — `ok` 에 `reason:null` 을 실으면
            // 화면이 "사유가 있는데 비었다"로 읽을 여지가 생긴다.
            if (why) o << ",\"reason\":" << jstr(why);
            o << ",\"message\":" << jstr(msg) << "}";
            ws_send(p.ws_fd, o.str());
        }
        if (!cmd_cb_) return;              // 등록 안 했으면 세기만 한다
        CmdResult r;
        r.kind = k; r.devid = park.devid; r.module = p.slot;
        r.value = p.g_arg; r.deviceResult = devResult; r.rid = p.wire_rid;
        cmd_cb_(r);
    }

    // 🔴 **한 창에 몇 건까지 묶을 수 있나** — 상수가 아니라 **지금 상한에서 계산한다.**
    //   `DOWN_BATCH_CAP_B` 는 `--down-cap` 으로 바뀐다. 리터럴로 박으면 손잡이를 돌렸을 때 어긋난다.
    //
    // 🔴 **최악값으로 센다** — 인자가 10자리(uint32 최대)일 때의 줄 길이로.
    //   실제 인자가 짧다고 그 수를 쓰면 **긴 인자에서 조용히 잘린다.**
    //     `G,999,255,4294967295,` + 체크섬 = 23B · 줄 사이 LF 1B
    //     192B 기준 → 23 + 7×24 = 191 ⇒ **8건**
    // ⚠ 묶는 제약이 둘이다 : 하행 배치(여기) **8** · ACK 되돌림 약 9(arduino 계산).
    //   **작은 쪽이 먼저 걸린다.** 넘기면 두 슬롯으로 쪼개져 "묶었는데 왜 느리지"가 된다.
    // 바이트 축만 본 값(최악 인자 기준). `max_per_batch()` 가 이것과 건수 상한 중 작은 것을 낸다.
    int max_per_batch_bytes() const {
        const int one = (int)std::string("G,999,255,4294967295,").size() + 2;
        const int cap = (DEV_RX_RING_B < DOWN_BATCH_CAP_B) ? DEV_RX_RING_B : DOWN_BATCH_CAP_B;
        int n = 0, used = 0;
        while (true) {
            const int add = (n == 0) ? one : one + 1;
            if (used + add > cap) break;
            used += add; n++;
        }
        return n;
    }
    int max_per_batch() const {
        const int one = (int)std::string("G,999,255,4294967295,").size() + 2;   // 23
        int byN = 0, used = 0;
        while (true) {
            const int add = (byN == 0) ? one : one + 1;    // 줄 사이 LF
            if (used + add > DOWN_BATCH_CAP_B) break;
            used += add; byN++;
        }
        // ⚠ **장치 수신 링(64B)은 여기 안 들어간다** — 한때 넣었다가 뺐다.
        //   링은 **배치 전체를 담지 않는다.** 장치의 `espRead()` 가 매 loop 마다 비우므로
        //   거기 쌓이는 것은 배치 크기가 아니라 **loop 한 바퀴 동안 도착한 바이트**다
        //   (LCD 4건 76B 는 79ms 에 걸쳐 오고 그 사이 loop 이 여러 바퀴 돈다).
        //   🔑 **정적 바이트 비교로 "링을 넘는다"고 판단하면 틀린다** — 시간축이 빠진 계산이다.

        // 🔴🔴 **바이트만 보면 틀린다.** 건수 상한이 따로 있고 **그쪽도 본다.**
        //   `DOWN_BATCH_MAX_N` 은 장치의 ACK 배출률에서 유도된 값이다(지금 4).
        //   ⚠ 바이트로 8이 들어가도 **건수에서 4로 끊긴다** — 나머지는 다음 창으로 밀린다.
        //   🔑 상한이 셋이면(바이트·건수·ACK 되돌림) **가장 작은 것이 답이다.**
        return (byN < DOWN_BATCH_MAX_N) ? byN : DOWN_BATCH_MAX_N;
    }

    // 묶음 발행. 🔑 **큐가 이미 한 거래로 묶는다** — 여기서 더하는 것은 **상한과 보고**다.
    //   ⚠ 상한을 넘기면 **자동 분할하지 않고 거절한다.** 분할하면 "묶었는데 왜 느리지"가 조용해진다.
    void send_batch(const std::string& devid,
                    const std::vector<std::pair<std::string, long> >& items,
                    int* queued, int* rejected,
                    sock_t ws_fd = BAD_SOCK, const std::string& brid = "") {
        *queued = 0; *rejected = 0;
        // 🔴 **실제 줄 길이로 잰다.** `max_per_batch()` 는 *어떤 인자가 와도* 되는 하한이고,
        //   여기서는 이 배치의 **진짜 바이트**를 세므로 짧은 인자면 더 들어간다.
        //   예) LCD 7자리 줄은 18B 라 링 64B 에 **3건**. 최악값(23B)으로는 2건뿐이다.
        //   🔑 상수는 **보장**을 말하고 이 계산은 **이번 배치**를 말한다. 둘 다 필요하다.
        size_t need = 0;
        for (size_t i = 0; i < items.size(); i++) {
            char est[48];
            // rid 는 최대 3자리(RID_SPACE 1000). idx·인자는 실제 값으로 센다.
            snprintf(est, sizeof(est), "G,999,%d,%ld,xx\n", 255, items[i].second);
            need += strlen(est);
        }
        if (!need) need = 1;
        need -= 1;                                   // 마지막 줄 뒤 LF 는 payload 안에 이미 있다
        // 🔴 **축을 섞지 마라.** 바이트는 위에서 **실제 길이**로 이미 쟀다 —
        //   여기서 최악값 기반 건수를 또 씌우면 **같은 바이트를 두 번 세는 것**이고,
        //   짧은 인자를 쓰는 배치가 이유 없이 거절된다. (내 시험이 그것을 잡았다)
        const int    capN = DOWN_BATCH_MAX_N;        // 건수 축 — ACK 배출률에서 유도
        const size_t capB = (size_t)DOWN_BATCH_CAP_B; // 바이트 축 — 하행 한 거래 상한
        if ((int)items.size() > capN || need > capB) {
            *rejected = (int)items.size();
            char rb[288];
            snprintf(rb, sizeof(rb),
                     "묶음 거절 — %zu건 %zuB 는 한 창에 안 들어간다 "
                     "(건수 상한 %d · 바이트 상한 %zuB). **나눠서 보내라** — "
                     "자동으로 쪼개면 두 창에 걸쳐 나가고 **묶은 뜻이 사라진다**",
                     items.size(), need, capN, capB);
            logf("!", rb);
            return;
        }
        // 🔴 **표지를 하나 발급한다** — 이 표지를 가진 줄들은 창에 통째로 들어가거나
        //   통째로 미뤄진다. 그것이 "묶는다"의 뜻이다("묶어진다"가 아니라).
        const long long bid = ++batch_seq_;
        for (size_t i = 0; i < items.size(); i++) {
            if (send_to_module(devid, items[i].first, items[i].second, bid, ws_fd, brid)) (*queued)++;
            else (*rejected)++;      // 사유는 send_to_module 이 이름을 지목해 로그에 남긴다
        }
    }

    // 🔴🔴 **모듈에 값을 보낸다** — 공개 API `ParkingServer::send()` 의 실물 (2026-08-19)
    //
    //   전에는 하행 경로가 **차단봉 전용**이었다(`dispatch_gate`, 0/1). 기여자가 자기 모듈에
    //   값을 보낼 방법이 없었다. 사용자 요구가 그것이다 —
    //   *"on/off 형태, LCD 에 전달하는 7자리 숫자 구조, 특정 동작을 수행할 수 있는 명령 구조"*
    //
    // 🔑 **`arg` 의 뜻을 서버는 모른다.** 켜기/끄기인지, 표시할 숫자인지, 동작 번호인지는
    //   **기여자가 정하고 양쪽(서버 호출부·장치 콜백)에 적는다.**
    //   🔴 **그 표가 어긋나면 조용히 다른 동작을 한다** — 값으로는 검증되지 않는다.
    //
    //   반환: 전선 큐에 들어갔으면 true. **`true` 는 "보냈다"이지 "됐다"가 아니다** —
    //   실제 수행 여부는 장치의 ACK(`result`)와 다음 `S` 의 에코가 답한다.
    // 🔴 `ws_fd`/`brid` (2026-08-20 · REQ-0281 · 잠금 concern `server-wire`=REQ-0272)
    //   **전선 형식은 한 글자도 안 바뀐다.** 바뀌는 것은 *"답을 어디로 돌려주나"* 하나다.
    //   기본값을 유지해 기여자 경로(`ParkingServer::send()`)는 그대로 둔다.
    bool send_to_module(const std::string& devid, const std::string& name, long arg,
                        long long batch_id = 0,
                        sock_t ws_fd = BAD_SOCK, const std::string& brid = "") {
        const Node* n = node_by_devid(devid);
        if (!n) {
            logf("!", "발행 실패 — 노드 `" + devid + "` 를 모른다 "
                      "(아직 안 붙었거나 devid 가 다르다)");
            return false;
        }
        int idx = -1;
        for (size_t k = 0; k < n->mods.size(); k++)
            if (n->mods[k].first == name) { idx = (int)k; break; }
        if (idx < 0) {
            logf("!", "발행 실패 — 노드 `" + devid + "` 에 모듈 `" + name + "` 이 없다. "
                      "**그 장치가 등록한 이름과 맞춰라**(대소문자까지)");
            return false;
        }
        // ⚠ 장치 파서가 `gidx <= 0xFF` 로 막는다. 넘으면 **보내도 거절된다** — 여기서 미리 말한다.
        if (idx > 255) {
            logf("!", "발행 실패 — 모듈 idx " + std::to_string(idx) + " 가 255 를 넘는다(장치 한계)");
            return false;
        }
        uint16_t rid = alloc_rid();
        if (rid == RID_NONE) { logf("!", "rid 공간 고갈 — 발행 포기"); return false; }
        Pending p;
        // 🔑 **`ws_fd == BAD_SOCK` 이면 답할 곳이 없다** — 기여자의 `srv.send()` 가 그 경우다.
        //   그 갈래를 자가검증이 지키고 있다(㊵ — "화면이 안 시킨 명령은 화면에 아무것도 안 보낸다").
        //   ⚠ 전에는 이 사실이 **주석에만** 있었다. 주석을 고치면 같이 사라지므로 시험으로 옮겼다.
        p.wire_rid = rid; p.ws_fd = ws_fd; p.browser_rid = brid;
        p.web_cmd = (ws_fd != BAD_SOCK);
        p.slot = name; p.kind = 'G'; p.mod_idx = idx; p.g_arg = arg;
        p.sent_ms = now_ms(); p.tries = 1;
        pend[rid] = p;
        if (!enqueue_down(pend[rid], build_line(gate_prefix(p)), true, false, batch_id)) {
            pend.erase(rid); rid_release(rid);
            return false;
        }
        gate_q++;
        // 🔑 **화면이 `requested` 로 볼 값.** 전선에 나간 뒤가 아니라 **큐에 든 뒤** 기록한다 —
        //   큐에서 죽어도 "내가 무엇을 시켰나"는 남아야 한다.
        mod_req[devid + "\t" + name] = arg;
        return true;
    }

    // 🔴 `dispatch_gate()` 가 여기 있었다. **2026-08-20 에 지웠다** — 호출자가 없어졌다.
    //   게이트 내장 버튼이 `1=열기 · 0=닫기` 를 **서버가 정한 뜻으로** 보냈고,
    //   장치의 기여자 표는 `1=열기 · 2=닫기` 였다. **닫기에서 갈려 장치가 거절했다**(실기).
    //   ⚠ 남겨 두지 않았다 — 호출자 없는 코드는 **있는 것처럼 읽힌다.**
    //   지금 모듈에 명령하는 길은 `send_to_module()` 하나다.
    //   ⚠ 자리 단위 `completion` 도 같이 없앴다 — 답은 **모듈의 `confirmed`** 가 나른다.
    //     **그게 맞다.** 그 자리에 서버가 정한 명령이 더는 없다. 값은 모듈의 `confirmed` 가 나른다.

    static std::string sim_prefix(const Pending& p) {
        char buf[32];
        snprintf(buf, sizeof(buf), "M,%u,", p.wire_rid);
        return std::string(buf);
    }
    static std::string test_prefix(const Pending& p) {
        char buf[64];
        snprintf(buf, sizeof(buf), "T,%u,%c,%s,%s,",
                 p.wire_rid, p.top, p.slot.c_str(), p.user_id.c_str());
        return std::string(buf);
    }

    // ---------- 타이머 (§7.3 재전송 / §3.4 offline)
    void tick() {
        long long t = now_ms();
        // ── 창을 포기하고 쏘는 경로 (설계 §3)
        // `S` 가 안 오면 창이 영영 안 열려 **하행이 조용히 사라진다.** 안 쏘는 것보다
        // 나가서 실패하는 것이 낫다 — 조용한 소실이 더 나쁘다.
        // ⚠ `DOWN_DMAX_MS`(2400) < `OFFLINE_MS`(3500) 이므로, 서버가 "장치 없음"으로
        // 판단하기 **전에** 한 번은 시도한다.
        // ⚠ `ard_seen` 이 false 면 `ard_last_ms` 는 의미 없는 0 이라 이 조건이 즉시 참이다 —
        // **그게 맞다.** 승격 직후 재하달은 창 개념이 없고, 보통은 같은 틱의 첫 `S` 처리가
        // 먼저 내보내므로 여기까지 오지 않는다.
        // ⚠ **슬롯당 1회로 묶는다**(위 `last_dmax_ms` 주석). 안 묶으면 200ms 버스트가 된다.
        // ── §5 등록 질의 `Q` ────────────────────────────────────────────────────
        // 🔴 **상한은 서버에 둔다 — 비용이 여기 있다.** `Q` 한 번이 **창당 1거래를 독점**하고
        //    그동안 그 노드의 하행이 못 나간다. 장치가 답하는 비용은 배치 하나이고 내 창을 안 먹는다.
        //    **비용 없는 쪽에 상한을 두면 복구 경로만 지운다**(설계 §5 · arduino 합의).
        // ⚠ **`Q` 를 보내는 것 자체가 이 소켓이 승격됐다는 뜻이다** — 승격 전이면 보낼 곳을 모른다.
        if (ard != BAD_SOCK && ard_seen && !park.reg_done && !park.reg_giveup
            && park.reg_first_ms && (t - park.reg_first_ms) > REG_TIMEOUT_MS
            && (t - park.last_q_ms) >= DOWN_SLOT_MS) {
            if (park.q_sent >= REG_Q_MAX) {
                park.reg_giveup = true;
                reg_giveups++;
                logf("!", "등록 포기 — `Q` " + std::to_string(REG_Q_MAX)
                          + "회에도 `D` 가 안 왔다. node_unregistered 로 굳힌다 (device="
                          + park.devid + ")");
                // 🔑 **끝없이 묻는 것보다 "모른다"를 확정하는 것이 낫다.** 안 그러면 이 노드가
                //    매 창을 질의로 먹는다. 재무장은 **새 세션**이다(설계 §5 의 열린 자리).
            } else {
                park.q_sent++; park.last_q_ms = t; reg_qsent++;
                // `Q,<ck>` — **인자 없음.** 주소는 소켓이 갖고 있고, 이 시점의 devid 는
                // 서버가 확신할 수 없는 값이다(미등록이라 묻는 것이다 · 설계 §5).
                std::string q = build_line("Q,");
                if (!send_raw(ard, q.data(), q.size(), "아두이노")) ard_send_failed();
                else logf("→ARD", "Q (등록 질의 " + std::to_string(park.q_sent) + "/"
                                  + std::to_string(REG_Q_MAX) + ")");
            }
        }

        // 🔴 `dmax_armed` 가 주 조건이다. 슬롯 문턱은 **그물로 남긴다** —
        //    재무장 논리에 결함이 생겨도(예: 줄이 폭주) 버스트로 돌아가지 않게.
        if (!downq.empty() && ard != BAD_SOCK && (t - ard_last_ms) > DOWN_DMAX_MS
            && dmax_armed && (t - last_dmax_ms) >= DOWN_SLOT_MS) {
            last_dmax_ms = t;
            dmax_armed = false;        // 장치가 한 줄이라도 말할 때까지 다시 안 쏜다
            dmax_flushes++;            // 그래서 이 수가 "몇 번 포기했나"로 읽힌다
            // ⚠ **탐침 크기로 쏜다**(4건 아님) — 증거가 없는 자리다.
            flush_downq("S 가 안 온다 — 창 포기(탐침)", true, DOWN_PROBE_N);
        }
        std::vector<uint16_t> dead_rids;
        for (std::map<uint16_t, Pending>::iterator it = pend.begin(); it != pend.end(); ++it) {
            Pending& p = it->second;
            // 🔴 **큐에서 기다리는 동안은 ACK 시계가 안 돈다.** 안 그러면 전선에 나가기도 전에
            // 재전송이 걸려 같은 rid 가 큐에 두 번 들어간다 — 큐가 없애려던 증폭이 큐 안에서 생긴다.
            if (p.queued) continue;
            if (t - p.sent_ms < ACK_TIMEOUT_MS) continue;
            if (p.tries >= ACK_MAX_TRIES) {
                ack_fail_count++;                         // 하행 건강 지표(소크 요약)
                logf("!", "ACK 타임아웃 최종 실패 wire_rid=" + std::to_string(p.wire_rid));
                send_err(p.ws_fd, p.browser_rid, "ack_timeout", "센서가 응답하지 않습니다");
                // 🔴 **무응답 — 재전송을 다 쓰고도 답이 없다.** 거절과 **다른 사건**이다.
                if (p.kind == 'G') notify_cmd(p, CmdResult::NO_ANSWER, -1);
                dead_rids.push_back(it->first);
                continue;
            }
            p.tries++;
            retx_count++;                                 // 하행 건강 지표(소크 요약)
            p.sent_ms = t;
            char buf[64];
            std::string line;
            if (p.kind == 'G') line = gate_prefix(p);      // 🔑 같은 rid → 장치가 멱등 캐시로 같은 답을 준다
            else if (p.kind == 'M') line = sim_prefix(p);  // 재전송이 두 걸음이 되면 안 된다(§12B.4)
            else if (p.kind == 'T') line = test_prefix(p); // 테스트도 같은 wire_rid 로 재전송
            else if (p.kind == 'R') {
                snprintf(buf, sizeof(buf), "R,%u,%s,%s,", p.wire_rid, p.slot.c_str(), p.user_id.c_str());
                line = buf;
            } else {
                snprintf(buf, sizeof(buf), "C,%u,%s,", p.wire_rid, p.slot.c_str());
                line = buf;
            }
            logf("↻", "재전송 " + std::to_string(p.tries) + "/" + std::to_string(ACK_MAX_TRIES)
                      + " (같은 wire_rid=" + std::to_string(p.wire_rid) + ") — 큐에 다시 넣는다");
            // 🔴 **재전송도 하행이다 — 같은 큐를 탄다.** 창 밖에서 쏘면 규율이 무의미해진다.
            //   announce=false : `queued` 를 다시 보내면 같은 rid 의 `expires_ms` 가 **늘어나** 단조성이 깨진다
            //   force=true     : 이미 약속한 건이므로 깊이 상한으로 거절하지 않는다
            // ⚠ 대가: 재시도 간격이 창 주기에 종속된다. `ACK_TIMEOUT_MS`(1500) + 창 대기(≤1200)
            //   → 3회 소진이 최악 ≈6.9초(전에는 4.5초). web 타이머는 `expires_ms(≤4800) + 6000`
            //   = 10.8초라 **아직 안 겹친다.** `ACK_TIMEOUT_MS` 는 **일부러 안 건드렸다** —
            //   한 교체에 변경 둘을 넣으면 깨질 때 어느 것 때문인지 못 가른다(원장 §6).
            // 🔴 **반환값을 봐야 한다.** 링크가 끊긴 뒤에는 `end_ard_session` 이 큐만 비우고
            // **전선에 나가 있던 pend 는 남는다.** 그 건이 여기로 와서 거절되면(`ard==BAD_SOCK`)
            // 이미 `device_offline` 로 답이 갔는데, 반환값을 무시하면 `pend` 에 남아
            // 몇 틱 뒤 **`ack_timeout` 으로 한 번 더** 답한다.
            // ⚠ 그 이름은 **"전선에 나갔다. 3회 재전송까지 했다"** 를 뜻한다(§8.16) —
            // 서버가 스스로 거절한 건에 붙이면 **로그에 거짓 문장이 남고 `ack_fail_count` 도 오염된다.**
            // 08-17 07:54:40 줄이 정확히 그 형태였다. 같은 것을 새 코드에 만들지 않는다.
            if (!enqueue_down(p, build_line(line), false, true)) {
                dead_rids.push_back(it->first);   // ⚠ 루프 안에서 erase 하면 반복자가 무효화된다
                continue;
            }
        }
        for (size_t i = 0; i < dead_rids.size(); i++) { pend.erase(dead_rids[i]); rid_release(dead_rids[i]); }
    }

    // 그 자리에 아직 ACK 를 못 받은 요청이 있는가.
    // 있으면 세계관이 갈라진 게 아니라 "진행 중"일 뿐이므로 손대면 안 된다(§7.6).
    bool has_pending_for(const std::string& slot) const {
        for (std::map<uint16_t, Pending>::const_iterator it = pend.begin(); it != pend.end(); ++it)
            if (it->second.slot == slot) return true;
        return false;
    }

    // ---------- §7.5 예약 은퇴
    // 예약된 자리에서 차가 빠지면(occupied 1→0) 그 예약은 소진된 것이다.
    // **ACK 를 기다리지 않고 서버 쪽에서 즉시 확정한다** — ACK 실패 시 살려 두면
    // 애초에 이걸 만들게 한 "죽은 자리" 버그가 은퇴 경로로 되돌아온다.
    void retire(int i) {
        logf("⏏", std::string("예약 소진(occupied 1→0) — ") + SLOT_ID[i]
                  + " 은퇴시킨다 (user=" + (slots[i].user_id.empty() ? "-" : slots[i].user_id) + ")");
        slots[i].reserved = 0;
        slots[i].user_id.clear();
        slots[i].reserved_at = 0;
        dispatch('C', BAD_SOCK, "", SLOT_ID[i], "");   // 아두이노에도 반영시킨다
    }

    // ---------- 아두이노 재부팅 → 재동기화 (§7.4)
    void resync_reservations(const char* why) {
        resync_count++;                      // --selftest 계측용 (§7.4 오탐 증명)
        std::vector<int> live;
        for (int i = 0; i < 10; i++) if (slots[i].reserved) live.push_back(i);
        logf("⟳", std::string("재부팅 감지(") + why + ") — 살아 있는 예약 "
                  + std::to_string(live.size()) + "건 재하달");
        for (size_t k = 0; k < live.size(); k++)
            dispatch('R', BAD_SOCK, "", SLOT_ID[live[k]], slots[live[k]].user_id);
    }
