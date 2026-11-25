/**
 * @file sales.h
 * @brief Transaction record, the dynamic transaction store, and the
 *        basic CRUD / display operations.
 */
#ifndef SALES_H
#define SALES_H

#include <stddef.h>

#define MAX_PRODUCT_LEN      50
#define DATE_LEN             11          /* "YYYY-MM-DD" + '\0'          */
#define INITIAL_CAPACITY     4           /* first malloc() size          */
#define MIN_TRANSACTION_ID   1
#define MAX_TRANSACTION_ID   99999999
#define MAX_QUANTITY         1000000
#define MIN_UNIT_PRICE       0.01
#define MAX_UNIT_PRICE       10000000.0

/** One sales transaction. */
typedef struct {
    int    id;                           /* unique                       */
    char   product[MAX_PRODUCT_LEN];
    int    quantity;                     /* > 0                          */
    double unitPrice;                    /* > 0                          */
    double total;                        /* quantity * unitPrice         */
    char   date[DATE_LEN];               /* YYYY-MM-DD                   */
} Transaction;

/**
 * Dynamic array of transactions. `capacity` slots are allocated; the
 * first `count` are in use. The array doubles with realloc() when full.
 */
typedef struct {
    Transaction *items;
    size_t       count;
    size_t       capacity;
} SalesDB;

typedef enum {
    SALES_OK = 0,
    SALES_DUPLICATE_ID,
    SALES_NOT_FOUND,
    SALES_INVALID,
    SALES_NO_MEMORY,
    SALES_FILE_MISSING,
    SALES_FILE_ERROR
} SalesStatus;

/* Lifetime */
SalesStatus sales_init(SalesDB *db);
void        sales_free(SalesDB *db);
const char *sales_status_message(SalesStatus status);

/* Validation and CRUD */
SalesStatus validate_transaction(const Transaction *t);
void        compute_total(Transaction *t);
SalesStatus add_transaction(SalesDB *db, const Transaction *t);
SalesStatus update_transaction(SalesDB *db, int id, const Transaction *updated);
SalesStatus delete_transaction(SalesDB *db, int id);
Transaction *find_transaction(SalesDB *db, int id);

/* Display */
void print_transaction_header(void);
void print_transaction(const Transaction *t);
void display_all_transactions(const SalesDB *db);

/* Case-insensitive helpers (manual implementations) */
int compare_ci(const char *a, const char *b);
int contains_ci(const char *haystack, const char *needle);

#endif /* SALES_H */
