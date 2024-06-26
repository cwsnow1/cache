#pragma once
#include <stdint.h>
#include <vector>

#ifdef __GNUC__
#include <pthread.h>
#include <unistd.h>

typedef pthread_t Thread_t;
typedef pthread_mutex_t Lock_t;
#define THREAD_FUNCTION_TYPE(function) void*(*function)(void*)
#endif

#ifdef _MSC_VER
#include <Windows.h>

typedef HANDLE Thread_t;
typedef RTL_CRITICAL_SECTION Lock_t;
#define THREAD_FUNCTION_TYPE(function)  LPTHREAD_START_ROUTINE function
#endif

namespace Multithreading {
    void InitializeLock(Lock_t *pLock);
    void Lock(Lock_t *pLock);
    void Unlock(Lock_t *pLock);
    void StartThread(THREAD_FUNCTION_TYPE(threadFunction), void *pThreadData, Thread_t *pThreadOut);
    void WaitForThreads(std::vector<Thread_t> threads);
}

