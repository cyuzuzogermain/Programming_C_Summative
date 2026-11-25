/**
 * @file main.c
 * @brief Menu-driven interface of the Sales Transaction Processing and
 *        Analysis System.
 *
 * Usage: ./sales_system [data_file]        (default: sales.txt)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "input.h"
#include "processing.h"
#include "sales.h"
#include "storage.h"

#define DEFAULT_DATA_FILE "sales.txt"

typedef struct {
    SalesDB     db;
    const char *dataFile;
    int         unsavedChanges;
} App;

static void print_error(SalesStatus status) {
    printf("  [ERROR] %s.\n", sales_status_message(status));
}

/** require_records() - prints a message and returns 0 when the store is empty. */
static int require_records(const App *app) {
    if (app->db.count == 0) {
        printf("No transactions recorded yet.\n");
        return 0;
    }
    return 1;
}

/** prompt_existing() - reads an ID and returns that transaction or NULL. */
static Transaction *prompt_existing(App *app, const char *prompt) {
    int id;
    if (!require_records(app)) return NULL;
    if (!read_int(prompt, MIN_TRANSACTION_ID, MAX_TRANSACTION_ID, &id)) return NULL;
    Transaction *t = find_transaction(&app->db, id);
    if (t == NULL) {
        printf("  [ERROR] No transaction with ID %d exists.\n", id);
    }
    return t;
}

/* ---------------------------------------------------------------------
 *  1-4: CRUD
 * ------------------------------------------------------------------- */

static void menu_add(App *app) {
    Transaction t;
    memset(&t, 0, sizeof t);
    printf("\n--- Add New Transaction ---\n");

    for (;;) {
        if (!read_int("Transaction ID (1-99999999): ", MIN_TRANSACTION_ID, MAX_TRANSACTION_ID, &t.id)) return;
        if (find_transaction(&app->db, t.id) == NULL) break;
        printf("  [ERROR] Transaction ID %d already exists. Duplicate IDs are not allowed.\n", t.id);
    }
    if (!read_text("Product name: ", t.product, sizeof t.product) ||
        !read_int("Quantity sold (1-1000000): ", 1, MAX_QUANTITY, &t.quantity) ||
        !read_double("Unit price (0.01-10000000): ", MIN_UNIT_PRICE, MAX_UNIT_PRICE, &t.unitPrice) ||
        !read_date("Date (YYYY-MM-DD, 't' = today): ", t.date, sizeof t.date, 0)) {
        return;
    }

    SalesStatus status = add_transaction(&app->db, &t);
    if (status != SALES_OK) {
        print_error(status);
        return;
    }
    app->unsavedChanges = 1;
    printf("Transaction %d added. Total amount: %.2f\n", t.id, t.quantity * t.unitPrice);
}

static void menu_update(App *app) {
    Transaction *existing = prompt_existing(app, "ID of the transaction to update: ");
    if (existing == NULL) return;

    print_transaction_header();
    print_transaction(existing);
    printf("Press Enter to keep the current value.\n");

    Transaction updated = *existing;
    char prompt[128];
    snprintf(prompt, sizeof prompt, "Product name [%s]: ", updated.product);
    if (!read_text_or_keep(prompt, updated.product, sizeof updated.product)) return;
    snprintf(prompt, sizeof prompt, "Quantity [%d]: ", updated.quantity);
    if (!read_int_or_keep(prompt, 1, MAX_QUANTITY, &updated.quantity)) return;
    snprintf(prompt, sizeof prompt, "Unit price [%.2f]: ", updated.unitPrice);
    if (!read_double_or_keep(prompt, MIN_UNIT_PRICE, MAX_UNIT_PRICE, &updated.unitPrice)) return;
    snprintf(prompt, sizeof prompt, "Date [%s]: ", updated.date);
    if (!read_date(prompt, updated.date, sizeof updated.date, 1)) return;

    SalesStatus status = update_transaction(&app->db, updated.id, &updated);
    if (status != SALES_OK) {
        print_error(status);
        return;
    }
    app->unsavedChanges = 1;
    printf("Transaction %d updated:\n", updated.id);
    print_transaction(find_transaction(&app->db, updated.id));
}

static void menu_delete(App *app) {
    Transaction *t = prompt_existing(app, "ID of the transaction to delete: ");
    if (t == NULL) return;

    int id = t->id, confirm;
    print_transaction_header();
    print_transaction(t);
    if (!read_yes_no("Delete this transaction? (y/n): ", &confirm)) return;
    if (!confirm) {
        printf("Deletion cancelled.\n");
        return;
    }
    SalesStatus status = delete_transaction(&app->db, id);
    if (status != SALES_OK) {
        print_error(status);
        return;
    }
    app->unsavedChanges = 1;
    printf("Transaction %d deleted.\n", id);
}

/* ---------------------------------------------------------------------
 *  5: Function pointers - operation chosen at runtime
 * ------------------------------------------------------------------- */

static void menu_process(App *app) {
    printf("\n--- Process Sales Data (function pointers) ---\n");
    for (int i = 0; i < SALES_OPERATION_COUNT; i++) {
        printf(" %d. %s\n", i + 1, SALES_OPERATIONS[i].name);
    }
    printf(" %d. Run ALL operations\n", SALES_OPERATION_COUNT + 1);

    int choice;
    if (!read_int("Choice: ", 1, SALES_OPERATION_COUNT + 1, &choice)) return;

    if (choice == SALES_OPERATION_COUNT + 1) {
        for (int i = 0; i < SALES_OPERATION_COUNT; i++) {
            printf("\n> %s\n", SALES_OPERATIONS[i].name);
            run_sales_operation(&app->db, SALES_OPERATIONS[i].execute);
        }
        return;
    }
    /* Pick the function pointer from the table and pass it on. */
    SalesOperationFn selected = SALES_OPERATIONS[choice - 1].execute;
    printf("\n> %s\n", SALES_OPERATIONS[choice - 1].name);
    run_sales_operation(&app->db, selected);
}

/* ---------------------------------------------------------------------
 *  6: Callbacks - predicates passed to generic functions
 * ------------------------------------------------------------------- */

static void menu_filter(App *app) {
    if (!require_records(app)) return;
    printf("\n--- Filter Transactions (callbacks) ---\n");
    printf(" 1. Transactions above a specified amount\n");
    printf(" 2. Transactions involving a specified product\n");
    printf(" 3. Count transactions with quantity >= N\n");
    printf(" 4. Count transactions on a specific date\n");

    int choice;
    if (!read_int("Choice: ", 1, 4, &choice)) return;

    size_t found = 0;
    if (choice == 1) {
        double threshold;
        if (!read_double("Show transactions with a total above: ", 0.0, 1e12, &threshold)) return;
        print_transaction_header();
        found = for_each_matching(&app->db, is_amount_above, &threshold, print_transaction_visitor);
        printf("%zu transaction(s) above %.2f.\n", found, threshold);
    } else if (choice == 2) {
        char product[MAX_PRODUCT_LEN];
        if (!read_text("Product (or part of the name): ", product, sizeof product)) return;
        print_transaction_header();
        found = for_each_matching(&app->db, is_product_containing, product, print_transaction_visitor);
        printf("%zu transaction(s) involve '%s'.\n", found, product);
    } else if (choice == 3) {
        int minimum;
        if (!read_int("Minimum quantity: ", 1, MAX_QUANTITY, &minimum)) return;
        found = count_matching(&app->db, is_quantity_at_least, &minimum);
        printf("%zu transaction(s) sold at least %d unit(s).\n", found, minimum);
    } else {
        char date[DATE_LEN];
        if (!read_date("Date (YYYY-MM-DD): ", date, sizeof date, 0)) return;
        found = count_matching(&app->db, is_on_date, date);
        printf("%zu transaction(s) on %s, revenue %.2f.\n",
               found, date, sum_matching(&app->db, is_on_date, date));
    }
}

/* ---------------------------------------------------------------------
 *  7: Manual searching (reuses the predicate callbacks)
 * ------------------------------------------------------------------- */

static void menu_search(App *app) {
    if (!require_records(app)) return;
    printf("\n--- Search Transactions ---\n");
    printf(" 1. By transaction ID\n 2. By product name\n 3. By transaction amount\n");

    int choice;
    if (!read_int("Choice: ", 1, 3, &choice)) return;

    if (choice == 1) {
        int id;
        if (!read_int("Transaction ID: ", MIN_TRANSACTION_ID, MAX_TRANSACTION_ID, &id)) return;
        Transaction *t = find_transaction(&app->db, id);
        if (t == NULL) {
            printf("No transaction with ID %d was found.\n", id);
            return;
        }
        print_transaction_header();
        print_transaction(t);
        return;
    }

    size_t found;
    if (choice == 2) {
        char product[MAX_PRODUCT_LEN];
        if (!read_text("Product name (partial match allowed): ", product, sizeof product)) return;
        if (count_matching(&app->db, is_product_containing, product) == 0) {
            printf("No transactions match product '%s'.\n", product);
            return;
        }
        print_transaction_header();
        found = for_each_matching(&app->db, is_product_containing, product, print_transaction_visitor);
    } else {
        AmountRange range;
        if (!read_double("Minimum amount: ", 0.0, 1e12, &range.min)) return;
        if (!read_double("Maximum amount (same as minimum for an exact match): ", range.min, 1e12, &range.max)) return;
        if (count_matching(&app->db, is_amount_in_range, &range) == 0) {
            printf("No transactions between %.2f and %.2f.\n", range.min, range.max);
            return;
        }
        print_transaction_header();
        found = for_each_matching(&app->db, is_amount_in_range, &range, print_transaction_visitor);
    }
    printf("%zu matching transaction(s).\n", found);
}

/* ---------------------------------------------------------------------
 *  8: Manual sorting with comparator callbacks
 * ------------------------------------------------------------------- */

static void menu_sort(App *app) {
    static const struct {
        const char           *label;
        TransactionComparator compare;
    } SORT_OPTIONS[] = {
        { "Transaction ID",                compare_by_id               },
        { "Product name",                  compare_by_product          },
        { "Total amount (ascending)",      compare_by_total_ascending  },
        { "Total amount (descending)",     compare_by_total_descending },
    };
    const int optionCount = (int)(sizeof SORT_OPTIONS / sizeof SORT_OPTIONS[0]);

    if (app->db.count < 2) {
        printf("At least two transactions are needed to sort.\n");
        return;
    }
    printf("\n--- Sort Transactions ---\n");
    for (int i = 0; i < optionCount; i++) {
        printf(" %d. %s\n", i + 1, SORT_OPTIONS[i].label);
    }
    int choice;
    if (!read_int("Choice: ", 1, optionCount, &choice)) return;

    sort_transactions(&app->db, SORT_OPTIONS[choice - 1].compare);
    app->unsavedChanges = 1;
    printf("Sorted by %s:\n", SORT_OPTIONS[choice - 1].label);
    display_all_transactions(&app->db);
}

/* ---------------------------------------------------------------------
 *  9-11: Report and files
 * ------------------------------------------------------------------- */

static void menu_report(App *app) {
    char product[MAX_PRODUCT_LEN] = "";
    if (app->db.count > 0 &&
        !read_line("Product for the revenue breakdown (Enter to skip): ", product, sizeof product)) {
        return;
    }
    generate_sales_report(&app->db, product);
}

static void menu_save(App *app) {
    SalesStatus status = save_transactions(&app->db, app->dataFile);
    if (status != SALES_OK) {
        print_error(status);
        return;
    }
    app->unsavedChanges = 0;
    printf("%zu transaction(s) saved to %s.\n", app->db.count, app->dataFile);
}

/**
 * menu_load() - loads into a fresh store and only replaces the current
 * data if loading succeeded, so a bad file cannot wipe what is in memory.
 */
static void menu_load(App *app) {
    if (app->unsavedChanges) {
        int discard;
        if (!read_yes_no("Loading discards unsaved changes. Continue? (y/n): ", &discard) || !discard) {
            printf("Load cancelled.\n");
            return;
        }
    }
    SalesDB fresh;
    if (sales_init(&fresh) != SALES_OK) {
        print_error(SALES_NO_MEMORY);
        return;
    }
    SalesStatus status = load_transactions(&fresh, app->dataFile);
    if (status != SALES_OK) {
        print_error(status);
        sales_free(&fresh);
        return;
    }
    sales_free(&app->db);
    app->db = fresh;
    app->unsavedChanges = 0;
    printf("%zu transaction(s) loaded from %s.\n", app->db.count, app->dataFile);
}

/** confirm_exit() - Return: 1 if the program should exit. */
static int confirm_exit(App *app) {
    if (!app->unsavedChanges) return 1;
    if (input_ended()) {
        printf("[WARNING] Input ended - unsaved changes were discarded.\n");
        return 1;
    }
    int save;
    if (!read_yes_no("Save changes before exiting? (y/n): ", &save)) return 1;
    if (save) {
        menu_save(app);
        if (app->unsavedChanges) {
            int quit;
            if (!read_yes_no("Saving failed. Exit anyway? (y/n): ", &quit)) return 1;
            return quit;
        }
    }
    return 1;
}

static void print_menu(const App *app) {
    printf("\n===== SALES TRANSACTION MANAGEMENT SYSTEM =====\n");
    printf("  %zu transaction(s) | file: %s%s\n", app->db.count, app->dataFile,
           app->unsavedChanges ? " | unsaved changes" : "");
    printf(" 1. Add New Transaction\n");
    printf(" 2. Display All Transactions\n");
    printf(" 3. Update Transaction\n");
    printf(" 4. Delete Transaction\n");
    printf(" 5. Process Sales Data\n");
    printf(" 6. Filter Transactions (Callbacks)\n");
    printf(" 7. Search Transactions\n");
    printf(" 8. Sort Transactions\n");
    printf(" 9. Generate Sales Report\n");
    printf("10. Save Transactions\n");
    printf("11. Load Transactions\n");
    printf(" 0. Exit\n");
}

int main(int argc, char *argv[]) {
    App app;
    app.dataFile       = (argc > 1) ? argv[1] : DEFAULT_DATA_FILE;
    app.unsavedChanges = 0;

    if (sales_init(&app.db) != SALES_OK) {
        fprintf(stderr, "[FATAL] Could not allocate memory for transactions.\n");
        return EXIT_FAILURE;
    }

    SalesStatus status = load_transactions(&app.db, app.dataFile);
    if (status == SALES_OK) {
        printf("Loaded %zu transaction(s) from %s.\n", app.db.count, app.dataFile);
    } else if (status == SALES_FILE_MISSING) {
        printf("No saved data found at '%s' - starting with no transactions.\n", app.dataFile);
    } else {
        fprintf(stderr, "[ERROR] Loading '%s' failed: %s.\n", app.dataFile, sales_status_message(status));
    }

    int running = 1;
    while (running) {
        print_menu(&app);
        int choice;
        if (!read_int("Select an option: ", 0, 11, &choice)) {
            running = !confirm_exit(&app);
            continue;
        }
        switch (choice) {
            case 1:  menu_add(&app);     break;
            case 2:  display_all_transactions(&app.db); break;
            case 3:  menu_update(&app);  break;
            case 4:  menu_delete(&app);  break;
            case 5:  menu_process(&app); break;
            case 6:  menu_filter(&app);  break;
            case 7:  menu_search(&app);  break;
            case 8:  menu_sort(&app);    break;
            case 9:  menu_report(&app);  break;
            case 10: menu_save(&app);    break;
            case 11: menu_load(&app);    break;
            case 0:  running = !confirm_exit(&app); break;
        }
        if (running && input_ended()) {
            running = !confirm_exit(&app);
        }
    }

    sales_free(&app.db);
    printf("Goodbye!\n");
    return EXIT_SUCCESS;
}
