#pragma once
#include <WinSock2.h>

class Server
{
public:
    bool Initialize();
    bool Run();
    void Shutdown();

private:
    SOCKET listenSocket_ = INVALID_SOCKET;
    SOCKET clientSocket_ = INVALID_SOCKET;

};