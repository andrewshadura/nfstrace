//------------------------------------------------------------------------------
// Author: Yauheni Azaranka
// Description: Wrapper for spinlock and lock guard based on RAII idiom.
// Copyright (c) 2013 EPAM Systems. All Rights Reserved.
//------------------------------------------------------------------------------
#ifndef SPINLOCK_H
#define SPINLOCK_H
//------------------------------------------------------------------------------
#include <pthread.h>
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
namespace NST
{
namespace utils
{

class Spinlock
{
public:
    Spinlock()
    {
        pthread_spin_init(&spinlock, PTHREAD_PROCESS_PRIVATE);
    }
    Spinlock(const Spinlock&)            = delete;
    Spinlock& operator=(const Spinlock&) = delete;
    ~Spinlock()
    {
        pthread_spin_destroy(&spinlock);
    }

    class Lock
    {
    public:
        Lock(const Spinlock& m) : locked(m)
        {
            pthread_spin_lock(&locked.spinlock);
        }
        Lock(const Lock&)            = delete;
        Lock& operator=(const Lock&) = delete;
        ~Lock()
        {
            pthread_spin_unlock(&locked.spinlock);
        }

    private:
        const Spinlock& locked;
    };

private:
    mutable pthread_spinlock_t spinlock;
};

} // utils
} // namespace NST
//------------------------------------------------------------------------------
#endif//SPINLOCK_H
//------------------------------------------------------------------------------
