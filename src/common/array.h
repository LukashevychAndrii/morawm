#pragma once

#include <stdint.h>
#include <stdlib.h>

#define MORA_ARRAY_INITIAL_CAPACITY 8

static inline void* mora_array_reserve(
    void* entries,
    size_t* capacity,
    size_t required,
    size_t element_size
) {
    if (element_size == 0 || required > SIZE_MAX / element_size) {
        return NULL;
    }

    if (required <= *capacity) {
        return entries;
    }

    size_t next_capacity = *capacity ? *capacity : MORA_ARRAY_INITIAL_CAPACITY;

    while (next_capacity < required) {
        if (next_capacity > SIZE_MAX / 2) {
            next_capacity = required;
            break;
        }

        next_capacity *= 2;
    }

    if (next_capacity > SIZE_MAX / element_size) {
        next_capacity = required;
    }

    void* next_entries = realloc(entries, next_capacity * element_size);

    if (next_entries) {
        *capacity = next_capacity;
    }

    return next_entries;
}

