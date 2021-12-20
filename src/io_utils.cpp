#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>

#include "default_test_params.h"
#include "cache.h"
#include "io_utils.h"
#include "test_params.h"

namespace io_utils {

const char params_filename[] = "./test_params.ini";

TestParams *loadTestParameters () {
    FILE *params_f = fopen(params_filename, "r");
    // If file does not exist, generate a default
    if (params_f == NULL) {
        return new TestParams();
    }
    uint8_t num_cache_levels;
    uint64_t min_block_size;
    uint64_t max_block_size;
    uint64_t min_cache_size;
    uint64_t max_cache_size;
    uint8_t min_associativity;
    uint8_t max_associativity;
    int32_t max_num_threads;
    // File exists, read it in
    assert(fscanf(params_f, "NUM_CACHE_LEVELS=%hhu\n",    &num_cache_levels));
    assert(fscanf(params_f, "MIN_BLOCK_SIZE=%lu\n",       &min_block_size));
    assert(fscanf(params_f, "MAX_BLOCK_SIZE=%lu\n",       &max_block_size));
    assert(fscanf(params_f, "MIN_CACHE_SIZE=%lu\n",       &min_cache_size));
    assert(fscanf(params_f, "MAX_CACHE_SIZE=%lu\n",       &max_cache_size));
    assert(fscanf(params_f, "MIN_ASSOCIATIVITY=%hhu\n",   &min_associativity));
    assert(fscanf(params_f, "MAX_ASSOCIATIVITY=%hhu\n",   &max_associativity));
    assert(fscanf(params_f, "MAX_NUM_THREADS=%d\n",       &max_num_threads));
    fclose(params_f);
    return new TestParams(num_cache_levels, min_block_size, max_block_size,
        min_cache_size, max_cache_size, min_associativity, max_associativity, max_num_threads);
}

uint8_t * readInFile (const char* filename, uint64_t *length) {
    assert(length);

    uint8_t *buffer = NULL;
    size_t m;

    FILE* f = fopen(filename, "r");
    if (f == NULL) {
        fprintf(stderr, "Unable to open file %s\n", filename);
        goto error;
    }

    if (fseek(f, 0, SEEK_END)) {
        fprintf(stderr, "Error in file seek of %s\n", filename);
        goto error;
    }
    m = static_cast<size_t>(ftell(f));
    if (m < 0) goto error;
    buffer = (uint8_t *) malloc(m);
    if (buffer == NULL)                 goto error;
    if (fseek(f, 0, SEEK_SET))          goto error;
    if (fread(buffer, 1, m, f) != m)    goto error;
    fclose(f);

    *length = (uint64_t) m;
    return buffer;

error:
    if (f) fclose(f);
    if (buffer) free(buffer);
    fprintf(stderr, "Error in reading file %s\n", filename);
    exit(1);
}

/**
 * @brief       Parses a single line of the trace file
 * 
 * @param line  Pointer within the buffer to the start of a line
 * @param addr  Out. Parsed address value
 * @param rw    Out. Access type, read or write
 */
static void parse_line (uint8_t *line, uint64_t *addr, Access *rw) {
    line += FIRST_ADDRESS_LENGTH_IN_BYTES;
    char rw_c = *line;
    *rw = (rw_c == 'R') ? READ : WRITE;
    line += RW_LENGHT_IN_BYTES + AFTER_RW_LENGTH_IN_BYTES;
    char* end_ptr;
    *addr = strtoll((char*) line, &end_ptr, 16);
    assert(*end_ptr == '\n');
}

Instruction * parseBuffer (uint8_t *buffer, uint64_t length) {
    assert(buffer);
    uint8_t *buffer_start = buffer;
    uint64_t num_lines = length / FILE_LINE_LENGTH_IN_BYTES;
    Instruction *accesses = new Instruction[num_lines];
    for (uint64_t i = 0; i < num_lines; i++, buffer += FILE_LINE_LENGTH_IN_BYTES) {
        parse_line(buffer, &accesses[i].ptr, &accesses[i].rw);
    }
    delete buffer_start;
    return accesses;
}

} // namespace io_utils