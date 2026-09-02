#include <WinSock2.h>
#include <WS2tcpip.h>
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>

#include <iostream>

#define DEFAULT_BUFLEN 512
#define DEFAULT_PORT "27015"

int main(int argc, char* argv[])
{
	WSADATA wsaData;
	SOCKET ConnectSocket = INVALID_SOCKET;
	struct addrinfo* result = NULL, * ptr = NULL, hints;

	const char* sendbuf = "this is a test";
	char recvbuf[DEFAULT_BUFLEN];
	int iResult;
	int recvbuflen = DEFAULT_BUFLEN;

	// 매개변수 검증
	if (argc != 2) {
		printf("Usage: %s server-name\n", argv[0]);
		return 1;
	}

	// Winsock 초기화
	iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0) {
		printf("WSAStartup failed with error: %d\n", iResult);
		return 1;
	}

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	iResult = getaddrinfo(argv[1], DEFAULT_PORT, &hints, &result);
	if (iResult != 0) {
		printf("getaddrinfo failed with error: %d\n", iResult);
		WSACleanup();
		return 1;
	}

	// 성공할때까지 주소에 연결 시도
	for (ptr = result; ptr != NULL; ptr = ptr->ai_next) {

		// 서버에 연결하기 위한 SOCKET을 생성
		ConnectSocket = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
		if (ConnectSocket == INVALID_SOCKET) {
			printf("socket failed with error: %ld\n", WSAGetLastError());
			WSACleanup();
			return 1;
		}

		// 서버에 연결
		iResult = connect(ConnectSocket, ptr->ai_addr, (int)ptr->ai_addrlen);
		if (iResult == SOCKET_ERROR) {
			closesocket(ConnectSocket);
			ConnectSocket = INVALID_SOCKET;
			continue;
		}
		break;
	}

	freeaddrinfo(result);

	if (ConnectSocket == INVALID_SOCKET) {
		printf("Unable to connet to serve!\n");
		WSACleanup();
		return 1;
	}

	// 초기 버퍼 전송
	iResult = send(ConnectSocket, sendbuf, (int)strlen(sendbuf), 0);
	if (iResult == SOCKET_ERROR) {
		printf("send failed with error: %d\n", WSAGetLastError());
		closesocket(ConnectSocket);
		WSACleanup();
		return 1;
	}

	printf("Bytes Sent: %ld\n", iResult);

	// 연결 종료
	iResult = shutdown(ConnectSocket, SD_SEND);
	if (iResult == SOCKET_ERROR) {
		printf("shutdown failed with error: %d\n", WSAGetLastError());
		closesocket(ConnectSocket);
		WSACleanup();
		return 1;
	}

	// 피어가 연결을 종료할 때까지 수신
	do {
		iResult = recv(ConnectSocket, recvbuf, recvbuflen, 0);
		if (iResult > 0) {
			printf("Bytes received: %d\n", iResult);
		}
		else if (iResult == 0) {
			printf("Connection closed\n");
		}
		else {
			printf("recv failed with error: %d\n", WSAGetLastError());
		}
	} while (iResult > 0);

	// 정리
	closesocket(ConnectSocket);
	WSACleanup();

	return 0;
}

