# Sales Transaction Processing and Analysis System

A menu-driven C11 application that records sales transactions and processes them through **function pointers** and **callbacks**. Transactions are stored in a dynamic array, searched and sorted with hand-written code, and saved to a file.

```bash
make                    # builds ./sales_system
./sales_system          # loads/saves sales.txt
./sales_system my.txt   # uses a different data file
```

## Program structure

| File | Responsibility |
|------|----------------|
| `sales.h` / `sales.c` | The `Transaction` and `SalesDB` structures, allocation and growth, validation, add, update, delete and find, table display |
| `processing.h` / `processing.c` | The function-pointer operation table, predicate, visitor and comparator callbacks, the generic filter, count and sum functions, manual sort, sales report |
| `storage.h` / `storage.c` | Save and load the text file |
| `input.h` / `input.c` | Validated input: integers, prices, text, dates (`YYYY-MM-DD` or `t` for today), y/n |
| `main.c` | The menu suggested in the brief (options 0–11) and its handlers |

```c
typedef struct {
    int id;  char product[50];  int quantity;  double unitPrice;
    double total;  char date[11];
} Transaction;

typedef struct { Transaction *items; size_t count; size_t capacity; } SalesDB;
```

## How the required C concepts are implemented

### Function pointers (menu 5)

`processing.c` defines a table of operations, each with a name and a function pointer:

```c
typedef void (*SalesOperationFn)(const SalesDB *db);
typedef struct { const char *name; SalesOperationFn execute; } SalesOperation;

const SalesOperation SALES_OPERATIONS[] = {
    { "Calculate total revenue",             op_total_revenue },
    { "Calculate average transaction value", op_average_value },
    { "Identify highest-value transaction",  op_highest       },
    { "Identify lowest-value transaction",   op_lowest        },
    { "Calculate total units sold",          op_total_units   },
};
```

The menu is built from this table. The user's choice selects `SALES_OPERATIONS[choice-1].execute`, which is passed to `run_sales_operation(db, fn)`, and that function calls it through the pointer. Option 6, "Run ALL", loops over the table. The menu has no switch statement over the operations, so adding one only requires a new table entry.

### Callback functions (menus 6, 7, 8, 9)

| Callback type | Signature | Implementations |
|---------------|-----------|-----------------|
| Predicate | `int (*)(const Transaction *, const void *context)` | `is_amount_above`, `is_amount_in_range`, `is_product_containing`, `is_product_equal`, `is_quantity_at_least`, `is_on_date` |
| Visitor | `void (*)(const Transaction *)` | `print_transaction_visitor` |
| Comparator | `int (*)(const Transaction *, const Transaction *)` | `compare_by_id`, `compare_by_product`, `compare_by_total_ascending`, `compare_by_total_descending` |

These functions receive the callbacks as arguments and call them:

- `for_each_matching(db, predicate, context, visitor)` calls the predicate on every record, and the visitor on each match.
- `count_matching(db, predicate, context)` counts the matches.
- `sum_matching(db, predicate, context)` adds up the totals of the matches. The report uses it for revenue per product.
- `sort_transactions(db, comparator)` uses the comparator to order the records.

The `void *context` argument lets one predicate signature take different parameters: a `double*` threshold, a `char*` product name, an `int*` quantity, or an `AmountRange*`.

The filter menu (6) shows: transactions above an amount, transactions involving a product, a count of transactions with quantity ≥ N, and a count plus revenue on a date. Search options 2 and 3 reuse the same predicates.

### Dynamic memory allocation

| Step | Code |
|------|------|
| Start-up | `sales_init()` calls `malloc(INITIAL_CAPACITY * sizeof(Transaction))` for 4 slots |
| Add a record when full | `ensure_capacity()` calls `realloc` to double the capacity (4 → 8 → 16 …) |
| Delete | Later records shift left. When fewer than ¼ of the slots are used, `realloc` halves the capacity. |
| Load from the menu | The file is loaded into a fresh `SalesDB`. The old one is freed and replaced only if loading succeeds, so a bad file cannot wipe the data in memory. |
| Exit | `sales_free()` |

Every `malloc` and `realloc` result is checked. If `realloc` fails, the old block is still valid and the function returns `SALES_NO_MEMORY`. The display shows slot usage, for example `[memory: 10 of 16 slots used]` after the 10 sample records loaded and the array grew twice. A run with an allocation-counting wrapper showed 0 blocks still allocated at exit.

### Manual searching and sorting

- Search by ID is a linear scan in `find_transaction()`. Search by product (partial, case-insensitive) and by amount (a minimum–maximum range; enter the same value twice for an exact match) both use `for_each_matching` with a predicate.
- Sorting is a hand-written **insertion sort** with a comparator callback; `qsort()` is not used. It can sort by ID, by product name (ties broken by ID), or by total amount ascending or descending.

### Reports (menu 9)

The report shows:

- the number of transactions
- total revenue
- the average transaction value
- the highest and lowest values, with their IDs
- the total quantity sold
- optionally, the revenue for one product (exact, case-insensitive match), with its share of the total

### File handling (menus 10 and 11, and start-up)

The file has one line per transaction, `id|product|quantity|unitPrice|date`. The total is not stored; it is recalculated when the file is loaded.

- **Save** writes `sales.txt.tmp`, checks every write and `fclose`, then calls `rename()`.
- **Load** sends every line through `add_transaction()`, so duplicate IDs, quantities ≤ 0, invalid prices, invalid dates and malformed lines are rejected and reported with their line numbers.
- A missing file starts an empty store. On exit, the program asks whether to save unsaved changes.

## Error handling and validation

| Rule | Where it is enforced |
|------|----------------------|
| Duplicate transaction IDs are rejected | `add_transaction`, the add prompt, and file loading |
| Quantity must be 1–1,000,000; zero and negative values are rejected | `read_int` and `validate_transaction` |
| Price must be 0.01–10,000,000; non-numeric input and NaN are rejected | `read_double` and `validate_transaction` |
| Dates must be real calendar dates (`2026-02-30` is rejected; leap years are handled) | `is_valid_date` |
| Searches with no results say so | Search and filter handlers |
| An empty store is handled: average shows N/A, highest and lowest say none, the report says there is no data, so nothing divides by zero | `calculate_average_value`, operations, report |
| Invalid menu choices and over-long input are rejected | `read_int` and `read_line` |
| Memory failures return `SALES_NO_MEMORY`, and start-up failure exits cleanly | `sales_init` and `ensure_capacity` |
| File open, read and write errors are reported with `strerror` | `storage.c` |

## Evidence (`samples/`)

| File | Contents |
|------|----------|
| `sales.txt` | 10 sample transactions |
| `demo_run.txt` | Four runs: **(1)** every menu option, including invalid input, CRUD, all 5 function-pointer operations, all 4 callback filters, 3 searches, 3 sorts, reports, save and load; **(2)** a restart that shows the data was saved; **(3)** loading `corrupted_example.txt`; **(4)** an empty store |
| `sales_after_demo.txt` | The file saved by run 1 |
| `corrupted_example.txt` | Invalid records used to test error handling |
