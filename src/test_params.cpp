#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "default_test_params.h"
#include "test_params.h"
#include "cache.h"

TestParams::TestParams() {
    this->num_cache_levels  = NUM_CACHE_LEVELS;
    this->min_block_size    = MIN_BLOCK_SIZE;
    this->max_block_size    = MAX_BLOCK_SIZE;
    this->min_cache_size    = MIN_CACHE_SIZE;
    this->max_cache_size    = MAX_CACHE_SIZE;
    this->min_associativity = MIN_ASSOCIATIVITY;
    this->max_associativity = MAX_ASSOCIATIVITY;
    this->max_num_threads   = MAX_NUM_THREADS;
    verify();
}

TestParams::TestParams(uint8_t num_cache_levels, uint64_t min_block_size, uint64_t max_block_size, uint64_t min_cache_size,
    uint64_t max_cache_size, uint8_t min_associativity, uint8_t  max_associativity, int32_t  max_num_threads) {

    this->num_cache_levels  = num_cache_levels;
    this->min_block_size    = min_block_size;
    this->max_block_size    = max_block_size;
    this->min_cache_size    = min_cache_size;
    this->max_cache_size    = max_cache_size;
    this->min_associativity = min_associativity;
    this->max_associativity = max_associativity;
    this->max_num_threads   = max_num_threads;
    verify();
}

void TestParams::verify () {
    // Check that all values were read in correctly
    assert(num_cache_levels);
    assert(min_block_size);
    assert(max_block_size);
    assert(min_cache_size);
    assert(max_cache_size);
    assert(min_associativity);
    assert(max_associativity);
    assert(max_num_threads);

    // Check that values make sense. May help in understanding why a parameter config
    // will be found to have 0 possible cache configs
    assert(num_cache_levels <= MAX_NUM_CACHE_LEVELS);
    assert(min_block_size <= max_block_size);
    assert(min_cache_size <= max_cache_size);
    assert(min_cache_size >= min_block_size);
    assert(num_cache_levels <= MAIN_MEMORY && "Update access_time_in_cycles & enum cache_levels");
    return;
}