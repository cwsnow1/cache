// Default cache test parameters that will be used
// to generate the .ini file if it doesn't exist
#define NUM_CACHE_LEVELS    (2)
#define MIN_BLOCK_SIZE      (256)
#define MAX_BLOCK_SIZE      (256)
#define MIN_CACHE_SIZE      (MIN_BLOCK_SIZE * 16)
#define MAX_CACHE_SIZE      (16384)
#define MIN_ASSOCIATIVITY   (1)
#define MAX_ASSOCIATIVITY   (2)
#define MAX_NUM_THREADS     (12) // -1 for no limit
