#include "list.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

DoubleList::DoubleList(uint64_t capacity) {
    pHead_ = nullptr;
    pTail_ = nullptr;
    count_ = 0;
    capacity_ = capacity;
}

DoubleList::~DoubleList() {
    for_each_in_double_list(this) {
        (void)poolIndex;
        delete elementIterator;
    }
}

bool DoubleList::RemoveElement(DoubleListElement* pElement) {
    for_each_in_double_list(this) {
        (void)poolIndex;
        if (elementIterator == pElement) {
            if (pElement->pPrevious_) {
                pElement->pPrevious_->pNext_ = pElement->pNext_;
            }
            if (pElement->pNext_) {
                pElement->pNext_->pPrevious_ = pElement->pPrevious_;
            }
            if (pHead_ == pElement) {
                pHead_ = pElement->pNext_;
            }
            if (pTail_ == pElement) {
                pTail_ = pElement->pPrevious_;
            }
            count_--;
            return true;
        }
    }
    return false;
}

bool DoubleList::AddElementToTail(DoubleListElement* pElement) {
    if (count_ == capacity_) {
        return false;
    }
    pElement->pNext_ = NULL;
    pElement->pPrevious_ = pTail_;
    if (pTail_) {
        pTail_->pNext_ = pElement;
    } else {
        pHead_ = pElement;
    }
    pTail_ = pElement;
    count_++;
    return true;
}

bool DoubleList::PushElement(DoubleListElement* pElement) {
    if (count_ == capacity_) {
        return false;
    }
    pElement->pPrevious_ = NULL;
    pElement->pNext_ = pHead_;
    if (pHead_) {
        pHead_->pPrevious_ = pElement;
    }
    pHead_ = pElement;
    count_++;
    return true;
}

DoubleListElement* DoubleList::PopElement() {
    DoubleListElement* head = pHead_;
    if (head == pTail_) {
        pTail_ = nullptr;
    }
    if (head) {
        if (head->pNext_) {
            head->pNext_->pPrevious_ = NULL;
        }
        pHead_ = head->pNext_;
        count_--;
    }
    return head;
}
