#pragma once

/**
 * Trace file format is as follows:
 * 0xdeadbeefdead: W 0xbeefdeadbeef\n
 * 0xbeefdeadbeef: R 0xdeadbeefdead\n
 * etc.
 * 
 * The first address is a don't care, that is 16 bytes including WS before rw
 * 1 byte rw
 * 3 bytes dc including WS
 * 12 bytes for address
 * 1 byte newline
 */
#define FILE_LINE_LENGTH_IN_BYTES           (33)
#define FIRST_ADDRESS_LENGTH_IN_BYTES       (16)
#define RW_LENGHT_IN_BYTES                  (1)
#define AFTER_RW_LENGTH_IN_BYTES            (3)

/**
 * @brief           Prints collected statistics to given stream
 * 
 * @param cache     Cache structure whose stats to print
 * @param cycle     Cycle, used to calculate CPI
 * @param stream    Output stream to print to
 */
void io_utils__print_stats (cache_t *cache, uint64_t cycle, FILE* stream);

/**
 * @brief           Prints collected statistics to given stream in the form of a comma separated values file
 * 
 * @param cache     Cache structure whose stats to print
 * @param cycle     Cycle, used to calculate CPI
 * @param stream    Output stream to print to
 */
void io_utils__print_stats_csv (cache_t *cache, uint64_t cycle, FILE* stream);

/**
 * @brief Prints a message that the config of the cache structure(s) given
 * 
 * @param cache         The pointer to the top-level cache structure
 * @param stream        The output stream
 */
void io_utils__print_config (cache_t *cache, FILE* stream);

/**
 * @brief Loads test_params.ini if extant, creates it otherwise
 * 
 */
void io_utils__load_test_parameters (void);

/**
 *  @brief                  Takes in a trace file
 * 
 *  @param filename         Name of the trace file to read
 *  @param num_accesses     Output. Returns the length of the file in bytes
 *  @return                 Array of file contents
 */
uint8_t * io_utils__read_in_file (const char* filename, uint64_t *length);

/**
 * @brief           Parses the contents of a trace file and coverts to internal structure array
 * 
 * @param buffer    Pointer to the contents of the file
 * @param length    Lenght of buffer in bytes
 * @return          Array of instruction structs for internal use
 */
instruction_t * io_utils__parse_buffer (uint8_t *buffer, uint64_t length);
