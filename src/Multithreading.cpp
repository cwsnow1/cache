#ifdef __GNUC__
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#endif

#ifdef _MSC_VER
#include <Windows.h>
#endif

#include "Multithreading.h"

#ifdef __GNUC__

void Multithreading::Lock(Lock_t *pLock) {
    pthread_mutex_lock(pLock);
}

void Multithreading::Unlock(Lock_t *pLock) {
    pthread_mutex_unlock(pLock);
}

void Multithreading::InitializeLock(Lock_t *pLock) {
    if (pthread_mutex_init(pLock, NULL) != 0) {
        fprintf(stderr, "Mutex lock init failed\n");
        exit(1);
    }
}

void Multithreading::StartThread(THREAD_FUNCTION_TYPE(threadFunction), void *pThreadData, Thread_t *pThreadOut) {
    if (pthread_create(pThreadOut, NULL, threadFunction, pThreadData)) {
        fprintf(stderr, "Error in creating thread\n");
    }
}

void Multithreading::WaitForThreads(uint64_t numberOfThreads, Thread_t *pThreadArray) {
    for (uint64_t i = 0; i < numberOfThreads; i++) {
        pthread_join(pThreadArray[i], NULL);
    }
}


#elif defined(_MSC_VER)

void Multithreading::Lock(Lock_t *pLock) {
    EnterCriticalSection(pLock);
}

void Multithreading::Unlock(Lock_t *pLock) {
    LeaveCriticalSection(pLock);
}

void Multithreading::InitializeLock(Lock_t *pLock) {
    InitializeCriticalSection(pLock);
}

void Multithreading::StartThread(THREAD_FUNCTION_TYPE(threadFunction), void *pThreadData, Thread_t *pThreadOut) {
    DWORD threadIdentifier; // not used
    *pThreadOut = CreateThread(
        NULL,
        0,
        threadFunction,
        pThreadData,
        0,
        &threadIdentifier
    );
}

void Multithreading::WaitForThreads(uint64_t numberOfThreads, Thread_t *pThreadArray) {
    // Windows max number of threads is 64, only wait for the last 64 threads if number of configs exceeds this
    if (numberOfThreads > MAXIMUM_WAIT_OBJECTS) {
        pThreadArray = &pThreadArray[numberOfThreads - MAXIMUM_WAIT_OBJECTS];
        numberOfThreads = MAXIMUM_WAIT_OBJECTS;
    }
    WaitForMultipleObjects(static_cast<DWORD>(numberOfThreads), static_cast<const HANDLE*>(pThreadArray), TRUE, INFINITE);
}

#else
#error "Unsupported OS"
#endif
