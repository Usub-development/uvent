//
// Created by root on 10/21/25.
//

#ifndef TLSREGISTRY_H
#define TLSREGISTRY_H

#include <uvent/pool/TLS.h>
#include <uvent/utils/datastructures/array/ConcurrentVector.h>

namespace usub::uvent::thread
{
    class TLSRegistry
    {
    public:
        friend class Uvent;

        explicit TLSRegistry(int threadCount);

        ThreadLocalStorage* getStorage(int index) const;
    private:
        array::concurrent::LockFreeVector<ThreadLocalStorage*> tls_storage_;
    };
}

#endif //TLSREGISTRY_H
