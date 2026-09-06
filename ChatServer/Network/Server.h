#pragma once
#include <WinSock2.h>

#include <memory>
#include <mutex>
#include <vector>

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
    bool postRecv(Session* session);    // WSARecv() 등록
    bool postSend(Session* session, DWORD bytesTransferred);  // WSASend() 등록
    void onIoCompleted( // Worker가 IO완료 시 처리
        BOOL result,
        DWORD bytesTransferred,
        DWORD error,
        OVERLAPPED* overlapped
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