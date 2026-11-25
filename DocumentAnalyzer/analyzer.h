/**
 * @file analyzer.h
 * @brief Per-file analysis task used by the multi-threaded document analyzer.
 *
 * Each worker thread receives a pointer to its own ThreadTask. The task
 * holds the thread's inputs (file name, keyword) and its outputs (counts,
 * matches, status). No two threads ever touch the same task, and the main
 * thread reads results only after pthread_join(), so no locking is needed.
 */
#ifndef ANALYZER_H
#define ANALYZER_H

#include <pthread.h>
#include <stddef.h>

#define REPORT_PATH_MAX   1024
#define ERROR_MESSAGE_MAX 256
#define EXCERPT_MAX       120     /* characters of each matching line kept */

/** Lifecycle / outcome of one task. */
typedef enum {
    TASK_PENDING = 0,             /* thread not started yet              */
    TASK_SUCCESS,                 /* file analysed and report written    */
    TASK_INPUT_ERROR,             /* input file missing/unreadable       */
    TASK_REPORT_ERROR,            /* analysis ok, report could not be written */
    TASK_MEMORY_ERROR,            /* allocation failed during analysis   */
    TASK_THREAD_ERROR             /* pthread_create() failed             */
} TaskStatus;

/** One line that contains the keyword. */
typedef struct {
    long  lineNumber;
    long  occurrences;            /* occurrences on this line            */
    char *excerpt;                /* heap copy of (the start of) the line */
} LineMatch;

/** Everything one thread needs (inputs) and produces (outputs). */
typedef struct {
    /* --- inputs, set by the main thread before pthread_create() --- */
    int         threadNumber;     /* 1-based, for display                */
    const char *inputFile;
    const char *keyword;
    char        reportFile[REPORT_PATH_MAX];

    /* --- outputs, written only by the owning thread --- */
    long        totalLines;
    long        totalOccurrences;
    long        matchingLines;
    LineMatch  *matches;          /* dynamic array, grows with realloc()  */
    size_t      matchCapacity;
    TaskStatus  status;
    char        errorMessage[ERROR_MESSAGE_MAX];

    /* --- bookkeeping for the main thread --- */
    pthread_t   thread;
    int         threadCreated;    /* 1 if pthread_create() succeeded     */
} ThreadTask;

int   init_thread_task(ThreadTask *task, int threadNumber, const char *inputFile,
                       const char *keyword, const char *outputDir);
void *process_file(void *arg);
long  count_keyword_occurrences(const char *line, const char *keyword, size_t keywordLength);
int   generate_report(ThreadTask *task);
void  display_results(const ThreadTask *tasks, int taskCount);
void  free_thread_task(ThreadTask *task);
const char *task_status_text(TaskStatus status);

#endif /* ANALYZER_H */
