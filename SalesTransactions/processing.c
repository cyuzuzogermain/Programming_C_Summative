/**
 * @file processing.c
 * @brief Function-pointer operations, callbacks, manual sorting, reports.
 */
#include "processing.h"

#include <stdio.h>
#include <string.h>

/* =====================================================================
 *  Calculations
 * ===================================================================== */

double calculate_total_revenue(const SalesDB *db) {
    double revenue = 0.0;
    for (size_t i = 0; i < db->count; i++) {
        revenue += db->items[i].total;
    }
    return revenue;
}

/** Return: 0 when there are no transactions (avoids division by zero). */
int calculate_average_value(const SalesDB *db, double *average) {
    if (db->count == 0) {
        *average = 0.0;
        return 0;
    }
    *average = calculate_total_revenue(db) / (double)db->count;
    return 1;
}

const Transaction *find_highest_transaction(const SalesDB *db) {
    const Transaction *best = NULL;
    for (size_t i = 0; i < db->count; i++) {
        if (best == NULL || db->items[i].total > best->total) {
            best = &db->items[i];
        }
    }
    return best;
}

const Transaction *find_lowest_transaction(const SalesDB *db) {
    const Transaction *lowest = NULL;
    for (size_t i = 0; i < db->count; i++) {
        if (lowest == NULL || db->items[i].total < lowest->total) {
            lowest = &db->items[i];
        }
    }
    return lowest;
}

long calculate_total_units(const SalesDB *db) {
    long units = 0;
    for (size_t i = 0; i < db->count; i++) {
        units += db->items[i].quantity;
    }
    return units;
}

/* =====================================================================
 *  1. Operations selected at runtime through function pointers
 * ===================================================================== */

static void op_total_revenue(const SalesDB *db) {
    printf("Total revenue: %.2f\n", calculate_total_revenue(db));
}

static void op_average_value(const SalesDB *db) {
    double average;
    if (calculate_average_value(db, &average)) {
        printf("Average transaction value: %.2f (over %zu transactions)\n", average, db->count);
    } else {
        printf("Average transaction value: N/A (no transactions)\n");
    }
}

static void op_highest(const SalesDB *db) {
    const Transaction *t = find_highest_transaction(db);
    if (t == NULL) {
        printf("No transactions recorded.\n");
        return;
    }
    printf("Highest-value transaction:\n");
    print_transaction_header();
    print_transaction(t);
}

static void op_lowest(const SalesDB *db) {
    const Transaction *t = find_lowest_transaction(db);
    if (t == NULL) {
        printf("No transactions recorded.\n");
        return;
    }
    printf("Lowest-value transaction:\n");
    print_transaction_header();
    print_transaction(t);
}

static void op_total_units(const SalesDB *db) {
    printf("Total units sold: %ld\n", calculate_total_units(db));
}

/** The dispatch table: menu choice N calls SALES_OPERATIONS[N-1].execute. */
const SalesOperation SALES_OPERATIONS[] = {
    { "Calculate total revenue",            op_total_revenue },
    { "Calculate average transaction value", op_average_value },
    { "Identify highest-value transaction",  op_highest       },
    { "Identify lowest-value transaction",   op_lowest        },
    { "Calculate total units sold",          op_total_units   },
};
const int SALES_OPERATION_COUNT = (int)(sizeof SALES_OPERATIONS / sizeof SALES_OPERATIONS[0]);

void run_sales_operation(const SalesDB *db, SalesOperationFn operation) {
    if (operation == NULL) {
        printf("[ERROR] No operation selected.\n");
        return;
    }
    operation(db);                       /* call through the pointer */
}

/* =====================================================================
 *  2. Predicate callbacks
 * ===================================================================== */

int is_amount_above(const Transaction *t, const void *context) {
    return t->total > *(const double *)context;
}

int is_amount_in_range(const Transaction *t, const void *context) {
    const AmountRange *range = context;
    /* Half-cent tolerance so "search 2500" matches a stored 2500.00. */
    return t->total >= range->min - 0.005 && t->total <= range->max + 0.005;
}

int is_product_containing(const Transaction *t, const void *context) {
    return contains_ci(t->product, (const char *)context);
}

int is_product_equal(const Transaction *t, const void *context) {
    return compare_ci(t->product, (const char *)context) == 0;
}

int is_quantity_at_least(const Transaction *t, const void *context) {
    return t->quantity >= *(const int *)context;
}

int is_on_date(const Transaction *t, const void *context) {
    return strcmp(t->date, (const char *)context) == 0;
}

/* =====================================================================
 *  3. Higher-order functions that receive and invoke callbacks
 * ===================================================================== */

size_t for_each_matching(const SalesDB *db, TransactionPredicate matches,
                         const void *context, TransactionVisitor visit) {
    size_t found = 0;
    for (size_t i = 0; i < db->count; i++) {
        if (matches(&db->items[i], context)) {        /* callback #1 */
            if (visit != NULL) {
                visit(&db->items[i]);                 /* callback #2 */
            }
            found++;
        }
    }
    return found;
}

size_t count_matching(const SalesDB *db, TransactionPredicate matches, const void *context) {
    return for_each_matching(db, matches, context, NULL);
}

double sum_matching(const SalesDB *db, TransactionPredicate matches, const void *context) {
    double sum = 0.0;
    for (size_t i = 0; i < db->count; i++) {
        if (matches(&db->items[i], context)) {
            sum += db->items[i].total;
        }
    }
    return sum;
}

void print_transaction_visitor(const Transaction *t) {
    print_transaction(t);
}

/* =====================================================================
 *  4. Comparator callbacks and manual sorting
 * ===================================================================== */

int compare_by_id(const Transaction *a, const Transaction *b) {
    return (a->id > b->id) - (a->id < b->id);
}

int compare_by_product(const Transaction *a, const Transaction *b) {
    int result = compare_ci(a->product, b->product);
    return (result != 0) ? result : compare_by_id(a, b);
}

int compare_by_total_ascending(const Transaction *a, const Transaction *b) {
    if (a->total < b->total) return -1;
    if (a->total > b->total) return 1;
    return compare_by_id(a, b);
}

int compare_by_total_descending(const Transaction *a, const Transaction *b) {
    return compare_by_total_ascending(b, a);
}

/**
 * sort_transactions() - insertion sort written by hand (no qsort()).
 * The comparator callback decides the order, so one routine handles
 * every sort key and direction.
 */
void sort_transactions(SalesDB *db, TransactionComparator compare) {
    for (size_t i = 1; i < db->count; i++) {
        Transaction current = db->items[i];
        size_t j = i;
        while (j > 0 && compare(&db->items[j - 1], &current) > 0) {
            db->items[j] = db->items[j - 1];
            j--;
        }
        db->items[j] = current;
    }
}

/* =====================================================================
 *  Reports
 * ===================================================================== */

/**
 * generate_sales_report() - summary of the whole store. If @product is
 * not NULL, also shows the revenue for that product (exact,
 * case-insensitive match), computed with the sum_matching() callback.
 */
void generate_sales_report(const SalesDB *db, const char *product) {
    printf("\n================ SALES SUMMARY REPORT ================\n");
    if (db->count == 0) {
        printf("No transactions recorded - nothing to report.\n");
        printf("======================================================\n");
        return;
    }

    double average;
    calculate_average_value(db, &average);
    const Transaction *highest = find_highest_transaction(db);
    const Transaction *lowest  = find_lowest_transaction(db);

    printf("Total number of transactions : %zu\n", db->count);
    printf("Total revenue                : %.2f\n", calculate_total_revenue(db));
    printf("Average transaction value    : %.2f\n", average);
    printf("Highest transaction value    : %.2f (ID %d, %s)\n", highest->total, highest->id, highest->product);
    printf("Lowest transaction value     : %.2f (ID %d, %s)\n", lowest->total, lowest->id, lowest->product);
    printf("Total quantity sold          : %ld units\n", calculate_total_units(db));

    if (product != NULL && product[0] != '\0') {
        size_t matches = count_matching(db, is_product_equal, product);
        double revenue = sum_matching(db, is_product_equal, product);
        if (matches == 0) {
            printf("Revenue for '%s': no transactions for this product\n", product);
        } else {
            printf("Revenue for '%s': %.2f from %zu transaction(s) (%.1f%% of total)\n",
                   product, revenue, matches, revenue / calculate_total_revenue(db) * 100.0);
        }
    }
    printf("======================================================\n");
}
