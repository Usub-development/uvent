//
// Created by Kirill Zhukov on 09/28/2024.
//

#include "include/uvent/system/Defines.h"

#if UVENT_DEBUG

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
    } else {
        std::lock_guard<std::mutex> lock(cout_mutex);
        std::cout << "Pinned thread to core: " << core_id << std::endl;
    }
}

#elif defined(__APPLE__)
#include <pthread.h>

void set_thread_name(const std::string& name) {
    pthread_setname_np(name.c_str()); // only for current thread
}
#endif

#endif