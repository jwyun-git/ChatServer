#pragma once

#include <WinSock2.h>
#include <Windows.h>

#include <functional>
#include <thread>

class Iocp
{
public:
	using CompletionHandler = 
		std::function<void(BOOL, DWORD, DWORD, OVERLAPPED*)>;

	bool Initialize();
	bool Register(SOCKET socket, ULONG_PTR completionKey);
	void SetCompletionHandler(CompletionHandler handler);
	void Shutdown();

private:
	void workerThread();

private:
	HANDLE handle_ = nullptr;
	std::thread worker_;
	CompletionHandler completionHandler_;
};