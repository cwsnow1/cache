#include <assert.h>
#include <inttypes.h>
#include <memory>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "Cache.h"
#include "Memory.h"
#include "SimTracer.h"
#include "debug.h"
#include "list.h"

#if (SIM_TRACE == 1)
extern SimTracer* gSimTracer;
#else
extern SimTracer dummySimTracer;
extern SimTracer* gSimTracer;
#endif

#if (CONSOLE_PRINT == 1)
#define DEBUG_TRACE printf
#else
#define DEBUG_TRACE(...)
#endif

// =====================================
//          Public Functions
// =====================================

Cache::Cache(Cache* pUpperCache, CacheLevel cacheLevel, uint8_t numCacheLevels, Configuration* pCacheConfigs)
    : Memory(pUpperCache, cacheLevel), config_(pCacheConfigs[cacheLevel]) {
    Configuration cacheConfig = pCacheConfigs[cacheLevel];
    blockSizeBits_ = 0;
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
    assert_release(numBlocks % config_.associativity == 0 && "Number of blocks must divide evenly with associativity");
    numSets_ = numBlocks / config_.associativity;
    assert_release(isPowerOfTwo(numSets_) && "Number of sets must be a power of 2");
    blockAddressToSetIndexMask_ = numSets_ - 1;
    earliestNextUsefulCycle_ = UINT64_MAX;
    if (cacheLevel < numCacheLevels - 1) {
        pLowerCache_ =
            std::make_unique<Cache>(this, static_cast<CacheLevel>(cacheLevel + 1), numCacheLevels, pCacheConfigs);
    } else {
        pLowerCache_ = std::make_unique<Memory>(this, kMainMemory);
    }
    pMainMemory_ = pLowerCache_.get();
    for (; pMainMemory_->GetCacheLevel() != kMainMemory; pMainMemory_ = &pMainMemory_->GetLowerCache())
        ;
}

void Cache::AllocateMemory() {
    sets_ = std::vector<Set>(numSets_);
    for (uint64_t i = 0; i < numSets_; i++) {
        sets_[i].ways = std::vector<Block>(config_.associativity);
        sets_[i].lruList = std::vector<uint8_t>(config_.associativity);
        for (uint8_t j = 0; j < config_.associativity; j++) {
            sets_[i].lruList[j] = j;
        }
    }
    pRequestManager_ = std::make_unique<RequestManager>(cacheLevel_);
    if (pLowerCache_->GetCacheLevel() != kMainMemory) {
        static_cast<Cache*>(pLowerCache_.get())->AllocateMemory();
    } else {
        pLowerCache_->AllocateMemory();
    }
}

void Cache::SetThreadId(uint64_t threadId) {
    threadId_ = threadId;
    if (cacheLevel_ != kMainMemory) {
        static_cast<Cache*>(pLowerCache_.get())->SetThreadId(threadId);
    }
}

bool Cache::IsCacheConfigValid(Configuration config) {
    assert_release((config.cacheSize % config.blockSize == 0) && "Block size must be a factor of cache size!");
    uint64_t numBlocks = config.cacheSize / config.blockSize;
    return numBlocks >= config.associativity;
}

void Cache::FreeMemory() {
    for (uint64_t i = 0; i < numSets_; i++) {
        sets_[i].ways.clear();
        sets_[i].ways.shrink_to_fit();
        sets_[i].lruList.clear();
        sets_[i].lruList.shrink_to_fit();
    }
    sets_.clear();
    sets_.shrink_to_fit();
    pRequestManager_.reset(nullptr);
    if (pLowerCache_->GetCacheLevel() != kMainMemory) {
        static_cast<Cache*>(pLowerCache_.get())->FreeMemory();
    } else {
        pLowerCache_->FreeMemory();
    }
}

void Cache::ProcessCache(uint64_t cycle, std::vector<int16_t>& completedRequests) {
    pMainMemory_->InternalProcessCache(cycle, completedRequests);
}

// =====================================
//          Private Functions
// =====================================

void Cache::updateLRUList(uint64_t setIndex, uint8_t mruIndex) {
    if (config_.associativity == 1) {
        return;
    }
    std::vector<uint8_t>& lruList = sets_[setIndex].lruList;
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
    gSimTracer->Print(SIM_TRACE__LRU_UPDATE, static_cast<Memory*>(this), static_cast<uint32_t>(setIndex), lruList[0],
                      lruList[config_.associativity - 1]);
}

int16_t Cache::evictBlock(uint64_t setIndex) {
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
    if (pLowerCache_->AddAccessRequest(lowerCacheAccess, cycle_) == RequestManager::kInvalidRequestIndex) {
        gSimTracer->Print(SIM_TRACE__EVICT_FAILED, static_cast<Memory*>(this));
        DEBUG_TRACE("Cache[%hhu] could not make request to lower cache in evictBlock, returning\n", cacheLevel_);
        return -1;
    }
    sets_[setIndex].ways[lruBlockIndex].valid = false;
    return lruBlockIndex;
}

bool Cache::findBlockInSet(uint64_t setIndex, uint64_t blockAddress, uint8_t& pBlockIndex) {
    for (uint64_t i = 0; i < config_.associativity; i++) {
        if (sets_[setIndex].ways[i].valid && (sets_[setIndex].ways[i].blockAddress == blockAddress)) {
            pBlockIndex = i;
            updateLRUList(setIndex, i);
            return true;
        }
    }
    return false;
}

int16_t Cache::requestBlock(uint64_t setIndex, uint64_t blockAddress) {
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

Status Cache::handleAccess(Request& request) {
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

    const Instruction& access = request.instruction;
    const uint64_t blockAddress = addressToBlockAddress(access.ptr);
    const uint64_t setIndex = addressToSetIndex(access.ptr);
    if (sets_[setIndex].busy) {
        DEBUG_TRACE("Cache[%hhu] set %" PRIu64 " is busy\n", cacheLevel_, setIndex);
        return kBusy;
    }
    wasWorkDoneThisCycle_ = true;
    uint8_t blockIndex;
    bool hit = findBlockInSet(setIndex, blockAddress, blockIndex);
    request.attemptCount++;
    if (hit) {
        gSimTracer->Print(SIM_TRACE__HIT, static_cast<Memory*>(this), pRequestManager_->GetPoolIndex(&request),
                          blockAddress >> 32, blockAddress & UINT32_MAX, setIndex);
        if (access.rw == READ) {
            if (request.attemptCount == 1) {
                ++stats_.readHits;
            }
        } else {
            if (request.attemptCount == 1) {
                ++stats_.writeHits;
            }
            sets_[setIndex].ways[blockIndex].dirty = true;
        }
    } else {
        gSimTracer->Print(SIM_TRACE__MISS, static_cast<Memory*>(this), pRequestManager_->GetPoolIndex(&request),
                          setIndex);
        if (access.rw == READ) {
            if (request.attemptCount == 1) {
                ++stats_.readMisses;
            }
        } else {
            if (request.attemptCount == 1) {
                ++stats_.writeMisses;
            }
        }

        int16_t requestedBlock = requestBlock(setIndex, blockAddress);
        if (requestedBlock < 0) {
            return kMiss;
        }
        // Cast is OK after check above
        blockIndex = static_cast<uint8_t>(requestedBlock);
        sets_[setIndex].busy = true;
        DEBUG_TRACE("Cache[%hhu] set %" PRIu64 " marked as busy due to miss\n", cacheLevel_, setIndex);
        if (access.rw == WRITE) {
            sets_[setIndex].ways[blockIndex].dirty = true;
        }
    }
    return hit ? kHit : kMiss;
}

void Cache::ResetCacheSetBusy(uint64_t setIndex) {
    sets_[setIndex].busy = false;
}

void Cache::InternalProcessCache(uint64_t cycle, std::vector<int16_t>& completedRequests) {
    wasWorkDoneThisCycle_ = false;
    cycle_ = cycle;
    if (pLowerCache_->GetWasWorkDoneThisCycle()) {
        for_each_in_double_list(pRequestManager_->GetBusyRequests()) {
            DEBUG_TRACE("Cache[%hhu] trying request %" PRIu64 " from busy requests list, address=0x%012" PRIx64 "\n",
                        cacheLevel_, poolIndex, pRequestManager_->GetRequestAtIndex(poolIndex).instruction.ptr);
            Status status = handleAccess(pRequestManager_->GetRequestAtIndex(poolIndex));
            if (status == kHit) {
                DEBUG_TRACE("Cache[%hhu] hit, set=%" PRIu64 "\n", cacheLevel_,
                            addressToSetIndex(pRequestManager_->GetRequestAtIndex(poolIndex).instruction.ptr));
                if (pUpperCache_) {
                    Cache* upperCache = static_cast<Cache*>(pUpperCache_);
                    uint64_t setIndex =
                        upperCache->addressToSetIndex(pRequestManager_->GetRequestAtIndex(poolIndex).instruction.ptr);
                    DEBUG_TRACE("Cache[%hhu] marking set %" PRIu64 " as no longer busy\n",
                                static_cast<uint8_t>(pUpperCache_->GetCacheLevel()), setIndex);
                    static_cast<Cache*>(pUpperCache_)->ResetCacheSetBusy(setIndex);
                }
                if (cacheLevel_ == kL1) {
                    completedRequests.push_back(poolIndex);
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
        DEBUG_TRACE("Cache[%hhu] trying request %" PRIu64 " from waiting list, address=0x%012" PRIx64 "\n", cacheLevel_,
                    poolIndex, pRequestManager_->GetRequestAtIndex(poolIndex).instruction.ptr);
        Status status = handleAccess(pRequestManager_->GetRequestAtIndex(poolIndex));
        switch (status) {
        case kHit:
            DEBUG_TRACE("Cache[%hhu] hit, set=%" PRIu64 "\n", cacheLevel_,
                        addressToSetIndex(pRequestManager_->GetRequestAtIndex(poolIndex).instruction.ptr));
            if (pUpperCache_) {
                Cache* const upperCache = static_cast<Cache*>(pUpperCache_);
                uint64_t setIndex =
                    upperCache->addressToSetIndex(pRequestManager_->GetRequestAtIndex(poolIndex).instruction.ptr);
                DEBUG_TRACE("Cache[%hhu] marking set %" PRIu64 " as no longer busy\n",
                            static_cast<uint8_t>(pUpperCache_->GetCacheLevel()), setIndex);
                upperCache->sets_[setIndex].busy = false;
            } else {
                assert(cacheLevel_ == kL1);
                completedRequests.push_back(poolIndex);
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
            DEBUG_TRACE("Cache[%hhu] request %" PRIu64 " is still waiting, breaking out of loop\n", cacheLevel_,
                        poolIndex);
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
        Cache* upperCache = static_cast<Cache*>(pUpperCache_);
        upperCache->InternalProcessCache(cycle, completedRequests);
        upperCache->SetWasWorkDoneThisCycle(wasWorkDoneThisCycle_);
    }
}
