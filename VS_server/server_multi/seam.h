// seam.h — 이음매: 디바이스 → 도메인 (REQ-0096 단계 C). `struct Server` 의 몸통 조각.
    // ═══════════════════════════════════════════════════════════════════
    // 🔴 **단독으로 컴파일되지 않는다.** `struct Server` 의 **몸통 조각**이고
    //   `server.cpp` 안 그 자리에 include 된다. **위치가 곧 문법이다.**
    // 🔑 옮긴 것이지 고친 것이 아니다 — 전처리 결과가 같아 **`.o` 가 바이트 동일**해야 한다.
    //   대조가 0 이 아니면 이동이 아니라 재배치다. 되돌리고 보고한다(REQ-0272).
    // ═══════════════════════════════════════════════════════════════════

    // ---------- 이음매: 디바이스 → 도메인 (REQ-0096 단계 C)
    // **디바이스 계층은 이것만 부른다.** 무엇을 할지는 도메인이 정한다.
    // **방금 넣은 이벤트의 참조를 돌려준다.** 호출자가 `pending_events.back()` 을 다시 집으면
    // "emit_dev 는 반드시 push 한다"는 전제가 코드가 아니라 우연에 걸린다 — 나중에 여기에
    // 조기 반환(중복 억제·devid 가드·상한)이 하나 들어오는 순간 빈 벡터에 back() 을 부른다.
    // 경고 0 · 자가검증 통과로 다 빠져나가고 운영에서만 터지는 형태다.
    DeviceEvent& emit_dev(uint8_t kind, const std::string& dev, const std::string& reason) {
        DeviceEvent e;
        seam_clear_event(&e);
        e.kind = kind;
        seam_set_dev(e.device_id, dev.c_str());
        // reason 은 표시·기록용이라 잘려도 판정이 안 바뀐다. 다만 **잘렸다는 사실은 남긴다.**
        size_t n = sizeof(e.reason) - 1;
        if (reason.size() <= n) {
            memcpy(e.reason, reason.c_str(), reason.size());
            e.reason[reason.size()] = '\0';
        } else {
            memcpy(e.reason, reason.c_str(), n - 1);
            e.reason[n - 1] = '~';          // 잘림 표시
            e.reason[n] = '\0';
        }
        pending_events.push_back(e);
        return pending_events.back();
    }

    // DEV_ACK 전용 — 계약(`server_seam.h`)이 `rid`·`result` 를 정의해 뒀으므로 **채워서 낸다.**
    // 지금 소비자는 이 둘을 안 읽지만, 0 으로 두면 이벤트가 "rid 0 번 명령의 성공"이라고
    // 거짓말한다. 값이 있는데 안 싣는 것과 없는 것은 다르다.
    void emit_dev_ack(const std::string& dev, uint16_t rid, uint8_t result,
                      const std::string& reason) {
        DeviceEvent& e = emit_dev(DEV_ACK, dev, reason);   // 반환 참조를 쓴다(back() 아님)
        e.rid    = rid;
        e.result = result;
    }

    // 도메인이 이벤트를 소비한다. `run()` 루프 끝에서 한 번에 부른다.
    // **여기서 하는 일이 옮기기 전의 직접 호출과 같아야 한다** — 단계 C 는 구조만 바꾸고
    // 동작은 안 바꾼다. 같은 틱 안에서 소비하므로 지연도 없다(브라우저가 보는 것은 동일).
    void drain_dev_events() {
        if (pending_events.empty()) return;
        bool need_snapshot = false, need_log = false;
        for (size_t i = 0; i < pending_events.size(); i++) {
            switch (pending_events[i].kind) {
                case DEV_DISCONNECT:
                    need_snapshot = true;   // 화면에 "센서 끊김"이 뜨게(옮기기 전과 동일)
                    break;
                case DEV_ACK:
                    // 옮기기 전 A 분기 끝의 `write_log_if_changed(); push_snapshot();` 과 같다.
                    // ⚠ **예약 상태 변경(slots[])은 아직 A 분기에 그대로 있다.** 그것까지 옮기면
                    // `send_ack` 와 상태 변경의 순서가 바뀐다 — 이음매 1 의 마지막(DEV_SENSORS)에서
                    // 같은 문제를 한꺼번에 다룬다. 여기서는 **기록·화면만** 옮긴다.
                    need_log = true; need_snapshot = true;
                    break;
                case DEV_ONLINE:
                case DEV_OFFLINE:
                    // ⚠ 이 두 종류는 **파일 쓰기까지** 해야 한다(§9.4 개정 9).
                    // 안 하면 장치가 조용할 때 `device.online` 이 영영 false 로 안 남는다 —
                    // 필드가 가장 필요한 순간에 거짓말을 한다.
                    need_log = true; need_snapshot = true;
                    break;
                default:
                    break;                  // 아직 안 옮긴 종류 — 직접 호출이 담당한다
            }
        }
        pending_events.clear();
        // 한 틱에 여러 건이 겹쳐도 각각 한 번이면 된다(같은 내용을 두 번 보낼 이유가 없다).
        // 순서는 옮기기 전과 같게 **기록 먼저, 그다음 화면**이다.
        if (need_log) write_log_if_changed();
        if (need_snapshot) push_snapshot();
    }

    void send_err(sock_t fd, const std::string& rid, const char* code, const char* msg) {
        std::ostringstream o;
        o << "{\"type\":\"error\",\"rid\":" << (rid.empty() ? std::string("null") : jstr(rid))
          << ",\"code\":\"" << code << "\",\"message\":" << jstr(msg) << "}";
        if (fd != BAD_SOCK && conns.count(fd)) ws_send(fd, o.str());
    }
    // §4-B (REQ-0155 에서 web 과 합의한 계약) — **"받았고 아직 안 보냈다"**
    //
    // 🔴 **별도 타입이어야 한다. `ack` 에 필드로 얹으면 안 된다.** web 의 dispatcher 는
    // 모르는 타입을 조용히 무시하므로 별도 타입은 옛 화면에서 안전하지만, `ack` 에 얹으면
    // **옛 화면이 `type==='ack'` 만 보고 pending 을 지우며 "예약되었습니다"를 띄운다** —
    // **전선에 나가지도 않은 요청을 성공으로 선언한다.**
    //
    // `ahead`      : 앞에 몇 건. "대기 중"만으로는 **멈춘 것과 밀린 것**을 못 가른다.
    // `expires_ms` : 🔴 **지속시간이다. 절대 시각이 아니다.** epoch 로 주면 시계 어긋난 만큼 틀린다.
    //   그리고 이 값이 재는 것은 **"큐에서 나갈 때까지"**이고 **최종 결말까지가 아니다** —
    //   web 이 자기 타이머를 `expires_ms + 6초` 로 잡으므로, 여기에 총예산을 넣으면 **이중 계산**이 된다.
    void send_queued(const DownQ& q) {
        int ahead = 0;
        for (size_t i = 0; i < downq.size(); i++) {
            if (downq[i].wire_rid == q.wire_rid) break;
            ahead++;
        }
        long long left = q.deadline_ms - now_ms();
        if (left < 0) left = 0;                 // 마감을 넘겨도 음수를 내보내지 않는다
        std::ostringstream o;
        o << "{\"type\":\"queued\",\"rid\":" << jstr(q.brid)
          << ",\"slot\":" << (q.slot.empty() || q.slot == "??" ? std::string("null")
                                                              : std::string("\"") + q.slot + "\"")
          << ",\"ahead\":" << ahead
          << ",\"expires_ms\":" << left
          // 🔴 **이탈 후 예산**(REQ-0166). `expires_ms` 는 큐 대기까지만 덮으므로
          // 이 둘을 더한 것이 화면이 기다려야 하는 전부다. **화면이 짐작하지 않는다.**
          // ⚠ 필드를 더하는 것이 안전한 이유: `queued` 자체가 새 타입이라
          // **모르는 화면은 프레임 통째로 무시한다** — 옛 화면을 깨뜨리는 경로가 없다.
          << ",\"ack_budget_ms\":" << ack_budget_ms() << "}";
        if (q.ws_fd != BAD_SOCK && conns.count(q.ws_fd)) ws_send(q.ws_fd, o.str());
    }
    void send_ack(sock_t fd, const std::string& rid, const std::string& slot, int result,
                  char kind = 'R') {
        const char* m = "예약되었습니다";
        if (kind == 'T')      m = "테스트 값을 적용했습니다";
        else if (kind == 'C') m = "예약을 취소했습니다";
        else if (kind == 'M') m = "시뮬레이션 한 걸음 진행했습니다";
        // 🔴 `G` 가 없어서 이 ACK 이 "예약되었습니다" 로 나갔던 적이 있다(2026-08-19).
        //   ⚠ 시험이 전부 `ws_fd = BAD_SOCK` 이라 이 줄이 한 번도 안 돌았다 —
        //     `send_ack` 자체를 건너뛰는 경로였다. **시험이 실기와 다르게 밟은 것**이다.
        //
        // 🔴🔴 **모듈 종류를 이름으로 말하지 마라** (2026-08-20 · REQ-0306)
        //   `G` 는 **모든 모듈 명령**의 하행 타입이지 차단봉 전용이 아니다.
        //   전에는 `(top=='1') ? "차단봉을 열었습니다" : "차단봉을 닫았습니다"` 였는데
        //   `send_to_module()` 은 **`p.top` 을 안 세운다** → 항상 0 →
        //   화면에서 `LD` 토글·`L2` 숫자를 눌러도 **"차단봉을 닫았습니다"** 가 나갔다.
        //   🔑 **값의 뜻은 기여자가 정한다**(우리 제1계약). 서버가 모르는 것을 이름 붙이면
        //     반드시 틀리고, **그 틀림이 사용자에게 문장으로 보인다.**
        //   ⚠ 그래서 `top` 인자도 없앴다 — 읽는 곳이 여기뿐이었고, `G` 는 그것을 세우지 않는다.
        //     **세우지 않는 값을 읽는 코드가 이 결함의 기전이었다.**
        else if (kind == 'G') m = "장치가 명령을 수행했습니다";
        if (result == 1) m = "이미 주차된 자리입니다";
        else if (result == 2) m = "이미 예약된 자리입니다";
        // 🔴 `result=3` 의 뜻이 종류마다 다르다. `G` 에서는 **"장치가 수행할 수 없다"** 이지
        //   "잘못된 요청"이 아니다 — 사용자가 할 일이 다르다(다시 눌러도 같다 vs 요청을 고쳐라).
        else if (result == 3) m = (kind == 'G') ? "장치가 이 조작을 수행할 수 없습니다"
                                                : "잘못된 요청입니다";
        else if (result == 4) m = "테스트 모드가 꺼져 있습니다";
        // result=5 는 **성공이 아니다.** 버튼을 눌렀는데 아무 일도 안 난 것을
        // 화면이 성공으로 표시하면 안 되므로 값과 문구를 따로 둔다(§12B.4).
        else if (result == 5) m = "바꿀 시뮬 자리가 없습니다";
        std::ostringstream o;
        o << "{\"type\":\"ack\",\"rid\":" << jstr(rid) << ",\"slot\":";
        // 무장/해제처럼 자리가 없는 응답은 null 이다 — 전선의 "??" 를 그대로 흘리지 않는다(§5.4).
        if (slot.empty() || slot == "??") o << "null"; else o << "\"" << slot << "\"";
        o << ",\"result\":" << result << ",\"message\":" << jstr(m) << "}";
        if (fd != BAD_SOCK && conns.count(fd)) ws_send(fd, o.str());   // 요청자에게만
    }

