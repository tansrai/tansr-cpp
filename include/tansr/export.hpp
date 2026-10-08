#pragma once

#if defined(_WIN32) && defined(TANSR_SHARED)
#if defined(TANSR_BUILDING_LIBRARY)
#define TANSR_API __declspec(dllexport)
#else
#define TANSR_API __declspec(dllimport)
#endif
#else
#define TANSR_API
#endif
