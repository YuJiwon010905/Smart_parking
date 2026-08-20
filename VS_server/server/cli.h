// cli.h — 명령줄 인자 해석. **`main()` 의 몸통 조각**이다.
// ═══════════════════════════════════════════════════════════════════
// 🔴 **단독 컴파일 불가.** `argc`·`argv`·`log_path` 를 바깥과 나눠 쓰고,
//   잘못된 값이면 `return` 으로 `main()` 을 빠져나간다. 위치가 곧 문법이다.
//
// 🔑 **왜 뗐나** — `main()` 을 열었을 때 **주차장을 조립하는 코드가 먼저 보여야** 한다.
//   손잡이 해석 66줄이 위에 있으면 흐름이 화면 밖으로 밀린다.
//   **여기 있는 것은 전부 "어떻게 켜는가"이고, 남은 것이 "무엇을 하는가"다.**
//
// ⚠ 손잡이 셋은 **시험 전용**이고 각각 이유가 주석에 붙어 있다.
//   `--max-line` · `--down-cap` · `--down-immediate` 는 명세를 바꾸는 것이 아니라
//   **명세를 재는 계측기를 넓히는 것**이다. 운영에서 쓰지 마라.
// ═══════════════════════════════════════════════════════════════════
    // 로그 경로(REQ-0111 로그 계약 §2.4) — 비워 두면 기본 경로를 쓴다.
    // **셸 리다이렉션에 맡기지 않는 이유**: 08-16 에 나중 뜬 인스턴스가 다른 곳에 쓰는 바람에
    // 관측이 조용히 끊겼다. 어디에 쓰는지는 서버가 정하고, 경계 줄에 log= 로 적어 둔다.
    std::string log_path;
    for (int i = 1; i < argc; i++) {
        std::string a(argv[i]);
        if (a.compare(0, 6, "--log=") == 0) log_path = a.substr(6);
        // 🔴 **계측기를 넓히는 수단이지 명세 변경이 아니다**(§2.1 은 64 그대로).
        // `AT+CIPSEND` 최대 길이 시험에서만 쓴다 — 서버가 64B 초과를 **체크섬 검사 전에**
        // 버리면 **AT 잘림이 아니라 이 상수를 재게 된다.**
        if (a.compare(0, 11, "--max-line=") == 0) {
            int v = atoi(a.substr(11).c_str());
            if (v >= 64 && v <= 1024) {
                MAX_LINE = v;
                std::cout << "*** --max-line=" << v << " — **시험용**이다. 명세 §2.1 은 64 다 ***\n";
            } else {
                std::cerr << "--max-line 은 64~1024 여야 한다 (받은 값: " << a.substr(11) << ")\n";
                return 2;
            }
        }
        // 🔴 **임시값을 갈아끼우는 손잡이.** arduino 의 `rxmax`(바이트)가 나오면 이걸로 넣는다 —
        // 재빌드가 필요하면 "나중에 하자"가 되고, 그러면 임시값이 영구값이 된다.
        if (a.compare(0, 11, "--down-cap=") == 0) {
            int v = atoi(a.substr(11).c_str());
            if (v >= 32 && v <= 4096) {
                DOWN_BATCH_CAP_B = v;
                std::cout << "*** --down-cap=" << v << "B — 하행 배치 상한(창당). "
                          << "큐 깊이는 " << (v * (DOWNQ_WAIT_CAP_MS / DOWN_SLOT_MS))
                          << "B 로 같이 움직인다 ***\n";
            } else {
                std::cerr << "--down-cap 은 32~4096 이어야 한다 (받은 값: " << a.substr(11) << ")\n";
                return 2;
            }
        }
        // 🔴 옛 거동(이벤트 시점에 즉시 송신). **착지 위상을 겨냥하는 시험 전용**이다 —
        // 원장 §8.17 의 미해결 물음이 이 수단 없이는 영영 안 갈린다.
        if (a == "--down-immediate") {
            DOWN_IMMEDIATE = true;
            std::cout << "*** --down-immediate — 하행이 **창을 무시하고 즉시** 나간다. "
                      << "슬롯 규율이 꺼진 상태다(시험 전용) ***\n";
        }
    }
    // ── 🔴 포트를 **하나씩** 준다 (사용자 확정 2026-08-20 · `--port-offset` 은 없앴다)
    //   `--port-web=` 웹/WS · `--port-ardu=` 아두이노 · `--port-cam=` 카메라(폰)
    for (int i = 1; i < argc; i++) {
        std::string a(argv[i]);
        const char* keys[3] = { "--port-web=", "--port-ardu=", "--port-cam=" };
        int* dst[3] = { &g_port_web, &g_port_ardu, &g_port_cam };
        for (int k = 0; k < 3; k++) {
            const size_t kl = strlen(keys[k]);
            if (a.size() <= kl || a.compare(0, kl, keys[k]) != 0) continue;
            const int v = atoi(a.c_str() + kl);
            // ⚠ **범위를 안 보면 조용히 엉뚱한 포트에 붙는다.** 음수는 0번으로, 큰 값은
            //   htons 에서 잘려 아무 데나 간다 — 그리고 그건 나중에 "서버가 이상하다"로 보고된다.
            //   **의심스러우면 안 뜬다.**
            if (v < 1 || v > 65535) {
                std::cerr << keys[k] << " 는 1 ~ 65535 여야 한다 (받은 값: " << v << ")\n";
                return 1;
            }
            *dst[k] = v;
        }
    }
    // 🔴 **자기들끼리 겹치는지 본다.** 겹치면 한 포트가 안 뜨는 게 아니라
    //   **엉뚱한 대상이 그 소켓을 받는다**(아두이노가 카메라 자리에 붙는다). 증상이 안 보인다.
    //   🔑 우리 규칙: **증상이 보이면 말하고, 안 보이면 막아라.** 이건 안 보인다.
    //   ⚠ **어느 둘이 겹치는지 이름으로 말한다** — "충돌"만 찍으면 사람이 헤맨다.
    {
        const char* nm[3] = { "--port-web", "--port-ardu", "--port-cam" };
        const int   pv[3] = { g_port_web, g_port_ardu, g_port_cam };
        for (int i2 = 0; i2 < 3; i2++)
            for (int j2 = i2 + 1; j2 < 3; j2++)
                if (pv[i2] == pv[j2]) {
                    std::cerr << "🔴 포트가 겹친다 — " << nm[i2] << " 와 " << nm[j2]
                              << " 가 둘 다 " << pv[i2] << " 다.\n"
                              << "   같은 포트를 둘이 쓰면 **엉뚱한 대상이 그 소켓을 받는다.**\n"
                              << "   기본값: 웹 " << PORT_HTTP << " · 아두이노 " << PORT_ARDUINO
                              << " · 카메라 " << PORT_PHONE << "\n";
                    return 1;
                }
    }
    for (int i = 1; i < argc; i++) {
        std::string a(argv[i]);
        if (a.compare(0, 11, "--park-dev=") != 0) continue;
        g_park_dev_pin = a.substr(11);
        if (g_park_dev_pin.empty()) {
            std::cerr << "--park-dev= 에 devid 를 줘라 (예: --park-dev=P1A)\n";
            return 1;
        }
    }
