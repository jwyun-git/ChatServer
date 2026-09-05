#include <WinSock2.h>
#include <WS2tcpip.h>

#include <iostream>
#include <string>

constexpr char DEFAULT_PORT[] = "27015";
constexpr int BUFFER_SIZE = 1024;

int main()
{
    WSADATA wsaData;
    SOCKET connectSocket = INVALID_SOCKET;

    addrinfo* result = nullptr;
    addrinfo* ptr = nullptr;
    addrinfo hints{};

    char recvBuffer[BUFFER_SIZE]{};
    int iResult;

    // Winsock 초기화
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        std::cerr << "WSAStartup failed with error: "
            << iResult << '\n';
        return 1;
    }

    // 서버 주소 정보 설정
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    iResult = getaddrinfo(
        "localhost",
        DEFAULT_PORT,
        &hints,
        &result
    );

    if (iResult != 0) {
        std::cerr << "getaddrinfo failed with error: "
            << iResult << '\n';

        WSACleanup();
        return 1;
    }

    // 주소 목록을 순회하며 서버 연결 시도
    for (ptr = result; ptr != nullptr; ptr = ptr->ai_next) {

        // 서버 연결용 소켓 생성
        connectSocket = socket(
            ptr->ai_family,
            ptr->ai_socktype,
            ptr->ai_protocol
        );

        if (connectSocket == INVALID_SOCKET) {
            std::cerr << "socket failed with error: "
                << WSAGetLastError() << '\n';

            freeaddrinfo(result);
            WSACleanup();
            return 1;
        }

        // 서버에 연결
        iResult = connect(
            connectSocket,
            ptr->ai_addr,
            static_cast<int>(ptr->ai_addrlen)
        );

        if (iResult == SOCKET_ERROR) {
            closesocket(connectSocket);
            connectSocket = INVALID_SOCKET;
            continue;
        }

        break;
    }

    freeaddrinfo(result);

    if (connectSocket == INVALID_SOCKET) {
        std::cerr << "Unable to connect to server.\n";

        WSACleanup();
        return 1;
    }

    std::cout << "Connected to server.\n";
    std::cout << "Type 'quit' to exit.\n";

    std::string message;

    // 사용자 메시지 입력 및 송수신
    while (true) {
        std::cout << "> ";

        std::getline(std::cin, message);

        if (message == "quit") {
            break;
        }

        if (message.empty()) {
            continue;
        }

        // 서버에 메시지 전송
        iResult = send(
            connectSocket,
            message.data(),
            static_cast<int>(message.size()),
            0
        );

        if (iResult == SOCKET_ERROR) {
            std::cerr << "send failed with error: "
                << WSAGetLastError() << '\n';
            break;
        }

        std::cout << "Bytes sent: "
            << iResult << '\n';

        // 서버가 에코한 메시지 수신
        iResult = recv(
            connectSocket,
            recvBuffer,
            BUFFER_SIZE,
            0
        );

        if (iResult > 0) {
            std::cout << "Received: "
                << std::string(recvBuffer, iResult)
                << '\n';
        }
        else if (iResult == 0) {
            std::cout << "Connection closed.\n";
            break;
        }
        else {
            std::cerr << "recv failed with error: "
                << WSAGetLastError() << '\n';
            break;
        }
    }

    // 더 이상 데이터를 보내지 않음을 서버에 알림
    iResult = shutdown(connectSocket, SD_SEND);

    if (iResult == SOCKET_ERROR) {
        std::cerr << "shutdown failed with error: "
            << WSAGetLastError() << '\n';

        closesocket(connectSocket);
        WSACleanup();
        return 1;
    }

    // 소켓 및 Winsock 정리
    closesocket(connectSocket);
    WSACleanup();

    return 0;
}