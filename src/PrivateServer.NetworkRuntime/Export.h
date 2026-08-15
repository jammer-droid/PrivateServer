#pragma once

#if defined(PSNR_EXPORTS)
#define PSNR_API __declspec(dllexport) // for DLL project build
#elif defined(PSNR_INTERNAL_BUILD)
#define PSNR_API // for the supporting static library linked into the DLL
#else
#define PSNR_API __declspec(dllimport) // for using DLL project
#endif
