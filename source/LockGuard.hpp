#ifndef __LOCK_GUARD_HPP__
#define __LOCK_GUARD_HPP__
#include <pthread.h>
class Mutex
{
public:
    Mutex()
    {
        pthread_mutex_init(&_lock, nullptr);
    }
    void Lock()
    {
        pthread_mutex_lock(&_lock);
    }
    void Unlock()
    {
        pthread_mutex_unlock(&_lock);
    }
    pthread_mutex_t *Get()
    {
        return &_lock;
    }
    ~Mutex()
    {
        pthread_mutex_destroy(&_lock);
    }

private:
    pthread_mutex_t _lock;
};

class LockGuard
{
public:
    LockGuard(Mutex *mutex) : _mutex(mutex)
    {
        _mutex->Lock();
    }
    ~LockGuard()
    {
        _mutex->Unlock();
    }

private:
    Mutex *_mutex;
};
#endif