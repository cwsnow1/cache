#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "list.h"
#include "cache.h"
#include "sim_trace.h"

// Obviously these are approximations
const uint64_t access_time_in_cycles[] = {
    3,  // L1
    12, // L2
    38, // L3
    195 // Main Memory
};

#ifdef CONSOLE_PRINT
#define DEBUG_TRACE printf
#else
#define DEBUG_TRACE(...)
#endif

struct RequestManager {
    RequestManager(Cache *cache) {
        max_outstanding_requests = MAX_NUM_REQUESTS << cache->getCacheLevel();
        request_pool = new Request[max_outstanding_requests];
        outstanding_requests = double_list__init_list(max_outstanding_requests);
        free_requests = double_list__init_list(max_outstanding_requests);
        for (uint64_t i = 0; i < max_outstanding_requests; i++) {
            double_list_element_t *element = new double_list_element_t;
            element->pool_index = i;
            double_list__push_element(free_requests, element);
        }
    }

    ~RequestManager() {
        delete request_pool;
        double_list__free_list(outstanding_requests);
        double_list__free_list(free_requests);
    }

    Request *request_pool;
    double_list_t *outstanding_requests;
    double_list_t *free_requests;
    uint64_t max_outstanding_requests;
};


bool cache__is_cache_config_valid (Config config) {
    assert((config.cache_size % config.block_size == 0) && "Block size must be a factor of cache size!");
    uint64_t num_blocks = config.cache_size / config.block_size;
    return num_blocks >= config.associativity;
}

Cache::Cache (Cache *upper_cache, uint8_t cache_level, Config *cache_configs, uint64_t thread_id) {
    Config cache_config = cache_configs[cache_level];
    this->upper_cache = upper_cache;
    lower_cache = cache_level == cache_configs[cache_level].num_cache_levels - 1 ? NULL : new Cache(this, cache_level + 1, cache_configs, thread_id);
    this->cache_level = cache_level;
    config.cache_size = cache_config.cache_size;
    config.block_size = cache_config.block_size;
    block_size_bits = 0;
    this->thread_id = thread_id;
    uint64_t tmp = config.block_size;
    for (; (tmp & 1) == 0; tmp >>= 1) {
        block_size_bits++;
    }
    assert((tmp == 1) && "Block size must be a power of 2!");
    assert((config.cache_size % config.block_size == 0) && "Block size must be a factor of cache size!");
    uint64_t num_blocks = config.cache_size / config.block_size;
    assert(num_blocks < cache_config.associativity);
    config.associativity = cache_config.associativity;
    assert(num_blocks % config.associativity == 0 && "Number of blocks must divide evenly with associativity");
    num_sets = num_blocks / config.associativity;
    tmp = num_sets;
    for (; (tmp & 1) == 0; tmp >>= 1)
        ;
    assert(tmp == 1 && "Number of sets must be a power of 2");
    block_addr_to_set_index_mask = num_sets - 1;
    sets = new Set[num_sets];
    for (uint64_t i = 0; i < num_sets; i++) {
        sets[i].ways = new Block[config.associativity];
        sets[i].lru_list = new uint8_t[config.associativity];
        for (uint64_t j = 0; j < config.associativity; j++) {
            sets[i].lru_list[j] = (uint8_t) j;
        }
    }
    request_manager = new RequestManager(this);
    if (lower_cache = NULL) {
        lower_cache = new MainMemory(this, thread_id);
    }
}

Cache::Cache() {
    memset(this, 0, sizeof(Cache));
}

MainMemory::MainMemory (Cache *upper_cache, uint64_t thread_id) {
    Cache();
    cache_level = MAIN_MEMORY;
    this->thread_id = thread_id;
    this->upper_cache = upper_cache;
    lower_cache = NULL;
    config.cache_size = 0;
    request_manager = new RequestManager(this);
}

Cache::~Cache () {
    if (sets) {
        for (int i = 0; i < num_sets; i++) {
            if (sets[i].ways) {
                delete sets[i].ways;
                delete sets[i].lru_list;
            }
        }
        delete sets;
    }
    if (request_manager) {
        request_manager->~RequestManager();
    }
    if (lower_cache) {
        lower_cache->~Cache();
    }
}

void Cache::printInfo () {
    if (config.cache_size) {
        printf("== CACHE LEVEL %u ==\n", cache_level);
        printf("cache_size = %lu, block_size = %lu, num_sets = %lu, associativity = %lu\n",
            config.cache_size, config.block_size, num_sets, config.associativity);
        if (lower_cache) {
            lower_cache->printInfo();
        }
    }
}

inline uint64_t Cache::addrToBlockAddr (uint64_t addr) {
    return addr >> block_size_bits;
}


inline uint64_t Cache::blockAddrToSetIndex (uint64_t block_addr) {
    return (block_addr & block_addr_to_set_index_mask);
}

inline uint64_t Cache::addrToSetIndex (uint64_t addr) {
    return blockAddrToSetIndex(addrToBlockAddr(addr));
}

void Cache::updateLRUList (uint64_t set_index, uint8_t mru_index) {
    uint8_t *lru_list = sets[set_index].lru_list;
    uint8_t prev_val = mru_index;
    // find MRU index in the lru_list
    for (uint8_t i = 0; i < config.associativity; i++) {
        uint8_t tmp = lru_list[i];
        lru_list[i] = prev_val;
        if (tmp == mru_index) {
            break;
        }
        prev_val = tmp;
    }
#ifdef SIM_TRACE
    uint64_t values[MAX_NUM_SIM_TRACE_VALUES] = {set_index, lru_list[0], lru_list[associativity - 1]};
    sim_trace__print(SIM_TRACE__LRU_UPDATE, thread_id, values);
#endif
}

/**
 * @brief               Handles the eviction and subsequent interactions with lower cache(s)
 * 
 * @param cache         Current cache structure
 * @param set_index     Set index from which block needs to be evicted
 * @param block_addr    Block address that will replace the evicted block
 * @param cycle         Current clock cycle
 * @return              Block index within the provided set that the new block occupies, -1 if evict request failed
 */
int16_t Cache::evictBlock (uint64_t set_index, uint64_t block_addr, uint64_t cycle) {
    int16_t lru_block_index = sets[set_index].lru_list[config.associativity - 1];
    if (!sets[set_index].ways[lru_block_index].valid) {
        DEBUG_TRACE("Cache[%hhu] not evicting invalid block from set %lu\n", cache_level, set_index);
        return lru_block_index;
    }
    uint64_t old_block_addr = sets[set_index].ways[lru_block_index].block_addr;
    Instruction lower_cache_access = {
        .ptr = old_block_addr << block_size_bits,
        .rw  = READ,
    };
    if (sets[set_index].ways[lru_block_index].dirty) {
        lower_cache_access.rw = WRITE;
        ++stats.writebacks;
        sets[set_index].ways[lru_block_index].dirty = false;
    }
    if (lower_cache->addAccessRequest(lower_cache_access, cycle) == -1) {
        DEBUG_TRACE("Cache[%hhu] could not make request to lower cache in evict_block, returning\n", cache_level);
        return -1;
    }
    sets[set_index].ways[lru_block_index].valid = false;
    return lru_block_index;
}

bool Cache::findBlockInSet (uint64_t set_index, uint64_t block_addr, uint8_t *block_index) {
    for (int i = 0; i < config.associativity; i++) {
        if (sets[set_index].ways[i].valid && (sets[set_index].ways[i].block_addr == block_addr)) {
            *block_index = i;
            updateLRUList(set_index, i);
            return true;
        }
    }
    return false;
}

int16_t Cache::requestBlock (uint64_t set_index, uint64_t block_addr, uint64_t cycle) {
    int16_t block_index = evictBlock(set_index, block_addr, cycle);
    if (block_index == -1) {
        return -1;
    }
#ifdef SIM_TRACE
    uint64_t values[MAX_NUM_SIM_TRACE_VALUES] = {set_index, block_index};
    sim_trace__print(SIM_TRACE__EVICT, thread_id, values);
#endif
    Instruction read_request_to_lower_cache = {
        .ptr = block_addr << block_size_bits,
        .rw  = READ,
    };
    if (lower_cache->addAccessRequest(read_request_to_lower_cache, cycle) == -1) {
        DEBUG_TRACE("Cache[%hhu] could not make request to lower cache in request_block, returning\n", cache_level);
        return -1;
    }
    sets[set_index].ways[block_index].block_addr = block_addr;
    sets[set_index].ways[block_index].valid = true;
    assert(sets[set_index].ways[block_index].dirty == false);
    return block_index;
}

/**
 * @brief           Attempts a read or write to the given cache
 * 
 * @param cache     Current cache structure, if cache_size is 0, it is assumed to main memory
 * @param request   Request structure to attempt
 * @param cycle     Current clock cycle
 * @return true     If the request was completed and need not be called again
 */
bool Cache::handleAccess (Request request, uint64_t cycle) {
    if (cycle - request.cycle < access_time_in_cycles[cache_level]) {
        DEBUG_TRACE("%lu/%lu cycles for this operation in cache_level=%hhu\n", cycle - request.cycle, access_time_in_cycles[cache_level], cache_level);
        return false;
    }
    if (config.cache_size == 0) {
        // This is the main memory "cache"
        return true;
    }
    Instruction access = request.instruction;
    uint64_t block_addr = addrToBlockAddr(access.ptr);
    uint64_t set_index = addrToSetIndex(access.ptr);
    if (sets[set_index].busy) {
        DEBUG_TRACE("Cache[%hhu] set %lu is busy\n", cache_level, set_index);
        return false;
    }
#ifdef SIM_TRACE
    {
        char rw = access.rw == READ ? 'r' : 'w';
        uint64_t values[MAX_NUM_SIM_TRACE_VALUES] = {(uint64_t) rw, (uint64_t) cache_level, block_addr, set_index};
        sim_trace__print(SIM_TRACE__ACCESS_BEGIN, thread_id, values);
    }
#endif
    uint8_t block_index;
    bool hit = findBlockInSet(set_index, block_addr, &block_index);
    if (hit) {
        if (access.rw == READ) {
            if (request.first_attempt) {
                ++stats.read_hits;
            }
        } else {
            if (request.first_attempt) {
                ++stats.write_hits;
            }
            sets[set_index].ways[block_index].dirty = true;
        }
    } else {
#ifdef SIM_TRACE
        {
            uint64_t values[MAX_NUM_SIM_TRACE_VALUES] = {set_index};
            sim_trace__print(SIM_TRACE__MISS, thread_id, values);
        }
#endif
        request.first_attempt = false;
        int16_t requested_block = requestBlock(set_index, block_addr, cycle);
        if (requested_block == -1) {
            return hit;
        }
        block_index = (uint8_t) requested_block;
        sets[set_index].busy = true;
        DEBUG_TRACE("Cache[%hhu] set %lu marked as busy due to miss\n", cache_level, set_index);
        if (access.rw == READ) {
            ++stats.read_misses;
        } else {
            ++stats.write_misses;
            sets[set_index].ways[block_index].dirty = true;
        }
    }
    return hit;
}

int16_t Cache::addAccessRequest (Instruction access, uint64_t cycle) {
    double_list_element_t *element = double_list__pop_element(request_manager->free_requests);
    if (element) {
        assert(double_list__add_element_to_tail(request_manager->outstanding_requests, element));
        uint64_t pool_index = element->pool_index;
        request_manager->request_pool[pool_index].instruction = access;
        request_manager->request_pool[pool_index].cycle = cycle;
        request_manager->request_pool[pool_index].first_attempt = true;
        DEBUG_TRACE("Cache[%hhu] New request added at index %lu\n", cache_level, pool_index);
        return (int16_t) pool_index;
    }
    return -1;
}

uint64_t Cache::processCache (uint64_t cycle, int16_t *completed_requests) {
    uint64_t num_requests_completed = 0;
    for_each_in_double_list(request_manager->outstanding_requests) {
        DEBUG_TRACE("Cache[%hhu] Trying request %lu, addr=0x%012lx\n", cache_level, pool_index, request_manager->request_pool[pool_index].instruction.ptr);
        if (handleAccess(request_manager->request_pool[pool_index], cycle)) {
            DEBUG_TRACE("Cache[%hhu] hit, set=%lu\n", cache_level, addrToSetIndex(request_manager->request_pool[pool_index].instruction.ptr));
            if (upper_cache) {
                uint64_t set_index = upper_cache->addrToSetIndex(request_manager->request_pool[pool_index].instruction.ptr);
                DEBUG_TRACE("Cache[%hhu] marking set %lu as no longer busy\n", (uint8_t)(cache_level - 1), set_index);
                upper_cache->sets[set_index].busy = false;
            }
            if (completed_requests) {
                completed_requests[num_requests_completed++] = pool_index;
            }
            assert(double_list__remove_element(request_manager->outstanding_requests, element_i));
            assert(double_list__push_element(request_manager->free_requests, element_i));
        }
    }
    DEBUG_TRACE("\n");
    if (lower_cache) {
        lower_cache->processCache(cycle, NULL);
    }
    return num_requests_completed;
}
