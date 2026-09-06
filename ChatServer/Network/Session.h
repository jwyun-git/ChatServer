#pragma once
#include <WinSock2.h>

#include <deque>
#include <mutex>
#include <string>

#include "IocpEvent.h"

class Session
{
public:
	SOCKET socket = INVALID_SOCKET;

	IocpEvent recvEvent;
	IocpEvent sendEvent;
	
	std::deque<std::string> sendQueue;
	std::mutex sendMutex;

	bool isSending = false;
	DWORD sendOffset = 0;

private:

};
