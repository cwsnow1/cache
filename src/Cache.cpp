#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "list.h"
#include "Cache.h"
#include "SimTracer.h"
#include "debug.h"

// Obviously these are approximations
const uint64_t access_time_in_cycles[] = {
    3,  // L1
    12, // L2
    38, // L3
    195 // Main Memory
};

#if (SIM_TRACE == 1)
extern SimTracer *g_SimTracer;
#else
SimTracer dummySimTracer = SimTracer();
SimTracer *g_SimTracer = &dummySimTracer;
#endif

#if (CONSOLE_PRINT == 1)
#define DEBUG_TRACE printf
#else
#define DEBUG_TRACE(...)
#endif

// =====================================
//          Public Functions
// =====================================

Cache::Cache (Cache *upper_cache, CacheLevel cache_level, uint8_t num_cache_levels, Configuration *cache_configs, uint64_t config_index) {
    Configuration cache_config = cache_configs[cache_level];
    upper_cache_ = upper_cache;
    cache_level_ = cache_level;
    config_.cache_size = cache_config.cache_size;
    config_.block_size = cache_config.block_size;
    block_size_bits_ = 0;
    earliest_next_useful_cycle_ = UINT64_MAX;
    config_index_ = config_index;
    uint64_t tmp = config_.block_size;
    for (; (tmp & 1) == 0; tmp >>= 1) {
        block_size_bits_++;
    }
    assert_release((tmp == 1) && "Block size must be a power of 2!");
    assert_release((config_.cache_size % config_.block_size == 0) && "Block size must be a factor of cache size!");
    uint64_t num_blocks = config_.cache_size / config_.block_size;
    if (num_blocks < cache_config.associativity) {
        assert(false);
    }
    config_.associativity = cache_config.associativity;
    assert_release(num_blocks % config_.associativity == 0 && "Number of blocks must divide evenly with associativity");
    num_sets_ = num_blocks / config_.associativity;
    tmp = num_sets_;
    for (; (tmp & 1) == 0; tmp >>= 1)
        ;
    assert_release(tmp == 1 && "Number of sets must be a power of 2");
    block_address_to_set_index_mask_ = num_sets_ - 1;
    if (cache_level < num_cache_levels - 1) {
        lower_cache_ = new Cache(this, static_cast<CacheLevel>(cache_level + 1), num_cache_levels, cache_configs, config_index);
        lower_cache_->upper_cache_ = this;
    } else {
        initMainMemory(this);
    }
}

Cache::~Cache () {
    if (lower_cache_) {
        delete lower_cache_;
    }
}

void Cache::AllocateMemory () {
    if (cache_level_ != kMainMemory) {
        sets_ = new Set[num_sets_];
        for (uint64_t i = 0; i < num_sets_; i++) {
            sets_[i].ways = new Block[config_.associativity];
            sets_[i].lru_list = new uint8_t[config_.associativity];
            for (uint64_t j = 0; j < config_.associativity; j++) {
                sets_[i].lru_list[j] = (uint8_t) j;
            }
        }
        lower_cache_->AllocateMemory();
    }
    initRequestManager();
}

void Cache::SetThreadId (uint64_t thread_id) {
    thread_id_ = thread_id;
    if (lower_cache_) {
        lower_cache_->SetThreadId(thread_id);
    }
}

bool Cache::IsCacheConfigValid (Configuration config) {
    assert_release((config.cache_size % config.block_size == 0) && "Block size must be a factor of cache size!");
    uint64_t num_blocks = config.cache_size / config.block_size;
    return num_blocks >= config.associativity;
}

void Cache::FreeMemory () {
    if (sets_) {
        for (uint64_t i = 0; i < num_sets_; i++) {
            if (sets_[i].ways) {
                delete[] sets_[i].ways;
                delete[] sets_[i].lru_list;
            }
        }
        delete[] sets_;
    }
    delete[] request_manager_.request_pool;
    delete request_manager_.waiting_requests;
    delete request_manager_.free_requests;
    delete request_manager_.busy_requests;
    if (lower_cache_) {
        lower_cache_->FreeMemory();
    }
}

int16_t Cache::AddAccessRequest (Instruction access, uint64_t cycle) {
    DoubleListElement *element = request_manager_.free_requests->PopElement();
    if (element) {
        CODE_FOR_ASSERT(bool ret =) request_manager_.waiting_requests->AddElementToTail(element);
        assert(ret);
        uint64_t pool_index = element->pool_index_;
        request_manager_.request_pool[pool_index].instruction = access;
        request_manager_.request_pool[pool_index].cycle = cycle;
        request_manager_.request_pool[pool_index].cycle_to_call_back = cycle + access_time_in_cycles[cache_level_];
        request_manager_.request_pool[pool_index].first_attempt = true;
        DEBUG_TRACE("Cache[%hhu] New request added at index %lu, call back at tick %lu\n", cache_level_, pool_index, request_manager_.request_pool[pool_index].cycle_to_call_back);
        g_SimTracer->Print(SIM_TRACE__REQUEST_ADDED, this, pool_index, access.rw == READ ? 'r' : 'w', (access.ptr >> 32), access.ptr & UINT32_MAX, access_time_in_cycles[cache_level_]);

        return (int16_t) pool_index;
    }
    return -1;
}

uint64_t Cache::ProcessCache (uint64_t cycle, int16_t *completed_requests) {
    return main_memory_->internalProcessCache(cycle, completed_requests);
}

uint64_t Cache::GetEarliestNextUsefulCycle() {
    uint64_t earliest_next_useful_cycle = UINT64_MAX;
    for (Cache *cache_i = this; cache_i != NULL; cache_i = cache_i->lower_cache_) {
        if (cache_i->earliest_next_useful_cycle_ < earliest_next_useful_cycle) {
            earliest_next_useful_cycle = cache_i->earliest_next_useful_cycle_;
        }
    }
    return earliest_next_useful_cycle;
}

// =====================================
//          Private Functions
// =====================================

void Cache::initMainMemory (Cache *lowest_cache) {
    Cache *mm = new Cache();
    lowest_cache->lower_cache_ = mm;
    mm->cache_level_ = kMainMemory;
    mm->upper_cache_ = lowest_cache;
    mm->earliest_next_useful_cycle_ = UINT64_MAX;
    for (Cache *cache_i = lowest_cache; cache_i != NULL; cache_i = cache_i->upper_cache_) {
        cache_i->main_memory_ = mm;
    }
}

void Cache::initRequestManager () {
    request_manager_.max_outstanding_requests = kMaxNumberOfRequests << cache_level_;
    request_manager_.request_pool = new Request[request_manager_.max_outstanding_requests];
    request_manager_.waiting_requests = new DoubleList(request_manager_.max_outstanding_requests);
    request_manager_.busy_requests = new DoubleList(request_manager_.max_outstanding_requests);
    request_manager_.free_requests = new DoubleList(request_manager_.max_outstanding_requests);
    for (uint64_t i = 0; i < request_manager_.max_outstanding_requests; i++) {
        DoubleListElement *element = new DoubleListElement;
        element->pool_index_ = i;
        request_manager_.free_requests->PushElement(element);
    }
}

void Cache::updateLRUList (uint64_t set_index, uint8_t mru_index) {
    if (config_.associativity == 1) {
        return;
    }
    uint8_t *lru_list = sets_[set_index].lru_list;
    uint8_t prev_val = mru_index;
    // find MRU index in the lru_list
    for (uint8_t i = 0; i < config_.associativity; i++) {
        uint8_t tmp = lru_list[i];
        lru_list[i] = prev_val;
        if (tmp == mru_index) {
            break;
        }
        prev_val = tmp;
    }
    g_SimTracer->Print(SIM_TRACE__LRU_UPDATE, this, (uint32_t) set_index, lru_list[0], lru_list[config_.associativity - 1]);
}

int16_t Cache::evictBlock (uint64_t set_index, uint64_t block_address) {
    int16_t lru_block_index = sets_[set_index].lru_list[config_.associativity - 1];
    if (!sets_[set_index].ways[lru_block_index].valid) {
        DEBUG_TRACE("Cache[%hhu] not evicting invalid block from set %lu\n", cache_level_, set_index);
        return lru_block_index;
    }
    uint64_t old_block_addr = sets_[set_index].ways[lru_block_index].block_address;
    Instruction lower_cache_access = Instruction(old_block_addr << block_size_bits_, READ);
    if (sets_[set_index].ways[lru_block_index].dirty) {
        lower_cache_access.rw = WRITE;
        ++stats_.writebacks;
        sets_[set_index].ways[lru_block_index].dirty = false;
    }
    if (lower_cache_->AddAccessRequest(lower_cache_access, cycle_) == -1) {
        g_SimTracer->Print(SIM_TRACE__EVICT_FAILED, this);
        DEBUG_TRACE("Cache[%hhu] could not make request to lower cache in evictBlock, returning\n", cache_level_);
        return -1;
    }
    sets_[set_index].ways[lru_block_index].valid = false;
    return lru_block_index;
}

bool Cache::findBlockInSet (uint64_t set_index, uint64_t block_address, uint8_t *block_index) {
    for (uint64_t i = 0; i < config_.associativity; i++) {
        if (sets_[set_index].ways[i].valid && (sets_[set_index].ways[i].block_address == block_address)) {
            *block_index = i;
            updateLRUList(set_index, i);
            return true;
        }
    }
    return false;
}

int16_t Cache::requestBlock (uint64_t set_index, uint64_t block_address) {
    int16_t block_index = evictBlock(set_index, block_address);
    if (block_index == -1) {
        return -1;
    }
    g_SimTracer->Print(SIM_TRACE__EVICT, this, set_index, block_index);
    Instruction read_request_to_lower_cache = Instruction(block_address << block_size_bits_, READ);
    if (lower_cache_->AddAccessRequest(read_request_to_lower_cache, cycle_) == -1) {
        g_SimTracer->Print(SIM_TRACE__REQUEST_FAILED, this, NULL);
        DEBUG_TRACE("Cache[%hhu] could not make request to lower cache in requestBlock, returning\n", cache_level_);
        return -1;
    }
    sets_[set_index].ways[block_index].block_address = block_address;
    sets_[set_index].ways[block_index].valid = true;
    assert(sets_[set_index].ways[block_index].dirty == false);
    return block_index;
}

Status Cache::handleAccess (Request *request) {
    if (cycle_ < request->cycle_to_call_back) {
        DEBUG_TRACE("%lu/%lu cycles for this operation in cache_level=%hhu\n", cycle_ - request->cycle, access_time_in_cycles[cache_level_], cache_level_);
        if (earliest_next_useful_cycle_ > request->cycle_to_call_back) {
            DEBUG_TRACE("Cache[%hhu] next useful cycle set to %lu\n", cache_level_, request->cycle_to_call_back);
            earliest_next_useful_cycle_ = request->cycle_to_call_back;
        }
        return kMissing;
    }
    earliest_next_useful_cycle_ = UINT64_MAX;
    if (cache_level_ == kMainMemory) {
        // Main memory always hits
        work_done_this_cycle_ = true;
        return kHit;
    }
    Instruction access = request->instruction;
    uint64_t block_address = addressToBlockAddress(access.ptr);
    uint64_t set_index = addressToSetIndex(access.ptr);
    if (sets_[set_index].busy) {
        DEBUG_TRACE("Cache[%hhu] set %lu is busy\n", cache_level_, set_index);
        return kBusy;
    }
    work_done_this_cycle_ = true;
    uint8_t block_index;
    bool hit = findBlockInSet(set_index, block_address, &block_index);
    if (hit) {
        g_SimTracer->Print(SIM_TRACE__HIT, this,
            request - request_manager_.request_pool, block_address>>32, block_address & UINT32_MAX, set_index);
        if (access.rw == READ) {
            if (request->first_attempt) {
                ++stats_.read_hits;
            }
        } else {
            if (request->first_attempt) {
                ++stats_.write_hits;
            }
            sets_[set_index].ways[block_index].dirty = true;
        }
    } else {
        g_SimTracer->Print(SIM_TRACE__MISS, this, request - request_manager_.request_pool, set_index);
        request->first_attempt = false;
        int16_t requested_block = requestBlock(set_index, block_address);
        if (requested_block == -1) {
            return kMiss;
        }
        block_index = (uint8_t) requested_block;
        sets_[set_index].busy = true;
        DEBUG_TRACE("Cache[%hhu] set %lu marked as busy due to miss\n", cache_level_, set_index);
        if (access.rw == READ) {
            ++stats_.read_misses;
        } else {
            ++stats_.write_misses;
            sets_[set_index].ways[block_index].dirty = true;
        }
    }
    return hit ? kHit : kMiss;
}

uint64_t Cache::internalProcessCache (uint64_t cycle, int16_t *completed_requests) {
    uint64_t num_requests_completed = 0;
    work_done_this_cycle_ = false;
    cycle_ = cycle;
    CODE_FOR_ASSERT(bool ret);
    if (lower_cache_ && lower_cache_->work_done_this_cycle_) {
        for_each_in_double_list(request_manager_.busy_requests) {
            DEBUG_TRACE("Cache[%hhu] trying request %lu from busy requests list, address=0x%012lx\n", cache_level_, pool_index, request_manager_.request_pool[pool_index].instruction.ptr);
            Status status = handleAccess(&request_manager_.request_pool[pool_index]);
            if (status == kHit) {
                DEBUG_TRACE("Cache[%hhu] hit, set=%lu\n", cache_level_, addressToSetIndex(request_manager_.request_pool[pool_index].instruction.ptr));
                if (upper_cache_) {
                    uint64_t set_index = upper_cache_->addressToSetIndex(request_manager_.request_pool[pool_index].instruction.ptr);
                    DEBUG_TRACE("Cache[%hhu] marking set %lu as no longer busy\n", (uint8_t)(cache_level_ - 1), set_index);
                    upper_cache_->sets_[set_index].busy = false;
                }
                if (cache_level_ == kL1) {
                    completed_requests[num_requests_completed++] = pool_index;
                }
                CODE_FOR_ASSERT(ret =) request_manager_.busy_requests->RemoveElement(element_i);
                assert(ret);
                CODE_FOR_ASSERT(ret =) request_manager_.free_requests->PushElement(element_i);
                assert(ret);
            }
        }
    } else {
        if (lower_cache_ && request_manager_.busy_requests->PeekHead()) {
            DEBUG_TRACE("Cache[%hhu] no work was done in lower cache, not checking busy list\n", cache_level_);
        }
    }
    for_each_in_double_list(request_manager_.waiting_requests) {
        DEBUG_TRACE("Cache[%hhu] trying request %lu from waiting list, address=0x%012lx\n", cache_level_, pool_index, request_manager_.request_pool[pool_index].instruction.ptr);
        Status status = handleAccess(&request_manager_.request_pool[pool_index]);
        switch (status) {
        case kHit:
            DEBUG_TRACE("Cache[%hhu] hit, set=%lu\n", cache_level_, addressToSetIndex(request_manager_.request_pool[pool_index].instruction.ptr));
            if (upper_cache_) {
                uint64_t set_index = upper_cache_->addressToSetIndex(request_manager_.request_pool[pool_index].instruction.ptr);
                DEBUG_TRACE("Cache[%hhu] marking set %lu as no longer busy\n", (uint8_t)(cache_level_ - 1), set_index);
                upper_cache_->sets_[set_index].busy = false;
            }
            if (cache_level_ == kL1) {
                completed_requests[num_requests_completed++] = pool_index;
            }
            CODE_FOR_ASSERT(ret =) request_manager_.waiting_requests->RemoveElement(element_i);
            assert(ret);
            CODE_FOR_ASSERT(ret =) request_manager_.free_requests->PushElement(element_i);
            assert(ret);
            break;
        case kMiss:
        case kBusy:
            CODE_FOR_ASSERT(ret =) request_manager_.waiting_requests->RemoveElement(element_i);
            assert(ret);
            request_manager_.busy_requests->AddElementToTail(element_i);
            break;
        case kMissing:
            DEBUG_TRACE("Cache[%hhu] request %lu is still waiting, breaking out of loop\n", cache_level_, pool_index);
            goto out_of_loop; // Break out of for loop
            break;
        default:
            assert_release(0);
            break;
        }
    }
out_of_loop:
    DEBUG_TRACE("\n");

    if (upper_cache_) {
        num_requests_completed = upper_cache_->internalProcessCache(cycle, completed_requests);
        upper_cache_->work_done_this_cycle_ = work_done_this_cycle_;
    }
    return num_requests_completed;
}
