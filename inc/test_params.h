class TestParams {
public:
    TestParams();

    TestParams(uint8_t  num_cache_levels, uint64_t min_block_size, uint64_t max_block_size, uint64_t min_cache_size,
        uint64_t max_cache_size, uint8_t min_associativity, uint8_t  max_associativity, int32_t  max_num_threads);

    uint8_t getNumCacheLevels() {
        return num_cache_levels;
    }

    uint64_t getMinBlockSize() {
        return min_block_size;
    }

    uint64_t getMaxBlockSize() {
        return max_block_size;
    }
    uint64_t getMinCacheSize() {
        return min_cache_size;
    }

    uint64_t getMaxCacheSize() {
        return max_cache_size;
    }

    uint8_t getMinAssociativity() {
        return min_associativity;
    }

    uint8_t getMaxAssociativity() {
        return max_associativity;
    }

    int32_t getMaxNumThreads() {
        return max_num_threads;
    }

private:
    /**
     * @brief Verifies the global test parameters struct is valid
     * 
     */
    void verify();

    uint8_t  num_cache_levels;
    uint64_t min_block_size;
    uint64_t max_block_size;
    uint64_t min_cache_size;
    uint64_t max_cache_size;
    uint8_t  min_associativity;
    uint8_t  max_associativity;
    int32_t  max_num_threads;
};
