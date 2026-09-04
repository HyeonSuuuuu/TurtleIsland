#pragma once

// 프로젝트 파일(Server.vcxproj)은 이 헤더를 참조하는데 트리에 없어서 빌드가 깨져 있었다.
// main.cpp 가 필요로 하는 것만 담은 최소 구성이다.
//
// WinSock2.h 는 반드시 Windows.h 보다 먼저 와야 한다. 순서가 바뀌면 Windows.h 가
// 구버전 winsock.h 를 끌고 들어와 sockaddr_in 등이 재정의 에러를 낸다.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
