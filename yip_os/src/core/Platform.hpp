#pragma once

#ifdef __linux__
    #define YIPOS_LINUX 1
    #define YIPOS_WINDOWS 0
#elif defined(_WIN32)
    #define YIPOS_LINUX 0
    #define YIPOS_WINDOWS 1
#else
    #error "Unsupported platform"
#endif
