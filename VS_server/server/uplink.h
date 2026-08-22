// uplink.h — 아두이노 상행(장치 → 서버) 파서. `struct Server` 의 몸통 조각.
    // ═══════════════════════════════════════════════════════════════════
    // 🔴 **이 파일은 단독으로 컴파일되지 않는다.**
    //   `struct Server` 의 **몸통 조각**이고, `server.cpp` 안에서 그 자리에 include 된다.
    //   include 문을 클래스 밖으로 옮기면 컴파일이 깨진다 — 위치가 곧 문법이다.
    //
    // 🔑 **왜 이 형태인가**: 코드를 *옮긴 것*이지 *고친 것*이 아님을 증명하려고.
    //   전처리 결과가 원본과 같으므로 **`.o` 가 바이트 동일**해야 한다.
    //   그 대조가 0 이 아니면 이동이 아니라 재배치다 — 그때는 되돌리고 보고한다(REQ-0272).
    // ═══════════════════════════════════════════════════════════════════

    // ---------- 아두이노 라인 처리
    // ⏳ **호환 껍데기 — 자가검증 전용.** 실기 호출부 둘은 이미 `park` 를 명시로 넘긴다.
    //   자가검증 50여 곳을 이 조각에서 같이 고치면 **거동 변화 0 의 증명 범위가 넓어진다.**
    //   🔴 ②-b(소켓별 버퍼 라우팅)에서 없앤다. **그때까지 새 호출부는 이걸 쓰지 마라.**
    void on_ard_line(const std::string& line) { on_ard_line(park, line); }

    // 🔴 ②-a (REQ-0263/0262) — **노드를 인자로 받는다.** 시그니처만 바꾼다.
    //   지금 호출부는 둘 다 `park` 를 넘기므로 **거동 변화 0 이어야 하고, 산출물 대조로 증명한다.**
    //   🔑 이것이 슬롯 경로 추출의 전제다 — `park` 32회 참조가 인자로 바뀌지 않으면
    //     "노드 안에서 닫힌 파서"가 안 되고, 그러면 떼어 내도 전역을 계속 만진다(설계 §8.13).
    void on_ard_line(Node& n, const std::string& line) {
        // 🔑 **받은 바이트 수를 같이 남긴다** — `AT+CIPSEND` 조용한 잘림(③)의 유일한 판별자다.
        // 장치는 `SEND OK` 를 받으면 **성공으로 세므로 잘린 것을 모른다.**
        // **서버가 "몇 바이트 받았나"를 적어야 "보낸 만큼 왔나"를 대조할 수 있다.**
        // ⚠ 64B 이하에서는 값이 뻔하지만, **긴 줄 시험에서 이 한 칸이 시험의 본체**가 된다.
        {
            char rb[24];
            snprintf(rb, sizeof(rb), " (rx=%zuB)", line.size());
            logf("←ARD", line + rb);
        }

        // 🔴 **전제 감시 — 조건을 적었으면 그것을 보는 코드를 같은 자리에 둔다**(CLAUDE.md).
        // `DOWN_BATCH_MAX_N` 은 배출 6 에서 유도됐고, 배출 6 은 `S_worst ≤ 60B` 를 전제한다.
        // ⚠ **개정에서 그 전제가 깨지는데 서버는 아무것도 안 하고 계속 4건을 보낸다** —
        //   그게 이 감시가 없을 때의 모습이다. **적어 두기만 하면 다음 사람이 안 본다.**
        // 🔑 새 배출률을 **여기서 계산하지 않는다**(arduino 의 상수를 복제하게 된다).
        //   서버는 **"전제가 깨졌다"까지만 말하고**, 값은 원본을 가진 쪽이 준다.
        // 🔑 **장치가 말했다 = 조건이 바뀌었다.** 창 포기를 다시 무장한다(REQ-0210).
        //    `S` 만이 아니라 **어떤 줄이든** 무장한다 — 깨진 줄도 "링크가 살아 있다"는 증거다.
        dmax_armed = true;

        if (!line.empty() && line[0] == 'S') {
            if ((int)line.size() > s_max_b) s_max_b = (int)line.size();
            if ((int)line.size() > DEV_S_WORST_ASSUMED_B && !s_worst_warned) {
                s_worst_warned = true;
                char wb[224];
                snprintf(wb, sizeof(wb),
                         "전제 붕괴 — S 프레임 %dB > 가정 %dB. "
                         "DEV_ACK_DRAIN_PER_SLOT=%d(→ 하행 %d건/창)의 근거가 깨졌다. "
                         "arduino 에 배출률 재확인 필요(자리 수 n 이 커졌을 가능성)",
                         (int)line.size(), DEV_S_WORST_ASSUMED_B,
                         DEV_ACK_DRAIN_PER_SLOT, DOWN_BATCH_MAX_N);
                logf("!!", wb);
            }
        }

        // ---------- 소크 관측 (REQ-0065) — **체크섬 검사보다 먼저 센다.**
        // 여기서 재는 것은 "쓸 만한 프레임"이 아니라 **수신 자체의 공백**이다.
        // 깨진 줄도 링크가 살아 있었다는 증거이므로 공백을 끊는다.
        {
            long long t = now_ms();
            if (sess_last_line_ms) {
                long long gap = t - sess_last_line_ms;
                if (gap > sess_max_gap_ms) sess_max_gap_ms = gap;
                if (gap > all_max_gap_ms) { all_max_gap_ms = gap; all_max_gap_at = epoch_ms(); }
            }
            sess_last_line_ms = t;
            sess_frames++; all_frames++;
        }

        std::vector<std::string> f;
        if (!verify_line(line, f)) { drop_cksum++; logf("!", "체크섬 불일치 — 버림"); return; }
        if (f.empty()) return;

        if (f[0] == "S" && f.size() >= 6) {
            // 🔴 **삼중 검산 ③ — `S` 의 자리 폭이 선언 `n` 과 맞는가**(설계 §5).
            //    ①선언 n · ②실제 `D` 줄 수 · ③hex 폭 이 **한 함수(`moduleCount()`)에서 나오므로
            //    갈릴 수 없다. 갈리면 그 자체가 결함 신호다.**
            // ⚠ **등록이 끝난 노드에만 적용한다** — 옛 펌웨어는 등록을 안 하므로 이 검사를 안 탄다.
            //    (옛 10진 형식은 폭이 `n` 이고 hex 는 `ceil(n/4)` 라 규칙이 다르다)
            if (!n.reg_first_ms) n.reg_first_ms = now_ms();   // `REG_TIMEOUT` 의 기준
            if (n.reg_done && n.reg_n > 0) {
                const size_t want = (size_t)((n.reg_n + 3) / 4);
                if (f[2].size() != want) {
                    reg_widthbad++;
                    char wb[176];
                    snprintf(wb, sizeof(wb),
                             "🔴 자리 폭 불일치 — S 의 폭 %zu, 선언 n=%d 이면 %zu 여야 한다. "
                             "같은 함수에서 나온 값이 갈렸다 (누적 %lld)",
                             f[2].size(), n.reg_n, want, reg_widthbad);
                    logf("!!", wb);
                }
            }
            long seq = atol(f[1].c_str());
            long long up = atoll(f[4].c_str());
            // §7.4(개정 8) — 순환을 접은 uptime 전진량 하나로 본다.
            // **seq 는 판정에 쓰지 않는다**: 1Hz 에서 18.2시간마다 합법적으로 0 을 지나간다.
            bool reboot = uptime_says_reboot(ard_uptime, up);

            // ── 세션을 가로지르는 판정 (REQ-0118 (A)) — **보고 전용** ──────────────
            // 새 세션의 첫 프레임(`ard_uptime < 0`)에서만 본다. 지금까지 서버는 이 순간을
            // 무조건 "재부팅"이라 불렀지만, 직전 세션의 uptime 을 기억하면 갈린다:
            //   · 되감겼다  → 장치가 정말 재부팅했다
            //   · 이어진다  → 장치는 살아 있었고 **링크만 다시 선 것**이다
            // 08-16 기준선에서 새 연결 36건 중 35건이 후자였다. 지금까지 전부 전자로 셌다.
            //
            // ⚠ 위 `reboot` 변수에는 **손대지 않는다.** 그것은 §7.4 판정이고 여기는 장부다.
            if (ard_uptime < 0) {
                // 공백 G = 직전 프레임 도착 → 지금(초). **되감김만 보면 안 된다**:
                // 재부팅했는데 공백이 길면 새 uptime 이 옛 값을 넘어서 "안 죽었다"로 오분류된다.
                // (monitor 가 자기 규칙 `uptime>120` 에서 같은 함정을 밟았다 — 2026-08-16)
                //
                // 안 죽었다면 지금 uptime = 옛 uptime + G 이므로 **반드시 G 이상**이다.
                // 죽었다면 부팅이 공백 안에서 일어났으므로 **G 미만**이다. 그래서 G 가 기준이다.
                // 허용오차 2초: 장치 시계는 초 단위 절삭이고 도착 시각도 틱에 걸린다.
                long long G = xs_last_ms ? (now_ms() - xs_last_ms) / 1000 : -1;
                if (xs_uptime < 0 || xs_dev != f[5] || G < 0) {
                    xs_reconnect_unknown++;      // 기억 없음/다른 장치 — 모른다고 말한다
                } else if (up < xs_uptime || up + 2 < G) {
                    xs_reconnect_reboot++;
                    logf("⟳", "재연결 판정: **재부팅** — uptime " + std::to_string(xs_uptime)
                              + " → " + std::to_string(up) + " · 공백 " + std::to_string(G) + "초"
                              + (up < xs_uptime ? " (되감김)" : " (공백보다 짧은 가동시간)"));
                } else {
                    xs_reconnect_link++;
                    logf("=", "재연결 판정: **링크 재접속**(장치는 안 죽었다) — uptime "
                              + std::to_string(xs_uptime) + " → " + std::to_string(up)
                              + " (+" + std::to_string(up - xs_uptime) + "초) · 공백 "
                              + std::to_string(G) + "초");
                }
            }
            xs_uptime = up; xs_dev = f[5]; xs_last_ms = now_ms();   // 세션이 끊겨도 남는다

            ard_seq = seq; ard_uptime = up; ard_dev = f[5];
            ard_last_ms = now_ms(); ard_last_epoch_ms = epoch_ms(); ard_seen = true;

            // 🔴🔴 **자리 비트열 해독** — 형식이 둘이고 **틀리면 오류 없이 값만 어긋난다**
            //   옛 펌웨어 : 10진 문자열 `0110000010` — **한 칸에 한 자리**
            //   새 펌웨어 : hex `182` — 전체를 `n` 비트 정수로 보고 **슬롯 i = 비트 (n−1−i)**
            // ⚠ **`n` 을 모르면 hex 를 못 푼다.** 그리고 **첫 `S` 는 `D` 보다 먼저 온다**(명세 §5) —
            //   그때 `n=10` 을 가정해 풀면 **폭 검사도 체크섬도 통과하고 모든 비트가 어긋난다.**
            //   🔑 **모르는 값으로 해독하느니 안 하는 것이 낫다**(arduino 권고 · 채택).
            // ⚠ 2026-08-19 에 이 구멍이 **실기에서 열려 있었다** — 장치가 hex 를 보내는데
            //   서버가 10진으로 읽어 **엉뚱한 자리를 점유로 표시했다.** 폭 검사를 넣어 둔 것이
            //   **"처리된 것처럼" 보이게 만들었다.**
            int occ[10];
            int mod_state[REG_MODS_MAX];
            // 🔑 해독 규칙은 `decode_slot_bits()` 한 곳에 있다(원장 §8.23-(66)).
            int  occ_bits  = decode_mod_bits(f[2], mod_state);
            bool occ_known = (occ_bits > 0);
            for (int i = 0; i < 10; i++) occ[i] = mod_state[i];
            // 🔑 **자리 열 개를 넘는 비트를 보관한다** — 조작 완료 판정이 이것을 읽는다.
            //   ⚠ 못 읽었으면 `mod_bits_n = 0` 으로 남긴다. **모른다와 0 은 다르다.**
            n.mod_bits_n = occ_bits;
            if (occ_bits > 0) for (int i = 0; i < REG_MODS_MAX; i++) n.mod_bits[i] = mod_state[i];
            if (!occ_known) {
                // 🔴 미등록 + hex 로 보이는 폭 → **해독하지 않는다.** 등록 뒤 다음 `S` 부터 읽는다
                if (!occ_undecoded_warned) {
                    occ_undecoded_warned = true;
                    logf("!", "자리 비트열을 해독하지 않는다 — 등록 전이라 n 을 모른다(폭 "
                              + std::to_string(f[2].size()) + "). **등록되면 읽는다**");
                }
            }
            occ_undecoded += occ_known ? 0 : 1;

            // §2.4 tmask — **선택 필드다. 없으면 해제로 본다**(§2.1 규칙 8, 옛 펌웨어 수용).
            // f 는 체크섬을 뺀 필드들이므로 7번째(index 6)가 있으면 그것이 tmask 다.
            // ⚠ **2026-08-19 — 잠복 결함을 미리 닫는다.** 예전 조건은 `f[6].size() >= 10` 이라
            //   **10진 폭을 전제**했다. 지금은 장치가 이 칸을 아예 안 보내서(필드 6개) 안 돌지만,
            //   **tmask 가 hex 로 오는 순간 `res` 와 똑같이 조용히 죽는다.**
            //   🔑 arduino 가 확인해 줬다 — *"세 필드를 같이 바꿨으므로 읽는 쪽도 셋이다."*
            //   **잠복은 "나중에 밟는다"는 뜻이고 그때는 또 아무도 안 본다. 지금 닫는다.**
            int ovr[10];
            int ovr_bits_[REG_MODS_MAX];
            bool armed = false;
            for (int i = 0; i < 10; i++) ovr[i] = 0;
            if (f.size() >= 7 && f[6] != "-" && !f[6].empty()) {
                // 🔴 **칸이 있다는 것 자체가 "무장"이다.** 해독 성공 여부와 분리한다 —
                //   못 읽는다고 `armed=false` 로 답하면 **장치가 시험 모드인데 서버가 정상이라 믿는다.**
                //   못 읽으면 무장은 알리되 덮어쓰기 목록은 비운다(안전한 방향).
                armed = true;
                if (decode_mod_bits(f[6], ovr_bits_) > 0)
                    for (int i = 0; i < 10; i++) ovr[i] = ovr_bits_[i];
            }

            if (reboot) {
                reboot_by_uptime++;                  // 소크 관측(REQ-0065) — 2차 방어선이 실제로 몇 번 걸리는가
                resync_reservations("uptime 전진량이 한 바퀴에 가깝다");
            }

            // §7.5 — occupied 1→0 이면 예약 소진.
            // 재부팅했거나 기준선이 없으면 **판정하지 않고 기준선만 세운다**(§7.5-1).
            // 이걸 빼면 재부팅으로 occupied 가 초기화될 때 있지도 않은 1→0 전이가 잡혀
            // 방금 재하달한 예약을 그 자리에서 죽인다.
            if (base_valid && !reboot) {
                for (int i = 0; i < 10; i++) {
                    if (!(base_occ[i] == 1 && occ[i] == 0)) continue;      // 1→0 전이가 아니면 무관
                    if (slots[i].reserved != 1) continue;
                    if (has_pending_for(SLOT_ID[i])) continue;
                    // §7.5-3 **되돌림은 출차가 아니다.** 오버라이드 비트가 같이 1→0 이면
                    // 가짜 값을 걷어낸 것이지 차가 빠진 게 아니다. 이 구분이 없으면
                    // 해제(D) 한 번에 열 칸이 되돌아가면서 **예약이 통째로 은퇴한다.**
                    if (base_ovr[i] == 1 && ovr[i] == 0) {
                        logf("=", std::string("되돌림 감지 — ") + SLOT_ID[i]
                                  + " 은퇴시키지 않는다(테스트 오버라이드 해제)");
                        continue;
                    }
                    retire(i);
                }
            } else {
                logf("=", "은퇴 기준선 설정 — 이 프레임은 전이 판정을 건너뛴다");
            }
            for (int i = 0; i < 10; i++) {
                base_occ[i] = occ[i]; slots[i].occupied = occ[i];
                base_ovr[i] = ovr[i]; test_ovr[i] = ovr[i];
            }
            base_valid = true;
            if (test_armed != armed)
                logf("*", std::string("테스트 모드 ") + (armed ? "무장" : "해제")
                          + " (출처: S 의 tmask)");
            test_armed = armed;

            // reserved 는 서버가 durable owner 다(§7.4). 아두이노 값으로 덮지 않는다.
            // 다만 서버가 0 인데 아두이노가 1 이면 세계관이 갈라진 것이므로 C 를 다시 내려 맞춘다(§7.6).
            // 미결 요청이 있는 자리는 제외 — 아직 ACK 를 못 받았을 뿐인 정상 상태다.
            // 🔴 **2026-08-19 정정**: 이 조건이 예전에는 `f[3].size() >= 10` 이었다.
            //   장치가 hex 로 바뀌자(`21:12:09`) 폭이 3 이 되어 **조건이 영영 거짓** —
            //   자가 치유가 **한 번도 안 돌았는데 로그에 아무 흔적이 없었다.**
            //   ⚠ 오독이 아니라 **건너뜀**이라 `0` 조차 안 남았다. 그래서 **분모를 센다**(§8.23-(66)).
            int res_bits_[REG_MODS_MAX];
            int res_n_ = (f.size() >= 4) ? decode_mod_bits(f[3], res_bits_) : 0;
            if (res_n_ <= 0) {
                res_undecoded++;
                if (f.size() >= 4 && !res_undecoded_warned) {
                    res_undecoded_warned = true;
                    logf("!", "예약 마스크를 해독하지 않는다 — 등록 전이라 n 을 모른다(폭 "
                              + std::to_string(f[3].size()) + "). **등록되면 읽는다**");
                }
            } else {
                heal_checks++;               // 🔑 **분모** — 이 검사가 실제로 돌았다
                for (int i = 0; i < 10; i++)
                    if (res_bits_[i] && slots[i].reserved == 0 && !has_pending_for(SLOT_ID[i])) {
                        heal_fires++;
                        logf("⚠", std::string("불일치: 아두이노 ") + SLOT_ID[i]
                                  + " reserved=1, 서버 0 → C 재하달");
                        dispatch('C', BAD_SOCK, "", SLOT_ID[i], "");
                    }
            }
            // ═══ 내 창이 열렸다. 큐 전부를 한 거래로 내보낸다 ═══════════════════════
            //
            // 🔴 **왜 S 분기의 맨 끝인가**: 이 분기 안에서 하행이 새로 생긴다 —
            // 불일치 감지(`dispatch('C')`)와 uptime 재동기화(`resync_reservations`)가 그것이다.
            // flush 를 `ard_last_ms` 갱신 직후(위쪽)에 두면 **그 치유 `C` 가 다음 창까지 밀려**
            // 원장 §8.21 이 실측한 "갈린 구간 ≈1초"가 조용히 ≈2.2초로 늘어난다.
            // **실측값을 낡게 만드는 배치를 고르지 않는다.**
            //
            // ⚠ 그리고 이 자리 때문에 **승격 직후 두 줄의 순서가 뒤집힌다**(원장 §7.2):
            //     전: `⟳ 재하달` → `→ARD R` → `←ARD S`
            //     후: `⟳ 재하달`(큐) → `←ARD S` → `→ARD R`   ← **같은 틱이다**
            // `adopt_as_parking()` 이 `resync_reservations()` 를 부른 직후 `drain_ard_buf()` 가
            // 첫 프레임을 같은 틱에서 처리하므로 지연은 사실상 0 이고, **순서만 바뀐다.**
            // 🔑 **이것은 회귀가 아니라 이 설계의 정의다** — 서버는 장치가 말한 뒤에 말한다.
            // 면제(승격 시 즉시 송신)를 두지 않은 이유: 그 순간의 위상은 `ard_last_ms` 가
            // 아직 옛 세션 값이라 **측정할 수 없고**, 측정 못 하는 순간에 쏘는 것이 §8.15 다.
            // 🔴 **2026-08-19 — 방송보다 먼저 쏜다.** 전에는 이 아래 둘이 앞에 있었다:
            //     write_log_if_changed()  (파일 쓰기)
            //     push_snapshot()         (**붙어 있는 화면 수에 비례**하는 방송)
            //   창 M 에서 하행 송신 지연이 `≥2ms 9.4% → 40.9%` 로 늘었고, 후보 하나가 이것이었다.
            //   🔑 **하행은 슬롯 창에 묶여 있고 방송은 안 묶여 있다.** 묶인 쪽을 먼저 보낸다.
            //   ⚠ **원래 배치의 이유는 그대로 지킨다**: 이 분기 안에서 하행이 새로 생기는 자리는
            //     `resync_reservations()`(재부팅 감지)와 불일치 치유 `dispatch('C')` 인데
            //     **둘 다 이 줄보다 위에 있다.** 아래 둘은 하행을 만들지 않는다 —
            //     그래서 순서를 바꿔도 §8.21 이 실측한 "갈린 구간 ≈1초"가 안 늘어난다.
            //   🔑 **화면이 스냅샷을 몇 ms 늦게 받는 것은 아무것도 안 깨뜨린다. 장치는 다르다.**
            // 🔴 **flush 보다 먼저 부른다.** 콜백이 낸 명령이 **이 창에 같이 나가야** 한다 —
            //   뒤에 두면 다음 창(1.2초 뒤)으로 밀린다.
            //   ⚠ 이 자리는 이미 하행을 만드는 곳이다(`resync_reservations` · 치유 `dispatch('C')`
            //     가 위에 있다). 그래서 새 종류의 위험이 아니다.
            notify_occupancy_changes();
            flush_downq("S 도착 — 창 시작", false);
            write_log_if_changed();
            push_snapshot();
        }
        // ── §V 센서 값 프레임 `V,<v1>,…,<vk>,<ck>` ─────────────────────────────
        // 🔴 **위치가 뜻을 정한다** — 항목 i 는 **모듈 idx i** 다(`occ` 비트와 같은 주소 체계).
        //   각 항목은 hex **3자리** 또는 🔴 **빈 칸(= 못 쟀다)**.
        // ⚠ **빈 칸을 `0` 으로 읽지 마라.** `0` 은 "0cm" 이고 빈 칸은 **"못 쟀다"** 다 —
        //   그 둘을 접으면 반사 실패가 조용히 "아주 가깝다" 가 된다.
        // 🔑 **항목 수를 강제하지 않는다.** 장치 판마다 자리 수가 다를 수 있어(실측: 2칸 판과
        //   11칸 판이 같은 로그에 있었다) **모듈 수까지만 읽고 나머지는 버린다.**
        //   너무 적으면 **없는 것으로 둔다** — 조용히 밀려 엉뚱한 모듈에 붙는 것보다 낫다.
        else if (f[0] == "V") {
            Node& vn = n;
            const size_t have = vn.mods.size();
            size_t used = 0, miss = 0;
            for (size_t i = 1; i < f.size(); i++) {
                const size_t idx = i - 1;
                if (idx >= have || idx >= (size_t)REG_MODS_MAX) break;
                if (f[i].empty()) {                 // 🔴 못 쟀다 — 값을 지우고 그렇게 기록한다
                    vn.mod_val_has[idx] = false; miss++;
                    continue;
                }
                char* endp = 0;
                const long v = strtol(f[i].c_str(), &endp, 16);
                if (endp == f[i].c_str() || (endp && *endp)) { vmalformed++; continue; }
                vn.mod_val[idx] = v; vn.mod_val_has[idx] = true; vn.mod_val_ms[idx] = now_ms();
                used++;
                // 🔴 **값이 올 때마다 부른다** — 점유가 안 바뀌어도 온다(주차 유도).
                //   🔑 여기는 `S` 처리보다 **앞**이다(`V` 가 같은 배치에서 먼저 온다) →
                //     값 콜백이 점유 콜백보다 먼저 불린다. **저장해 뒀다 쓰는 것이 가능하다.**
                //   ⚠ 자리에 안 붙은 모듈은 부르지 않는다 — 화면에도 없는 것에 콜백만 가면 헷갈린다.
                if (val_cb_ && owner_ && idx < vn.mods.size()) {
                    const std::string zid = lot.zoneOfModule(vn.devid, vn.mods[idx].first, lot_);
                    if (!zid.empty()) val_cb_(*owner_, zid, vn.mods[idx].first, v);
                }
            }
            vframes++; vvalues += (long long)used; vmissing += (long long)miss;
        }
        // ── §5 등록 프레임 `D` ─────────────────────────────────────────────────
        // 🔴 **관측이지 제어가 아니다**(Node::reg_* 주석). 하행 경로를 안 바꾼다.
        // ⚠ 이 분기가 없으면 `D` 는 `drop_unknown`("AT 잡음 유입 의심")으로 떨어져
        //   **세션마다 11씩 오르며 거짓 경보를 만든다.**
        else if (f[0] == "D" && f.size() >= 3) {
            if (f[1] == "*") {
                // `D,*,<drain>,<n>` — 묶음의 맨 앞. **자기 완결적이라 언제 와도 같은 뜻이다.**
                // 🔴 `*` 는 모듈 `name` 이 될 수 없다(설계 §5). 여기 걸리는 것이 그 방어다 —
                //    필드 수·숫자 형식 둘 다 안 맞으면 **모듈로도 배출률로도 안 읽힌다.**
                if (f.size() < 4 || !all_digits(f[2]) || !all_digits(f[3])) {
                    reg_bad++;
                    logf("!", "D,* 형식 위반 — 버림 (name 이 '*' 인 모듈이거나 숫자가 아니다)");
                    return;
                }
                // 🔴 **명세 §7.4 가 약속한 검사다 — 적어 놓고 안 만들면 지켜지는 것처럼 보인다.**
                //   약속: *"새 모듈은 목록 끝에만 붙인다. 중간에 끼우지 않는다."*
                //   깨지면 **`idx` 가 밀려 전선에 나가 있던 `G` 가 다른 모듈을 친다** — 조용히.
                //   ⚠ 이 검사는 *"약속을 어겼나"* 만 잡는다. *"왜 어겼나"* 는 arduino 만 안다.
                prev_mods_snapshot = n.mods;   // 🔑 `D,*` 와 등록 완료는 **다른 프레임**이다 — 멤버로 잇는다
                n.reg_reset();
                n.reg_drain = atoi(f[2].c_str());
                n.reg_n     = atoi(f[3].c_str());
                if (n.reg_n < 0 || n.reg_n > REG_MODS_MAX) {
                    reg_bad++;
                    logf("!", "D,* 의 n=" + f[3] + " 이 범위 밖(0~"
                              + std::to_string(REG_MODS_MAX) + ") — 등록 무효");
                    n.reg_n = -1; return;
                }
                char b[160];
                snprintf(b, sizeof(b), "등록 시작 — 선언 drain=%d · n=%d (device=%s)",
                         n.reg_drain, n.reg_n, n.devid.c_str());
                logf("=", b);
                return;
            }
            // `D,<name>,<kind>` — 모듈 한 줄. **순서가 곧 `idx` 다**(등록 순서가 비트 자리를 정한다).
            if (n.reg_n < 0) { reg_bad++; logf("!", "D,* 없이 모듈 줄이 왔다 — 버림"); return; }
            if ((int)n.mods.size() >= n.reg_n) {
                reg_bad++;
                logf("!", "선언 n=" + std::to_string(n.reg_n) + " 보다 모듈 줄이 많다 — 버림");
                return;
            }
            n.mods.push_back(std::make_pair(f[1], f[2]));
            if ((int)n.mods.size() == n.reg_n) {
                n.reg_done = true;
                reg_ok++;
                bind_modules(n);          // 🔑 등록이 지형을 바꾼다 → epoch 이 여기서 오른다
                // 🔴 **삼중 검산 ①②** — 선언 `n` 과 실제 줄 수. ③(hex 폭)은 `S` 에서 본다.
                // 🔴 **앞부분 대조** — 겹치는 구간이 그대로인가. 하나라도 다르면 `idx` 가 밀린 것이다.
                if (!prev_mods_snapshot.empty()) {
                    size_t n_ov = prev_mods_snapshot.size() < n.mods.size()
                                ? prev_mods_snapshot.size() : n.mods.size();
                    for (size_t i = 0; i < n_ov; i++)
                        if (prev_mods_snapshot[i] != n.mods[i]) {
                            mod_order_changed++;
                            logf("!", "🔴 재등록에서 모듈 순서가 바뀌었다 — idx " + std::to_string(i)
                                      + " 가 `" + prev_mods_snapshot[i].first + "` → `"
                                      + n.mods[i].first + "`. **끝에만 붙인다는 약속이 깨졌다** "
                                      "(명세 §7.4). 떠 있는 조작 명령을 버린다");
                            // ⚠ **떠 있는 `G` 를 버린다.** 그 `idx` 는 이제 다른 모듈을 가리킨다 —
                            //   보내 놓고 결과를 기다리는 것보다 **실패로 끝내는 것이 정직하다.**
                            for (std::map<uint16_t, Pending>::iterator it = pend.begin();
                                 it != pend.end(); ) {
                                if (it->second.kind == 'G') {
                                    if (it->second.ws_fd != BAD_SOCK)
                                        send_err(it->second.ws_fd, it->second.browser_rid,
                                                 "node_unregistered",
                                                 "장치 구성이 바뀌어 조작을 취소했습니다");
                                    uint16_t drid = it->first;
                                    pend.erase(it++);
                                    rid_release(drid);
                                } else ++it;
                            }
                            break;
                        }
                }
                prev_mods_snapshot.clear();
                char b[192];
                snprintf(b, sizeof(b),
                         "등록 완료 — n=%d · drain=%d · 명령가능 %d개 (device=%s)",
                         n.reg_n, n.reg_drain, reg_cmdable(), n.devid.c_str());
                logf("=", b);

                // ── 노드 대장 (온보딩 2단계 · `docs/net/DESIGN-node-ledger.md` §0.4 ①) ──
                // 🔴 **여기서만 부른다.** `reg_done` 이 참인 자리이므로 목록이 완전하다.
                //   부분 목록으로 지문을 접으면 **거짓 "구성 변경"** 이 된다(명세 §0.3).
                // ⚠ 대장은 **판정에 관여하지 않는다.** 아래 어떤 값도 결속·하행을 안 바꾼다.
                if (!n.devid.empty()) {
                    std::vector<NodeLedger::Mod> lm;
                    for (size_t mi = 0; mi < n.mods.size(); mi++)
                        lm.push_back(NodeLedger::Mod(n.mods[mi].first, n.mods[mi].second));
                    NodeLedger::State st =
                        ledger_.onRegister(n.devid, peer_host(n.peer), lm, epoch_ms());
                    if (st == NodeLedger::NEW)               ledger_new_++;
                    else if (st == NodeLedger::NEEDS_REVIEW) ledger_review_++;

                    const NodeLedger::Entry* e = ledger_.find(n.devid);
                    char lb[320];
                    snprintf(lb, sizeof(lb),
                             "노드 대장 — %s = **%s** · 지문 %s · 접속 %lld회 · 상대 %s%s",
                             n.devid.c_str(), NodeLedger::stateName(st),
                             e ? e->fp.c_str() : "?",
                             e ? e->sessions : 0,
                             peer_host(n.peer).c_str(),
                             st == NodeLedger::NEEDS_REVIEW
                               ? " 🔴 **구성이 대장과 다르다 — 사람이 봐야 한다.**"
                                 " ⚠ 원인이 *다른 보드* 일 수 있다(devid 가 아직 고유하지 않다). 상대 IP 를 같이 봐라"
                               : "");
                    logf("=", lb);
                    ledger_save("등록완료");
                }
            }
            return;
        }
        else if (f[0] == "A" && f.size() >= 4) {
            uint16_t rid = (uint16_t)atoi(f[1].c_str());
            std::string slot = f[2];
            int result = atoi(f[3].c_str());
            std::map<uint16_t, Pending>::iterator it = pend.find(rid);
            if (it == pend.end()) {
                // 🔴 **계수한다.** 여기는 격리 가정(설계 §3.1)이 깨졌을 때 오르는 칸이다 —
                //   `RID_QUARANTINE_MS` 보다 늦게 오는 ACK 이 없다는 것을 **잰 적이 없다.**
                //   ⚠ 재전송 중복도 같은 칸에 온다. **둘을 여기서 못 가른다** — 값이 오르면
                //   그때 로그 시각으로 갈라야 한다. 그 한계를 요약 표에 적어 뒀다.
                ack_unknown_rid++;
                logf("!", "모르는 rid 의 ACK — 무시 (재전송 중복일 수 있다) rid=" + std::to_string(rid));
                return;
            }
            Pending p = it->second;
            pend.erase(it);
            rid_release(rid);

            // ── 왕복 실측 (설계 §1) — **`W_srv` 를 상수로 두지 않기 위한 유일한 입력**
            // 이 값은 `RTT + 장치 처리시간` 이라 **RTT 의 상한**이고, 창을 좁히는 방향이라
            // 보수적 = 안전하다. **재전송된 건은 표본에서 뺀다** — `sent_ms` 가 마지막 시도
            // 시각이라 값은 맞지만, 병리 구간이 섞이면 창이 그 최악에 영구히 묶인다.
            if (!p.queued && p.sent_ms > 0 && p.tries <= 1) {
                long long rtt = now_ms() - p.sent_ms;
                if (rtt >= 0 && rtt < DOWN_SLOT_MS * 4) {   // 그 이상은 표본이 아니라 사건이다
                    rtt_last_ms = rtt; rtt_n++;
                    if (rtt > rtt_max_ms) rtt_max_ms = rtt;
                }
            }

            // ── 🔴 에코 자리 대조 — **장치 멱등 캐시 재전송의 서명**을 여기서 본다
            //
            // arduino `docs/arduino/LEDGER.md` §25.3: 장치는 최근 서로 다른 `rid` **16개**를
            // 캐시에 들고 있고, 그 안에서 값이 재등장하면 **명령을 적용하지 않고 옛 ACK 을
            // 재전송한다.** 그러면 **에코된 자리가 옛 명령의 자리**다.
            // ⚠ **서버는 ACK 을 받으므로 `ack_timeout` 이 안 뜬다 — 실패가 성공처럼 보인다.**
            // 🔑 그래서 이 계수가 **`rid` 폭을 줄인 이 변경의 안전 지표**다(설계 §5).
            //
            // ⚠ **탐지만 한다. 거동은 안 바꾼다.** 바로 아래 줄이 "전선 값이 유효한 자리면
            //   그걸 쓴다"인데 주석은 "전선 값을 믿지 않는다"라고 적혀 있다 — **둘이 어긋나 있고
            //   그것을 고치는 것은 거동 변경**이라 이 배포에 안 넣는다(설계 §6.1 · 루트 결정 대기).
            if (slot != "??" && slot_index(slot) >= 0 && !p.slot.empty() && slot != p.slot) {
                ack_slot_mismatch++;
                logf("!", "🔴 ACK 에코 자리 불일치 rid=" + std::to_string(rid)
                          + " 보낸자리=" + p.slot + " 에코=" + slot
                          + " — 장치 멱등 캐시 재전송 의심(arduino §25.3)");
            }

            // result=3 이면 slot 은 "??" 다(§2.4). 전선의 자리 값을 믿지 않고
            // **매핑표의 원래 자리**를 쓴다 — 상관 키는 rid 이고 원본은 서버가 들고 있다.
            if (slot == "??" || slot_index(slot) < 0) slot = p.slot;
            int idx = slot_index(slot);
            // **예약 상태를 건드리는 것은 R/C 뿐이다.** T 를 여기에 섞으면
            // 테스트 주입 ACK 가 cancel 로 취급돼 **그 칸의 예약을 지워 버린다.**
            if (result == 0 && idx >= 0 && (p.kind == 'R' || p.kind == 'C')) {
                if (p.kind == 'R') {
                    slots[idx].reserved = 1;
                    // 화면·스냅샷에는 **원본**을 보여 준다(번호판이면 번호판 그대로).
                    // 전선에 나간 값(p.user_id)은 잘렸거나 비어 있을 수 있다.
                    slots[idx].user_id = p.plate;
                    slots[idx].reserved_at = epoch_ms();
                } else {
                    slots[idx].reserved = 0;
                    slots[idx].user_id.clear();
                    slots[idx].reserved_at = 0;
                }
            }
            // 테스트 결과는 여기서 상태에 반영하지 않는다 — 무장/오버라이드의 진실은
            // 다음 S 프레임의 tmask 다(§12A.4). ACK 는 "명령이 처리됐다"까지만 말한다.
            // 🔴 **`result=3` 은 `ack_timeout` 과 다른 칸에 센다.**
            //   `3` = *"장치가 못 한다"* → **재시도해도 같다.**
            //   `ack_timeout` = *"안 갔다"* → **재시도에 뜻이 있다.**
            //   ⚠ 합쳐 두면 `실패 N` 을 보고 재시도를 늘리는데, 절반은 늘려도 소용이 없다.
            //   (§8.16 의 `error` 한 칸 · §8.23-(38) 과 같은 형태다.)
            if (p.kind == 'G') gate_ans++;       // 🔑 **응답** — result 와 무관하게 답이 온 것
            // 🔴 **명령 결과 콜백** — `result` 로 갈래를 가른다.
            //   0 = 콜백이 true 를 냈다 · 3 = 장치가 거절했다.
            //   ⚠ 무응답은 여기 안 온다(ACK 이 없으니까) — 재전송 소진 자리에서 따로 부른다.
            if (p.kind == 'G')
                notify_cmd(p, (result == 0) ? CmdResult::OK : CmdResult::REJECTED, result);
            if (result == 3) {
                dev_reject++;
                logf("!", std::string("장치가 거절했다(result=3) — ") + p.kind + " " + slot
                          + " rid=" + std::to_string(rid) + ". **재시도는 뜻이 없다**");
            }
            if (p.ws_fd != BAD_SOCK) send_ack(p.ws_fd, p.browser_rid, slot, result, p.kind);
            // 이음매 1: 직접 호출 → 이벤트. **같은 틱의 drain 이 같은 일을 한다**(2481행).
            // 한 틱에 ACK 가 여러 건 겹치면 기록·화면이 건별 → 1회로 접힌다 — 이미 옮긴
            // 3종과 같은 성질이고, 브라우저가 보는 최종 상태는 같다.
            emit_dev_ack(park_dev, rid, (uint8_t)result,
                         std::string("ACK ") + p.kind + " " + slot
                         + " result=" + std::to_string(result));
        }
        else {
            drop_unknown++;
            // ⚠ **주석 정정(2026-08-18)**: 예전에는 이 칸이 오르면 **AT 잡음 유입**을 의심했다.
            // 등록(`D`)이 들어온 뒤로는 **그 해석이 더는 유일하지 않다** — `D` 는 위에서 처리되므로
            // 여기 안 오지만, **새 프레임 종류가 생기면 옛 서버에서 여기로 떨어진다.**
            // 🔑 **`모름` 이 오르면 "잡음"이 아니라 "내가 모르는 프레임"부터 의심해라.**
            logf("!", "모르는 타입 — 조용히 버림");
        }
    }
