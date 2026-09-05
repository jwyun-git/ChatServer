#include <WinSock2.h>
#include <WS2tcpip.h>

#include <iostream>
#include <string>

constexpr char DEFAULT_PORT[] = "27015";
constexpr int BUFFER_SIZE = 1024;

int main()
{
    WSADATA wsaData;
    int iResult;

    // Winsock 초기화
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        std::cerr << "WSAStartup failed with error: "
            << iResult << '\n';
        return 1;
    }

    addrinfo hints{};
    addrinfo* addressInfo = nullptr;

    // 서버 주소 정보 설정
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    iResult = getaddrinfo(
        nullptr,
        DEFAULT_PORT,
        &hints,
        &addressInfo
    );

    if (iResult != 0) {
        std::cerr << "getaddrinfo failed with error: "
            << iResult << '\n';

        WSACleanup();
        return 1;
    }

    // 리슨 소켓 생성
    SOCKET listenSocket = socket(
        addressInfo->ai_family,
        addressInfo->ai_socktype,
        addressInfo->ai_protocol
    );

    if (listenSocket == INVALID_SOCKET) {
        std::cerr << "socket failed with error: "
            << WSAGetLastError() << '\n';

        freeaddrinfo(addressInfo);
        WSACleanup();
        return 1;
    }

    // 소켓에 주소와 포트 바인딩
    iResult = bind(
        listenSocket,
        addressInfo->ai_addr,
        static_cast<int>(addressInfo->ai_addrlen)
    );

    freeaddrinfo(addressInfo);

    if (iResult == SOCKET_ERROR) {
        std::cerr << "bind failed with error: "
            << WSAGetLastError() << '\n';

        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    // 클라이언트 연결 요청 대기
    iResult = listen(listenSocket, SOMAXCONN);

    if (iResult == SOCKET_ERROR) {
        std::cerr << "listen failed with error: "
            << WSAGetLastError() << '\n';

        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "Server is listening on port "
        << DEFAULT_PORT << ".\n";

    // 클라이언트 연결 수락
    SOCKET clientSocket = accept(
        listenSocket,
        nullptr,
        nullptr
    );

    if (clientSocket == INVALID_SOCKET) {
        std::cerr << "accept failed with error: "
            << WSAGetLastError() << '\n';

        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "Client connected.\n";

    // 현재는 클라이언트 한 명만 처리하므로 리슨 소켓 종료
    closesocket(listenSocket);

    char buffer[BUFFER_SIZE]{};

    // 클라이언트가 연결을 종료할 때까지 데이터 수신
    do {
        iResult = recv(
            clientSocket,
            buffer,
            sizeof(buffer),
            0
        );

        if (iResult > 0) {
            std::cout << "Received: "
                << std::string(buffer, iResult)
                << '\n';

            // 수신한 데이터를 그대로 다시 전송
            int iSendResult = send(
                clientSocket,
                buffer,
                iResult,
                0
            );

            if (iSendResult == SOCKET_ERROR) {
                std::cerr << "send failed with error: "
                    << WSAGetLastError() << '\n';

                closesocket(clientSocket);
                WSACleanup();
                return 1;
            }

            std::cout << "Bytes sent: "
                << iSendResult << '\n';
        }
        else if (iResult == 0) {
            std::cout << "Connection closing...\n";
        }
        else {
            std::cerr << "recv failed with error: "
                << WSAGetLastError() << '\n';

            closesocket(clientSocket);
            WSACleanup();
            return 1;
        }

    } while (iResult > 0);

    // 송신 방향 연결 종료
    iResult = shutdown(clientSocket, SD_SEND);

    if (iResult == SOCKET_ERROR) {
        std::cerr << "shutdown failed with error: "
            << WSAGetLastError() << '\n';

        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    // 소켓 및 Winsock 정리
    closesocket(clientSocket);
    WSACleanup();

    return 0;
}