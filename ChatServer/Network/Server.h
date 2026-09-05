#pragma once
#include <WinSock2.h>

class Server
{
public:
    bool Start();
    void Run();
    void Stop();

private:
    SOCKET listenSocket_ = INVALID_SOCKET;

};