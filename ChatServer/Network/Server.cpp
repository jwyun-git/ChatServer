#include <WS2tcpip.h>
#include <iostream>
#include <string>
#include <algorithm>

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
    while (true) {
        auto session = std::make_shared<Session>();

        // 클라이언트 연결 수락
        session->socket = accept(
            listenSocket_,
            nullptr,
            nullptr
        );

        if (session->socket == INVALID_SOCKET) {
            std::cerr << "accept failed with error: "
                << WSAGetLastError() << '\n';

            return false;
        }

        std::cout << "Client connected.\n";

        // 클라이언트 소켓을 IOCP에 등록
        if (!iocp_.Register(session->socket, 0)) {
            closesocket(session->socket);
            session->socket = INVALID_SOCKET;

            continue;
        }

        // Session 목록에 추가
        {
            std::lock_guard<std::mutex> lock(sessionsMutex_);
            sessions_.push_back(session);
        }

        // 비동기 수신 요청
        if (!postRecv(session.get())) {
            disconnectSession(session);
            continue;
        }
    }
}

void Server::Shutdown()
{
    // Pending IO종료를 위해 소켓부터 닫음
    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        // 소켓 및 Winsock 정리
        for (auto& session : sessions_) {
            if (session->socket != INVALID_SOCKET) {
                closesocket(session->socket);
                session->socket = INVALID_SOCKET;
            }
        }
    }

    // Worker Thread 종료
    iocp_.Shutdown();
    
    // Worker 종료 후 Session 객체 제거
    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        sessions_.clear();
    }

    if (listenSocket_ != INVALID_SOCKET) {
        closesocket(listenSocket_);
        listenSocket_ = INVALID_SOCKET;
    }

    WSACleanup();
}

bool Server::postRecv(Session* session)
{
    session->recvEvent.overlapped = {};

    session->recvEvent.wsaBuf.buf = session->recvEvent.buffer;
    session->recvEvent.wsaBuf.len = BUFFER_SIZE;

    DWORD receivedBytes = 0;
    DWORD flags = 0;

    int iResult = WSARecv(
        session->socket,
        &session->recvEvent.wsaBuf,
        1,
        &receivedBytes,
        &flags,
        &session->recvEvent.overlapped,
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

bool Server::enqueueSend(Session* session, const char* data, DWORD dataSize)
{
    if (session == nullptr ||
        session->socket == INVALID_SOCKET ||
        dataSize == 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(session->sendMutex);
    session->sendQueue.emplace_back(
        data,
        dataSize
    );

    // 이미 진행중이면 Queue에만 추가
    if (session->isSending) {
        return true;
    }

    session->isSending = true;
    session->sendOffset = 0;

    if (!postSend(session)) {
        session->isSending = false;
        session->sendQueue.clear();
        session->sendOffset = 0;
        
        return false;
    }

    return true;
}

bool Server::postSend(Session* session)
{
    if (session->sendQueue.empty()) {
        return false;
    }

    const std::string& message =
        session->sendQueue.front();

    DWORD remainingBytes =
        static_cast<DWORD>(message.size()) - session->sendOffset;

    if (remainingBytes > BUFFER_SIZE) {
        std::cerr << "Send data exceeds buffer size.\n";
        return false;
    }

    session->sendEvent.overlapped = {};

    std::memcpy(
        session->sendEvent.buffer,
        message.data() + session->sendOffset,
        remainingBytes
    );

    session->sendEvent.wsaBuf.buf = session->sendEvent.buffer;
    session->sendEvent.wsaBuf.len = remainingBytes;

    DWORD sentBytes = 0;

    int iResult = WSASend(
        session->socket,
        &session->sendEvent.wsaBuf,
        1,
        &sentBytes,
        0,
        &session->sendEvent.overlapped,
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

bool Server::handleSendCompleted(Session* session, DWORD bytesTransferred)
{
    std::lock_guard<std::mutex> lock(session->sendMutex);

    if (!session->isSending ||
        session->sendQueue.empty()) {
        return false;
    }

    if (bytesTransferred == 0) {
        return false;
    }

    session->sendOffset += bytesTransferred;

    const DWORD messageSize =
        static_cast<DWORD>(
            session->sendQueue.front().size()
            );

    // 일부만 전송된 경우 나머지 다시 전송
    if (session->sendOffset < messageSize) {
        return postSend(session);
    }

    // 현재 메시지 전송 완료
    session->sendQueue.pop_front();
    session->sendOffset = 0;

    // 다음 메시지가 없다면 송신 종료
    if (session->sendQueue.empty()) {
        session->isSending = false;
        return true;
    }

    // 다음 메시지 송신
    return postSend(session);
}

void Server::onIoCompleted(
    BOOL result,
    DWORD bytesTransferred,
    DWORD error,
    OVERLAPPED* overlapped
)
{
    std::shared_ptr<Session> targetSession;
    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        for (const auto& session : sessions_) {
            if (overlapped == &session->recvEvent.overlapped ||
                overlapped == &session->sendEvent.overlapped) {

                targetSession = session;
                break;
            }
        }
    }

    if (targetSession == nullptr) {
        return;
    }

    if (!result) {
        std::cerr << "IO operation failed with error: "
            << error << "\n";
      
        disconnectSession(targetSession);
        return;
    }

    // Recv 완료
    if (overlapped == &targetSession->recvEvent.overlapped) {
        
        // 정상적인 연결 종료
        if (bytesTransferred == 0) {
            disconnectSession(targetSession);
            return;
        }

        std::cout << "Received: "
            << std::string(
                targetSession->recvEvent.buffer,
                bytesTransferred
            )
            << "\n";

        broadcast(
            targetSession,
            bytesTransferred
        );

        if (!postRecv(targetSession.get())) {
            disconnectSession(targetSession);
        }

        return;
    }

    // 송신 완료
    if (overlapped == &targetSession->sendEvent.overlapped) {
        std::cout << "Bytes sent: "
            << bytesTransferred << "\n";

        if (!handleSendCompleted(
            targetSession.get(),
            bytesTransferred
        )) {
            disconnectSession(targetSession);
        }
        return;
    }
}

void Server::broadcast(const std::shared_ptr<Session>& sender, DWORD bytesTransferred)
{
    std::vector<std::shared_ptr<Session>> recipients;
    
    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);

        for (const auto& session : sessions_) {
            if (session == sender) {
                continue;
            }

            if (session->socket == INVALID_SOCKET) {
                continue;
            }

            recipients.push_back(session);
        }
    }

    for (const auto& session : recipients) {
        if (!enqueueSend(
            session.get(),
            sender->recvEvent.buffer,
            bytesTransferred
        )) {
            disconnectSession(session);
        }
    }
}

void Server::disconnectSession(const std::shared_ptr<Session>& session)
{
    if (!session) {
        return;
    }

    if (session->socket != INVALID_SOCKET) {
        closesocket(session->socket);
        session->socket = INVALID_SOCKET;
    }

    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);

        sessions_.erase(
            std::remove_if(
                sessions_.begin(),
                sessions_.end(),
                [&session](const std::shared_ptr<Session>& current) {
                    return current == session;
                }),
                sessions_.end()
            );
    }
    std::cout << "Client discconected.\n";
}
