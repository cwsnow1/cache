#pragma once

#include <stdint.h>

struct DoubleListElement {
    DoubleListElement* pPrevious_;
    DoubleListElement* pNext_;
    uint64_t poolIndex_;
};

class DoubleList {
  public:
    /**
     * @brief Construct a new Double List object
     *
     * @param capacity Maximum capacity of this list
     */
    DoubleList(uint64_t capacity);

    DoubleList(const DoubleList&) = delete;

    DoubleList operator=(const DoubleList&) = delete;

    /**
     * @brief Destroy the Double List object
     *
     */
    ~DoubleList();

    /**
     * @brief           Looks for a specific element in a list and removes it if extant
     *
     * @param pElement  Element to search for
     * @return true     if element was found & removed
     */
    bool RemoveElement(DoubleListElement* pElement);

    /**
     * @brief           Adds element to the end of a list if it has room
     *
     * @param pElement  Element to add
     * @return true     if there was room & element was added
     */
    bool AddElementToTail(DoubleListElement* pElement);

    /**
     * @brief           Adds element to head of a list if it has room
     *
     * @param pElement  Element to add
     * @return true     if there was room & element was added
     */
    bool PushElement(DoubleListElement* pElement);

    /**
     * @brief       Removes element from head of list and returns it
     *
     * @return      Pointer to element if there is one, nullptr otherwise
     */
    DoubleListElement* PopElement();

    DoubleListElement* PeekHead() {
        return pHead_;
    }

    inline uint64_t GetCount() {
        return count_;
    }

    inline uint64_t GetCapacity() {
        return capacity_;
    }

  private:
    DoubleListElement* pHead_;
    DoubleListElement* pTail_;
    uint64_t count_;
    uint64_t capacity_;
};

#define for_each_in_double_list(list)                                                                                  \
    DoubleListElement* elementIterator = list->PeekHead();                                                             \
    DoubleListElement* nextElement = elementIterator ? elementIterator->pNext_ : nullptr;                              \
    for (uint64_t poolIndex = elementIterator ? elementIterator->poolIndex_ : 0; elementIterator != nullptr;           \
         elementIterator = nextElement, nextElement = elementIterator ? elementIterator->pNext_ : nullptr,             \
                  poolIndex = elementIterator ? elementIterator->poolIndex_ : 0)
