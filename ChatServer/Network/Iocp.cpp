#include <iostream>
#include <utility>

#include "Iocp.h"

bool Iocp::Initialize()
{
	// IOCP 생성
	handle_ = CreateIoCompletionPort(
		INVALID_HANDLE_VALUE,
		nullptr,
		0,
		0
	);

	if (handle_ == nullptr) {
		std::cerr << "CreateIoCompletionPort failed with error: "
			<< GetLastError() << "\n";
		return false;
	}

	// 스레드 생성
	worker_ = std::thread(&Iocp::workerThread, this);

	return true;
}

bool Iocp::Register(SOCKET socket, ULONG_PTR completionKey)
{
	// 소켓 등록
	HANDLE result = CreateIoCompletionPort(
		(HANDLE)socket,
		handle_,
		completionKey,
		0
	);

	if (result == nullptr) {
		std::cerr << "CreateIoCompletionPort register failed with error: "
			<< GetLastError() << "\n";
		return false;
	}

	return true;
}

void Iocp::SetCompletionHandler(CompletionHandler handler)
{
	completionHandler_ = std::move(handler);
}

void Iocp::Shutdown()
{
	if (handle_ == nullptr) {
		return;
	}

	// 인위적으로 IOCP 큐에 메시지를 넣어 종료
	PostQueuedCompletionStatus(handle_, 0, 1, nullptr);

	if (worker_.joinable()) {
		worker_.join();
	}

	CloseHandle(handle_);
	handle_ = nullptr;

}

void Iocp::workerThread()
{
	while (true) {
		DWORD bytesTransferred = 0;
		ULONG_PTR completionKey = 0;
		OVERLAPPED* overlapped = nullptr;

		BOOL result = GetQueuedCompletionStatus(
			handle_,
			&bytesTransferred,
			&completionKey,
			&overlapped,
			INFINITE	// 완료될때까지 계속 잠들어있음
		);

		// Worker Thread 종료 요청
		if (completionKey == 1 && overlapped == nullptr) {
			break;
		}

		DWORD error = ERROR_SUCCESS;

		if (!result) {
			error = GetLastError();
		}

		if (completionHandler_) {
			completionHandler_(
				result,
				bytesTransferred,
				error,
				overlapped
			);
		}
	}
}