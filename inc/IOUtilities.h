#pragma once
#include <stdint.h>
#include <stdio.h>

#include "Cache.h"

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
#define FILE_LINE_LENGTH_IN_BYTES (33)
#define FIRST_ADDRESS_LENGTH_IN_BYTES (16)
#define RW_LENGHT_IN_BYTES (1)
#define AFTER_RW_LENGTH_IN_BYTES (3)

class IOUtilities {
public:
    /**
     * @brief           Prints collected statistics to given stream
     *
     * @param memory    Memory structure whose stats to print
     * @param cycle     Cycle, used to calculate CPI
     * @param stream    Output stream to print to
     */
    static void PrintStatistics(Memory *memory, uint64_t cycle, FILE *stream);

    /**
     * @brief           Prints collected statistics to given stream in the form
     * of a comma separated values file
     *
     * @param memory    Memory structure whose stats to print
     * @param cycle     Cycle, used to calculate CPI
     * @param stream    Output stream to print to
     */
    static void PrintStatisticsCSV(Memory *memory, uint64_t cycle, FILE *stream);

    /**
     * @brief Prints a message that the config of the cache structure(s) given
     *
     * @param memory        The pointer to the top-level cache structure
     * @param stream        The output stream
     */
    static void PrintConfiguration(Memory *memory, FILE *stream);

    /**
     * @brief Loads test_params.ini if extant, creates it otherwise
     *
     */
    static void LoadTestParameters();

    /**
     *  @brief                  Takes in a trace file
     *
     *  @param filename         Name of the trace file to read
     *  @param num_accesses     Output. Returns the length of the file in bytes
     *  @return                 Array of file contents
     */
    static uint8_t *ReadInFile(const char *filename, uint64_t *length);

    /**
     * @brief           Parses the contents of a trace file and coverts to
     * internal structure array
     *
     * @param buffer    Pointer to the contents of the file
     * @param length    Lenght of buffer in bytes
     * @return          Array of instruction structs for internal use
     */
    static Instruction *ParseBuffer(uint8_t *buffer, uint64_t length);

private:

    /**
     * @brief Verifies the global test parameters struct is valid
     * 
     */
    static void verify_test_params ();

    /**
     * @brief       Parses a single line of the trace file
     * 
     * @param line  Pointer within the buffer to the start of a line
     * @param address  Out. Parsed address value
     * @param rw    Out. Access type, read or write
     */
    static void parseLine (uint8_t *line, uint64_t *address, access_t *rw);

};
