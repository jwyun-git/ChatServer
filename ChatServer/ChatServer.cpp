#include <WinSock2.h>
#include <WS2tcpip.h>

#include <iostream>
#include <string>

#include "Network/Server.h"


int main()
{
    Server server;
    
    if (!server.Initialize()) {
        return 1;
    }

    bool result = server.Run();

    server.Shutdown();

    return result ? 0 : 1;
}