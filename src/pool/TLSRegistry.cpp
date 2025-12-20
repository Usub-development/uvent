//
// Created by root on 10/21/25.
//

#include <uvent/pool/TLSRegistry.h>

namespace usub::uvent::thread
{
    TLSRegistry::TLSRegistry(int threadCount)
    {
        this->tls_storage_.reserve(threadCount);
        for (int i = 0; i < threadCount; ++i)
            this->tls_storage_.emplace_back(new ThreadLocalStorage{});
    }

    ThreadLocalStorage* TLSRegistry::getStorage(int index) const
    {
        return this->tls_storage_[index];
    }
}