// persist.h — data_log.json 읽기·쓰기 (§9). `struct Server` 의 몸통 조각.
    // ═══════════════════════════════════════════════════════════════════
    // 🔴 **단독으로 컴파일되지 않는다.** `struct Server` 의 **몸통 조각**이고
    //   `server.cpp` 안 그 자리에 include 된다. **위치가 곧 문법이다.**
    // 🔑 옮긴 것이지 고친 것이 아니다 — 전처리 결과가 같아 **`.o` 가 바이트 동일**해야 한다.
    //   대조가 0 이 아니면 이동이 아니라 재배치다. 되돌리고 보고한다(REQ-0272).
    // ═══════════════════════════════════════════════════════════════════

    // ---------- data_log.json (§9)
    std::string bits(bool reserved_bits) {
        std::string s;
        for (int i = 0; i < 10; i++)
            s += char('0' + (reserved_bits ? slots[i].reserved : slots[i].occupied));
        return s;
    }
    std::vector<std::string> log_entries;
    void write_log_if_changed() {
        if (no_disk) return;                 // --selftest 는 파일을 건드리지 않는다
        // 테스트 상태도 키에 넣는다 — 안 넣으면 무장/주입이 파일에 반영되지 않고
        // 폴백 화면이 낡은 "해제" 상태를 계속 보여 준다(개정 4 가 막으려는 바로 그 증상).
        // §9.4(개정 9) — **키에 online 을 넣는다.** 안 넣으면 장치가 죽어도 비트열이 그대로라
        // 파일이 안 써지고 `device.online` 이 영원히 true 로 남는다 — 필드가 거짓말을 한다.
        // 넣으면 오프라인/복귀 **전이가 그 자체로 상태 변화**가 되어 그 순간 한 건이 기록된다.
        // (`last_frame_ts` 는 키에 넣지 않는다. 넣으면 매 프레임이 변화가 되어 초당 한 번 쓴다.)
        bool online_now = device_online();
        std::string key = bits(false) + "|" + bits(true) + "|" + (test_armed ? "A" : "-");
        for (int i = 0; i < 10; i++) key += char('0' + ((test_armed && test_ovr[i]) ? 1 : 0));
        key += online_now ? "|O" : "|X";
        if (key == last_bits) return;            // §9.4 — 내용이 같으면 안 쓴다
        last_bits = key;

        std::ostringstream e;
        e << "{\"ts\":" << epoch_ms() << ",\"device_id\":" << jstr(ard_dev)
          << ",\"uptime\":" << (ard_uptime < 0 ? 0 : ard_uptime)
          << ",\"seq\":" << (ard_seq < 0 ? 0 : ard_seq)
          // §9.1(개정 9) — 장치 생사. 이게 없으면 폴백 화면이 **죽은 장치의 낡은 값을
          // 실측처럼 그린다**(개정 4의 test_mode 누락과 같은 종류의 구멍이었다).
          // last_frame_ts 는 epoch 시각이고 "파일을 쓴 시각"이 아니다 — 둘이 갈리는 게 요점이다.
          << ",\"device\":{\"online\":" << (online_now ? "true" : "false")
          // ⚠ 한 번도 프레임을 못 받았으면 `0` 이 아니라 **`null`** 이다(REQ-0132).
          // `0` 은 epoch 로 읽히므로 화면이 **1970년으로부터의 나이**를 그린다.
          // WS 스냅샷(`snapshot_json()`)과 **같은 규칙**이어야 한다 — 두 경로가 갈리면
          // 폴백 여부에 따라 화면이 다른 말을 한다.
          << ",\"last_frame_ts\":" << (ard_last_epoch_ms > 0
                                       ? std::to_string(ard_last_epoch_ms) : std::string("null"))
          << "}"
          << ",\"occupied\":\"" << bits(false) << "\",\"reserved\":\"" << bits(true) << "\"";
        // §9.1(개정 4) — 무장 여부와 칸별 주입 표시를 **파일에도** 넣는다.
        // WS 가 끊기면 브라우저는 이 파일로 폴백하는데, 이 두 필드가 없으면
        // **폴백 화면이 주입값을 실측처럼 그린다** — §12A.6 이 폴백 경로에서만 깨지고 있었다.
        {
            int n = 0;
            for (int i = 0; i < 10; i++) if (test_armed && test_ovr[i]) n++;
            e << ",\"test_mode\":{\"armed\":" << (test_armed ? "true" : "false")
              << ",\"override_count\":" << n << "}";
        }
        e << ",\"slots\":[";
        for (int i = 0; i < 10; i++) {
            if (i) e << ",";
            e << "{\"id\":\"" << SLOT_ID[i] << "\",\"occupied\":" << slots[i].occupied
              << ",\"reserved\":" << slots[i].reserved
              << ",\"overridden\":" << ((test_armed && test_ovr[i]) ? 1 : 0) << "}";
        }
        e << "]}";

        log_entries.insert(log_entries.begin(), e.str());       // 최신이 앞
        while ((int)log_entries.size() > LOG_KEEP) log_entries.pop_back();

        std::string body = "[\n";
        for (size_t i = 0; i < log_entries.size(); i++) {
            body += "  " + log_entries[i];
            if (i + 1 < log_entries.size()) body += ",";
            body += "\n";
        }
        body += "]\n";

        atomic_write_log(body);
    }

    // tmp 에 다 쓰고 원자적으로 갈아끼운다 (§9.2). 잠금도 복사본도 필요 없다.
    // **쓰기 경로는 이것 하나다** — 기동 시 빈 배열을 만들 때도 이 함수를 쓴다.
    void atomic_write_log(const std::string& body) {
        const char* tmp = "data_log.json.tmp";
        const char* dst = "data_log.json";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (!f) { logf("!", "data_log tmp 열기 실패"); return; }
            f << body;
            f.flush();
        }
        // 실패하면 **원인 코드를 반드시 남긴다.** 안 남기면 data_log.json 이 조용히 옛 내용을
        // 유지하는데 로그에는 아무것도 없다. 윈도우에서 백신·편집기가 파일을 잠깐 잡아
        // ERROR_SHARING_VIOLATION(32) 이 나는 것은 흔한 일이라 이 구분이 실제로 필요하다.
#ifdef _WIN32
        // 윈도우는 대상이 있으면 rename 이 실패한다 (§9.3).
        // "먼저 지우고 rename" 으로 흉내내면 그 틈에 읽은 클라이언트가 404 를 본다 — 원자성이 깨진다.
        if (!MoveFileExA(tmp, dst, MOVEFILE_REPLACE_EXISTING)) {
            DWORD e = GetLastError();
            char b[128];
            snprintf(b, sizeof(b), "MoveFileEx 실패 GetLastError=%lu%s",
                     (unsigned long)e,
                     e == 32 ? " (ERROR_SHARING_VIOLATION — 다른 프로그램이 파일을 잡고 있다)" : "");
            logf("!", b);
        }
#else
        if (rename(tmp, dst) != 0) {
            char b[128];
            snprintf(b, sizeof(b), "rename 실패 errno=%d (%s)", errno, strerror(errno));
            logf("!", b);
        }
#endif
    }

    // ── 🔴 A[1](B) 커서 영속 — **재시작을 건너 단조성을 유지한다**
    //
    // ⚠ **기본은 꺼져 있다**(`rid_persist_on = false`). 자가검증이 만드는 수십 개의 `Server` 가
    //   실기 커서 파일을 덮어쓰는 것을 **구조적으로** 막는다 — 시험이 운영 상태를 건드리면
    //   그건 시험이 아니라 사고다. **실기 기동 경로만 `rid_cursor_load()` 를 부른다.**
    // ── 🔴 `rid` 는 이제 `RidPool` 의 책임이다 (REQ-0272 3단계 · 2026-08-19)
    //
    //   전에는 상태 열 개와 함수 넷이 이 구조체 안에 흩어져 있었다. **자료가 아니라 책임을 옮겼다** —
    //   커서·격리표·해제 순번·블록 예약·디스크 경로가 전부 `ridpool.h` 뒤로 갔다.
    //   🔑 **여기 남는 것은 "무엇이 못 쓰는 rid 인가" 하나다.** 그건 `pend` 를 가진 이쪽만 안다.
    //   ⚠ 계수는 그대로 요약에 낸다 — **감출 것은 복잡함이고 드러낼 것은 상태다.**
    RidPool ridpool_;
    struct PendHas {
        const std::map<uint16_t, Pending>* m;
        bool operator()(uint16_t r) const { return m->count(r) != 0; }
    };
    uint16_t alloc_rid() {
        PendHas in; in.m = &pend;
        return ridpool_.alloc(in, now_ms());
    }
    void rid_release(uint16_t rid) { ridpool_.release(rid, now_ms()); }
    // ── 노드 대장 (온보딩 2단계 · `docs/net/DESIGN-node-ledger.md`) ─────────
    // 🔑 **경로를 만드는 것은 서버의 일이다**(오프셋에 따라 갈린다). 대장은 경로를 받기만 한다.
    // ⚠ `no_disk`(자가검증)면 빈 경로를 준다 — 대장이 스스로 "영속 안 함"으로 돈다.
    void ledger_load() {
        ledger_.load(no_disk ? std::string() : node_ledger_path(), srv_id);
        if (!ledger_.persistOn()) {
            // 🔴 조용히 넘어가면 **"재기동했더니 등록이 사라졌다"의 원인을 못 찾는다.**
            logf("!", "노드 대장 — **영속 안 함**"
                      + std::string(no_disk ? " (자가검증)" : " (HOME 없음 — 메모리로만 돈다)"));
            return;
        }
        char b[320];
        snprintf(b, sizeof(b),
                 "노드 대장 — %lld 노드 이어받음 · 할당 %zu · 깨진줄 %lld · 모르는줄 %lld · %s",
                 ledger_.loaded(), ledger_.assignCount(),
                 ledger_.linesBad(), ledger_.linesUnknown(), ledger_.path().c_str());
        logf("=", b);
    }
    // 🔴 **사건이 있을 때만 쓴다**(명세 §0.4). 매 프레임·매 슬롯이 아니다.
    //   `save()` 는 `dirty` 가 아니면 아무것도 안 하므로 불러도 싸다.
    void ledger_save(const char* why) {
        if (!ledger_.dirty()) return;
        if (!ledger_.save(srv_id))
            logf("!", std::string("노드 대장 저장 실패(") + why + ") — 실패 누적 "
                      + std::to_string(ledger_.saveFails()));
    }

    void rid_cursor_load() {
        // 🔑 **경로를 만드는 것은 서버의 일이다**(오프셋에 따라 갈린다). 풀은 경로를 받기만 한다.
        ridpool_.loadCursor(rid_cursor_path(), epoch_ms(), no_disk);
    }


