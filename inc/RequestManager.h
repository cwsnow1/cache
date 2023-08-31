#pragma once

#include "GlobalIncludes.h"
#include "Instruction.h"
#include "list.h"

struct Request {
    Instruction instruction;
    uint64_t cycle;
    uint64_t cycleToCallBack;
    uint64_t attemptCount;
};

class RequestManager {
   public:

    RequestManager() = default;

    /**
     * @brief               Initializes the request manager and the request lists it maintains
     * 
     * @param cacheLevel    The cache level of the cache whose requests this manager manages
     *
     */
    RequestManager (CacheLevel cacheLevel);

    /**
     * @brief Destroy the Request Manager object
     * 
     */
    ~RequestManager();

    /**
     * @brief           Add a request to the tail of the busy requests list
     * 
     * @param pElement  List element of request to add
     * 
     */
    void AddRequestToBusyList(DoubleListElement *pElement);

    /**
     * @brief           Remove given request from busy requests list
     * 
     * @param pElement  List element of request to be removed
     */
    void RemoveRequestFromBusyList(DoubleListElement *pElement);

    /**
     * @brief           Remove given request from waiting requests list
     * 
     * @param pElement  List element of request to be removed
     */
    void RemoveRequestFromWaitingList(DoubleListElement *pElement);

    /**
     * @brief           Add a request to the head of the free requests list
     * 
     * @param pElement  List element of request to add
     * 
     */
    void PushRequestToFreeList(DoubleListElement *pElement);

    /**
     * @brief           Add a request to the tail of the waiting requests list
     * 
     * @param pElement  List element of request to add
     * 
     */
    void AddRequestToWaitingList(DoubleListElement *pElement);

    /**
     * @brief   Pop request from head of free requests list
     * 
     * @return  List element of request, nullptr if list is empty
     */
    DoubleListElement *PopRequestFromFreeList(); 

    /**
     * @brief   Peek at the head element of the busy requests list
     * 
     * @return  List element of the head of the busy requests list
     */
    DoubleListElement *PeekHeadOfBusyList() { return pBusyRequests_->PeekHead(); }

    /**
     * @brief   Get the Waiting Requests object, for use in for_each_in_double_list macro
     * 
     * @return  Waiting requests list
     */
    DoubleList *GetWaitingRequests() { return pWaitingRequests_; }

    /**
     * @brief   Get the Busy Requests object, for use in for_each_in_double_list macro
     * 
     * @return  Busy requests list
     */
    DoubleList *GetBusyRequests() { return pBusyRequests_; }

    /**
     * @brief               Get the request at a given pool index
     * 
     * @param poolIndex     Pool index of request to get
     * @return              Pointer to request
     */
    Request *GetRequestAtIndex(uint64_t poolIndex) { return &pRequestPool_[poolIndex]; }

    /**
     * @brief           Get the pool index of a given request
     * 
     * @param pRequest  Pointer to request
     * @return          Pool index of request
     */
    uint64_t GetPoolIndex(Request *pRequest);

    /**
     * @brief Set up request at given pool index
     * 
     * @param poolIndex             Pool index of request to set up
     * @param access                Instruction structure for request
     * @param cycle                 Current cycle count
     * @param accessTimeInCycles    Access time for this instruction
     */
    void NewInstruction(uint64_t poolIndex, Instruction access, uint64_t cycle, uint64_t accessTimeInCycles);

    static constexpr uint64_t kMaxNumberOfRequests = 8;

    static constexpr int16_t kInvalidRequestIndex = -1;

   private:

    Request *pRequestPool_;
    DoubleList *pWaitingRequests_;
    DoubleList *pFreeRequests_;
    DoubleList *pBusyRequests_;
    uint64_t maxOutstandingRequests_;
};

inline uint64_t RequestManager::GetPoolIndex(Request *pRequest) {
    return pRequest - pRequestPool_;
}
