// Default cache test parameters that will be used
// to generate the .ini file if it doesn't exist
#define NUM_CACHE_LEVELS    (2)
#define MIN_BLOCK_SIZE      (64)
#define MAX_BLOCK_SIZE      (128)
#define MIN_CACHE_SIZE      (MIN_BLOCK_SIZE * 16)
#define MAX_CACHE_SIZE      (1024)
#define MIN_ASSOCIATIVITY   (1)
#define MAX_ASSOCIATIVITY   (4)
#define MAX_NUM_THREADS     (12) // -1 for no limit

// Defined for simplification, not a test parameter
#define MAX_NUM_CACHE_LEVELS (4)
