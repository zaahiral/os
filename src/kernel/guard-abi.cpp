#include "guard-abi.h"
// gcc requires these to be implemented.
// Equivalent to a spinlock.
namespace
{
    struct Guard {
        uint8_t initialised;
        uint32_t locked;

        void lock()
        {
            if (!__atomic_test_and_set(&locked, __ATOMIC_SEQ_CST)) 
            {
                return;
            }

            __builtin_trap();
        }

        void unlock()
        {
            __atomic_clear(&locked, __ATOMIC_SEQ_CST);
        }
    };

    static_assert(sizeof(Guard) == sizeof(uint64_t));

}

extern "C" int __cxa_guard_acquire(uint64_t* ptr) {
    auto guard = reinterpret_cast<Guard*>(ptr);
    guard->lock();
    
    if (__atomic_load_n(&guard->initialised, __ATOMIC_RELAXED))
    {
        guard->unlock();
        return 0;
    }
    else
    {
        return 1;
    }
}

extern "C" void __cxa_guard_release(uint64_t* ptr) {
    auto guard = reinterpret_cast<Guard*>(ptr);
    
    __atomic_store_n(&guard->initialised, 1, __ATOMIC_RELEASE);
    guard->unlock();
}

// Static objects register their destructors here. The kernel never exits, so they are never called.
extern "C" void* __dso_handle = nullptr;

extern "C" int __cxa_atexit(void (*)(void*), void*, void*)
{
    return 0;
}
