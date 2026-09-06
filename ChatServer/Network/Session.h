#pragma once
#include <WinSock2.h>
#include "IocpEvent.h"

class Session
{
public:
	SOCKET socket = INVALID_SOCKET;

	IocpEvent recvEvent;
	IocpEvent sendEvent;
	
private:

};
