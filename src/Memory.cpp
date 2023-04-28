#include <stdint.h>
#include <assert.h>
#include <string.h>

#include "Memory.h"
#include "list.h"
#include "debug.h"
#include "SimTracer.h"

#if (CONSOLE_PRINT == 1)
#define DEBUG_TRACE printf
#else
#define DEBUG_TRACE(...)
#endif

#if (SIM_TRACE == 1)
extern SimTracer *g_SimTracer;
#else
SimTracer dummySimTracer = SimTracer();
SimTracer *g_SimTracer = &dummySimTracer;
#endif

Memory::Memory(Memory *pLowestCache) {
    cacheLevel_ = kMainMemory;
    upperCache_ = pLowestCache;
    earliestNextUsefulCycle_ = UINT64_MAX;
}

void Memory::AllocateMemory() {
    requestManager_ = new RequestManager(cacheLevel_);
}

void Memory::FreeMemory() {
    delete requestManager_;
}

int16_t Memory::AddAccessRequest (Instruction access, uint64_t cycle) {
    DoubleListElement *element = requestManager_->freeRequests->PopElement();
    if (element) {
        CODE_FOR_ASSERT(bool ret =) requestManager_->waitingRequests->AddElementToTail(element);
        assert(ret);
        uint64_t pool_index = element->pool_index_;
        requestManager_->requestPool[pool_index].instruction = access;
        requestManager_->requestPool[pool_index].cycle = cycle;
        requestManager_->requestPool[pool_index].cycle_to_call_back = cycle + kAccessTimeInCycles[cacheLevel_];
        requestManager_->requestPool[pool_index].first_attempt = true;
        DEBUG_TRACE("Cache[%hhu] New request added at index %lu, call back at tick %lu\n", cacheLevel_, pool_index, requestManager_->requestPool[pool_index].cycle_to_call_back);
        g_SimTracer->Print(SIM_TRACE__REQUEST_ADDED, this, pool_index, access.rw == READ ? 'r' : 'w', (access.ptr >> 32), access.ptr & UINT32_MAX, kAccessTimeInCycles[cacheLevel_]);

        return (int16_t) pool_index;
    }
    return -1;
}

uint64_t Memory::InternalProcessCache (uint64_t cycle, int16_t *completed_requests) {
    uint64_t num_requests_completed = 0;
    Cache *upperCache = static_cast<Cache*>(upperCache_);
    wasWorkDoneThisCycle_ = false;
    cycle_ = cycle;
    CODE_FOR_ASSERT(bool ret);
    for_each_in_double_list(requestManager_->waitingRequests) {
        DEBUG_TRACE("Cache[%hhu] trying request %lu from waiting list, address=0x%012lx\n", cacheLevel_, pool_index, requestManager_->requestPool[pool_index].instruction.ptr);
        if (handleAccess(&requestManager_->requestPool[pool_index]) == kWaiting) { 
            DEBUG_TRACE("Cache[%hhu] request %lu is still waiting, breaking out of loop\n", cacheLevel_, pool_index);
            break;
        }
        DEBUG_TRACE("Cache[%hhu] hit, set=%lu\n", cacheLevel_, upperCache->addressToSetIndex(requestManager_->requestPool[pool_index].instruction.ptr));

        uint64_t setIndex = upperCache->addressToSetIndex(requestManager_->requestPool[pool_index].instruction.ptr);
        DEBUG_TRACE("Cache[%hhu] marking set %lu as no longer busy\n", (uint8_t)(cacheLevel_ - 1), setIndex);
        upperCache->ResetCacheSetBusy(setIndex);
    
        CODE_FOR_ASSERT(ret =) requestManager_->waitingRequests->RemoveElement(element_i);
        assert(ret);
        CODE_FOR_ASSERT(ret =) requestManager_->freeRequests->PushElement(element_i);
        assert(ret);
    }
    DEBUG_TRACE("\n");
    num_requests_completed = upperCache->InternalProcessCache(cycle, completed_requests);
    upperCache_->wasWorkDoneThisCycle_ = wasWorkDoneThisCycle_;

    return num_requests_completed;
}

Status Memory::handleAccess (Request *request) {
    if (cycle_ < request->cycle_to_call_back) {
        DEBUG_TRACE("%lu/%lu cycles for this operation in cacheLevel=%hhu\n", cycle_ - request->cycle, kAccessTimeInCycles[cacheLevel_], cacheLevel_);
        if (earliestNextUsefulCycle_ > request->cycle_to_call_back) {
            DEBUG_TRACE("Cache[%hhu] next useful cycle set to %lu\n", cacheLevel_, request->cycle_to_call_back);
            earliestNextUsefulCycle_ = request->cycle_to_call_back;
        }
        return kWaiting;
    }
    earliestNextUsefulCycle_ = UINT64_MAX;
    // Main memory always hits
    wasWorkDoneThisCycle_ = true;
    return kHit;
}

uint64_t Memory::CalculateEarliestNextUsefulCycle() {
    uint64_t earliest_next_useful_cycle = UINT64_MAX;
    for (Memory *cacheIterator = this; cacheIterator != nullptr; cacheIterator = cacheIterator->GetLowerCache()) {
        if (cacheIterator->GetEarliestNextUsefulCycle() < earliest_next_useful_cycle) {
            earliest_next_useful_cycle = cacheIterator->GetEarliestNextUsefulCycle();
        }
    }
    return earliest_next_useful_cycle;
}
