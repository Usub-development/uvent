//
// Created by Kirill Zhukov on 09/28/2024.
//

#include "uvent/system/Defines.h"

#if UVENT_DEBUG

#if defined(OS_WINDOWS)

#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

std::string get_caller_function_name(int depth) {
    void* stack[32];
    USHORT frames = RtlCaptureStackBackTrace(0, 32, stack, nullptr);

    if (frames <= depth)
        return "Unknown";

    HANDLE process = GetCurrentProcess();

    static bool initialized = false;
    if (!initialized) {
        SymInitialize(process, NULL, TRUE);
        initialized = true;
    }

    DWORD64 addr = reinterpret_cast<DWORD64>(stack[depth]);

    char buffer[sizeof(SYMBOL_INFO) + 256];
    PSYMBOL_INFO symbol = reinterpret_cast<PSYMBOL_INFO>(buffer);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 255;

    if (SymFromAddr(process, addr, 0, symbol)) {
        return std::string(symbol->Name);
    }

    return "Unknown";
}

void print_caller_function() {
    std::cout << "Caller: " << get_caller_function_name(4) << std::endl;
}

#else

#include <execinfo.h>
#include <cxxabi.h>

std::string get_caller_function_name(int depth) {
    void *buffer[10];
    int nptrs = backtrace(buffer, 10);
    if (nptrs <= depth) return "Unknown";

    char **symbols = backtrace_symbols(buffer, nptrs);
    std::string result = "Unknown";

    std::string sym = symbols[depth];
    size_t begin = sym.find('(');
    size_t end = sym.find('+', begin);
    if (begin != std::string::npos && end != std::string::npos) {
        std::string mangled = sym.substr(begin + 1, end - begin - 1);
        int status;
        char *demangled = abi::__cxa_demangle(mangled.c_str(), nullptr, nullptr, &status);
        if (status == 0 && demangled)
            result = demangled;
        else
            result = mangled;
        free(demangled);
    }

    free(symbols);
    return result;
}

void print_caller_function() {
    std::cout << "Caller: " << get_caller_function_name(4) << std::endl;
}

#endif

#endif  // UVENT_DEBUG

#ifdef OS_LINUX

std::mutex cout_mutex{};

void pin_thread_to_core(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    pthread_t thread = pthread_self();
    if (pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset) != 0) {
        std::lock_guard<std::mutex> lock(cout_mutex);
        std::cerr << "Failed to set affinity for core " << core_id << "\n";
    }
}

#elif defined(__APPLE__)
#include <pthread.h>

void set_thread_name(const std::string& name) {
    pthread_setname_np(name.c_str()); // only for current thread
}
#endif