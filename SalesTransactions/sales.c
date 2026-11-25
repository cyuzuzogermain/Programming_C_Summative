/**
 * @file sales.c
 * @brief Dynamic transaction store: allocation, growth, CRUD, display.
 */
#include "sales.h"
#include "input.h"     /* is_valid_date() */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =====================================================================
 *  String helpers
 * ===================================================================== */

int compare_ci(const char *a, const char *b) {
    while (*a != '\0' && *b != '\0') {
        int diff = tolower((unsigned char)*a) - tolower((unsigned char)*b);
        if (diff != 0) return diff;
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

int contains_ci(const char *haystack, const char *needle) {
    size_t hlen = strlen(haystack), nlen = strlen(needle);
    if (nlen == 0) return 1;
    for (size_t start = 0; start + nlen <= hlen; start++) {
        size_t i = 0;
        while (i < nlen && tolower((unsigned char)haystack[start + i]) ==
                           tolower((unsigned char)needle[i])) {
            i++;
        }
        if (i == nlen) return 1;
    }
    return 0;
}

/* =====================================================================
 *  Lifetime
 * ===================================================================== */

/** sales_init() - allocates the initial block of INITIAL_CAPACITY records. */
SalesStatus sales_init(SalesDB *db) {
    db->items = malloc(INITIAL_CAPACITY * sizeof *db->items);
    db->count = 0;
    if (db->items == NULL) {
        db->capacity = 0;
        return SALES_NO_MEMORY;
    }
    db->capacity = INITIAL_CAPACITY;
    return SALES_OK;
}

void sales_free(SalesDB *db) {
    free(db->items);
    db->items    = NULL;
    db->count    = 0;
    db->capacity = 0;
}

const char *sales_status_message(SalesStatus status) {
    switch (status) {
        case SALES_OK:           return "Success";
        case SALES_DUPLICATE_ID: return "A transaction with this ID already exists";
        case SALES_NOT_FOUND:    return "Transaction not found";
        case SALES_INVALID:      return "Invalid transaction data";
        case SALES_NO_MEMORY:    return "Memory allocation failed";
        case SALES_FILE_MISSING: return "Data file not found";
        case SALES_FILE_ERROR:   return "File access error";
    }
    return "Unknown error";
}

/**
 * ensure_capacity() - doubles the array with realloc() when it is full.
 * On failure the existing data is untouched.
 */
static SalesStatus ensure_capacity(SalesDB *db) {
    if (db->count < db->capacity) {
        return SALES_OK;
    }
    size_t newCapacity = (db->capacity == 0) ? INITIAL_CAPACITY : db->capacity * 2;
    Transaction *grown = realloc(db->items, newCapacity * sizeof *grown);
    if (grown == NULL) {
        return SALES_NO_MEMORY;
    }
    db->items    = grown;
    db->capacity = newCapacity;
    return SALES_OK;
}

/* =====================================================================
 *  Validation and CRUD
 * ===================================================================== */

void compute_total(Transaction *t) {
    t->total = t->quantity * t->unitPrice;
}

/** validate_transaction() - every field must be in range. */
SalesStatus validate_transaction(const Transaction *t) {
    if (t->id < MIN_TRANSACTION_ID || t->id > MAX_TRANSACTION_ID) return SALES_INVALID;
    if (t->product[0] == '\0' || strchr(t->product, '|') != NULL)  return SALES_INVALID;
    if (t->quantity <= 0 || t->quantity > MAX_QUANTITY)             return SALES_INVALID;
    if (!(t->unitPrice >= MIN_UNIT_PRICE && t->unitPrice <= MAX_UNIT_PRICE)) return SALES_INVALID;
    if (!is_valid_date(t->date))                                    return SALES_INVALID;
    return SALES_OK;
}

/** find_transaction() - linear search by ID; NULL if absent. */
Transaction *find_transaction(SalesDB *db, int id) {
    for (size_t i = 0; i < db->count; i++) {
        if (db->items[i].id == id) {
            return &db->items[i];
        }
    }
    return NULL;
}

/** add_transaction() - validates, rejects duplicate IDs, appends. */
SalesStatus add_transaction(SalesDB *db, const Transaction *t) {
    if (validate_transaction(t) != SALES_OK) return SALES_INVALID;
    if (find_transaction(db, t->id) != NULL) return SALES_DUPLICATE_ID;

    SalesStatus status = ensure_capacity(db);
    if (status != SALES_OK) return status;

    Transaction *slot = &db->items[db->count];
    *slot = *t;
    compute_total(slot);                 /* never trust a supplied total */
    db->count++;
    return SALES_OK;
}

/** update_transaction() - replaces the fields of transaction @id. */
SalesStatus update_transaction(SalesDB *db, int id, const Transaction *updated) {
    Transaction *existing = find_transaction(db, id);
    if (existing == NULL) return SALES_NOT_FOUND;
    if (updated->id != id) return SALES_INVALID;       /* IDs are immutable */
    if (validate_transaction(updated) != SALES_OK) return SALES_INVALID;

    *existing = *updated;
    compute_total(existing);
    return SALES_OK;
}

/**
 * delete_transaction() - removes transaction @id by shifting the rest
 * down; the block is shrunk with realloc() once it is mostly empty.
 */
SalesStatus delete_transaction(SalesDB *db, int id) {
    Transaction *target = find_transaction(db, id);
    if (target == NULL) return SALES_NOT_FOUND;

    size_t index = (size_t)(target - db->items);
    for (size_t i = index; i + 1 < db->count; i++) {
        db->items[i] = db->items[i + 1];
    }
    db->count--;

    if (db->capacity > INITIAL_CAPACITY && db->count < db->capacity / 4) {
        size_t newCapacity = db->capacity / 2;
        Transaction *shrunk = realloc(db->items, newCapacity * sizeof *shrunk);
        if (shrunk != NULL) {               /* failure is harmless */
            db->items    = shrunk;
            db->capacity = newCapacity;
        }
    }
    return SALES_OK;
}

/* =====================================================================
 *  Display
 * ===================================================================== */

void print_transaction_header(void) {
    printf("%-10s %-24s %8s %12s %14s  %-10s\n",
           "ID", "Product", "Qty", "Unit Price", "Total", "Date");
    printf("-----------------------------------------------------------------------------------\n");
}

void print_transaction(const Transaction *t) {
    printf("%-10d %-24.24s %8d %12.2f %14.2f  %-10s\n",
           t->id, t->product, t->quantity, t->unitPrice, t->total, t->date);
}

void display_all_transactions(const SalesDB *db) {
    if (db->count == 0) {
        printf("No transactions recorded.\n");
        return;
    }
    print_transaction_header();
    for (size_t i = 0; i < db->count; i++) {
        print_transaction(&db->items[i]);
    }
    printf("%zu transaction(s)  [memory: %zu of %zu slots used]\n",
           db->count, db->count, db->capacity);
}
