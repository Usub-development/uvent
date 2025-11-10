//
// Created by kirill on 8/26/24.
//

#ifndef UVENT_DEFINES_H
#define UVENT_DEFINES_H

#include <atomic>
#include <variant>
#include <iostream>

#if defined(__APPLE__) || defined(__MACH__)
#define OS_APPLE 1
#endif

#if defined(__BSD__)
#define OS_BSD 1
#endif


#if defined(__APPLE__) || defined(__BSD__)

#include <netinet/in.h>
#include <sys/Event.h>
#include <sys/time.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>

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

// -------------------- SIGPIPE compatibility (cross-platform) --------------------
#if defined(__unix__) || defined(__APPLE__) || defined(__MACH__) || defined(__linux__) || defined(__BSD__)
#include <csignal>
#include <cstring>

static inline void uvent_ignore_sigpipe_once() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGPIPE, &sa, nullptr);
}

__attribute__((constructor))
static void uvent_ignore_sigpipe_ctor() { uvent_ignore_sigpipe_once(); }

#define UVENT_IGNORE_SIGPIPE() uvent_ignore_sigpipe_once()
#else
#define UVENT_IGNORE_SIGPIPE() ((void)0)
#endif

#if defined(__linux__)
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#define UVENT_SEND_NOSIG_FLAGS MSG_NOSIGNAL
static inline void uvent_sock_nosigpipe(int) {}

#elif defined(__APPLE__) || defined(__BSD__) || defined(__MACH__)
#ifndef SO_NOSIGPIPE
#define SO_NOSIGPIPE 0x1022
#endif
#define UVENT_SEND_NOSIG_FLAGS 0
static inline void uvent_sock_nosigpipe(int fd) {
    int one = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
}
#else
#define UVENT_SEND_NOSIG_FLAGS 0
static inline void uvent_sock_nosigpipe(int) {}
#endif

#include <cstddef>
#include <sys/types.h>

static inline
#if defined(_WIN32) || defined(_WIN64)
int
#else
ssize_t
#endif
uvent_send_nosig(int fd, const void* buf, size_t len, int flags) {
#if defined(_WIN32) || defined(_WIN64)
    // На Windows SIGPIPE не возникает
    return ::send((SOCKET)fd, (const char*)buf, (int)len, flags);
#else
    return ::send(fd, buf, len, flags | UVENT_SEND_NOSIG_FLAGS);
#endif
}
// -------------------------------------------------------------------------------

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
