#pragma once
#include <WinSock2.h>

#include <condition_variable>
#include <mutex>

#include "Iocp.h"
#include "IocpEvent.h"

class Server
{
public:
    bool Initialize();
    bool Run();
    void Shutdown();

private:
    bool postRecv();    // WSARecv() 등록
    bool postSend(DWORD bytesTransferred);  // WSASend() 등록
    void onIoCompleted( // Worker가 IO완료 시 처리
        BOOL result,
        DWORD bytesTransferred,
        DWORD error,
        OVERLAPPED* overlapped
    );

private:
    SOCKET listenSocket_ = INVALID_SOCKET;
    SOCKET clientSocket_ = INVALID_SOCKET;

    Iocp iocp_;
    IocpEvent recvEvent_;
    IocpEvent sendEvent_;

    std::mutex mutex_;
    std::condition_variable condition_;
    bool disconnected_ = false;
};