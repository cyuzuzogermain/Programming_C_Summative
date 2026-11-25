/**
 * @file processing.h
 * @brief Function pointers and callbacks for sales processing.
 *
 *  1. SalesOperation table    - an array of {name, function pointer}.
 *                               The menu indexes it at runtime and calls
 *                               the selected function through the pointer.
 *  2. TransactionPredicate    - "does this record match?" callbacks passed
 *                               to filter / count / sum / search functions.
 *  3. TransactionVisitor      - "do something with a match" callback.
 *  4. TransactionComparator   - ordering callback passed to the manual sort.
 */
#ifndef PROCESSING_H
#define PROCESSING_H

#include "sales.h"

/* ---- 1. Runtime-selectable sales operations ------------------------- */
typedef void (*SalesOperationFn)(const SalesDB *db);

typedef struct {
    const char       *name;
    SalesOperationFn  execute;
} SalesOperation;

extern const SalesOperation SALES_OPERATIONS[];
extern const int            SALES_OPERATION_COUNT;

/** run_sales_operation() - invokes @operation through its pointer. */
void run_sales_operation(const SalesDB *db, SalesOperationFn operation);

/* Underlying calculations (all safe on an empty store) */
double             calculate_total_revenue(const SalesDB *db);
int                calculate_average_value(const SalesDB *db, double *average);
const Transaction *find_highest_transaction(const SalesDB *db);
const Transaction *find_lowest_transaction(const SalesDB *db);
long               calculate_total_units(const SalesDB *db);

/* ---- 2/3. Callbacks ------------------------------------------------- */
typedef int  (*TransactionPredicate)(const Transaction *t, const void *context);
typedef void (*TransactionVisitor)(const Transaction *t);

/** Range used by is_amount_in_range(). */
typedef struct {
    double min;
    double max;
} AmountRange;

/* Predicates: the context type is noted for each one. */
int is_amount_above(const Transaction *t, const void *context);      /* double*      */
int is_amount_in_range(const Transaction *t, const void *context);   /* AmountRange* */
int is_product_containing(const Transaction *t, const void *context);/* const char*  */
int is_product_equal(const Transaction *t, const void *context);     /* const char*  */
int is_quantity_at_least(const Transaction *t, const void *context); /* int*         */
int is_on_date(const Transaction *t, const void *context);           /* const char*  */

/** Calls @visit for every transaction where @matches returns true. */
size_t for_each_matching(const SalesDB *db, TransactionPredicate matches,
                         const void *context, TransactionVisitor visit);
/** Counts the transactions where @matches returns true. */
size_t count_matching(const SalesDB *db, TransactionPredicate matches, const void *context);
/** Sums the totals of the transactions where @matches returns true. */
double sum_matching(const SalesDB *db, TransactionPredicate matches, const void *context);

/** Visitor that prints one table row. */
void print_transaction_visitor(const Transaction *t);

/* ---- 4. Sorting ------------------------------------------------------ */
typedef int (*TransactionComparator)(const Transaction *a, const Transaction *b);

int compare_by_id(const Transaction *a, const Transaction *b);
int compare_by_product(const Transaction *a, const Transaction *b);
int compare_by_total_ascending(const Transaction *a, const Transaction *b);
int compare_by_total_descending(const Transaction *a, const Transaction *b);

/** Manual insertion sort that orders records using @compare. */
void sort_transactions(SalesDB *db, TransactionComparator compare);

/* ---- Reports --------------------------------------------------------- */
void generate_sales_report(const SalesDB *db, const char *product);

#endif /* PROCESSING_H */
