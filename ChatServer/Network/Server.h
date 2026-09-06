#pragma once
#include <WinSock2.h>

#include <memory>
#include <mutex>
#include <vector>
#include <cstdint>
#include <cstring>

#include "Iocp.h"
#include "IocpEvent.h"
#include "Session.h"

class Server
{
public:
    bool Initialize();
    bool Run();
    void Shutdown();

private:
    bool postRecv(Session* session);    // WSARecv()
    bool enqueueSend(                   // 보낼 메시지를 Queue에 넣음
        Session* session,
        const char* data,
        DWORD dataSize
    );
    bool postSend(Session* session);  // WSASend()

    bool handleSendCompleted(         // 하나의 WSASend 완료 후 다음 송신 결정
        Session* session,
        DWORD bytesTransferred
    );

    void onIoCompleted(               // Worker가 IO완료 시 처리
        BOOL result,
        DWORD bytesTransferred,
        DWORD error,
        OVERLAPPED* overlapped
    );

    void broadcast(
        const std::shared_ptr<Session>& sender,
        const char* data,
        std::uint32_t dataSize
    );

    void disconnectSession(
        const std::shared_ptr<Session>& session
    );

private:
    SOCKET listenSocket_ = INVALID_SOCKET;

    Iocp iocp_;
    std::vector<std::shared_ptr<Session>>sessions_;
    std::mutex sessionsMutex_;
    bool disconnected_ = false;
};