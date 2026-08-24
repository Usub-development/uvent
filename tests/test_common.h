#ifndef UVENT_TEST_COMMON_H
#define UVENT_TEST_COMMON_H

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#define CHECK(cond)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(cond))                                                                                                   \
        {                                                                                                              \
            std::fprintf(stderr, "CHECK failed: %s at %s:%d\n", #cond, __FILE__, __LINE__);                            \
            std::abort();                                                                                              \
        }                                                                                                              \
    }                                                                                                                  \
    while (0)

#define CHECK_EQ(a, b)                                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        auto _va = (a);                                                                                                \
        auto _vb = (b);                                                                                                \
        if (!(_va == _vb))                                                                                             \
        {                                                                                                              \
            std::fprintf(stderr, "CHECK_EQ failed: %s == %s (%lld vs %lld) at %s:%d\n", #a, #b, (long long)_va,        \
                         (long long)_vb, __FILE__, __LINE__);                                                          \
            std::abort();                                                                                              \
        }                                                                                                              \
    }                                                                                                                  \
    while (0)

struct TestCase
{
    const char* name;
    void (*fn)();
};

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

inline void run_one_test(const TestCase& t)
{
#ifndef _WIN32
    const pid_t pid = fork();
    if (pid == 0)
    {
        t.fn();
        _exit(0);
    }
    int status = 0;
    ::waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
    {
        std::fprintf(stderr, "[FAIL] %s (status %d)\n", t.name, status);
        std::abort();
    }
#else
    t.fn();
#endif
}

inline int run_tests(const std::vector<TestCase>& tests)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const char* only = std::getenv("UVENT_TEST");
    std::size_t ran = 0;
    for (auto& t : tests)
    {
        if (only && std::string(t.name).find(only) == std::string::npos)
            continue;
        ++ran;
        auto t0 = std::chrono::steady_clock::now();
        run_one_test(t);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        std::printf("[ OK ] %s (%lld ms)\n", t.name, (long long)ms);
    }
    std::printf("%zu tests passed\n", ran);
    return 0;
}

inline unsigned hw_threads()
{
    unsigned n = std::thread::hardware_concurrency();
    return n ? n : 4;
}

#endif
