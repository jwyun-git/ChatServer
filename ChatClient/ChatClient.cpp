#include <WinSock2.h>
#include <WS2tcpip.h>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "Packet.h"

constexpr char DEFAULT_PORT[] = "27015";
constexpr int BUFFER_SIZE = 1024;

int main()
{
    WSADATA wsaData;
    SOCKET connectSocket = INVALID_SOCKET;

    addrinfo* result = nullptr;
    addrinfo* ptr = nullptr;
    addrinfo hints{};

    int iResult;

    // Winsock 초기화
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        std::cerr << "WSAStartup failed with error: "
            << iResult << '\n';
        return 1;
    }

    // 서버 주소 정보 설정
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    iResult = getaddrinfo(
        "localhost",
        DEFAULT_PORT,
        &hints,
        &result
    );

    if (iResult != 0) {
        std::cerr << "getaddrinfo failed with error: "
            << iResult << '\n';

        WSACleanup();
        return 1;
    }

    // 주소 목록을 순회하며 서버 연결 시도
    for (ptr = result; ptr != nullptr; ptr = ptr->ai_next) {

        // 서버 연결용 소켓 생성
        connectSocket = socket(
            ptr->ai_family,
            ptr->ai_socktype,
            ptr->ai_protocol
        );

        if (connectSocket == INVALID_SOCKET) {
            std::cerr << "socket failed with error: "
                << WSAGetLastError() << '\n';

            freeaddrinfo(result);
            WSACleanup();
            return 1;
        }

        // 서버에 연결
        iResult = connect(
            connectSocket,
            ptr->ai_addr,
            static_cast<int>(ptr->ai_addrlen)
        );

        if (iResult == SOCKET_ERROR) {
            closesocket(connectSocket);
            connectSocket = INVALID_SOCKET;
            continue;
        }

        break;
    }

    freeaddrinfo(result);

    if (connectSocket == INVALID_SOCKET) {
        std::cerr << "Unable to connect to server.\n";

        WSACleanup();
        return 1;
    }

    std::cout << "Connected to server.\n";
    std::cout << "Type 'quit' to exit.\n";

   // 서버 메시지 수신 전용 스레드
    std::thread recvThread([connectSocket]() {
        char recvBuffer[BUFFER_SIZE]{};
        std::vector<char> accumulatedBuffer;

        while (true) {
            int receivedBytes = recv(
                connectSocket,
                recvBuffer,
                BUFFER_SIZE,
                0
            );

            if (receivedBytes > 0) {
                // 받은 데이터를 누적 버퍼에 추가
                accumulatedBuffer.insert(
                    accumulatedBuffer.end(),
                    recvBuffer,
                    recvBuffer + receivedBytes
                );

                while (true) {
                    // 패킷 헤더에 다 들어오지 않은 경우
                    if (accumulatedBuffer.size() < PACKET_HEADER_SIZE) {
                        break;
                    }

                    std::uint32_t networkPayloadSize = 0;

                    std::memcpy(
                        &networkPayloadSize,
                        accumulatedBuffer.data(),
                        PACKET_HEADER_SIZE
                    );

                    std::uint32_t payloadSize =
                        ntohl(networkPayloadSize);

                    // 비정상적인 패킷 크기 검사
                    if (payloadSize > MAX_PAYLOAD_SIZE) {
                        std::cerr << "\nInvalid packet size: "
                            << payloadSize << "\n";

                        return;
                    }

                    std::size_t packetSize =
                        PACKET_HEADER_SIZE + payloadSize;

                    // payload가 아직 전부 도착하지 않은 경우
                    if (accumulatedBuffer.size() < packetSize)
                    {
                        break;
                    }

                    const char* payload =
                        accumulatedBuffer.data() + PACKET_HEADER_SIZE;

                    std::cout << "\nReceived: "
                        << std::string(
                            payload,
                            payloadSize
                        ) << "\n";

                    // 처리한 패킷을 누적 버퍼에서 제거
                    accumulatedBuffer.erase(
                        accumulatedBuffer.begin(),
                        accumulatedBuffer.begin() + packetSize
                    );
                }
            }

            else if (receivedBytes == 0) {
                std::cout << "Connection closed.\n";
                break;
            }
            else {
                int error = WSAGetLastError();
                // 프로그램 종료과정에서 recv 해제된 경우
                if (error != WSAESHUTDOWN) {
                    std::cerr << "recv failed with error: "
                        << error << '\n';
                }
                break;
            }
        }
    });

    std::string message;

    // 사용자 메시지 입력 및 송신
    while (true) {
        std::cout << "> ";

        std::getline(std::cin, message);

        if (message == "quit") {
            break;
        }

        if (message.empty()) {
            continue;
        }

        std::uint32_t payloadSize =
            static_cast<std::uint32_t>(message.size());

        if (payloadSize > MAX_PAYLOAD_SIZE) {
            std::cerr << "Message is too long.\n";
            continue;
        }

        // 패킷 헤더는 Network Byte Order로 저장
        std::uint32_t networkPayloadSize =
            htonl(payloadSize);
        
        // [4-byte Payload Size][Payload]
        std::vector<char> packet(
            PACKET_HEADER_SIZE + payloadSize
        );

        std::memcpy(
            packet.data(),
            &networkPayloadSize,
            PACKET_HEADER_SIZE
        );

        std::memcpy(
            packet.data() + PACKET_HEADER_SIZE,
            message.data(),
            payloadSize
        );

        // 패킷 전체가 전송될때까지 반복
        int totalSentBytes = 0;
        int packetSize = static_cast<int>(packet.size());

        while (totalSentBytes < packetSize) {
            iResult = send(
                connectSocket,
                packet.data() + totalSentBytes,
                packetSize - totalSentBytes,
                0
            );

            if (iResult == SOCKET_ERROR) {
                std::cerr << "send failed with error: "
                    << WSAGetLastError() << '\n';
                break;
            }

            totalSentBytes += iResult;
        }

        if (iResult == SOCKET_ERROR) {
            break;
        }
      
        std::cout << "Bytes sent: "
            << totalSentBytes << '\n';
    }
    

    // 송수신을 종료해 recv() 대기 중인 스레드를 깨움
    iResult = shutdown(connectSocket, SD_BOTH);

    if (iResult == SOCKET_ERROR) {
        std::cerr << "shutdown failed with error: "
            << WSAGetLastError() << '\n';
    }

    if (recvThread.joinable()) {
        recvThread.join();
    }

    // 소켓 및 Winsock 정리
    closesocket(connectSocket);
    connectSocket = INVALID_SOCKET;
    WSACleanup();

    return 0;
}