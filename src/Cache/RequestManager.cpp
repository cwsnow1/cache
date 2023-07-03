#include <stdint.h>
#include <assert.h>

#include "RequestManager.h"
#include "list.h"
#include "Cache.h"
#include "debug.h"

RequestManager::RequestManager(CacheLevel pCacheLevel) {
    maxOutstandingRequests = RequestManager::kMaxNumberOfRequests << pCacheLevel;
    requestPool = new Request[maxOutstandingRequests];
    waitingRequests = new DoubleList(maxOutstandingRequests);
    busyRequests = new DoubleList(maxOutstandingRequests);
    freeRequests = new DoubleList(maxOutstandingRequests);
    for (uint64_t i = 0; i < maxOutstandingRequests; i++) {
        DoubleListElement *element = new DoubleListElement;
        element->pool_index_ = i;
        freeRequests->PushElement(element);
    }
}

RequestManager::~RequestManager() {
    delete[] requestPool;
    delete waitingRequests;
    delete freeRequests;
    delete busyRequests;
}

void RequestManager::AddRequestToBusyList(DoubleListElement *pElement) {
    busyRequests->AddElementToTail(pElement);
}

void RequestManager::RemoveRequestFromBusyList(DoubleListElement *pElement) {
    CODE_FOR_ASSERT(bool ret = ) busyRequests->RemoveElement(pElement);
    assert(ret);
}

void RequestManager::RemoveRequestFromWaitingList(DoubleListElement *pElement) {
    CODE_FOR_ASSERT(bool ret = ) waitingRequests->RemoveElement(pElement);
    assert(ret);
}

void RequestManager::AddRequestToWaitingList(DoubleListElement *pElement) {
    CODE_FOR_ASSERT(bool ret = ) waitingRequests->AddElementToTail(pElement);
    assert(ret);
}

void RequestManager::PushRequestToFreeList(DoubleListElement *pElement) {
    CODE_FOR_ASSERT(bool ret = ) freeRequests->PushElement(pElement);
    assert(ret);
}

DoubleListElement *RequestManager::PopRequestFromFreeList() {
    return freeRequests->PopElement();
}

void RequestManager::NewInstruction(uint64_t pPoolIndex, Instruction pAccess, uint64_t pCycle, uint64_t pAccessTimeInCycles) {
    Request *request = &requestPool[pPoolIndex];
    request->instruction = pAccess;
    request->cycle = pCycle;
    request->cycle_to_call_back = pCycle + pAccessTimeInCycles;
    request->first_attempt = true;
}
