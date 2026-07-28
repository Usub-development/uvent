#include "uvent/net/Resolver.h"

#include "uvent/system/Settings.h"
#include "uvent/system/SystemContext.h"

#ifdef OS_LINUX
#ifndef UVENT_ENABLE_IO_URING
#include "uvent/poll/EPoller.h"
#else
#include "uvent/poll/IOUringPoller.h"
#endif
#elif defined(OS_BSD) || defined(OS_APPLE)
#include "uvent/poll/KPoller.h"
#else
#include "uvent/poll/IocpPoller.h"
#endif

namespace usub::uvent::net
{
    namespace detail
    {
        Resolver& Resolver::instance()
        {
            static Resolver r;
            return r;
        }

        Resolver::Resolver()
        {
            int n = settings::resolver_threads;
            if (n < 1)
                n = 1;
            this->workers_.reserve(static_cast<size_t>(n));
            for (int i = 0; i < n; ++i)
                this->workers_.emplace_back([this] { this->workerLoop(); });
        }

        Resolver::~Resolver()
        {
            {
                std::lock_guard lk(this->mtx_);
                this->stop_ = true;
            }
            this->cv_.notify_all();
            for (auto& w : this->workers_)
                w.join();
        }

        void Resolver::submit(ResolveRequest* req)
        {
            {
                std::lock_guard lk(this->mtx_);
                this->queue_.push_back(req);
            }
            this->cv_.notify_one();
        }

        void Resolver::workerLoop()
        {
            for (;;)
            {
                ResolveRequest* req;
                {
                    std::unique_lock lk(this->mtx_);
                    this->cv_.wait(lk, [this] { return this->stop_ || !this->queue_.empty(); });
                    if (this->stop_ && this->queue_.empty())
                        return;
                    req = this->queue_.front();
                    this->queue_.pop_front();
                }

                req->rc = ::getaddrinfo(req->host.empty() ? nullptr : req->host.c_str(),
                                        req->service.empty() ? nullptr : req->service.c_str(), &req->hints,
                                        &req->result);
                if (req->rc != 0)
                    req->result = nullptr;

#ifdef UVENT_ENABLE_REUSEADDR
                system::co_spawn_static(req->waiter, req->origin_tid);
#else
                std::coroutine_handle<> h = req->waiter;
                system::this_thread::detail::st->enqueue(h);
                system::this_thread::detail::pl.wake();
#endif
            }
        }
    } // namespace detail

    ResolveAwaiter::ResolveAwaiter(std::string host, std::string service, const addrinfo& hints)
    {
        this->req_.host = std::move(host);
        this->req_.service = std::move(service);
        this->req_.hints = hints;
    }

    bool ResolveAwaiter::await_ready()
    {
        addrinfo probe = this->req_.hints;
        probe.ai_flags |= AI_NUMERICHOST;
        int rc = ::getaddrinfo(this->req_.host.empty() ? nullptr : this->req_.host.c_str(),
                               this->req_.service.empty() ? nullptr : this->req_.service.c_str(), &probe,
                               &this->req_.result);
        if (rc == 0)
        {
            this->req_.rc = 0;
            return true;
        }
        this->req_.result = nullptr;
        return false;
    }

    void ResolveAwaiter::await_suspend(std::coroutine_handle<> h)
    {
        this->req_.waiter = h;
        this->req_.origin_tid = system::this_thread::detail::t_id;
        detail::Resolver::instance().submit(&this->req_);
    }

    std::expected<AddrInfoPtr, int> ResolveAwaiter::await_resume() noexcept
    {
        if (this->req_.rc != 0)
            return std::unexpected(this->req_.rc);
        return AddrInfoPtr(this->req_.result);
    }
} // namespace usub::uvent::net
