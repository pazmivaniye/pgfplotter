#include "system.hpp"
#include <iostream>
#include <filesystem>

#if defined(__APPLE__) && defined(__MACH__)
#if !defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE
#define OS_MACOS
#else
static_assert(false, "Failed to detect an acceptable OS (iOS is not supported)."
    );
#endif
#elif defined(_WIN32) || defined(_WIN64)
#define OS_WINDOWS
#elif defined(__linux__)
#define OS_LINUX
#else
static_assert(false, "Failed to detect an acceptable OS (no valid macros define"
    "d).");
#endif
#if defined(OS_MACOS) || defined(OS_LINUX)
#define OS_UNIX
#endif

#ifdef OS_WINDOWS
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <sys/fcntl.h>
#include <cstring>
#endif

void pgfplotter::system_call(const std::string& file, const std::vector<std::
    string>& args)
{
#ifdef OS_WINDOWS
    std::string cmd = file;
    for(const auto& n : args)
    {
        cmd += " \"" + n + "\"";
    }
    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = true;
    sa.lpSecurityDescriptor = nullptr;
    HANDLE g_hChildStd_IN_Rd = nullptr;
    HANDLE g_hChildStd_IN_Wr = nullptr;
    HANDLE g_hChildStd_OUT_Rd = nullptr;
    HANDLE g_hChildStd_OUT_Wr = nullptr;
    if(!CreatePipe(&g_hChildStd_OUT_Rd, &g_hChildStd_OUT_Wr, &sa, 0))
    {
        throw std::runtime_error("Failed to create output pipe: Error " + std::
            to_string(GetLastError()) + ".");
    }
    if(!SetHandleInformation(g_hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0))
    {
        throw std::runtime_error("Failed to set output pipe to inherit: Error "
            + std::to_string(GetLastError()) + ".");
    }
    if(!CreatePipe(&g_hChildStd_IN_Rd, &g_hChildStd_IN_Wr, &sa, 0))
    {
        throw std::runtime_error("Failed to create input pipe: Error " + std::
            to_string(GetLastError()) + ".");
    }
    if(!SetHandleInformation(g_hChildStd_IN_Wr, HANDLE_FLAG_INHERIT, 0))
    {
        throw std::runtime_error("Failed to set input pipe to inherit: Error " +
            std::to_string(GetLastError()) + ".");
    }
    si.hStdError = g_hChildStd_OUT_Wr;
    si.hStdOutput = g_hChildStd_OUT_Wr;
    si.hStdInput = g_hChildStd_IN_Rd;
    si.dwFlags |= STARTF_USESTDHANDLES;
    PROCESS_INFORMATION pi;
    if(!CreateProcessA(nullptr, const_cast<char*>(cmd.c_str()), nullptr,
        nullptr, true, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    {
        throw std::runtime_error("Failed to create process: Error " + std::
            to_string(GetLastError()) + ".");
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode;
    if(!GetExitCodeProcess(pi.hProcess, &exitCode))
    {
        throw std::runtime_error("Failed to get exit code: Error " + std::
            to_string(GetLastError()) + ".");
    }
    if(exitCode == STILL_ACTIVE)
    {
        throw std::runtime_error("Wait returned before process completed.");
    }
    if(exitCode)
    {
        throw std::runtime_error("System call returned " + std::to_string(
            exitCode) + ".");
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(g_hChildStd_OUT_Wr);
    g_hChildStd_OUT_Wr = nullptr;
    CloseHandle(g_hChildStd_IN_Rd);
    g_hChildStd_OUT_Wr = nullptr;
#else
    const auto pid = fork();
    if(pid > 0)
    {
        int status;
        if(waitpid(pid, &status, 0) < 0)
        {
            throw std::runtime_error("Wait failed.");
        }
        if(!WIFEXITED(status))
        {
            throw std::runtime_error("System call did not exit normally.");
        }
        const auto exitCode = WEXITSTATUS(status);
        if(exitCode)
        {
            throw std::runtime_error("System call returned " + std::to_string(
                exitCode) + ".");
        }
    }
    else if(pid == 0)
    {
        std::vector<const char*> argv = {file.c_str()};
        for(const auto& n : args)
        {
            argv.push_back(n.c_str());
        }
        argv.push_back(nullptr);
        const auto fd = open("/dev/null", O_WRONLY);
        if(fd < 0)
        {
            std::cerr << "Warning: Failed to open \"/dev/null\": " << std::
                strerror(errno) << std::endl;
        }
        else
        {
            dup2(fd, 1);
            dup2(fd, 2);
            close(fd);
        }
        const int status = execvp(argv[0], const_cast<char**>(argv.data()));
        std::exit(status);
    }
    else
    {
        throw std::runtime_error("Fork failed: " + std::string(std::strerror(
            errno)) + ".");
    }
#endif
}

void pgfplotter::split_path(const std::string& path, std::string& dir, std::
    string& name)
{
    const std::filesystem::path p(path);
    dir = p.parent_path().string();
    name = p.filename().string();
}
