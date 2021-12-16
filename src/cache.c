#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "list.h"
#include "cache.h"
#include "sim_trace.h"
#include "inlines.h"

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

static void init_main_memory (cache_t *lowest_cache);
static void init_request_manager (cache_t *me);

bool cache__is_cache_config_valid (config_t config) {
    assert((config.cache_size % config.block_size == 0) && "Block size must be a factor of cache size!");
    uint64_t num_blocks = config.cache_size / config.block_size;
    return num_blocks >= config.associativity;
}

bool cache__init (cache_t *caches, uint8_t cache_level, config_t *cache_configs, uint64_t thread_id) {
    assert(caches);
    cache_t *me = &caches[cache_level];
    config_t cache_config = cache_configs[cache_level];
    if (cache_level != 0) {
        me->upper_cache = &caches[cache_level - 1];
    } else {
        me->upper_cache = NULL;
    }
    me->lower_cache = (cache_level == cache_configs[cache_level].num_cache_levels - 1) ? NULL : &caches[cache_level + 1];
    me->cache_level = cache_level;
    me->cache_size = cache_config.cache_size;
    me->block_size = cache_config.block_size;
    me->block_size_bits = 0;
    me->thread_id = thread_id;
    uint64_t tmp = me->block_size;
    for (; (tmp & 1) == 0; tmp >>= 1) {
        me->block_size_bits++;
    }
    assert((tmp == 1) && "Block size must be a power of 2!");
    assert((me->cache_size % me->block_size == 0) && "Block size must be a factor of cache size!");
    me->num_blocks = me->cache_size / me->block_size;
    if (me->num_blocks < cache_config.associativity) {
        memset(me, 0, sizeof(cache_t));
        return false; // Redundant config
    }
    me->associativity = cache_config.associativity;
    me->num_sets = CEILING_DIVIDE(me->num_blocks, me->associativity);
    uint8_t set_bits = BITS(me->num_sets);
    // If num_sets is not a power of 2, round up to the next greatest power of 2 for bitmask
    uint64_t num_sets_ceiling = (1 << (set_bits - 1)) < me->num_sets ? 1 << set_bits : me->num_sets;
    // num_sets_ceiling is a power of 2, so the mask is one less
    me->block_addr_to_set_index_mask = num_sets_ceiling - 1;
    me->sets = (set_t*) malloc(sizeof(set_t) * me->num_sets);
    memset(me->sets, 0, sizeof(set_t) * me->num_sets);
    for (int i = 0; i < me->num_sets; i++) {
        me->sets[i].ways = (block_t*) malloc(sizeof(block_t) * me->associativity);
        me->sets[i].lru_list = (uint8_t*) malloc(sizeof(uint8_t) * me->associativity);
        memset(me->sets[i].ways, 0, sizeof(block_t) * me->associativity);
        for (int j = 0; j < me->associativity; j++) {
            me->sets[i].lru_list[j] = (uint8_t) j;
        }
    }
    init_request_manager(me);
    if (me->lower_cache) {
        bool ret = cache__init(caches, cache_level + 1, cache_configs, thread_id);
        assert(ret);
    } else {
        init_main_memory(me);
    }
    return true;
}

static void init_main_memory (cache_t *lowest_cache) {
    lowest_cache->lower_cache = (cache_t*) malloc(sizeof(cache_t));
    memset(lowest_cache->lower_cache, 0, sizeof(cache_t));
    lowest_cache->lower_cache->cache_level = MAIN_MEMORY;
    lowest_cache->lower_cache->thread_id = lowest_cache->thread_id;
    lowest_cache->lower_cache->upper_cache = lowest_cache;
    init_request_manager(lowest_cache->lower_cache);
}

static void init_request_manager (cache_t *me) {
    assert(me);
    me->request_manager.max_outstanding_requests = MAX_NUM_REQUESTS << me->cache_level;
    me->request_manager.request_pool = (request_t*) malloc(sizeof(request_t) * me->request_manager.max_outstanding_requests);
    me->request_manager.outstanding_requests = double_list__init_list(me->request_manager.max_outstanding_requests);
    me->request_manager.free_requests = double_list__init_list(me->request_manager.max_outstanding_requests);
    for (uint64_t i = 0; i < me->request_manager.max_outstanding_requests; i++) {
        double_list_element_t *element = (double_list_element_t*) malloc(sizeof(double_list_element_t));
        element->pool_index = i;
        double_list__push_element(me->request_manager.free_requests, element);
    }
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
    if (me->lower_cache) {
        cache__reset(me->lower_cache);
    }
    memset(me, 0, sizeof(cache_t));
}

void cache__print_info (cache_t *me) {
    if (me->cache_size) {
        printf("== CACHE LEVEL %u ==\n", me->cache_level);
        printf("cache_size = %lu, block_size = %lu, num_blocks = %lu, num_sets = %lu, associativity = %lu\n",
            me->cache_size, me->block_size, me->num_blocks, me->num_sets, me->associativity);
        if (me->lower_cache) {
            cache__print_info(me->lower_cache);
        }
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
 *  @return             Slot index
 */
static inline uint64_t block_addr_to_set_index (cache_t *cache, uint64_t block_addr) {
    return (block_addr & cache->block_addr_to_set_index_mask);
}

/**
 *  @brief Translates a raw address to a set index
 * 
 *  @param cache        Cache struct to use for config
 *  @param block_addr   Raw address, 64 bits
 *  @return             Slot index
 */
static inline uint64_t addr_to_set_index (cache_t *cache, uint64_t addr) {
    return block_addr_to_set_index(cache, addr_to_block_addr(cache, addr));
}

/**
 * @brief               Reorder the LRU list for the given set
 * 
 * @param cache         Cache structure
 * @param set_index    Slot whose LRU list is to be reordered
 * @param mru_index     Block index that is now the most recently used
 */
static void update_lru_list (cache_t * cache, uint64_t set_index, uint8_t mru_index) {
    uint8_t *lru_list = cache->sets[set_index].lru_list;
    uint8_t prev_val = mru_index;
    // find MRU index in the lru_list
    for (uint8_t i = 0; i < cache->associativity; i++) {
        uint8_t tmp = lru_list[i];
        lru_list[i] = prev_val;
        if (tmp == mru_index) {
            break;
        }
        prev_val = tmp;
    }
#ifdef SIM_TRACE
    uint64_t values[MAX_NUM_SIM_TRACE_VALUES] = {set_index, lru_list[0], lru_list[cache->associativity - 1]};
    sim_trace__print(SIM_TRACE__LRU_UPDATE, cache->thread_id, values);
#endif
}

/**
 * @brief               Handles the eviction and subsequent interactions with lower cache(s)
 * 
 * @param cache         Current cache structure
 * @param set_index    Slot index from which block needs to be evicted
 * @param block_addr    Block address that will replace the evicted block
 * @return              Block index within the provided set that the new block occupies
 */
static int16_t evict_block (cache_t *cache, uint64_t set_index, uint64_t block_addr, uint64_t cycle) {
    int16_t lru_block_index = cache->sets[set_index].lru_list[cache->associativity - 1];
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
    if (!cache__add_access_request(cache->lower_cache, lower_cache_access, cycle)) {
        DEBUG_TRACE("Cache[%hhu] could not make request in evict, returning\n", cache->cache_level);
        return -1;
    }
    cache->sets[set_index].ways[lru_block_index].valid = false;
    //update_lru_list(cache, set_index, lru_block_index);
    return lru_block_index;
}

/**
 * @brief               Searches the given set for the given block address
 * 
 * @param cache         Current cache structure
 * @param set_index    Slot index to search
 * @param block_addr    Block address for which to search
 * @param block_index   Output: The block index within the set iff found
 * @return true         if the block is found in the set
 */
static bool find_block_in_set (cache_t *cache, uint64_t set_index, uint64_t block_addr, uint8_t *block_index) {
    for (int i = 0; i < cache->associativity; i++) {
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
 * @param set_index    Slot in which to put new block
 * @param block_addr    Block address of block to acquire
 * @return              Block index acquired within set
 */
static int16_t request_block (cache_t *cache, uint64_t set_index, uint64_t block_addr, uint64_t cycle) {
    int16_t block_index = evict_block(cache, set_index, block_addr, cycle);
    if (block_index == -1) {
        return -1;
    }
#ifdef SIM_TRACE
    uint64_t values[MAX_NUM_SIM_TRACE_VALUES] = {set_index, block_index};
    sim_trace__print(SIM_TRACE__EVICT, cache->thread_id, values);
#endif
    instruction_t read_request_to_lower_cache = {
        .ptr = block_addr << cache->block_size_bits,
        .rw  = READ,
    };
    bool ret = cache__add_access_request(cache->lower_cache, read_request_to_lower_cache, cycle);
    if (!ret) {
        DEBUG_TRACE("Cache[%hhu] could not make request in request, returning\n", cache->cache_level);
        return -1;
    }
    cache->sets[set_index].ways[block_index].block_addr = block_addr;
    cache->sets[set_index].ways[block_index].valid = true;
    assert(cache->sets[set_index].ways[block_index].dirty == false);
    return block_index;
}

static bool handle_access (cache_t *cache, request_t request, uint64_t cycle) {
    assert(cache);
    if (cycle - request.cycle < access_time_in_cycles[cache->cache_level]) {
        DEBUG_TRACE("%lu/%lu cycles for this operation in cache_level=%hhu\n", cycle - request.cycle, access_time_in_cycles[cache->cache_level], cache->cache_level);
        return false;
    }
    instruction_t access = request.instruction;
    uint64_t block_addr = addr_to_block_addr(cache, access.ptr);
    uint64_t set_index = addr_to_set_index(cache, access.ptr);
    if (cache->sets[set_index].busy) {
        DEBUG_TRACE("Cache[%hhu] set %lu is busy\n", cache->cache_level, set_index);
        return false;
    }
#ifdef SIM_TRACE
    {
        char rw = access.rw == READ ? 'r' : 'w';
        uint64_t values[MAX_NUM_SIM_TRACE_VALUES] = {(uint64_t) rw, (uint64_t) cache->cache_level, block_addr, set_index};
        sim_trace__print(SIM_TRACE__ACCESS_BEGIN, cache->thread_id, values);
    }
#endif
    uint8_t block_index;
    bool hit = find_block_in_set(cache, set_index, block_addr, &block_index);
    if (hit) {
        if (access.rw == READ) {
            ++cache->stats.read_hits;
        } else {
            ++cache->stats.write_hits;
            cache->sets[set_index].ways[block_index].dirty = true;
        }
    } else {
#ifdef SIM_TRACE
        {
            uint64_t values[MAX_NUM_SIM_TRACE_VALUES] = {set_index};
            sim_trace__print(SIM_TRACE__MISS, cache->thread_id, values);
        }
#endif

        int16_t requested_block = request_block(cache, set_index, block_addr, cycle);
        if (requested_block == -1) {
            return hit;
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
    return hit;
}

static void process_main_memory (cache_t *mm, uint64_t cycle) {
    if (mm->request_manager.outstanding_requests->count) {
        for_each_in_double_list(mm->request_manager.outstanding_requests) {
            if (cycle - mm->request_manager.request_pool[pool_index].cycle == access_time_in_cycles[MAIN_MEMORY]) {
                DEBUG_TRACE("Main memory hit\n");
                uint64_t set_index = addr_to_set_index(mm->upper_cache, mm->request_manager.request_pool[pool_index].instruction.ptr);
                DEBUG_TRACE("Cache[%hhu] marking set %lu as no longer busy\n", mm->upper_cache->cache_level, set_index);
                mm->upper_cache->sets[set_index].busy = false;
                mm->request_manager.request_pool[pool_index].valid = false;
                assert(double_list__remove_element(mm->request_manager.outstanding_requests, element_i));
                assert(double_list__push_element(mm->request_manager.free_requests, element_i));
            } else {
                DEBUG_TRACE("MM request %lu: %lu/%lu cycles\n", pool_index, cycle - mm->request_manager.request_pool[pool_index].cycle, access_time_in_cycles[MAIN_MEMORY]);
            }
        }
    }
}

bool cache__add_access_request (cache_t *cache, instruction_t access, uint64_t cycle) {
    double_list_element_t *element = double_list__pop_element(cache->request_manager.free_requests);
    if (element) {
        assert(double_list__add_element_to_tail(cache->request_manager.outstanding_requests, element));
        uint64_t pool_index = element->pool_index;
        cache->request_manager.request_pool[pool_index].instruction = access;
        cache->request_manager.request_pool[pool_index].cycle = cycle;
        cache->request_manager.request_pool[pool_index].valid = true;
        DEBUG_TRACE("Cache[%hhu] New request added at index %lu\n", cache->cache_level, pool_index);
        return true;
    }
    return false;
}

void cache__process_cache (cache_t *cache, uint64_t cycle) {
    if (cache->request_manager.outstanding_requests->count) {
        for_each_in_double_list(cache->request_manager.outstanding_requests) {
            DEBUG_TRACE("Cache[%hhu] Trying request %lu, addr=0x%012lx\n", cache->cache_level, pool_index, cache->request_manager.request_pool[pool_index].instruction.ptr);
            if (handle_access(cache, cache->request_manager.request_pool[pool_index], cycle)) {
                DEBUG_TRACE("Cache[%hhu] hit, set=%lu\n", cache->cache_level, addr_to_set_index(cache, cache->request_manager.request_pool[pool_index].instruction.ptr));
                if (cache->upper_cache) {
                    uint64_t set_index = addr_to_set_index(cache->upper_cache, cache->request_manager.request_pool[pool_index].instruction.ptr);
                    DEBUG_TRACE("Cache[%hhu] marking set %lu as no longer busy\n", (uint8_t)(cache->cache_level - 1), set_index);
                    cache->upper_cache->sets[set_index].busy = false;
                }
                cache->request_manager.request_pool[pool_index].valid = false;
                assert(double_list__remove_element(cache->request_manager.outstanding_requests, element_i));
                assert(double_list__push_element(cache->request_manager.free_requests, element_i));
            }
        }
    }
    DEBUG_TRACE("\n");
    if (cache->lower_cache->cache_size) {
        cache__process_cache(cache->lower_cache, cycle);
    } else {
        process_main_memory(cache->lower_cache, cycle);
    }
}
