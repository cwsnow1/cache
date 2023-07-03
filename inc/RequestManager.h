#pragma once

#include "GlobalIncludes.h"
#include "Instruction.h"
#include "list.h"

struct Request {
    Instruction instruction;
    uint64_t cycle;
    uint64_t cycle_to_call_back;
    bool first_attempt;
};

class RequestManager {
   public:

    RequestManager() = default;

    /**
     * @brief Initializes the request manager and the request lists it maintains
     * 
     * @param pCacheLevel   The cache level of the cache whose requests this manager manages
     *
     */
    RequestManager (enum CacheLevel pCacheLevel);

    /**
     * @brief Destroy the Request Manager object
     * 
     */
    ~RequestManager();

    /**
     * @brief Add a request to the tail of the busy requests list
     * 
     * @param pElement List element of request to add
     * 
     */
    void AddRequestToBusyList(DoubleListElement *pElement);

    /**
     * @brief Remove given request from busy requests list
     * 
     * @param pElement List element of request to be removed
     */
    void RemoveRequestFromBusyList(DoubleListElement *pElement);

    /**
     * @brief Remove given request from waiting requests list
     * 
     * @param pElement List element of request to be removed
     */
    void RemoveRequestFromWaitingList(DoubleListElement *pElement);

    /**
     * @brief Add a request to the head of the free requests list
     * 
     * @param pElement List element of request to add
     * 
     */
    void PushRequestToFreeList(DoubleListElement *pElement);

    /**
     * @brief Add a request to the tail of the waiting requests list
     * 
     * @param pElement List element of request to add
     * 
     */
    void AddRequestToWaitingList(DoubleListElement *pElement);

    /**
     * @brief Pop request from head of free requests list
     * 
     * @return DoubleListElement* List element of request, nullptr if list is empty
     */
    DoubleListElement *PopRequestFromFreeList(); 

    /**
     * @brief Peek at the head element of the busy requests list
     * 
     * @return DoubleListElement* List element of the head of the busy requests list
     */
    DoubleListElement *PeekHeadOfBusyList() { return busyRequests->PeekHead(); }

    /**
     * @brief Get the Waiting Requests object, for use in for_each_in_double_list macro
     * 
     * @return DoubleList* Waiting requests list
     */
    DoubleList *GetWaitingRequests() { return waitingRequests; }

    /**
     * @brief Get the Busy Requests object, for use in for_each_in_double_list macro
     * 
     * @return DoubleList* Busy requests list
     */
    DoubleList *GetBusyRequests() { return busyRequests; }

    /**
     * @brief Get the request at a given pool index
     * 
     * @param pPoolIndex Pool index of request to get
     * @return Request* Pointer to request
     */
    Request *GetRequestAtIndex(uint64_t pPoolIndex) { return &requestPool[pPoolIndex]; }

    /**
     * @brief Get the pool index of a given request
     * 
     * @param pRequest Pointer to request
     * @return uint64_t Pool index of request
     */
    uint64_t GetPoolIndex(Request *pRequest);

    /**
     * @brief Set up request at given pool index
     * 
     * @param pPoolIndex            Pool index of request to set up
     * @param pAccess               Instruction structure for request
     * @param pCycle                Current cycle count
     * @param pAccessTimeInCycles   Access time for this instruction
     */
    void NewInstruction(uint64_t pPoolIndex, Instruction pAccess, uint64_t pCycle, uint64_t pAccessTimeInCycles);

    static constexpr uint64_t kMaxNumberOfRequests = 8;

   private:

    Request *requestPool;
    DoubleList *waitingRequests;
    DoubleList *freeRequests;
    DoubleList *busyRequests;
    uint64_t maxOutstandingRequests;
};

inline uint64_t RequestManager::GetPoolIndex(Request *pRequest) {
    return pRequest - requestPool;
}
