#pragma once

#include "Export.h"

// C++ name mangling off (C 방식 linkage)
// 이 함수 선언 자체의 linkage를 C로 지정
extern "C" PSNR_API int nr_get_version();
