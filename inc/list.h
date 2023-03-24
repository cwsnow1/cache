#pragma once

#include <stdint.h>

struct DoubleListElement {
    DoubleListElement *previous_;
    DoubleListElement *next_;
    uint64_t pool_index_;
};

class DoubleList {
   public:
    DoubleList(uint64_t capacity);

    ~DoubleList();

    /**
     * @brief           Looks for a specific element in a list and removes it if
     * extant
     *
     * @param list      List to search in
     * @param element   Element to search for
     * @return true     if element was found & removed
     */
    bool RemoveElement(DoubleListElement *element);

    /**
     * @brief           Adds element to the end of a list if it has room
     *
     * @param list      List to add element to
     * @param element   Element to add
     * @return true     if there was room & element was added
     */
    bool AddElementToTail(DoubleListElement *element);

    /**
     * @brief           Adds element to head of a list if it has room
     *
     * @param list      List to add element to
     * @param element   Element to add
     * @return true     if there was room & element was added
     */
    bool PushElement(DoubleListElement *element);

    /**
     * @brief       Removes element from head of list and returns it
     *
     * @param list  List to pop from
     * @return      Pointer to element if there is one, NULL otherwise
     */
    DoubleListElement *PopElement();

    DoubleListElement *PeekHead() { return head_; }

   private:
    DoubleListElement *head_;
    DoubleListElement *tail_;
    uint64_t count_;
    uint64_t capacity_;
};

#define for_each_in_double_list(list)   DoubleListElement *element_i = list->PeekHead();                        \
                                        DoubleListElement *next_element = element_i ? element_i->next_ : NULL;  \
                                        for (uint64_t pool_index = element_i ? element_i->pool_index_ : 0;      \
                                        element_i != NULL;                                                      \
                                        element_i = next_element,                                               \
                                        next_element = element_i ? element_i->next_ : NULL,                     \
                                        pool_index = element_i ? element_i->pool_index_ : 0)

