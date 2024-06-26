#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <string.h>

#include "Memory.h"
#include "SimTracer.h"
#include "debug.h"
#include "list.h"

#if (CONSOLE_PRINT == 1)
#define DEBUG_TRACE printf
#else
#define DEBUG_TRACE(...)
#endif

#if (SIM_TRACE == 1)
extern SimTracer* gSimTracer;
#else
SimTracer dummySimTracer = SimTracer();
SimTracer* gSimTracer = &dummySimTracer;
#endif

Memory::Memory(Memory* pLowestCache, CacheLevel cacheLevel) : pUpperCache_(pLowestCache), pMainMemory_(this), cacheLevel_(cacheLevel) {
    earliestNextUsefulCycle_ = UINT64_MAX;
    pLowerCache_ = nullptr;
}

void Memory::AllocateMemory() {
    pRequestManager_ = std::make_unique<RequestManager>(cacheLevel_);
}

void Memory::FreeMemory() {
    pRequestManager_.reset(nullptr);
}

int16_t Memory::AddAccessRequest(Instruction access, uint64_t cycle) {
    DoubleListElement* element = pRequestManager_->PopRequestFromFreeList();
    if (element) {
        pRequestManager_->AddRequestToWaitingList(element);
        uint64_t poolIndex = element->poolIndex_;
        pRequestManager_->NewInstruction(poolIndex, access, cycle, kAccessTimeInCycles[cacheLevel_]);
        DEBUG_TRACE("Cache[%hhu] New request type %d added at index %" PRIu64 ", call back at tick %" PRIu64 "\n",
                    cacheLevel_, access.rw, poolIndex, pRequestManager_->GetRequestAtIndex(poolIndex).cycleToCallBack);
        gSimTracer->Print(SIM_TRACE__REQUEST_ADDED, this, poolIndex, access.rw, (access.ptr >> 32),
                          access.ptr & UINT32_MAX, kAccessTimeInCycles[cacheLevel_]);

        return static_cast<int16_t>(poolIndex);
    }
    return RequestManager::kInvalidRequestIndex;
}

void Memory::InternalProcessCache(uint64_t cycle, std::vector<int16_t>& completedRequests) {
    Cache* const upperCache = static_cast<Cache*>(pUpperCache_);
    wasWorkDoneThisCycle_ = false;
    cycle_ = cycle;
    for_each_in_double_list(pRequestManager_->GetWaitingRequests()) {
        DEBUG_TRACE("Cache[%hhu] trying request %" PRIu64 " from waiting list, address=0x%012" PRIx64 "\n", cacheLevel_,
                    poolIndex, pRequestManager_->GetRequestAtIndex(poolIndex).instruction.ptr);
        if (handleAccess(pRequestManager_->GetRequestAtIndex(poolIndex)) == kWaiting) {
            DEBUG_TRACE("Cache[%hhu] request %" PRIu64 " is still waiting, breaking out of loop\n", cacheLevel_,
                        poolIndex);
            break;
        }
        DEBUG_TRACE("Cache[%hhu] hit\n", cacheLevel_);

        uint64_t setIndex =
            upperCache->addressToSetIndex(pRequestManager_->GetRequestAtIndex(poolIndex).instruction.ptr);
        DEBUG_TRACE("Cache[%hhu] marking set %" PRIu64 " as no longer busy\n", static_cast<uint8_t>(cacheLevel_ - 1),
                    setIndex);
        upperCache->ResetCacheSetBusy(setIndex);

        pRequestManager_->RemoveRequestFromWaitingList(elementIterator);
        pRequestManager_->PushRequestToFreeList(elementIterator);
    }
    DEBUG_TRACE("\n");
    upperCache->InternalProcessCache(cycle, completedRequests);
    pUpperCache_->wasWorkDoneThisCycle_ = wasWorkDoneThisCycle_;

}

Status Memory::handleAccess(Request& request) {
    if (cycle_ < request.cycleToCallBack) {
        DEBUG_TRACE("%" PRIu64 "/%" PRIu64 " cycles for this operation in cacheLevel=%hhu\n", cycle_ - request.cycle,
                    kAccessTimeInCycles[cacheLevel_], cacheLevel_);
        if (earliestNextUsefulCycle_ > request.cycleToCallBack) {
            DEBUG_TRACE("Cache[%hhu] next useful cycle set to %" PRIu64 "\n", cacheLevel_, request.cycleToCallBack);
            earliestNextUsefulCycle_ = request.cycleToCallBack;
        }
        return kWaiting;
    }
    earliestNextUsefulCycle_ = UINT64_MAX;
    // Main memory always hits
    wasWorkDoneThisCycle_ = true;
    return kHit;
}

uint64_t Memory::CalculateEarliestNextUsefulCycle() const {
    uint64_t earliestNextUsefulCycle = UINT64_MAX;
    for (auto cacheIterator = this; cacheIterator != nullptr; cacheIterator = &cacheIterator->GetLowerCache()) {
        if (cacheIterator->GetEarliestNextUsefulCycle() < earliestNextUsefulCycle) {
            earliestNextUsefulCycle = cacheIterator->GetEarliestNextUsefulCycle();
        }
    }
    return earliestNextUsefulCycle;
}
