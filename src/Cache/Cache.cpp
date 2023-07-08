#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>

#include "list.h"
#include "Cache.h"
#include "SimTracer.h"
#include "debug.h"
#include "Memory.h"

#if (SIM_TRACE == 1)
extern SimTracer *gSimTracer;
#else
extern SimTracer dummySimTracer;
extern SimTracer *gSimTracer;
#endif

#if (CONSOLE_PRINT == 1)
#define DEBUG_TRACE printf
#else
#define DEBUG_TRACE(...)
#endif

// =====================================
//          Public Functions
// =====================================

Cache::Cache (Cache *pUpperCache, CacheLevel cacheLevel, uint8_t numCacheLevels, Configuration *pCacheConfigs, uint64_t configIndex) {
    Configuration cacheConfig = pCacheConfigs[cacheLevel];
    pUpperCache_ = pUpperCache;
    cacheLevel_ = cacheLevel;
    config_.cacheSize = cacheConfig.cacheSize;
    config_.blockSize = cacheConfig.blockSize;
    blockSizeBits_ = 0;
    configIndex_ = configIndex;
    uint64_t tmp = config_.blockSize;
    for (; (tmp & 1) == 0; tmp >>= 1) {
        blockSizeBits_++;
    }
    assert_release((tmp == 1) && "Block size must be a power of 2!");
    assert_release((config_.cacheSize % config_.blockSize == 0) && "Block size must be a factor of cache size!");
    uint64_t numBlocks = config_.cacheSize / config_.blockSize;
    if (numBlocks < cacheConfig.associativity) {
        assert(false);
    }
    config_.associativity = cacheConfig.associativity;
    assert_release(numBlocks % config_.associativity == 0 && "Number of blocks must divide evenly with associativity");
    numSets_ = numBlocks / config_.associativity;
    tmp = numSets_;
    for (; (tmp & 1) == 0; tmp >>= 1)
        ;
    assert_release(tmp == 1 && "Number of sets must be a power of 2");
    blockAddressToSetIndexMask_ = numSets_ - 1;
    earliestNextUsefulCycle_ = UINT64_MAX;
    if (cacheLevel < numCacheLevels - 1) {
        pLowerCache_ = new Cache(this, static_cast<CacheLevel>(cacheLevel + 1), numCacheLevels, pCacheConfigs, configIndex);
        pLowerCache_->SetUpperCache(static_cast<Memory*>(this));
    } else {
        pLowerCache_ = new Memory(this);
    }
    Memory *pMainMemory = this;
    for (; pMainMemory->GetCacheLevel() != kMainMemory; pMainMemory = pMainMemory->GetLowerCache())
        ;
    pMainMemory_ = pMainMemory;
}

Cache::~Cache () {
    if (pLowerCache_) {
        delete pLowerCache_;
    }
}

void Cache::AllocateMemory () {
    sets_ = new Set[numSets_];
    for (uint64_t i = 0; i < numSets_; i++) {
        sets_[i].ways = new Block[config_.associativity];
        sets_[i].lruList = new uint8_t[config_.associativity];
        for (uint8_t j = 0; j < config_.associativity; j++) {
            sets_[i].lruList[j] = j;
        }
    }
    pRequestManager_ = new RequestManager(cacheLevel_);
    if (pLowerCache_->GetCacheLevel() != kMainMemory) {
        static_cast<Cache*>(pLowerCache_)->AllocateMemory();
    } else {
        pLowerCache_->AllocateMemory();
    }
}

void Cache::SetThreadId (uint64_t threadId) {
    threadId_ = threadId;
    if (cacheLevel_ != kMainMemory) {
        static_cast<Cache*>(pLowerCache_)->SetThreadId(threadId);
    }
}

bool Cache::IsCacheConfigValid (Configuration config) {
    assert_release((config.cacheSize % config.blockSize == 0) && "Block size must be a factor of cache size!");
    uint64_t numBlocks = config.cacheSize / config.blockSize;
    return numBlocks >= config.associativity;
}

void Cache::FreeMemory () {
    assert(sets_);
    for (uint64_t i = 0; i < numSets_; i++) {
        assert(sets_[i].ways);
        delete[] sets_[i].ways;
        delete[] sets_[i].lruList;
    }
    delete[] sets_;
    delete pRequestManager_;
    if (pLowerCache_->GetCacheLevel() != kMainMemory) {
        static_cast<Cache*>(pLowerCache_)->FreeMemory();
    } else {
        pLowerCache_->FreeMemory();
    }
}

uint64_t Cache::ProcessCache (uint64_t cycle, int16_t *pCompletedRequests) {
    return pMainMemory_->InternalProcessCache(cycle, pCompletedRequests);
}

// =====================================
//          Private Functions
// =====================================

void Cache::updateLRUList (uint64_t setIndex, uint8_t mruIndex) {
    if (config_.associativity == 1) {
        return;
    }
    uint8_t *lruList = sets_[setIndex].lruList;
    uint8_t previousValue = mruIndex;
    // find MRU index in the lruList
    for (uint8_t i = 0; i < config_.associativity; i++) {
        uint8_t tmp = lruList[i];
        lruList[i] = previousValue;
        if (tmp == mruIndex) {
            break;
        }
        previousValue = tmp;
    }
    gSimTracer->Print(SIM_TRACE__LRU_UPDATE, static_cast<Memory*>(this), static_cast<uint32_t>(setIndex), lruList[0], lruList[config_.associativity - 1]);
}

int16_t Cache::evictBlock (uint64_t setIndex) {
    int16_t lruBlockIndex = sets_[setIndex].lruList[config_.associativity - 1];
    if (!sets_[setIndex].ways[lruBlockIndex].valid) {
        DEBUG_TRACE("Cache[%hhu] not evicting invalid block from set %" PRIu64 "\n", cacheLevel_, setIndex);
        return lruBlockIndex;
    }
    uint64_t oldBlockAddress = sets_[setIndex].ways[lruBlockIndex].blockAddress;
    Instruction lowerCacheAccess = Instruction(oldBlockAddress << blockSizeBits_, READ);
    if (sets_[setIndex].ways[lruBlockIndex].dirty) {
        lowerCacheAccess.rw = WRITE;
        ++stats_.writebacks;
        sets_[setIndex].ways[lruBlockIndex].dirty = false;
    }
    if (pLowerCache_->AddAccessRequest(lowerCacheAccess, cycle_) == -1) {
        gSimTracer->Print(SIM_TRACE__EVICT_FAILED, static_cast<Memory*>(this));
        DEBUG_TRACE("Cache[%hhu] could not make request to lower cache in evictBlock, returning\n", cacheLevel_);
        return -1;
    }
    sets_[setIndex].ways[lruBlockIndex].valid = false;
    return lruBlockIndex;
}

bool Cache::findBlockInSet (uint64_t setIndex, uint64_t blockAddress, uint8_t& pBlockIndex) {
    for (uint64_t i = 0; i < config_.associativity; i++) {
        if (sets_[setIndex].ways[i].valid && (sets_[setIndex].ways[i].blockAddress == blockAddress)) {
            pBlockIndex = i;
            updateLRUList(setIndex, i);
            return true;
        }
    }
    return false;
}

int16_t Cache::requestBlock (uint64_t setIndex, uint64_t blockAddress) {
    int16_t blockIndex = evictBlock(setIndex);
    if (blockIndex == -1) {
        return -1;
    }
    gSimTracer->Print(SIM_TRACE__EVICT, static_cast<Memory*>(this), setIndex, blockIndex);
    Instruction readRequestToLowerCache = Instruction(blockAddress << blockSizeBits_, READ);
    if (pLowerCache_->AddAccessRequest(readRequestToLowerCache, cycle_) == -1) {
        gSimTracer->Print(SIM_TRACE__REQUEST_FAILED, static_cast<Memory*>(this), NULL);
        DEBUG_TRACE("Cache[%hhu] could not make request to lower cache in requestBlock, returning\n", cacheLevel_);
        return -1;
    }
    sets_[setIndex].ways[blockIndex].blockAddress = blockAddress;
    sets_[setIndex].ways[blockIndex].valid = true;
    assert(sets_[setIndex].ways[blockIndex].dirty == false);
    return blockIndex;
}

Status Cache::handleAccess (Request *request) {
    if (cycle_ < request->cycleToCallBack) {
        DEBUG_TRACE("%" PRIu64 "/%" PRIu64 " cycles for this operation in cacheLevel=%hhu\n", cycle_ - request->cycle, kAccessTimeInCycles[cacheLevel_], cacheLevel_);
        if (earliestNextUsefulCycle_ > request->cycleToCallBack) {
            DEBUG_TRACE("Cache[%hhu] next useful cycle set to %" PRIu64 "\n", cacheLevel_, request->cycleToCallBack);
            earliestNextUsefulCycle_ = request->cycleToCallBack;
        }
        return kWaiting;
    }
    earliestNextUsefulCycle_ = UINT64_MAX;

    Instruction access = request->instruction;
    uint64_t blockAddress = addressToBlockAddress(access.ptr);
    uint64_t setIndex = addressToSetIndex(access.ptr);
    if (sets_[setIndex].busy) {
        DEBUG_TRACE("Cache[%hhu] set %" PRIu64 " is busy\n", cacheLevel_, setIndex);
        return kBusy;
    }
    wasWorkDoneThisCycle_ = true;
    uint8_t blockIndex;
    bool hit = findBlockInSet(setIndex, blockAddress, blockIndex);
    if (hit) {
        gSimTracer->Print(SIM_TRACE__HIT, static_cast<Memory*>(this),
            pRequestManager_->GetPoolIndex(request), blockAddress>>32, blockAddress & UINT32_MAX, setIndex);
        if (access.rw == READ) {
            if (request->IsFirstAttempt) {
                ++stats_.readHits;
            }
        } else {
            if (request->IsFirstAttempt) {
                ++stats_.writeHits;
            }
            sets_[setIndex].ways[blockIndex].dirty = true;
        }
    } else {
        gSimTracer->Print(SIM_TRACE__MISS, static_cast<Memory*>(this), pRequestManager_->GetPoolIndex(request), setIndex);
        request->IsFirstAttempt = false;
        int16_t requestedBlock = requestBlock(setIndex, blockAddress);
        if (requestedBlock < 0) {
            return kMiss;
        }
        // Cast is OK after check above
        blockIndex = static_cast<uint8_t>(requestedBlock);
        sets_[setIndex].busy = true;
        DEBUG_TRACE("Cache[%hhu] set %" PRIu64 " marked as busy due to miss\n", cacheLevel_, setIndex);
        if (access.rw == READ) {
            ++stats_.readMisses;
        } else {
            ++stats_.writeMisses;
            sets_[setIndex].ways[blockIndex].dirty = true;
        }
    }
    return hit ? kHit : kMiss;
}

void Cache::ResetCacheSetBusy(uint64_t setIndex) {
    sets_[setIndex].busy = false;
}

uint64_t Cache::InternalProcessCache (uint64_t cycle, int16_t *pCompletedRequests) {
    uint64_t numberOfRequestsCompleted = 0;
    wasWorkDoneThisCycle_ = false;
    cycle_ = cycle;
    if (pLowerCache_->GetWasWorkDoneThisCycle()) {
        for_each_in_double_list(pRequestManager_->GetBusyRequests()) {
            DEBUG_TRACE("Cache[%hhu] trying request %" PRIu64 " from busy requests list, address=0x%012" PRIx64 "\n", cacheLevel_, poolIndex, pRequestManager_->GetRequestAtIndex(poolIndex)->instruction.ptr);
            Status status = handleAccess(pRequestManager_->GetRequestAtIndex(poolIndex));
            if (status == kHit) {
                DEBUG_TRACE("Cache[%hhu] hit, set=%" PRIu64 "\n", cacheLevel_, addressToSetIndex(pRequestManager_->GetRequestAtIndex(poolIndex)->instruction.ptr));
                if (pUpperCache_) {
                    Cache *upperCache = static_cast<Cache*>(pUpperCache_);
                    uint64_t setIndex = upperCache->addressToSetIndex(pRequestManager_->GetRequestAtIndex(poolIndex)->instruction.ptr);
                    DEBUG_TRACE("Cache[%hhu] marking set %" PRIu64 " as no longer busy\n", static_cast<uint8_t> (cacheLevel_ - 1), setIndex);
                    static_cast<Cache*>(pUpperCache_)->ResetCacheSetBusy(setIndex);
                }
                if (cacheLevel_ == kL1) {
                    pCompletedRequests[numberOfRequestsCompleted++] = poolIndex;
                }
                pRequestManager_->RemoveRequestFromBusyList(elementIterator);
                pRequestManager_->PushRequestToFreeList(elementIterator);
            }
        }
    } else {
        if (pLowerCache_ && pRequestManager_->PeekHeadOfBusyList()) {
            DEBUG_TRACE("Cache[%hhu] no work was done in lower cache, not checking busy list\n", cacheLevel_);
        }
    }
    for_each_in_double_list(pRequestManager_->GetWaitingRequests()) {
        DEBUG_TRACE("Cache[%hhu] trying request %" PRIu64 " from waiting list, address=0x%012" PRIx64 "\n", cacheLevel_, poolIndex, pRequestManager_->GetRequestAtIndex(poolIndex)->instruction.ptr);
        Status status = handleAccess(pRequestManager_->GetRequestAtIndex(poolIndex));
        switch (status) {
        case kHit:
            DEBUG_TRACE("Cache[%hhu] hit, set=%" PRIu64 "\n", cacheLevel_, addressToSetIndex(pRequestManager_->GetRequestAtIndex(poolIndex)->instruction.ptr));
            if (pUpperCache_) {
                Cache *upperCache = static_cast<Cache*>(pUpperCache_);
                uint64_t setIndex = upperCache->addressToSetIndex(pRequestManager_->GetRequestAtIndex(poolIndex)->instruction.ptr);
                DEBUG_TRACE("Cache[%hhu] marking set %" PRIu64 " as no longer busy\n", static_cast<uint8_t> (cacheLevel_ - 1), setIndex);
                upperCache->sets_[setIndex].busy = false;
            }
            if (cacheLevel_ == kL1) {
                pCompletedRequests[numberOfRequestsCompleted++] = poolIndex;
            }
            pRequestManager_->RemoveRequestFromWaitingList(elementIterator);
            pRequestManager_->PushRequestToFreeList(elementIterator);
            break;
        case kMiss:
        case kBusy:
            pRequestManager_->RemoveRequestFromWaitingList(elementIterator);
            pRequestManager_->AddRequestToBusyList(elementIterator);
            break;
        case kWaiting:
            DEBUG_TRACE("Cache[%hhu] request %" PRIu64 " is still waiting, breaking out of loop\n", cacheLevel_, poolIndex);
            goto out_of_loop; // Break out of for loop
            break;
        default:
            assert_release(0);
            break;
        }
    }
out_of_loop:
    DEBUG_TRACE("\n");

    if (pUpperCache_) {
        Cache *upperCache = static_cast<Cache*>(pUpperCache_);
        numberOfRequestsCompleted = upperCache->InternalProcessCache(cycle, pCompletedRequests);
        upperCache->SetWasWorkDoneThisCycle(wasWorkDoneThisCycle_);
    }
    return numberOfRequestsCompleted;
}
