/**
 * @file analyzer.c
 * @brief File processing, keyword search and report generation run by
 *        each worker thread.
 */
#define _POSIX_C_SOURCE 200809L   /* getline(), strerror_r() */

#include "analyzer.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* =====================================================================
 *  Task set-up / tear-down
 * ===================================================================== */

/**
 * build_report_path() - "<outputDir>/<file name without dir and extension>_report.txt"
 * e.g. "docs/document1.txt" -> "./document1_report.txt"
 */
static int build_report_path(char *dest, size_t size, const char *inputFile, const char *outputDir) {
    const char *base = strrchr(inputFile, '/');
    base = (base != NULL) ? base + 1 : inputFile;

    size_t stemLength = strlen(base);
    const char *dot = strrchr(base, '.');
    if (dot != NULL && dot != base) {            /* keep names like ".notes" */
        stemLength = (size_t)(dot - base);
    }
    if (stemLength == 0) {
        base = "unnamed";
        stemLength = strlen(base);
    }
    int written = snprintf(dest, size, "%s/%.*s_report.txt", outputDir, (int)stemLength, base);
    return written > 0 && (size_t)written < size;
}

/**
 * init_thread_task() - fills in the inputs of one task (called by the
 * main thread before the worker starts).
 * Return: 1 on success, 0 if the report path would be too long.
 */
int init_thread_task(ThreadTask *task, int threadNumber, const char *inputFile,
                     const char *keyword, const char *outputDir) {
    memset(task, 0, sizeof *task);
    task->threadNumber = threadNumber;
    task->inputFile    = inputFile;
    task->keyword      = keyword;
    task->status       = TASK_PENDING;

    if (!build_report_path(task->reportFile, sizeof task->reportFile, inputFile, outputDir)) {
        snprintf(task->errorMessage, sizeof task->errorMessage, "report path is too long");
        return 0;
    }
    return 1;
}

/** free_thread_task() - releases every excerpt and the match array. */
void free_thread_task(ThreadTask *task) {
    for (long i = 0; i < task->matchingLines; i++) {
        free(task->matches[i].excerpt);
    }
    free(task->matches);
    task->matches       = NULL;
    task->matchCapacity = 0;
}

const char *task_status_text(TaskStatus status) {
    switch (status) {
        case TASK_PENDING:      return "Not started";
        case TASK_SUCCESS:      return "Completed";
        case TASK_INPUT_ERROR:  return "Failed (input file error)";
        case TASK_REPORT_ERROR: return "Failed (report could not be written)";
        case TASK_MEMORY_ERROR: return "Failed (out of memory)";
        case TASK_THREAD_ERROR: return "Failed (thread could not be created)";
    }
    return "Unknown";
}

/* =====================================================================
 *  Searching
 * ===================================================================== */

/**
 * count_keyword_occurrences() - case-insensitive count of non-overlapping
 * occurrences of @keyword in @line ("IoT" matches "iot", "IOT", "IoT-based").
 */
long count_keyword_occurrences(const char *line, const char *keyword, size_t keywordLength) {
    long count = 0;
    if (keywordLength == 0) {
        return 0;
    }
    const char *cursor = line;
    while (*cursor != '\0') {
        size_t i = 0;
        while (i < keywordLength && cursor[i] != '\0' &&
               tolower((unsigned char)cursor[i]) == tolower((unsigned char)keyword[i])) {
            i++;
        }
        if (i == keywordLength) {
            count++;
            cursor += keywordLength;              /* non-overlapping */
        } else {
            cursor++;
        }
    }
    return count;
}

/**
 * record_match() - appends one matching line to task->matches, doubling
 * the array with realloc() when it is full.
 * Return: 1 on success, 0 if memory could not be allocated.
 */
static int record_match(ThreadTask *task, long lineNumber, long occurrences, const char *line) {
    if ((size_t)task->matchingLines == task->matchCapacity) {
        size_t newCapacity = (task->matchCapacity == 0) ? 16 : task->matchCapacity * 2;
        LineMatch *grown = realloc(task->matches, newCapacity * sizeof *grown);
        if (grown == NULL) {
            return 0;
        }
        task->matches       = grown;
        task->matchCapacity = newCapacity;
    }

    size_t length = strcspn(line, "\r\n");
    if (length > EXCERPT_MAX) {
        length = EXCERPT_MAX;
    }
    char *excerpt = malloc(length + 1);
    if (excerpt == NULL) {
        return 0;
    }
    memcpy(excerpt, line, length);
    excerpt[length] = '\0';

    LineMatch *match   = &task->matches[task->matchingLines];
    match->lineNumber  = lineNumber;
    match->occurrences = occurrences;
    match->excerpt     = excerpt;
    task->matchingLines++;
    return 1;
}

/** fail_task() - records an error status and message for this task. */
static void fail_task(ThreadTask *task, TaskStatus status, const char *what, int errorNumber) {
    task->status = status;
    if (errorNumber != 0) {
        char reason[128];
        /* XSI strerror_r is thread-safe (plain strerror() is not). */
        if (strerror_r(errorNumber, reason, sizeof reason) != 0) {
            snprintf(reason, sizeof reason, "error %d", errorNumber);
        }
        snprintf(task->errorMessage, sizeof task->errorMessage, "%s: %s", what, reason);
    } else {
        snprintf(task->errorMessage, sizeof task->errorMessage, "%s", what);
    }
}

/**
 * analyse_file() - reads the file line by line, counting lines,
 * occurrences and matching lines.
 * Return: 1 on success, 0 on failure (status/message already set).
 */
static int analyse_file(ThreadTask *task) {
    struct stat info;
    if (stat(task->inputFile, &info) != 0) {
        fail_task(task, TASK_INPUT_ERROR, "cannot access file", errno);
        return 0;
    }
    if (!S_ISREG(info.st_mode)) {
        fail_task(task, TASK_INPUT_ERROR, "not a regular file", 0);
        return 0;
    }

    FILE *file = fopen(task->inputFile, "r");
    if (file == NULL) {
        fail_task(task, TASK_INPUT_ERROR, "cannot open file", errno);
        return 0;
    }

    size_t keywordLength = strlen(task->keyword);
    char *line = NULL;                 /* getline() grows this as needed */
    size_t lineCapacity = 0;
    int ok = 1;

    while (getline(&line, &lineCapacity, file) != -1) {
        task->totalLines++;
        long found = count_keyword_occurrences(line, task->keyword, keywordLength);
        if (found > 0) {
            task->totalOccurrences += found;
            if (!record_match(task, task->totalLines, found, line)) {
                fail_task(task, TASK_MEMORY_ERROR, "out of memory while recording matches", 0);
                ok = 0;
                break;
            }
        }
    }
    if (ok && ferror(file)) {
        fail_task(task, TASK_INPUT_ERROR, "read error", errno);
        ok = 0;
    }
    free(line);
    fclose(file);
    return ok;
}

/* =====================================================================
 *  Reporting
 * ===================================================================== */

/**
 * generate_report() - writes this task's report file. It is written for
 * failed analyses too, so every input has a report stating its status.
 * Return: 1 on success, 0 if the report could not be written.
 */
int generate_report(ThreadTask *task) {
    FILE *report = fopen(task->reportFile, "w");
    if (report == NULL) {
        return 0;
    }

    char timestamp[32] = "unknown";
    time_t now = time(NULL);
    struct tm local;
    if (localtime_r(&now, &local) != NULL) {
        strftime(timestamp, sizeof timestamp, "%Y-%m-%d %H:%M:%S", &local);
    }

    int bad = 0;
    bad |= fprintf(report, "==================== DOCUMENT ANALYSIS REPORT ====================\n") < 0;
    bad |= fprintf(report, "Generated            : %s\n", timestamp) < 0;
    bad |= fprintf(report, "Processed by thread  : %d\n", task->threadNumber) < 0;
    bad |= fprintf(report, "Input file           : %s\n", task->inputFile) < 0;
    bad |= fprintf(report, "Search keyword       : \"%s\" (case-insensitive)\n", task->keyword) < 0;

    if (task->status == TASK_SUCCESS) {
        bad |= fprintf(report, "Total lines processed: %ld\n", task->totalLines) < 0;
        bad |= fprintf(report, "Keyword occurrences  : %ld\n", task->totalOccurrences) < 0;
        bad |= fprintf(report, "Matching lines       : %ld\n", task->matchingLines) < 0;

        if (task->matchingLines == 0) {
            bad |= fprintf(report, "Line numbers         : none - keyword not found in this file\n") < 0;
        } else {
            bad |= fprintf(report, "Line numbers         : ") < 0;
            for (long i = 0; i < task->matchingLines; i++) {
                bad |= fprintf(report, "%s%ld", i ? ", " : "", task->matches[i].lineNumber) < 0;
            }
            bad |= fprintf(report, "\n\n------------------------- Matching lines -------------------------\n") < 0;
            for (long i = 0; i < task->matchingLines; i++) {
                const LineMatch *m = &task->matches[i];
                bad |= fprintf(report, "Line %-5ld (%ld match%s): %s\n", m->lineNumber, m->occurrences,
                               m->occurrences == 1 ? "" : "es", m->excerpt) < 0;
            }
        }
        bad |= fprintf(report, "\nProcessing status    : SUCCESS - analysis completed\n") < 0;
    } else {
        bad |= fprintf(report, "\nProcessing status    : FAILED - %s\n", task->errorMessage) < 0;
    }
    bad |= fprintf(report, "==================================================================\n") < 0;

    if (fclose(report) != 0) {
        bad = 1;
    }
    return !bad;
}

/* =====================================================================
 *  Thread entry point
 * ===================================================================== */

/**
 * process_file() - pthread start routine. @arg is this thread's
 * ThreadTask; the thread writes only into that struct.
 */
void *process_file(void *arg) {
    ThreadTask *task = arg;

    printf("[Thread %d] Processing %s ...\n", task->threadNumber, task->inputFile);

    if (analyse_file(task)) {
        task->status = TASK_SUCCESS;
    }

    if (!generate_report(task)) {
        int reportErrno = errno;
        if (task->status == TASK_SUCCESS) {
            fail_task(task, TASK_REPORT_ERROR, "cannot write report", reportErrno);
        }
        printf("[Thread %d] ERROR: could not write report %s\n", task->threadNumber, task->reportFile);
    }

    if (task->status == TASK_SUCCESS) {
        printf("[Thread %d] Successfully processed %s (%ld occurrence%s)\n", task->threadNumber,
               task->inputFile, task->totalOccurrences, task->totalOccurrences == 1 ? "" : "s");
    } else {
        printf("[Thread %d] ERROR processing %s: %s\n",
               task->threadNumber, task->inputFile, task->errorMessage);
    }
    return NULL;
}

/* =====================================================================
 *  Summary (main thread, after every pthread_join())
 * ===================================================================== */

void display_results(const ThreadTask *tasks, int taskCount) {
    int succeeded = 0;

    printf("\n===== PROCESSING SUMMARY =====\n");
    for (int i = 0; i < taskCount; i++) {
        const ThreadTask *t = &tasks[i];
        if (t->status == TASK_SUCCESS) {
            succeeded++;
            printf("%s: %s - %ld line(s), %ld occurrence(s), %ld matching line(s)%s -> %s\n",
                   t->inputFile, task_status_text(t->status), t->totalLines,
                   t->totalOccurrences, t->matchingLines,
                   t->totalOccurrences == 0 ? " [keyword not found]" : "", t->reportFile);
        } else {
            printf("%s: %s - %s\n", t->inputFile, task_status_text(t->status), t->errorMessage);
        }
    }

    printf("\n");
    if (succeeded == taskCount) {
        printf("All processing tasks completed.\n");
        printf("Reports generated successfully.\n");
    } else {
        printf("%d of %d processing task(s) completed successfully; %d failed.\n",
               succeeded, taskCount, taskCount - succeeded);
    }
}
