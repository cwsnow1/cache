#include <stdint.h>
#include <assert.h>

#include "RequestManager.h"
#include "list.h"
#include "Cache.h"
#include "debug.h"

RequestManager::RequestManager(CacheLevel pCacheLevel) {
    maxOutstandingRequests_ = RequestManager::kMaxNumberOfRequests << pCacheLevel;
    pRequestPool_ = new Request[maxOutstandingRequests_];
    pWaitingRequests_ = new DoubleList(maxOutstandingRequests_);
    pBusyRequests_ = new DoubleList(maxOutstandingRequests_);
    pFreeRequests_ = new DoubleList(maxOutstandingRequests_);
    for (uint64_t i = 0; i < maxOutstandingRequests_; i++) {
        DoubleListElement *element = new DoubleListElement;
        element->poolIndex_ = i;
        pFreeRequests_->PushElement(element);
    }
}

RequestManager::~RequestManager() {
    delete[] pRequestPool_;
    delete pWaitingRequests_;
    delete pFreeRequests_;
    delete pBusyRequests_;
}

void RequestManager::AddRequestToBusyList(DoubleListElement *pElement) {
    pBusyRequests_->AddElementToTail(pElement);
}

void RequestManager::RemoveRequestFromBusyList(DoubleListElement *pElement) {
    CODE_FOR_ASSERT(bool ret = ) pBusyRequests_->RemoveElement(pElement);
    assert(ret);
}

void RequestManager::RemoveRequestFromWaitingList(DoubleListElement *pElement) {
    CODE_FOR_ASSERT(bool ret = ) pWaitingRequests_->RemoveElement(pElement);
    assert(ret);
}

void RequestManager::AddRequestToWaitingList(DoubleListElement *pElement) {
    CODE_FOR_ASSERT(bool ret = ) pWaitingRequests_->AddElementToTail(pElement);
    assert(ret);
}

void RequestManager::PushRequestToFreeList(DoubleListElement *pElement) {
    CODE_FOR_ASSERT(bool ret = ) pFreeRequests_->PushElement(pElement);
    assert(ret);
}

DoubleListElement *RequestManager::PopRequestFromFreeList() {
    return pFreeRequests_->PopElement();
}

void RequestManager::NewInstruction(uint64_t poolIndex, Instruction access, uint64_t cycle, uint64_t accessTimeInCycles) {
    Request *pRequest = &pRequestPool_[poolIndex];
    pRequest->instruction = access;
    pRequest->cycle = cycle;
    pRequest->cycleToCallBack = cycle + accessTimeInCycles;
    pRequest->IsFirstAttempt = true;
}
