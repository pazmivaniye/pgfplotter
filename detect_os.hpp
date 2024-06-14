#ifndef PGFPLOTTER_DETECT_OS_HPP
#define PGFPLOTTER_DETECT_OS_HPP

#if defined(__APPLE__) && defined(__MACH__)
#if !defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE
#define OS_MACOS
#else
static_assert(false, "Failed to detect an acceptable OS (iOS is not supported)."
    "\n");
#endif
#elif defined(_WIN32) || defined(_WIN64)
#define OS_WINDOWS
#elif defined(__linux__)
#define OS_LINUX
#else
static_assert(false, "Failed to detect an acceptable OS (no valid macros define"
    "d).\n");
#endif
#if defined(OS_MACOS) || defined(OS_LINUX)
#define OS_UNIX
#endif

#endif
