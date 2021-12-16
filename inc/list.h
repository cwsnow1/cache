
#pragma once

typedef struct double_list_element_s {
    struct double_list_element_s *prev;
    struct double_list_element_s *next;
    uint64_t pool_index;
} double_list_element_t;

typedef struct double_list_s {
    double_list_element_t *head;
    double_list_element_t *tail;
    uint64_t count;
    uint64_t capacity;
} double_list_t;

double_list_t *double_list__init_list(uint64_t capacity);

bool double_list__remove_element(double_list_t *list, double_list_element_t *element);

bool double_list__add_element_to_tail(double_list_t *list, double_list_element_t *element);

bool double_list__push_element(double_list_t *list, double_list_element_t *element);

double_list_element_t *double_list__pop_element(double_list_t *list);

#define for_each_in_double_list(list)   double_list_element_t *element_i = list->head;                              \
                                        double_list_element_t *next_element = element_i ? element_i->next : NULL;   \
                                        for (uint64_t pool_index = element_i ? element_i->pool_index : 0;           \
                                        element_i != NULL;                                                          \
                                        element_i = next_element,                                                   \
                                        next_element = element_i ? element_i->next : NULL,                          \
                                        pool_index = element_i ? element_i->pool_index : 0)

