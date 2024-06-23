#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

typedef enum access_e {
    READ,
    WRITE,
    NEITHER
} access_t;

struct Instruction {
    uint64_t ptr;
    access_t rw;
    size_t dataAccessIndex;

    static constexpr size_t invalidIndex = SIZE_MAX;

    Instruction() {
    }
    Instruction(uint64_t ptr, access_t rw) {
        this->ptr = ptr;
        this->rw = rw;
        dataAccessIndex = invalidIndex;
    }
};

struct MemoryAccesses {
    std::vector<Instruction> dataAccesses_;
    std::vector<Instruction> instructionAccesses_;

    MemoryAccesses() {
        dataAccesses_ = std::vector<Instruction>();
        instructionAccesses_ = std::vector<Instruction>();
    }

    ~MemoryAccesses() {
    }
};
