// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"

BOOL APIENTRY DllMain(HMODULE hModule, // 로드된 DLL 모듈 핸들
                      DWORD reason,    // 호출 이유
                      LPVOID reserved) // attach/detach 상황에 대한 Windows 내부 정보
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
