#include <stdio.h>
#include <stdint.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>

#if (SIM_TRACE == 0)
#undef SIM_TRACE
#define SIM_TRACE (1)
#endif

#include "Cache.h"
#include "SimTracer.h"
#include "sim_trace_decoder.h"

constexpr uint64_t kMaxNumberOfThreads = (MEMORY_USAGE_LIMIT / kSimTraceBufferSizeInBytes);
constexpr int kOutputFilesizeMaxLength = 100;

static uint32_t bufferSize;
static uint16_t numberOfThreads;
static uint16_t numberOfConfigs;
static uint32_t *pOldestIndices;
static uint8_t numberOfCacheLevels;
static Configuration *pConfigs;
static uint8_t **pBuffers;

/**
 * File format is as follows:
 * uint32_t buffer size in bytes
 * uint16_t number of configs
 * uint8_t numberOfCacheLevels
 * Config entries
 * 
 * Config entry is as follows:
 * uint32_t buffer append point offset
 * config of each cache
 * buffer
 */

static FILE *readInFileHeader (const char *filename) {
    FILE *pFile = fopen(filename, "rb");
    assert(pFile);

    // uint32_t, buffer size in bytes
    assert(fread(&bufferSize, sizeof(uint32_t), 1, pFile));
    assert(bufferSize);

    // uint16_t, number of configs
    assert(fread(&numberOfConfigs, sizeof(uint16_t), 1, pFile));
    assert(numberOfConfigs);

    // uint8_t numberOfCacheLevels
    assert(fread(&numberOfCacheLevels, sizeof(uint8_t), 1, pFile) == 1);

    pConfigs = new Configuration[numberOfCacheLevels];
    assert(numberOfCacheLevels);

    numberOfThreads = numberOfConfigs > kMaxNumberOfThreads ? kMaxNumberOfThreads : numberOfConfigs;

    // buffers
    pBuffers = new uint8_t*[numberOfThreads];
    assert(pBuffers);
    for (uint64_t threadId = 0; threadId < numberOfThreads; threadId++) {
        pBuffers[threadId] =  new uint8_t[bufferSize];
        assert(pBuffers[threadId]);
    }
    return pFile;
}

int main (int argc, char* argv[]) {
    if (argc < 3) {
        printf("Please provide a .bin file to decode and an output filename\n");
        exit(1);
    }

    FILE* pInputFile = readInFileHeader(argv[1]);

    // Decode & write trace files
    FILE *pOutputFile = nullptr;
    char outputFilenameBuffer[kOutputFilesizeMaxLength];
    int outputFilenameLength = sprintf(outputFilenameBuffer, "%s", argv[2]);
    assert(outputFilenameLength > 0);
    uint16_t configIndex = 0;
    for (uint16_t threadId = 0; threadId < numberOfThreads && configIndex < numberOfConfigs; threadId++, configIndex++) {

        uint32_t bufferAppendPointOffset;
        assert(fread(&bufferAppendPointOffset, sizeof(uint32_t), 1, pInputFile) == 1);
        assert(fread(&pConfigs[0], sizeof(Configuration), numberOfCacheLevels, pInputFile) == numberOfCacheLevels);
        assert(fread(pBuffers[threadId], sizeof(uint8_t), bufferSize, pInputFile) == bufferSize);

        uint8_t *pLastEntry = pBuffers[threadId] + SIM_TRACE_LAST_ENTRY_OFFSET;

        char *pOutputFilenameBegin = outputFilenameBuffer + outputFilenameLength;
        for (uint8_t j = 0; j < numberOfCacheLevels; j++) {
            int bytesWritten = sprintf(pOutputFilenameBegin, "_%" PRIu64 "_%" PRIu64 "_%" PRIu64 "", pConfigs[j].cacheSize, pConfigs[j].blockSize, pConfigs[j].associativity);
            pOutputFilenameBegin += bytesWritten;
        }
        sprintf(pOutputFilenameBegin, ".txt");
        pOutputFile = fopen(outputFilenameBuffer, "w");
        assert(pOutputFile);
        fprintf(pOutputFile, "Cycle\t\tCache level\tMessage\n");
        fprintf(pOutputFile, "=============================================================\n");

        uint8_t *pBuffer = pBuffers[threadId] + bufferAppendPointOffset;
        bool wrapped = false;
        // Find sync pattern
        uint16_t bytesLost = 0;
        for (; *(reinterpret_cast<sync_pattern_t*>(pBuffer)) != kSyncPattern; pBuffer++, bytesLost++) {
            if (pBuffer >= pLastEntry) {
                pBuffer = pBuffers[threadId];
                wrapped = true;
            }
        }
        uint64_t entryCounter = 0;
        uint64_t cycle = 0;
        fprintf(pOutputFile, "%u bytes lost before first sync pattern found\n", bytesLost);
        pBuffer += sizeof(sync_pattern_t);
        while (true) {
            if (wrapped && pBuffer >= pBuffers[threadId] + bufferAppendPointOffset) {
                break;
            }

            // Skip sync pattern
            if (entryCounter == SIM_TRACE_SYNC_INTERVAL) {
                assert(*(reinterpret_cast<sync_pattern_t*>(pBuffer)) == kSyncPattern);
                pBuffer += sizeof(sync_pattern_t);
                entryCounter = 0;
            }
            entryCounter++;

            SimTraceEntry entry = *(reinterpret_cast<SimTraceEntry*> (pBuffer));
            assert(entry.trace_entry_id < kNumberOfSimTraceEntries);
            pBuffer += sizeof(SimTraceEntry);
            assert(pBuffer < pBuffers[threadId] + bufferSize);
            cycle += entry.cycle_offset;
            fprintf(pOutputFile, "%012" PRIu64 "\t%u\t\t", cycle, entry.cache_level);

            int numberOfArguments = kNumberOfArgumentsInSimTraceEntry[entry.trace_entry_id];
            switch (numberOfArguments)
            {
            case 0:
                fprintf(pOutputFile, "%s", simTraceEntryDefinitions[entry.trace_entry_id]);
                break;
            case 1:
                fprintf(pOutputFile, simTraceEntryDefinitions[entry.trace_entry_id], *(reinterpret_cast<sim_trace_entry_data_t*>(pBuffer)));
                break;
            case 2:
                fprintf(pOutputFile, simTraceEntryDefinitions[entry.trace_entry_id],
                    *(reinterpret_cast<sim_trace_entry_data_t*>(pBuffer)), *(reinterpret_cast<sim_trace_entry_data_t*>(pBuffer) + 1));
                break;
            case 3:
                fprintf(pOutputFile, simTraceEntryDefinitions[entry.trace_entry_id],
                    *(reinterpret_cast<sim_trace_entry_data_t*>(pBuffer)), *(reinterpret_cast<sim_trace_entry_data_t*>(pBuffer) + 1), *(reinterpret_cast<sim_trace_entry_data_t*>(pBuffer) + 2));
                break;
            case 4:
                fprintf(pOutputFile, simTraceEntryDefinitions[entry.trace_entry_id],
                    *(reinterpret_cast<sim_trace_entry_data_t*>(pBuffer)), *(reinterpret_cast<sim_trace_entry_data_t*>(pBuffer) + 1),
                    *(reinterpret_cast<sim_trace_entry_data_t*>(pBuffer) + 2), *(reinterpret_cast<sim_trace_entry_data_t*>(pBuffer) + 3));
                break;
            case 5:
                fprintf(pOutputFile, simTraceEntryDefinitions[entry.trace_entry_id],
                    *(reinterpret_cast<sim_trace_entry_data_t*>(pBuffer)), *(reinterpret_cast<sim_trace_entry_data_t*>(pBuffer) + 1),
                    *(reinterpret_cast<sim_trace_entry_data_t*>(pBuffer) + 2), *(reinterpret_cast<sim_trace_entry_data_t*>(pBuffer) + 3), *(reinterpret_cast<sim_trace_entry_data_t*>(pBuffer) + 4));
                break;
            default:
                fprintf(stderr, "MAX_NUM_SIM_TRACE_VALUES is too low\n");
                break;
            }
            pBuffer += sizeof(sim_trace_entry_data_t) * numberOfArguments;
            if (pBuffer >= pLastEntry) {
                pBuffer = pBuffers[threadId];
                wrapped = true;
            }
        }
        fclose(pOutputFile);
    }
    delete[] pOldestIndices;
    for (uint32_t threadId = 0; threadId; threadId++) {
        delete[] pBuffers[threadId];
    }
    delete[] pConfigs;
    delete[] pBuffers;
    return 0;
}
