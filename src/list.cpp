#include "list.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

DoubleList::DoubleList(uint64_t capacity) {
    head_ = NULL;
    tail_ = NULL;
    count_ = 0;
    capacity_ = capacity;
}

DoubleList::~DoubleList() {
    for_each_in_double_list(this) {
        (void)pool_index;
        delete element_i;
    }
}

bool DoubleList::RemoveElement(DoubleListElement *element) {
    for_each_in_double_list(this) {
        (void)pool_index;
        if (element_i == element) {
            if (element->previous_) {
                element->previous_->next_ = element->next_;
            }
            if (element->next_) {
                element->next_->previous_ = element->previous_;
            }
            if (head_ == element) {
                head_ = element->next_;
            }
            if (tail_ == element) {
                tail_ = element->previous_;
            }
            count_--;
            return true;
        }
    }
    return false;
}

bool DoubleList::AddElementToTail(DoubleListElement *element) {
    if (count_ == capacity_) {
        return false;
    }
    element->next_ = NULL;
    element->previous_ = tail_;
    if (tail_) {
        tail_->next_ = element;
    } else {
        head_ = element;
    }
    tail_ = element;
    count_++;
    return true;
}

bool DoubleList::PushElement(DoubleListElement *element) {
    if (count_ == capacity_) {
        return false;
    }
    element->previous_ = NULL;
    element->next_ = head_;
    if (head_) {
        head_->previous_ = element;
    }
    head_ = element;
    count_++;
    return true;
}

DoubleListElement *DoubleList::PopElement() {
    DoubleListElement *head = head_;
    if (head) {
        if (head->next_) {
            head->next_->previous_ = NULL;
        }
        head_ = head->next_;
        count_--;
    }
    return head;
}
