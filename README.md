# ChatServer

Windows IOCP(I/O Completion Ports)를 기반으로 구현한 **비동기 멀티클라이언트 채팅 서버**입니다.

Microsoft Winsock 예제의 기본 TCP 연결 흐름을 바탕으로 시작해, Blocking Echo Server에서 IOCP 기반 비동기 서버 구조로 단계적으로 확장했습니다.

## 주요 기능

- Windows Winsock 기반 TCP 통신
- IOCP 기반 비동기 `WSARecv` / `WSASend`
- Worker Thread를 통한 I/O 완료 처리
- 다중 클라이언트 동시 접속
- Session 단위 연결 관리
- 클라이언트 연결 종료 및 Session 정리
- 다중 클라이언트 Broadcast 채팅
- Session별 Send Queue
- Partial Send 처리
- Length-Prefix 기반 TCP Packet Framing
- 클라이언트 송신 / 수신 분리
- Network Byte Order 기반 패킷 길이 전달
- 최대 Payload 크기 검증

## 프로젝트 구조

```text
ChatServer/
├─ ChatServer.sln
├─ Common/
│  └─ Packet.h
├─ ChatServer/
│  ├─ ChatServer.cpp
│  └─ Network/
│     ├─ Iocp.h
│     ├─ Iocp.cpp
│     ├─ IocpEvent.h
│     ├─ Server.h
│     ├─ Server.cpp
│     └─ Session.h
└─ ChatClient/
   └─ ChatClient.cpp
```

## 서버 구조

```text
Main Thread
   │
   └─ accept()
        │
        └─ Session 생성
             │
             └─ IOCP 등록
                  │
                  └─ WSARecv
                       │
                       ▼
                IOCP Completion Queue
                       │
                       ▼
                  Worker Thread
                       │
             ┌─────────┴─────────┐
             │                   │
        Recv 완료             Send 완료
             │                   │
       Packet Parsing       Send Queue 처리
             │                   │
        Broadcast          다음 WSASend
```

메인 스레드는 새로운 클라이언트의 연결을 수락하고, 실제 송수신 처리는 IOCP Worker Thread가 담당합니다.

## Session

각 클라이언트 연결은 `Session` 객체로 관리합니다.

Session은 다음 상태를 가집니다.

- 연결된 `SOCKET`
- 비동기 수신용 `IocpEvent`
- 비동기 송신용 `IocpEvent`
- TCP 수신 누적 버퍼
- 비동기 송신을 위한 Send Queue
- Partial Send 처리를 위한 송신 Offset
- 송신 상태 동기화를 위한 Mutex

서버는 여러 Session을 컨테이너로 관리하며, Session 목록 접근 시 Mutex를 사용합니다.

## Send Queue

동일한 Session에서 이전 `WSASend()`가 완료되기 전에 새로운 송신 요청이 발생하면 같은 `OVERLAPPED` 및 송신 버퍼를 재사용할 수 있습니다.

이를 방지하기 위해 Session별 Send Queue를 사용합니다.

```text
Broadcast 발생
    │
    ▼
Send Queue
[A][B][C]
 │
 ▼
WSASend(A)
 │
 ▼
Send Completion
 │
 ▼
WSASend(B)
 │
 ▼
Send Completion
 │
 ▼
WSASend(C)
```

한 Session에는 동시에 하나의 비동기 송신만 진행되도록 구성했습니다.

또한 `WSASend()` 완료 시 실제 전송된 바이트 수를 확인해 Partial Send가 발생하면 남은 데이터를 이어서 전송합니다.

## TCP Packet Framing

TCP는 Byte Stream 기반 프로토콜이므로 한 번의 `send()`와 한 번의 `recv()`가 1:1로 대응되지 않습니다.

예를 들어 송신 측에서:

```text
[hello]
[world]
```

를 보냈더라도 수신 측에서는 다음과 같이 전달될 수 있습니다.

```text
recv #1 : [hel]
recv #2 : [lo][wor]
recv #3 : [ld]
```

이를 처리하기 위해 Length-Prefix 방식의 패킷 프레이밍을 구현했습니다.

### Packet Format

```text
┌──────────────────────┬─────────────────────┐
│ Payload Size         │ Payload             │
│ uint32_t / 4 bytes   │ N bytes             │
└──────────────────────┴─────────────────────┘
```

Payload Size는 Network Byte Order로 전송합니다.

송신:

```text
htonl(payloadSize)
```

수신:

```text
ntohl(networkPayloadSize)
```

수신 데이터는 Session의 누적 버퍼에 저장한 뒤, 완성된 패킷이 존재할 때만 Payload를 꺼내 처리합니다.

## Broadcast

한 클라이언트가 메시지를 전송하면 송신자를 제외한 현재 연결된 다른 Session에 메시지를 전달합니다.

```text
Client A ── "hello" ──> Server
                         │
                         ├──> Client B
                         └──> Client C
```

현재 구현에서는 송신자 자신에게는 메시지를 다시 보내지 않습니다.

## ChatClient

클라이언트는 송신과 수신을 분리했습니다.

```text
Main Thread
 └─ 사용자 입력
     └─ send()

Recv Thread
 └─ recv()
     └─ Packet Parsing
         └─ 메시지 출력
```

따라서 사용자가 입력 중인 상태에서도 다른 클라이언트의 메시지를 즉시 수신할 수 있습니다.

## 개발 환경

- Windows 11
- Visual Studio 2026
- C++20
- Winsock2
- IOCP
- `Ws2_32.lib`

프로젝트 속성의 Linker → Input → Additional Dependencies에 다음 라이브러리를 추가합니다.

```text
Ws2_32.lib
```

`Common` 헤더 사용을 위해 ChatServer와 ChatClient 프로젝트의:

```text
C/C++ → General → Additional Include Directories
```

에 다음 경로를 추가합니다.

```text
$(SolutionDir)Common
```

## 실행 방법

1. `ChatServer.sln`을 Visual Studio에서 엽니다.
2. ChatServer 프로젝트를 빌드하고 실행합니다.
3. ChatClient 프로젝트를 여러 개 실행합니다.
4. 각 클라이언트에서 메시지를 입력합니다.
5. `quit`을 입력하면 해당 클라이언트가 종료됩니다.

기본 접속 정보:

```text
Host : localhost
Port : 27015
```

## 테스트

다음 항목을 확인했습니다.

- 여러 클라이언트 동시 접속
- A → B/C Broadcast
- 여러 클라이언트의 교차 송신
- 연속 메시지 송수신
- 한글, 영문, 숫자, 공백 및 특수문자
- 클라이언트 종료 후 다른 클라이언트 연결 유지
- 클라이언트 재접속
- 최대 Payload 크기 검증
- TCP 패킷 누적 및 복원
- Send Queue 기반 연속 비동기 송신
- Partial Send 처리

## 구현 과정

프로젝트는 다음 순서로 기능을 확장했습니다.

```text
Blocking TCP Echo Server / Client
        ↓
Server 클래스 분리
        ↓
IOCP 생성 및 Worker Thread
        ↓
WSARecv / WSASend 비동기 송수신
        ↓
Session 분리
        ↓
멀티클라이언트 지원
        ↓
Session 연결 종료 처리
        ↓
Broadcast 채팅
        ↓
Send Queue
        ↓
TCP Packet Framing
```

단순히 최종 구조를 한 번에 구현하지 않고 각 단계에서 실행 및 동작을 확인하며 기능을 확장했습니다.

## 현재 제한사항

현재 프로젝트는 네트워크 및 IOCP 구조 학습에 초점을 맞춘 콘솔 기반 포트폴리오 프로젝트입니다.

- 사용자 인증 및 로그인 미구현
- 채팅방(Room) 기능 미구현
- DB 연동 미구현
- 서버 Graceful Shutdown 고도화 필요
- 다수 Worker Thread 환경에 대한 추가 동기화 검증 필요
- 클라이언트 콘솔 입력과 수신 출력이 동시에 발생할 경우 화면이 섞여 보일 수 있음

## 향후 개선 가능 항목

- Session ID 및 Completion Key 기반 Session 탐색 개선
- 다중 IOCP Worker Thread
- Room / Channel 시스템
- 접속자 이름 및 명령어
- Load Test Client
- 서버 통계 및 로깅
- Graceful Shutdown
- 패킷 타입 및 Protocol ID 확장
- MySQL 기반 사용자/채팅 데이터 저장

## 참고

초기 TCP 연결 코드는 Microsoft Learn의 Winsock Server/Client 예제를 참고하여 구성했으며, 이후 IOCP, Session 관리, 비동기 송수신, Send Queue 및 Packet Framing 구조를 직접 확장했습니다.

- Microsoft Learn: Winsock Finished Server and Client Code
