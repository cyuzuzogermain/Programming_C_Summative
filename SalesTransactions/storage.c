/**
 * @file storage.c
 * @brief Saving / loading transactions (see storage.h for the format).
 *
 * save: writes "<path>.tmp" then rename()s it over the real file, so a
 *       failed write never destroys previously saved data.
 * load: every line goes through add_transaction(), so loaded records are
 *       validated and duplicate IDs rejected exactly like typed-in ones.
 *       Bad lines are reported with their line number and skipped.
 */
#include "storage.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_BUFFER 256

SalesStatus save_transactions(const SalesDB *db, const char *path) {
    char tmpPath[1024];
    if (snprintf(tmpPath, sizeof tmpPath, "%s.tmp", path) >= (int)sizeof tmpPath) {
        return SALES_FILE_ERROR;
    }
    FILE *file = fopen(tmpPath, "w");
    if (file == NULL) {
        fprintf(stderr, "[ERROR] Cannot open '%s' for writing: %s\n", tmpPath, strerror(errno));
        return SALES_FILE_ERROR;
    }

    int failed = fprintf(file, "# Sales transactions: id|product|quantity|unitPrice|date\n") < 0;
    for (size_t i = 0; i < db->count && !failed; i++) {
        const Transaction *t = &db->items[i];
        failed = fprintf(file, "%d|%s|%d|%.2f|%s\n",
                         t->id, t->product, t->quantity, t->unitPrice, t->date) < 0;
    }
    if (fclose(file) != 0) failed = 1;

    if (failed) {
        fprintf(stderr, "[ERROR] Write to '%s' failed. Previous data kept.\n", tmpPath);
        remove(tmpPath);
        return SALES_FILE_ERROR;
    }
    if (rename(tmpPath, path) != 0) {
        fprintf(stderr, "[ERROR] Cannot replace '%s': %s\n", path, strerror(errno));
        remove(tmpPath);
        return SALES_FILE_ERROR;
    }
    return SALES_OK;
}

/**
 * parse_line() - splits "id|product|qty|price|date" into @t.
 * Return: 1 if all five fields are present and numeric fields parse.
 */
static int parse_line(char *line, Transaction *t) {
    char *fields[5];
    int count = 0;
    char *cursor = line;

    fields[count++] = cursor;
    for (; *cursor != '\0'; cursor++) {
        if (*cursor == '|') {
            if (count == 5) return 0;            /* too many fields */
            *cursor = '\0';
            fields[count++] = cursor + 1;
        }
    }
    if (count != 5) return 0;

    char *end;
    errno = 0;
    long id = strtol(fields[0], &end, 10);
    if (errno || *end != '\0' || end == fields[0] || id < MIN_TRANSACTION_ID || id > MAX_TRANSACTION_ID) return 0;

    long quantity = strtol(fields[2], &end, 10);
    if (errno || *end != '\0' || end == fields[2] || quantity < 0 || quantity > MAX_QUANTITY) return 0;

    double price = strtod(fields[3], &end);
    if (errno || *end != '\0' || end == fields[3]) return 0;

    if (strlen(fields[1]) >= sizeof t->product || strlen(fields[4]) >= sizeof t->date) return 0;

    memset(t, 0, sizeof *t);
    t->id        = (int)id;
    t->quantity  = (int)quantity;
    t->unitPrice = price;
    strcpy(t->product, fields[1]);
    strcpy(t->date, fields[4]);
    return 1;
}

SalesStatus load_transactions(SalesDB *db, const char *path) {
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        if (errno == ENOENT) return SALES_FILE_MISSING;
        fprintf(stderr, "[ERROR] Cannot open '%s': %s\n", path, strerror(errno));
        return SALES_FILE_ERROR;
    }

    char line[LINE_BUFFER];
    int lineNumber = 0, skipped = 0;
    SalesStatus result = SALES_OK;

    while (fgets(line, sizeof line, file) != NULL) {
        lineNumber++;
        if (strchr(line, '\n') == NULL && !feof(file)) {
            int ch;
            while ((ch = fgetc(file)) != '\n' && ch != EOF) { }
            fprintf(stderr, "[WARNING] Line %d is too long - skipped\n", lineNumber);
            skipped++;
            continue;
        }
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0' || line[0] == '#') continue;

        Transaction t;
        if (!parse_line(line, &t)) {
            fprintf(stderr, "[WARNING] Line %d is malformed - skipped\n", lineNumber);
            skipped++;
            continue;
        }
        SalesStatus status = add_transaction(db, &t);
        if (status == SALES_NO_MEMORY) {
            result = SALES_NO_MEMORY;
            break;
        }
        if (status != SALES_OK) {
            fprintf(stderr, "[WARNING] Line %d (ID %d): %s - skipped\n",
                    lineNumber, t.id, sales_status_message(status));
            skipped++;
        }
    }
    if (result == SALES_OK && ferror(file)) {
        fprintf(stderr, "[ERROR] Read error in '%s'\n", path);
        result = SALES_FILE_ERROR;
    }
    fclose(file);
    if (skipped > 0) {
        fprintf(stderr, "[WARNING] %d invalid line(s) skipped while loading '%s'.\n", skipped, path);
    }
    return result;
}
