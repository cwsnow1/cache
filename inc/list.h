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

/**
 * @brief           Allocates a double list and sets capacity, no elements
 * 
 * @param capacity  The maximum capacity for this list
 * @return          Pointer to the new list
 */
double_list_t *double_list__init_list(uint64_t capacity);

/**
 * @brief           Looks for a specific element in a list and removes it if extant
 * 
 * @param list      List to search in
 * @param element   Element to search for
 * @return true     if element was found & removed
 */
bool double_list__remove_element(double_list_t *list, double_list_element_t *element);

/**
 * @brief           Adds element to the end of a list if it has room
 * 
 * @param list      List to add element to
 * @param element   Element to add
 * @return true     if there was room & element was added
 */
bool double_list__add_element_to_tail(double_list_t *list, double_list_element_t *element);

/**
 * @brief           Adds element to head of a list if it has room
 * 
 * @param list      List to add element to
 * @param element   Element to add
 * @return true     if there was room & element was added
 */
bool double_list__push_element(double_list_t *list, double_list_element_t *element);

/**
 * @brief       Removes element from head of list and returns it
 * 
 * @param list  List to pop from
 * @return      Pointer to element if there is one, NULL otherwise
 */
double_list_element_t *double_list__pop_element(double_list_t *list);

/**
 * @brief       Walk the list & free memory for all elements in list and frees this list
 * 
 * @param list  List to free
 */
void double_list__free_list(double_list_t *list);

#define for_each_in_double_list(list)   double_list_element_t *element_i = list->head;                              \
                                        double_list_element_t *next_element = element_i ? element_i->next : NULL;   \
                                        for (uint64_t pool_index = element_i ? element_i->pool_index : 0;           \
                                        element_i != NULL;                                                          \
                                        element_i = next_element,                                                   \
                                        next_element = element_i ? element_i->next : NULL,                          \
                                        pool_index = element_i ? element_i->pool_index : 0)

