#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>

#include "list.h"

double_list_t *double_list__init_list(uint64_t capacity) {
    double_list_t *list = (double_list_t*) malloc(sizeof(double_list_t));
    assert(list);
    list->head = NULL;
    list->tail = NULL;
    list->count = 0;
    list->capacity = capacity;
    return list;
}

void double_list__free_list(double_list_t *list) {
    if (list) {
        for_each_in_double_list(list) {
            (void) pool_index;
            free(element_i);
        }
        free(list);
    }
}

bool double_list__remove_element(double_list_t *list, double_list_element_t *element) {
    for_each_in_double_list(list) {
        (void) pool_index;
        if (element_i == element) {
            if (element->prev) {
                element->prev->next = element->next;
            }
            if (element->next) {
                element->next->prev = element->prev;
            }
            if (list->head == element) {
                list->head = element->next;
            }
            if (list->tail == element) {
                list->tail = element->prev;
            }
            list->count--;
            return true;
        }
    }
    return false;
}

bool double_list__add_element_to_tail(double_list_t *list, double_list_element_t *element) {
    if (list->count == list->capacity) {
        return false;
    }
    element->next = NULL;
    element->prev = list->tail;
    if (list->tail) {
        list->tail->next = element;
    }
    else {
        list->head = element;
    }
    list->tail = element;
    list->count++;
    return true;
}

bool double_list__push_element(double_list_t *list, double_list_element_t *element) {
    if (list->count == list->capacity) {
        return false;
    }
    element->prev = NULL;
    element->next = list->head;
    if (list->head) {
        list->head->prev = element;
    }
    list->head = element;
    list->count++;
    return true;
}

double_list_element_t *double_list__pop_element(double_list_t *list) {
    double_list_element_t *head = list->head;
    if (head) {
        if (head->next) {
            head->next->prev = NULL;
        }
        list->head = head->next;
        list->count--;
    }
    return head;
}
