//
// Created by kirill on 8/26/24.
//

#ifndef UVENT_DEFINES_H
#define UVENT_DEFINES_H

#include <atomic>
#include <variant>
#include <iostream>

#if defined(__APPLE__) || defined(__BSD__) || defined(__MACH__)

#include <netinet/in.h>
#include <sys/Event.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/sendfile.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>

#define OS_BSD

typedef std::variant<sockaddr_in, sockaddr_in6> client_addr_t;

#elif defined(__linux__)

#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <arpa/inet.h>
#include <sys/sendfile.h>
#include <fcntl.h>
#include <unistd.h>
#include <atomic>
#include <pthread.h>
#include <sched.h>
#include <netdb.h>

#define OS_LINUX

typedef std::variant<sockaddr_in, sockaddr_in6> client_addr_t;

extern std::mutex cout_mutex;

extern void pin_thread_to_core(int core_id);

inline void set_thread_name(std::string &&name, pthread_t thread) {
    pthread_setname_np(thread, name.c_str());
}

inline std::string get_thread_name(pthread_t thread) {
    char buf[16] = {};
    pthread_getname_np(thread, buf, sizeof(buf));
    return buf;
}

#elif defined(__APPLE__)
#include <pthread.h>

void set_thread_name(const std::string& name) {
    pthread_setname_np(name.c_str()); // only for current thread
}

std::string get_thread_name() {
    return "unsupported";
}

#elif defined(_WIN32) || defined(_WIN64)

#include <winsock.h> // sockaddr_in: https://learn.microsoft.com/ru-ru/windows/win32/api/winsock/ns-winsock-sockaddr_in
#include <ws2ipdef.h> // sockaddr_in6: https://learn.microsoft.com/ru-ru/windows/win32/api/ws2ipdef/ns-ws2ipdef-sockaddr_in6_lh

#define OS_WINDOWS

typedef std::variant <sockaddr_in, sockaddr_in6> client_addr_t;
typedef int socklen_t;

#else

#error "Current OS not supported"

#endif

#if UVENT_DEBUG

#include <string>
#include <system_error>
#include <execinfo.h>
#include <iostream>
#include <cxxabi.h>

extern std::string get_caller_function_name(int depth = 2);

extern void print_caller_function();

#define debug_error_print(message)                                                  \
    message += " (" + std::string(__FILE__) + ":" + std::to_string(__LINE__) + ")"; \
    throw std::system_error(errno, std::generic_category(), message);

#endif

#endif //UVENT_DEFINES_H
