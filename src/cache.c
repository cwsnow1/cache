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

#if (CONSOLE_PRINT == 1)
#define DEBUG_TRACE printf
#else
#define DEBUG_TRACE(...)
#endif

// =====================================
// Private Function Forward Declarations
// =====================================

static          void        init_main_memory        (cache_t *lowest_cache);
static          void        init_request_manager    (cache_t *cache);
static inline   uint64_t    addr_to_block_addr      (cache_t *cache, uint64_t addr);
static inline   uint64_t    block_addr_to_set_index (cache_t *cache, uint64_t block_addr);
static inline   uint64_t    addr_to_set_index       (cache_t *cache, uint64_t addr);
static          void        update_lru_list         (cache_t *cache, uint64_t set_index, uint8_t mru_index);
static          int16_t     evict_block             (cache_t *cache, uint64_t set_index, uint64_t block_addr);
static          bool        find_block_in_set       (cache_t *cache, uint64_t set_index, uint64_t block_addr, uint8_t *block_index);
static          int16_t     request_block           (cache_t *cache, uint64_t set_index, uint64_t block_addr);
static          status_t    handle_access           (cache_t *cache, request_t *request);
static          uint64_t    internal_process_cache  (cache_t *cache, uint64_t cycle, int16_t *completed_requests);

// =====================================
//          Public Functions
// =====================================

bool cache__init (cache_t *caches, uint8_t cache_level, uint8_t num_cache_levels, config_t *cache_configs, uint64_t config_index) {
    assert(caches);
    cache_t *me = &caches[cache_level];
    config_t cache_config = cache_configs[cache_level];
    if (cache_level != 0) {
        me->upper_cache = &caches[cache_level - 1];
    } else {
        me->upper_cache = NULL;
    }
    me->lower_cache = (cache_level == num_cache_levels - 1) ? NULL : &caches[cache_level + 1];
    me->cache_level = cache_level;
    me->config.cache_size = cache_config.cache_size;
    me->config.block_size = cache_config.block_size;
    me->block_size_bits = 0;
    me->config_index = config_index;
    me->earliest_next_useful_cycle = UINT64_MAX;
    uint64_t tmp = me->config.block_size;
    for (; (tmp & 1) == 0; tmp >>= 1) {
        me->block_size_bits++;
    }
    assert((tmp == 1) && "Block size must be a power of 2!");
    assert((me->config.cache_size % me->config.block_size == 0) && "Block size must be a factor of cache size!");
    uint64_t num_blocks = me->config.cache_size / me->config.block_size;
    if (num_blocks < cache_config.associativity) {
        memset(me, 0, sizeof(cache_t));
        return false; // Redundant config
    }
    me->config.associativity = cache_config.associativity;
    assert(num_blocks % me->config.associativity == 0 && "Number of blocks must divide evenly with associativity");
    me->num_sets = num_blocks / me->config.associativity;
    tmp = me->num_sets;
    for (; (tmp & 1) == 0; tmp >>= 1)
        ;
    assert(tmp == 1 && "Number of sets must be a power of 2");
    me->block_addr_to_set_index_mask = me->num_sets - 1;
    me->sets = (set_t*) malloc(sizeof(set_t) * me->num_sets);
    memset(me->sets, 0, sizeof(set_t) * me->num_sets);
    for (int i = 0; i < me->num_sets; i++) {
        me->sets[i].ways = (block_t*) malloc(sizeof(block_t) * me->config.associativity);
        me->sets[i].lru_list = (uint8_t*) malloc(sizeof(uint8_t) * me->config.associativity);
        memset(me->sets[i].ways, 0, sizeof(block_t) * me->config.associativity);
        for (int j = 0; j < me->config.associativity; j++) {
            me->sets[i].lru_list[j] = (uint8_t) j;
        }
    }
    init_request_manager(me);
    if (me->lower_cache) {
        bool ret = cache__init(caches, cache_level + 1, num_cache_levels, cache_configs, config_index);
        assert(ret);
    } else {
        init_main_memory(me);
    }
    return true;
}

void cache__set_thread_id (cache_t *cache, uint64_t thread_id) {
    for (cache_t *cache_i = cache; cache_i != NULL; cache_i = cache_i->lower_cache) {
        cache_i->thread_id = thread_id;
    }
}

bool cache__is_cache_config_valid (config_t config) {
    assert((config.cache_size % config.block_size == 0) && "Block size must be a factor of cache size!");
    uint64_t num_blocks = config.cache_size / config.block_size;
    return num_blocks >= config.associativity;
}

void cache__reset (cache_t *me) {
    if (me->sets) {
        for (int i = 0; i < me->num_sets; i++) {
            if (me->sets[i].ways) {
                free(me->sets[i].ways);
                free(me->sets[i].lru_list);
            }
        }
        free(me->sets);
    }
    if (me->request_manager.request_pool) {
        free(me->request_manager.request_pool);
    }
    double_list__free_list(me->request_manager.waiting_requests);
    double_list__free_list(me->request_manager.free_requests);
    double_list__free_list(me->request_manager.busy_requests);
    if (me->lower_cache) {
        cache__reset(me->lower_cache);
    } else {
        free(me);
    }
}

void cache__print_info (cache_t *me) {
    if (me->config.cache_size) {
        printf("== CACHE LEVEL %u ==\n", me->cache_level);
        printf("cache_size = %lu, block_size = %lu, num_sets = %lu, associativity = %lu\n",
            me->config.cache_size, me->config.block_size, me->num_sets, me->config.associativity);
        if (me->lower_cache) {
            cache__print_info(me->lower_cache);
        }
    }
}

int16_t cache__add_access_request (cache_t *cache, instruction_t access, uint64_t cycle) {
    double_list_element_t *element = double_list__pop_element(cache->request_manager.free_requests);
    if (element) {
        assert(double_list__add_element_to_tail(cache->request_manager.waiting_requests, element));
        uint64_t pool_index = element->pool_index;
        cache->request_manager.request_pool[pool_index].instruction = access;
        cache->request_manager.request_pool[pool_index].cycle = cycle;
        cache->request_manager.request_pool[pool_index].cycle_to_call_back = cycle + access_time_in_cycles[cache->cache_level];
        cache->request_manager.request_pool[pool_index].first_attempt = true;
        DEBUG_TRACE("Cache[%hhu] New request added at index %lu, call back at tick %lu\n", cache->cache_level, pool_index, cache->request_manager.request_pool[pool_index].cycle_to_call_back);
#ifdef SIM_TRACE
        uint64_t values[MAX_NUM_SIM_TRACE_VALUES] = {pool_index, access.ptr, access_time_in_cycles[cache->cache_level]};
        sim_trace__print(SIM_TRACE__REQUEST_ADDED, cache, values);
#endif

        return (int16_t) pool_index;
    }
    return -1;
}

uint64_t cache__process_cache (cache_t *cache, uint64_t cycle, int16_t *completed_requests) {
    return internal_process_cache(cache->main_memory, cycle, completed_requests);
}

// =====================================
//          Private Functions
// =====================================

/**
 * @brief Initializes the private, never-missing cache that represents main memory
 * 
 * @param lowest_cache The lowest level of cache that will be main memory's upper_cache
 */
static void init_main_memory (cache_t *lowest_cache) {
    cache_t *mm =(cache_t*) malloc(sizeof(cache_t));
    lowest_cache->lower_cache = mm;
    memset(mm, 0, sizeof(cache_t));
    mm->cache_level = MAIN_MEMORY;
    mm->config_index = lowest_cache->config_index;
    mm->upper_cache = lowest_cache;
    mm->earliest_next_useful_cycle = UINT64_MAX;
    for (cache_t *cache_i = lowest_cache; cache_i != NULL; cache_i = cache_i->upper_cache) {
        cache_i->main_memory = mm;
    }
    init_request_manager(mm);
}

/**
 * @brief Initializes the request manager and the request lists it maintains
 * 
 * @param me The parent cache structure
 */
static void init_request_manager (cache_t *cache) {
    assert(cache);
    cache->request_manager.max_outstanding_requests = MAX_NUM_REQUESTS << cache->cache_level;
    cache->request_manager.request_pool = (request_t*) malloc(sizeof(request_t) * cache->request_manager.max_outstanding_requests);
    cache->request_manager.waiting_requests = double_list__init_list(cache->request_manager.max_outstanding_requests);
    cache->request_manager.busy_requests = double_list__init_list(cache->request_manager.max_outstanding_requests);
    cache->request_manager.free_requests = double_list__init_list(cache->request_manager.max_outstanding_requests);
    for (uint64_t i = 0; i < cache->request_manager.max_outstanding_requests; i++) {
        double_list_element_t *element = (double_list_element_t*) malloc(sizeof(double_list_element_t));
        element->pool_index = i;
        double_list__push_element(cache->request_manager.free_requests, element);
    }
}

/**
 *  @brief Translates raw address to block address
 * 
 *  @param cache        Cache struct to use for config
 *  @param addr         Raw address, 64 bits
 *  @return             Block address, i.e. x MSB of the raw address
 */
static inline uint64_t addr_to_block_addr (cache_t *cache, uint64_t addr) {
    return addr >> cache->block_size_bits;
}

/**
 *  @brief Translates a block address to a set index
 * 
 *  @param cache        Cache struct to use for config
 *  @param block_addr   Block address, i.e. the raw address shifted
 *  @return             Set index
 */
static inline uint64_t block_addr_to_set_index (cache_t *cache, uint64_t block_addr) {
    return (block_addr & cache->block_addr_to_set_index_mask);
}

/**
 *  @brief Translates a raw address to a set index
 * 
 *  @param cache        Cache struct to use for config
 *  @param block_addr   Raw address, 64 bits
 *  @return             Set index
 */
static inline uint64_t addr_to_set_index (cache_t *cache, uint64_t addr) {
    return block_addr_to_set_index(cache, addr_to_block_addr(cache, addr));
}

/**
 * @brief               Reorder the LRU list for the given set
 * 
 * @param cache         Cache structure
 * @param set_index    Set whose LRU list is to be reordered
 * @param mru_index     Block index that is now the most recently used
 */
static void update_lru_list (cache_t * cache, uint64_t set_index, uint8_t mru_index) {
    uint8_t *lru_list = cache->sets[set_index].lru_list;
    uint8_t prev_val = mru_index;
    // find MRU index in the lru_list
    for (uint8_t i = 0; i < cache->config.associativity; i++) {
        uint8_t tmp = lru_list[i];
        lru_list[i] = prev_val;
        if (tmp == mru_index) {
            break;
        }
        prev_val = tmp;
    }
#ifdef SIM_TRACE
    uint64_t values[MAX_NUM_SIM_TRACE_VALUES] = {set_index, lru_list[0], lru_list[cache->config.associativity - 1]};
    sim_trace__print(SIM_TRACE__LRU_UPDATE, cache, values);
#endif
}

/**
 * @brief               Handles the eviction and subsequent interactions with lower cache(s)
 * 
 * @param cache         Current cache structure
 * @param set_index     Set index from which block needs to be evicted
 * @param block_addr    Block address that will replace the evicted block
 * @return              Block index within the provided set that the new block occupies, -1 if evict request failed
 */
static int16_t evict_block (cache_t *cache, uint64_t set_index, uint64_t block_addr) {
    int16_t lru_block_index = cache->sets[set_index].lru_list[cache->config.associativity - 1];
    if (!cache->sets[set_index].ways[lru_block_index].valid) {
        DEBUG_TRACE("Cache[%hhu] not evicting invalid block from set %lu\n", cache->cache_level, set_index);
        return lru_block_index;
    }
    uint64_t old_block_addr = cache->sets[set_index].ways[lru_block_index].block_addr;
    instruction_t lower_cache_access = {
        .ptr = old_block_addr << cache->block_size_bits,
        .rw  = READ,
    };
    if (cache->sets[set_index].ways[lru_block_index].dirty) {
        lower_cache_access.rw = WRITE;
        ++cache->stats.writebacks;
        cache->sets[set_index].ways[lru_block_index].dirty = false;
    }
    if (cache__add_access_request(cache->lower_cache, lower_cache_access, cache->cycle) == -1) {
#ifdef SIM_TRACE
        sim_trace__print(SIM_TRACE__EVICT_FAILED, cache, NULL);
#endif
        DEBUG_TRACE("Cache[%hhu] could not make request to lower cache in evict_block, returning\n", cache->cache_level);
        return -1;
    }
    cache->sets[set_index].ways[lru_block_index].valid = false;
    return lru_block_index;
}

/**
 * @brief               Searches the given set for the given block address
 * 
 * @param cache         Current cache structure
 * @param set_index     Set index to search
 * @param block_addr    Block address for which to search
 * @param block_index   Output: The block index within the set iff found
 * @return true         if the block is found in the set
 */
static bool find_block_in_set (cache_t *cache, uint64_t set_index, uint64_t block_addr, uint8_t *block_index) {
    for (int i = 0; i < cache->config.associativity; i++) {
        if (cache->sets[set_index].ways[i].valid && (cache->sets[set_index].ways[i].block_addr == block_addr)) {
            *block_index = i;
            update_lru_list(cache, set_index, i);
            return true;
        }
    }
    return false;
}

/**
 * @brief               Acquires the given block address into the given set. Makes any subsequent calls necessary to lower caches
 * 
 * @param cache         Current cache structure
 * @param set_index     Set in which to put new block
 * @param block_addr    Block address of block to acquire
 * @return              Block index acquired within set, -1 if request failed
 */
static int16_t request_block (cache_t *cache, uint64_t set_index, uint64_t block_addr) {
    int16_t block_index = evict_block(cache, set_index, block_addr);
    if (block_index == -1) {
        return -1;
    }
#ifdef SIM_TRACE
    uint64_t values[MAX_NUM_SIM_TRACE_VALUES] = {set_index, block_index};
    sim_trace__print(SIM_TRACE__EVICT, cache, values);
#endif
    instruction_t read_request_to_lower_cache = {
        .ptr = block_addr << cache->block_size_bits,
        .rw  = READ,
    };
    if (cache__add_access_request(cache->lower_cache, read_request_to_lower_cache, cache->cycle) == -1) {
#ifdef SIM_TRACE
        sim_trace__print(SIM_TRACE__REQUEST_FAILED, cache, NULL);
#endif
        DEBUG_TRACE("Cache[%hhu] could not make request to lower cache in request_block, returning\n", cache->cache_level);
        return -1;
    }
    cache->sets[set_index].ways[block_index].block_addr = block_addr;
    cache->sets[set_index].ways[block_index].valid = true;
    assert(cache->sets[set_index].ways[block_index].dirty == false);
    return block_index;
}

/**
 * @brief           Attempts a read or write to the given cache
 * 
 * @param cache     Current cache structure, if cache_size is 0, it is assumed to main memory
 * @param request   Request structure to attempt
 * @return true     If the request was completed and need not be called again
 */
static status_t handle_access (cache_t *cache, request_t *request) {
    assert(cache);
    if (cache->cycle < request->cycle_to_call_back) {
        DEBUG_TRACE("%lu/%lu cycles for this operation in cache_level=%hhu\n", cache->cycle - request->cycle, access_time_in_cycles[cache->cache_level], cache->cache_level);
        if (cache->earliest_next_useful_cycle > request->cycle_to_call_back) {
            DEBUG_TRACE("Cache[%hhu] next useful cycle set to %lu\n", cache->cache_level, request->cycle_to_call_back);
            cache->earliest_next_useful_cycle = request->cycle_to_call_back;
        }
        return WAITING;
    }
    cache->earliest_next_useful_cycle = UINT64_MAX;
    if (cache->cache_level == MAIN_MEMORY) {
        // Main memory always hits
        cache->work_done_this_cycle = true;
        return HIT;
    }
    instruction_t access = request->instruction;
    uint64_t block_addr = addr_to_block_addr(cache, access.ptr);
    uint64_t set_index = addr_to_set_index(cache, access.ptr);
    if (cache->sets[set_index].busy) {
        DEBUG_TRACE("Cache[%hhu] set %lu is busy\n", cache->cache_level, set_index);
        return BUSY;
    }
    cache->work_done_this_cycle = true;
#ifdef SIM_TRACE
    {
        char rw = access.rw == READ ? 'r' : 'w';
        uint64_t pool_index = request - cache->request_manager.request_pool;
        uint64_t values[MAX_NUM_SIM_TRACE_VALUES] = {pool_index, (uint64_t) rw, block_addr, set_index};
        sim_trace__print(SIM_TRACE__ACCESS_BEGIN, cache, values);
    }
#endif
    uint8_t block_index;
    bool hit = find_block_in_set(cache, set_index, block_addr, &block_index);
    if (hit) {
        if (access.rw == READ) {
            if (request->first_attempt) {
                ++cache->stats.read_hits;
            }
        } else {
            if (request->first_attempt) {
                ++cache->stats.write_hits;
            }
            cache->sets[set_index].ways[block_index].dirty = true;
        }
    } else {
#ifdef SIM_TRACE
        {
            uint64_t pool_index = request - cache->request_manager.request_pool;
            uint64_t values[MAX_NUM_SIM_TRACE_VALUES] = {pool_index, set_index};
            sim_trace__print(SIM_TRACE__MISS, cache, values);
        }
#endif
        request->first_attempt = false;
        int16_t requested_block = request_block(cache, set_index, block_addr);
        if (requested_block == -1) {
            return MISS;
        }
        block_index = (uint8_t) requested_block;
        cache->sets[set_index].busy = true;
        DEBUG_TRACE("Cache[%hhu] set %lu marked as busy due to miss\n", cache->cache_level, set_index);
        if (access.rw == READ) {
            ++cache->stats.read_misses;
        } else {
            ++cache->stats.write_misses;
            cache->sets[set_index].ways[block_index].dirty = true;
        }
    }
    return hit ? HIT : MISS;
}

/**
 * @brief                       Simulate a clock cycle in the cache structure(s)
 * 
 * @param cache                 Main memory cache structure. This function will recursively call all upper caches
 * @param cycle                 Current clock cycle
 * @param completed_requests    Out. An array of the request indices that were completed this tick. Length is the return value
 * 
 * @return                      Number of requests completed this tick
 */
static uint64_t internal_process_cache (cache_t *cache, uint64_t cycle, int16_t *completed_requests) {
    uint64_t num_requests_completed = 0;
    cache->work_done_this_cycle = false;
    cache->cycle = cycle;
    if (cache->lower_cache && cache->lower_cache->work_done_this_cycle) {
        for_each_in_double_list(cache->request_manager.busy_requests) {
            DEBUG_TRACE("Cache[%hhu] trying request %lu from busy requests list, addr=0x%012lx\n", cache->cache_level, pool_index, cache->request_manager.request_pool[pool_index].instruction.ptr);
            status_t status = handle_access(cache, &cache->request_manager.request_pool[pool_index]);
            if (status == HIT) {
                DEBUG_TRACE("Cache[%hhu] hit, set=%lu\n", cache->cache_level, addr_to_set_index(cache, cache->request_manager.request_pool[pool_index].instruction.ptr));
                if (cache->upper_cache) {
                    uint64_t set_index = addr_to_set_index(cache->upper_cache, cache->request_manager.request_pool[pool_index].instruction.ptr);
                    DEBUG_TRACE("Cache[%hhu] marking set %lu as no longer busy\n", (uint8_t)(cache->cache_level - 1), set_index);
                    cache->upper_cache->sets[set_index].busy = false;
                }
                if (cache->cache_level == L1) {
                    completed_requests[num_requests_completed++] = pool_index;
                }
                assert(double_list__remove_element(cache->request_manager.busy_requests, element_i));
                assert(double_list__push_element(cache->request_manager.free_requests, element_i));
            }
        }
    } else {
        if (cache->lower_cache && cache->request_manager.busy_requests->head) {
            DEBUG_TRACE("Cache[%hhu] no work was done in lower cache, not checking busy list\n", cache->cache_level);
        }
    }
    for_each_in_double_list(cache->request_manager.waiting_requests) {
        DEBUG_TRACE("Cache[%hhu] trying request %lu from waiting list, addr=0x%012lx\n", cache->cache_level, pool_index, cache->request_manager.request_pool[pool_index].instruction.ptr);
        status_t status = handle_access(cache, &cache->request_manager.request_pool[pool_index]);
        switch (status) {
        case HIT:
            DEBUG_TRACE("Cache[%hhu] hit, set=%lu\n", cache->cache_level, addr_to_set_index(cache, cache->request_manager.request_pool[pool_index].instruction.ptr));
            if (cache->upper_cache) {
                uint64_t set_index = addr_to_set_index(cache->upper_cache, cache->request_manager.request_pool[pool_index].instruction.ptr);
                DEBUG_TRACE("Cache[%hhu] marking set %lu as no longer busy\n", (uint8_t)(cache->cache_level - 1), set_index);
                cache->upper_cache->sets[set_index].busy = false;
            }
            if (cache->cache_level == L1) {
                completed_requests[num_requests_completed++] = pool_index;
            }
            assert(double_list__remove_element(cache->request_manager.waiting_requests, element_i));
            assert(double_list__push_element(cache->request_manager.free_requests, element_i));
            break;
        case MISS:
        case BUSY:
            assert(double_list__remove_element(cache->request_manager.waiting_requests, element_i));
            double_list__add_element_to_tail(cache->request_manager.busy_requests, element_i);
            break;
        case WAITING:
            DEBUG_TRACE("Cache[%hhu] request %lu is still waiting, breaking out of loop\n", cache->cache_level, pool_index);
            goto out_of_loop; // Break out of for loop
            break;
        default:
            assert(0);
            break;
        }
    }
out_of_loop:
    DEBUG_TRACE("\n");

    if (cache->upper_cache) {
        num_requests_completed = internal_process_cache(cache->upper_cache, cycle, completed_requests);
        cache->upper_cache->work_done_this_cycle = cache->work_done_this_cycle;
    }
    return num_requests_completed;
}
