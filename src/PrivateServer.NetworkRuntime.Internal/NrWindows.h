#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN // 잘 사용하지 않는 WinAPI 묶음 제외
#endif                      // !WIN32_LEAN_AND_MEAN

#ifndef NOMINMAX
#define NOMINMAX // Windows.h 에서 min, max 매크로 정의하지 못하게 막음(std와 충돌 방지)
#endif           // !NOMINMAX

#include <WinSock2.h> // Winsock 2 API 기본 헤더
#include <WS2tcpip.h> // 주소 변환(IPv4, IPv6) 관련 보조 API
#include <MSWSock.h>  // MS 확장 Winsock API
#include <Windows.h>  // Windows API
