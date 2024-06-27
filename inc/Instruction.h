#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

enum access_t {
    READ,
    WRITE,
    NEITHER
};

struct Instruction {
    uint64_t ptr;
    access_t rw;
    size_t dataAccessIndex = invalidIndex;

    static constexpr size_t invalidIndex = SIZE_MAX;

    Instruction() = default;
    Instruction(uint64_t ptr, access_t rw) : ptr(ptr), rw(rw) {
    }
};

struct MemoryAccesses {
    std::vector<Instruction> dataAccesses_;
    std::vector<Instruction> instructionAccesses_;
};
