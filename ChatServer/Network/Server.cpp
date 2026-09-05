#include "Server.h"
#include <WS2tcpip.h>
#include <iostream>
#include <string>

constexpr char DEFAULT_PORT[] = "27015";
constexpr int BUFFER_SIZE = 1024;

bool Server::Initialize()
{
    // Winsock 초기화
    WSADATA wsaData;
    int iResult;
    
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        std::cerr << "WSAStartup failed with error: "
            << iResult << '\n';
        return false;
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
        return false;
    }

    // 리슨 소켓 생성
    listenSocket_ = socket(
        addressInfo->ai_family,
        addressInfo->ai_socktype,
        addressInfo->ai_protocol
    );

    if (listenSocket_ == INVALID_SOCKET) {
        std::cerr << "socket failed with error: "
            << WSAGetLastError() << '\n';

        freeaddrinfo(addressInfo);
        WSACleanup();
        return false;
    }

    // 소켓에 주소와 포트 바인딩
    iResult = bind(
        listenSocket_,
        addressInfo->ai_addr,
        static_cast<int>(addressInfo->ai_addrlen)
    );

    freeaddrinfo(addressInfo);

    // bind 실패
    if (iResult == SOCKET_ERROR) {
        std::cerr << "bind failed with error: "
            << WSAGetLastError() << '\n';

        closesocket(listenSocket_);
        listenSocket_ = INVALID_SOCKET;
        WSACleanup();
        return false;
    }

    // 클라이언트 연결 요청 대기
    iResult = listen(listenSocket_, SOMAXCONN);

    // listen 실패
    if (iResult == SOCKET_ERROR) {
        std::cerr << "listen failed with error: "
            << WSAGetLastError() << '\n';

        closesocket(listenSocket_);
        listenSocket_ = INVALID_SOCKET;
        WSACleanup();
        return false;
    }

    std::cout << "Server is listening on port "
        << DEFAULT_PORT << ".\n";

    return true;
}

bool Server::Run()
{
    // 클라이언트 연결 수락
    clientSocket_ = accept(
        listenSocket_,
        nullptr,
        nullptr
    );

    if (clientSocket_ == INVALID_SOCKET) {
        std::cerr << "accept failed with error: "
            << WSAGetLastError() << '\n';

        closesocket(listenSocket_);
        WSACleanup();
        return false;
    }

    std::cout << "Client connected.\n";
    
    char buffer[BUFFER_SIZE]{};
    int iResult;

    // 클라이언트가 연결을 종료할 때까지 데이터 수신
    do {
        iResult = recv(
            clientSocket_,
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
                clientSocket_,
                buffer,
                iResult,
                0
            );

            // send 실패
            if (iSendResult == SOCKET_ERROR) {
                std::cerr << "send failed with error: "
                    << WSAGetLastError() << '\n';
                return false;
            }

            std::cout << "Bytes sent: "
                << iSendResult << '\n';
        }
        else if (iResult == 0) {
            std::cout << "Connection closing...\n";
        }
        // recv 실패
        else {
            std::cerr << "recv failed with error: "
                << WSAGetLastError() << '\n';
            return false;
        }

    } while (iResult > 0);

    // 송신 방향 연결 종료
    iResult = shutdown(clientSocket_, SD_SEND);

    // shutdown 실패
    if (iResult == SOCKET_ERROR) {
        std::cerr << "shutdown failed with error: "
            << WSAGetLastError() << '\n';

        return false;
    }

    return true;
}

void Server::Shutdown()
{
    // 소켓 및 Winsock 정리
    if (clientSocket_ != INVALID_SOCKET) {
        closesocket(clientSocket_);
        clientSocket_ = INVALID_SOCKET;
    }

    if (listenSocket_ != INVALID_SOCKET) {
        closesocket(listenSocket_);
        listenSocket_ = INVALID_SOCKET;
    }

    WSACleanup();
}