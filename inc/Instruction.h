#pragma once

#include <stdint.h>

typedef enum access_e { READ, WRITE } access_t;

struct Instruction {
    uint64_t ptr;
    access_t rw;

    Instruction() {}
    Instruction(uint64_t ptr, access_t rw) {
        this->ptr = ptr;
        this->rw = rw;
    }
};

struct MemoryAccesses {
    Instruction *pDataAccesses;
    Instruction *pInstructionAccesses;
    uint64_t length;

    ~MemoryAccesses() {
        delete[] pDataAccesses;
        delete[] pInstructionAccesses;
    }
};
