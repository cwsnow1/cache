#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "cache.h"
#include "sim_trace.h"

extern cache_t **g_caches;
extern test_params_t g_test_params;


bool cache__is_cache_config_valid (config_t config) {
    assert((config.cache_size % config.block_size == 0) && "Block size must be a factor of cache size!");
    uint64_t num_blocks = config.cache_size / config.block_size;
    return num_blocks >= config.num_blocks_per_slot;
}

bool cache__init (cache_t *caches, uint8_t cache_level, config_t *cache_configs, uint64_t thread_id) {
    assert(caches);
    cache_t *me = &caches[cache_level];
    config_t cache_config = cache_configs[cache_level];
    me->lower_cache = (cache_level == g_test_params.num_cache_levels - 1) ? NULL : &caches[cache_level + 1];
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
    if (me->num_blocks < cache_config.num_blocks_per_slot) {
        memset(me, 0, sizeof(cache_t));
        return false; // Redundant config
    }
    me->num_blocks_per_slot = cache_config.num_blocks_per_slot;
    me->num_slots = me->num_blocks / me->num_blocks_per_slot;
    for (tmp = me->num_slots; (tmp & 1) == 0; tmp >>= 1)
        ;
    assert((tmp == 1) && "Number of slots must be a power of 2!"); // TODO: does it?
    me->slots = (slot_t*) malloc(sizeof(slot_t) * me->num_slots);
    memset(me->slots, 0, sizeof(slot_t) * me->num_slots);
    for (int i = 0; i < me->num_slots; i++) {
        me->slots[i].blocks = (block_t*) malloc(sizeof(block_t) * me->num_blocks_per_slot);
        me->slots[i].lru_list = (uint8_t*) malloc(sizeof(uint8_t) * me->num_blocks_per_slot);
        memset(me->slots[i].blocks, 0, sizeof(block_t) * me->num_blocks_per_slot);
        for (int j = 0; j < me->num_blocks_per_slot; j++) {
            me->slots[i].lru_list[j] = (uint8_t) j;
        }
    }
    if (me->lower_cache) {
        bool ret = cache__init(caches, cache_level + 1, cache_configs, thread_id);
        assert(ret);
    }
    return true;
}

void cache__reset (cache_t *me) {
    if (me->slots) {
        for (int i = 0; i < me->num_slots; i++) {
            if (me->slots[i].blocks) {
                free(me->slots[i].blocks);
                free(me->slots[i].lru_list);
            }
        }
        free(me->slots);
    }
    if (me->lower_cache) {
        cache__reset(me->lower_cache);
    }
    memset(me, 0, sizeof(cache_t));
}

void cache__print_info (cache_t *me) {
    printf("== CACHE LEVEL %u ==\n", me->cache_level);
    printf("cache_size = %lu, block_size = %lu, num_blocks = %lu, num_slots = %lu, num_blocks_per_slot = %lu\n",
        me->cache_size, me->block_size, me->num_blocks, me->num_slots, me->num_blocks_per_slot);
    if (me->lower_cache) {
        cache__print_info(me->lower_cache);
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
 *  @brief Translates a block address to a slot index
 * 
 *  @param cache        Cache struct to use for config
 *  @param block_addr   Block address, i.e. the raw address shifted
 *  @return             Slot index
 */
static inline uint64_t block_addr_to_slot_index (cache_t *cache, uint64_t block_addr) {
    uint64_t mask = cache->num_slots - 1;
    return (block_addr & mask);
}

/**
 *  @brief Translates a raw address to a slot index
 * 
 *  @param cache        Cache struct to use for config
 *  @param block_addr   Raw address, 64 bits
 *  @return             Slot index
 */
static inline uint64_t addr_to_slot_index (cache_t *cache, uint64_t addr) {
    return block_addr_to_slot_index(cache, addr_to_block_addr(cache, addr));
}

/**
 * @brief               Reorder the LRU list for the given slot
 * 
 * @param cache         Cache structure
 * @param slot_index    Slot whose LRU list is to be reordered
 * @param mru_index     Block index that is now the most recently used
 */
static void update_lru_list (cache_t * cache, uint64_t slot_index, uint8_t mru_index) {
    uint8_t *lru_list = cache->slots[slot_index].lru_list;
    uint8_t prev_val = mru_index;
    // find MRU index in the lru_list
    for (uint8_t i = 0; i < cache->num_blocks_per_slot; i++) {
        uint8_t tmp = lru_list[i];
        lru_list[i] = prev_val;
        if (tmp == mru_index) {
            break;
        }
        prev_val = tmp;
    }
#ifdef SIM_TRACE
    uint64_t values[MAX_NUM_SIM_TRACE_VALUES] = {slot_index, lru_list[0], lru_list[cache->num_blocks_per_slot - 1]};
    sim_trace__print(SIM_TRACE__LRU_UPDATE, cache->thread_id, values);
#endif
}

/**
 * @brief               Handles the eviction and subsequent interactions with lower cache(s)
 * 
 * @param cache         Current cache structure
 * @param slot_index    Slot index from which block needs to be evicted
 * @param block_addr    Block address that will replace the evicted block
 * @return              Block index within the provided slot that the new block occupies
 */
static uint8_t evict_block (cache_t *cache, uint64_t slot_index, uint64_t block_addr) {
    uint8_t lru_block_index = cache->slots[slot_index].lru_list[cache->num_blocks_per_slot - 1];
    uint64_t old_block_addr = cache->slots[slot_index].blocks[lru_block_index].block_addr;
    instruction_t lower_cache_access = {
        .ptr = old_block_addr << cache->block_size_bits,
        .rw  = READ,
    };
    if (cache->slots[slot_index].blocks[lru_block_index].dirty) {
        lower_cache_access.rw = WRITE;
        ++cache->stats.writebacks;
        cache->slots[slot_index].blocks[lru_block_index].dirty = false;
    }
    if (cache->lower_cache) {
        cache__handle_access(cache->lower_cache, lower_cache_access);
    }
    update_lru_list(cache, slot_index, lru_block_index);
    return lru_block_index;
}

/**
 * @brief               Searches the given slot for the given block address
 * 
 * @param cache         Current cache structure
 * @param slot_index    Slot index to search
 * @param block_addr    Block address for which to search
 * @param block_index   Output: The block index within the slot iff found
 * @return true         if the block is found in the slot
 */
static bool find_block_in_slot (cache_t *cache, uint64_t slot_index, uint64_t block_addr, uint8_t *block_index) {
    for (int i = 0; i < cache->num_blocks_per_slot; i++) {
        if (cache->slots[slot_index].blocks[i].block_addr == block_addr) {
            *block_index = i;
            update_lru_list(cache, slot_index, i);
            return true;
        }
    }
    return false;
}

/**
 * @brief               Acquires the given block address into the given slot. Makes any subsequent calls necessary to lower caches
 * 
 * @param cache         Current cache structure
 * @param slot_index    Slot in which to put new block
 * @param block_addr    Block address of block to acquire
 * @return              Block index acquired within slot
 */
static uint8_t request_block (cache_t *cache, uint64_t slot_index, uint64_t block_addr) {
    uint8_t block_index = evict_block(cache, slot_index, block_addr);
#ifdef SIM_TRACE
    uint64_t values[MAX_NUM_SIM_TRACE_VALUES] = {slot_index, block_index};
    sim_trace__print(SIM_TRACE__EVICT, cache->thread_id, values);
#endif
    instruction_t read_request_to_lower_cache = {
        .ptr = block_addr << cache->block_size_bits,
        .rw  = READ,
    };
    if (cache->lower_cache) {
        cache__handle_access(cache->lower_cache, read_request_to_lower_cache);
    }
    cache->slots[slot_index].blocks[block_index].block_addr = block_addr;
    assert(cache->slots[slot_index].blocks[block_index].dirty == false);
    return block_index;
}

void cache__handle_access (cache_t *cache, instruction_t access) {
    assert(cache);
    uint64_t block_addr = addr_to_block_addr(cache, access.ptr);
    uint64_t slot_index = addr_to_slot_index(cache, access.ptr);
#ifdef SIM_TRACE
    {
        char rw = access.rw == READ ? 'r' : 'w';
        uint64_t values[MAX_NUM_SIM_TRACE_VALUES] = {(uint64_t) rw, (uint64_t) cache->cache_level, block_addr, slot_index};
        sim_trace__print(SIM_TRACE__ACCESS_BEGIN, cache->thread_id, values);
    }
#endif
    uint8_t block_index;
    bool hit = find_block_in_slot(cache, slot_index, block_addr, &block_index);
    if (hit) {
        if (access.rw == READ) {
            ++cache->stats.read_hits;
        } else {
            ++cache->stats.write_hits;
            cache->slots[slot_index].blocks[block_index].dirty = true;
        }
    } else {
#ifdef SIM_TRACE
        {
            uint64_t values[MAX_NUM_SIM_TRACE_VALUES] = {slot_index};
            sim_trace__print(SIM_TRACE__MISS, cache->thread_id, values);
        }
#endif
        block_index = request_block(cache, slot_index, block_addr);
        if (access.rw == READ) {
            ++cache->stats.read_misses;
        } else {
            ++cache->stats.write_misses;
            cache->slots[slot_index].blocks[block_index].dirty = true;
        }
    }
}
