#include <iostream>
#include <thread>
#include <cstdint>

#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")


// =====================================================
// Arduino -> Server
// =====================================================

const uint8_t MSG_ENTRY_APPROACH = 1;
const uint8_t MSG_EXIT_APPROACH = 2;
const uint8_t MSG_PARKING_FULL = 3;

const uint8_t MSG_SLOT_OCCUPIED_BASE = 11;
const uint8_t MSG_SLOT_EMPTY_BASE = 21;

const uint8_t MSG_PARK_COMPLETE_BASE = 31;

const uint8_t MSG_WRONG_PARK_BASE = 41;
const uint8_t MSG_WRONG_CLEAR_BASE = 51;

const uint8_t MSG_EXIT_COMPLETE = 60;


// =====================================================
// Server -> Arduino
// =====================================================

const uint8_t CMD_ASSIGN_P1 = 101;
const uint8_t CMD_ASSIGN_P2 = 102;
const uint8_t CMD_ASSIGN_P3 = 103;

const uint8_t CMD_EXIT_ALLOW = 110;
const uint8_t CMD_RESET = 120;


// =====================================================
// Arduino 메시지 해석
// =====================================================

void printReceivedCode(uint8_t code) {

    std::cout
        << "\n[ARDUINO -> SERVER] CODE = "
        << static_cast<int>(code)
        << std::endl;


    // -----------------------------------------
    // 입차
    // -----------------------------------------

    if (code == MSG_ENTRY_APPROACH) {

        std::cout
            << "입차 차량이 감지되었습니다."
            << std::endl;

        std::cout
            << "101 / 102 / 103 중 하나를 입력하여 "
            << "주차면을 배정하세요."
            << std::endl;
    }


    // -----------------------------------------
    // 출차
    // -----------------------------------------

    else if (code == MSG_EXIT_APPROACH) {

        std::cout
            << "출차 차량이 감지되었습니다."
            << std::endl;

        std::cout
            << "110을 입력하면 출차를 허가합니다."
            << std::endl;
    }


    // -----------------------------------------
    // 만차
    // -----------------------------------------

    else if (code == MSG_PARKING_FULL) {

        std::cout
            << "현재 주차장이 만차입니다."
            << std::endl;
    }


    // -----------------------------------------
    // OCCUPIED
    // -----------------------------------------

    else if (code >= 11 &&
        code <= 13) {

        int slot =
            code - 10;

        std::cout
            << "P"
            << slot
            << " = OCCUPIED"
            << std::endl;
    }


    // -----------------------------------------
    // EMPTY
    // -----------------------------------------

    else if (code >= 21 &&
        code <= 23) {

        int slot =
            code - 20;

        std::cout
            << "P"
            << slot
            << " = EMPTY"
            << std::endl;
    }


    // -----------------------------------------
    // 정상 주차 완료
    // -----------------------------------------

    else if (code >= 31 &&
        code <= 33) {

        int slot =
            code - 30;

        std::cout
            << "P"
            << slot
            << " 정상 주차 완료"
            << std::endl;
    }


    // -----------------------------------------
    // 오주차
    // -----------------------------------------

    else if (code >= 41 &&
        code <= 43) {

        int slot =
            code - 40;

        std::cout
            << "오주차 발생 : P"
            << slot
            << std::endl;
    }


    // -----------------------------------------
    // 오주차 해제
    // -----------------------------------------

    else if (code >= 51 &&
        code <= 53) {

        int slot =
            code - 50;

        std::cout
            << "오주차 해제 : P"
            << slot
            << std::endl;
    }


    // -----------------------------------------
    // 출차 완료
    // -----------------------------------------

    else if (code == MSG_EXIT_COMPLETE) {

        std::cout
            << "출차 완료"
            << std::endl;
    }


    else {

        std::cout
            << "알 수 없는 코드입니다."
            << std::endl;
    }
}


// =====================================================
// Arduino -> Server 수신 Thread
// =====================================================

void receiveThread(SOCKET clientSocket) {

    while (true) {

        uint8_t code = 0;


        int received =
            recv(
                clientSocket,
                reinterpret_cast<char*>(&code),
                1,
                0
            );


        if (received <= 0) {

            std::cout
                << "\n클라이언트 연결이 종료되었습니다."
                << std::endl;

            break;
        }


        printReceivedCode(code);
    }
}


// =====================================================
// Server -> Arduino 숫자 전송
// =====================================================

bool sendCode(
    SOCKET clientSocket,
    uint8_t code
) {

    int sent =
        send(
            clientSocket,
            reinterpret_cast<const char*>(&code),
            1,
            0
        );


    if (sent == SOCKET_ERROR) {

        std::cerr
            << "전송 실패. Error = "
            << WSAGetLastError()
            << std::endl;

        return false;
    }


    std::cout
        << "[SERVER -> ARDUINO] CODE = "
        << static_cast<int>(code)
        << std::endl;


    return true;
}


// =====================================================
// 사용자 입력 처리
// =====================================================

void inputThread(SOCKET clientSocket) {

    while (true) {

        int input;


        std::cout
            << "\n명령 입력 "
            << "(101=P1, 102=P2, 103=P3, "
            << "110=출차허가, 120=RESET, 0=종료): ";

        std::cin >> input;


        // 프로그램 종료
        if (input == 0) {

            std::cout
                << "서버 종료 요청"
                << std::endl;

            shutdown(
                clientSocket,
                SD_BOTH
            );

            break;
        }


        // -------------------------------------
        // 허용된 명령인지 확인
        // -------------------------------------

        if (
            input != CMD_ASSIGN_P1 &&
            input != CMD_ASSIGN_P2 &&
            input != CMD_ASSIGN_P3 &&
            input != CMD_EXIT_ALLOW &&
            input != CMD_RESET
            ) {

            std::cout
                << "잘못된 명령입니다."
                << std::endl;

            continue;
        }


        // uint8_t 변환
        uint8_t code =
            static_cast<uint8_t>(input);


        sendCode(
            clientSocket,
            code
        );
    }
}


// =====================================================
// MAIN
// =====================================================

int main() {

    WSADATA wsaData;


    SOCKET serverSocket =
        INVALID_SOCKET;

    SOCKET clientSocket =
        INVALID_SOCKET;


    sockaddr_in address{};


    int addressLength =
        sizeof(address);


    const int PORT = 8080;


    // =================================================
    // Winsock 초기화
    // =================================================

    if (
        WSAStartup(
            MAKEWORD(2, 2),
            &wsaData
        ) != 0
        ) {

        std::cerr
            << "WSAStartup 실패"
            << std::endl;

        return 1;
    }


    // =================================================
    // Socket 생성
    // =================================================

    serverSocket =
        socket(
            AF_INET,
            SOCK_STREAM,
            IPPROTO_TCP
        );


    if (serverSocket == INVALID_SOCKET) {

        std::cerr
            << "Socket 생성 실패"
            << std::endl;

        WSACleanup();

        return 1;
    }


    // =================================================
    // Address
    // =================================================

    address.sin_family =
        AF_INET;

    address.sin_addr.s_addr =
        INADDR_ANY;

    address.sin_port =
        htons(PORT);


    // =================================================
    // Bind
    // =================================================

    if (
        bind(
            serverSocket,
            reinterpret_cast<sockaddr*>(
                &address
                ),
            sizeof(address)
        )
        == SOCKET_ERROR
        ) {

        std::cerr
            << "Bind 실패. Error = "
            << WSAGetLastError()
            << std::endl;

        closesocket(serverSocket);

        WSACleanup();

        return 1;
    }


    // =================================================
    // Listen
    // =================================================

    if (
        listen(
            serverSocket,
            SOMAXCONN
        )
        == SOCKET_ERROR
        ) {

        std::cerr
            << "Listen 실패"
            << std::endl;

        closesocket(serverSocket);

        WSACleanup();

        return 1;
    }


    std::cout
        << "====================================="
        << std::endl;

    std::cout
        << "SMART PARKING TEST SERVER"
        << std::endl;

    std::cout
        << "PORT = "
        << PORT
        << std::endl;

    std::cout
        << "ESP-01 연결 대기 중..."
        << std::endl;

    std::cout
        << "====================================="
        << std::endl;


    // =================================================
    // Client Accept
    // =================================================

    clientSocket =
        accept(
            serverSocket,
            reinterpret_cast<sockaddr*>(
                &address
                ),
            &addressLength
        );


    if (clientSocket == INVALID_SOCKET) {

        std::cerr
            << "Accept 실패"
            << std::endl;

        closesocket(serverSocket);

        WSACleanup();

        return 1;
    }


    std::cout
        << "\nESP-01 연결 성공!"
        << std::endl;


    std::cout
        << "\n사용 가능한 명령"
        << std::endl;

    std::cout
        << "101 : P1 배정"
        << std::endl;

    std::cout
        << "102 : P2 배정"
        << std::endl;

    std::cout
        << "103 : P3 배정"
        << std::endl;

    std::cout
        << "110 : 출차 허가"
        << std::endl;

    std::cout
        << "120 : 시스템 Reset"
        << std::endl;

    std::cout
        << "0   : 서버 종료"
        << std::endl;


    // =================================================
    // Threads
    // =================================================

    std::thread receiver(
        receiveThread,
        clientSocket
    );


    std::thread input(
        inputThread,
        clientSocket
    );


    input.join();


    receiver.join();


    // =================================================
    // Cleanup
    // =================================================

    closesocket(
        clientSocket
    );


    closesocket(
        serverSocket
    );


    WSACleanup();


    std::cout
        << "서버 종료"
        << std::endl;


    return 0;
}