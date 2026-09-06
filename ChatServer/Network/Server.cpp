#include <WS2tcpip.h>
#include <iostream>
#include <string>

#include "Server.h"
#include "IocpEvent.h"

constexpr char DEFAULT_PORT[] = "27015";

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

    // IOCP 초기화
    if (!iocp_.Initialize()) {
        WSACleanup();
        return false;
    }

    iocp_.SetCompletionHandler(
        [this](
            BOOL result,
            DWORD bytesTransferred,
            DWORD error,
            OVERLAPPED*  overlapped
            ) {
            onIoCompleted(
                result,
                bytesTransferred,
                error,
                overlapped
            );
        }
    );

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
        
        iocp_.Shutdown();
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
        iocp_.Shutdown();
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

        iocp_.Shutdown();
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

        iocp_.Shutdown();
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

        return false;
    }

    std::cout << "Client connected.\n";

    // 클라이언트 소켓을 IOCP에 등록
    if (!iocp_.Register(clientSocket_, 0)) {
        return false;
    }

    // 비동기 수신 요청
    if (!postRecv()) {
        return false;
    }

    // 클라이언트 연결 종료까지 대기
    {
        std::unique_lock<std::mutex> lock(mutex_);

        condition_.wait(
            lock,
            [this]() {
                return disconnected_;
            }
        );
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

    iocp_.Shutdown();

    WSACleanup();
}

bool Server::postRecv()
{
    recvEvent_.overlapped = {};

    recvEvent_.wsaBuf.buf = recvEvent_.buffer;
    recvEvent_.wsaBuf.len = BUFFER_SIZE;

    DWORD receivedBytes = 0;
    DWORD flags = 0;

    int iResult = WSARecv(
        clientSocket_,
        &recvEvent_.wsaBuf,
        1,
        &receivedBytes,
        &flags,
        &recvEvent_.overlapped,
        nullptr
    );

    if (iResult == SOCKET_ERROR) {
        int error = WSAGetLastError();
        // WSA_IO_PENDING이 아닌 경우 실제 수신 오류
        if (error != WSA_IO_PENDING) {
            std::cerr << "WSARecv failed with error: "
                << error << "\n";
            return false;
        }
    }

    return true;
}

bool Server::postSend(DWORD bytesTransferred)
{
    sendEvent_.overlapped = {};
    std::memcpy(
        sendEvent_.buffer,
        recvEvent_.buffer,
        bytesTransferred
    );

    sendEvent_.wsaBuf.buf = sendEvent_.buffer;
    sendEvent_.wsaBuf.len = bytesTransferred;

    DWORD sentBytes = 0;

    int iResult = WSASend(
        clientSocket_,
        &sendEvent_.wsaBuf,
        1,
        &sentBytes,
        0,
        &sendEvent_.overlapped,
        nullptr
    );

    if (iResult == SOCKET_ERROR) {
        int error = WSAGetLastError();
        if (error != WSA_IO_PENDING) {
            std::cerr << "WSASend failed with error: "
                << error << "\n";

            return false;
        }
    }
    
    return true;
}

void Server::onIoCompleted(
    BOOL result,
    DWORD bytesTransferred,
    DWORD error,
    OVERLAPPED* overlapped
)
{
    if (!result) {
        std::cerr << "IO operation failed with error: "
            << error << "\n";
        {
            std::lock_guard<std::mutex> lock(mutex_);
            disconnected_ = true;
        }

        condition_.notify_one();
        return;
    }

    // 수신 완료
    if (overlapped == &recvEvent_.overlapped) {
        
        // 정상적인 연결 종료
        if (bytesTransferred == 0) {
            std::cout << "Connection closing...\n";
            {
                std::lock_guard<std::mutex> lock(mutex_);
                disconnected_ = true;
            }
            condition_.notify_one();
            return;
        }

        std::cout << "Received: "
            << std::string(
                recvEvent_.buffer,
                bytesTransferred
            )
            << "\n";

        // 받은 데이터를 비동기로 다시 전송
        if (!postSend(bytesTransferred)) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                disconnected_ = true;
            }
            condition_.notify_one();
        }

        return;
    }

    // 송신 완료
    if (overlapped == &sendEvent_.overlapped) {
        std::cout << "Bytes sent: "
            << bytesTransferred << "\n";

        // Echo 송신 완료, 다음 수신 요청
        if (!postRecv()) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                disconnected_ = true;
            }
            condition_.notify_one();
        }
    }
}