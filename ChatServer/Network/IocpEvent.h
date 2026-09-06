#pragma once

#include <WinSock2.h>
#include <Windows.h>

constexpr int BUFFER_SIZE = 1024;

struct IocpEvent
{
	OVERLAPPED overlapped;
	WSABUF wsaBuf{};
	char buffer[BUFFER_SIZE]{};
};