/**
 * @file main.c
 * @brief Multi-threaded Document Search and Analysis System.
 *
 * Usage:  ./document_analyzer [-o output_dir] "keyword" file1 [file2 ...]
 *
 * One POSIX thread is created per input file. Every thread searches its
 * own file and writes its own report; the main thread then joins every
 * thread that was successfully created and prints a summary.
 *
 * Exit status: 0 if every file was processed, 1 if any task failed,
 *              2 for usage errors.
 */
#define _POSIX_C_SOURCE 200809L   /* getopt() */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "analyzer.h"

static void print_usage(const char *program) {
    fprintf(stderr,
            "Usage: %s [-o output_dir] \"keyword\" file1 [file2 ...]\n"
            "  Searches every file concurrently (one thread per file) for the keyword\n"
            "  (case-insensitive) and writes <file>_report.txt for each input.\n"
            "  -o DIR   directory for the reports (default: current directory)\n",
            program);
}

/** validate_keyword() - non-empty and free of line breaks. */
static int validate_keyword(const char *keyword) {
    if (keyword[0] == '\0') {
        fprintf(stderr, "[ERROR] The search keyword must not be empty.\n");
        return 0;
    }
    if (strpbrk(keyword, "\r\n") != NULL) {
        fprintf(stderr, "[ERROR] The search keyword must not contain line breaks.\n");
        return 0;
    }
    return 1;
}

/** validate_output_dir() - must be an existing, writable directory. */
static int validate_output_dir(const char *dir) {
    struct stat info;
    if (stat(dir, &info) != 0 || !S_ISDIR(info.st_mode)) {
        fprintf(stderr, "[ERROR] Output directory '%s' does not exist.\n", dir);
        return 0;
    }
    if (access(dir, W_OK | X_OK) != 0) {
        fprintf(stderr, "[ERROR] Output directory '%s' is not writable.\n", dir);
        return 0;
    }
    return 1;
}

/**
 * make_report_names_unique() - two inputs such as a/notes.txt and
 * b/notes.txt would both produce notes_report.txt; the later one gets
 * the thread number appended so no thread overwrites another's report.
 */
static void make_report_names_unique(ThreadTask *tasks, int count) {
    for (int i = 1; i < count; i++) {
        for (int j = 0; j < i; j++) {
            if (strcmp(tasks[i].reportFile, tasks[j].reportFile) == 0) {
                char *suffix = strstr(tasks[i].reportFile, "_report.txt");
                size_t room = sizeof tasks[i].reportFile - (size_t)(suffix - tasks[i].reportFile);
                snprintf(suffix, room, "_%d_report.txt", tasks[i].threadNumber);
                break;
            }
        }
    }
}

int main(int argc, char *argv[]) {
    const char *outputDir = ".";
    int option;

    while ((option = getopt(argc, argv, "o:h")) != -1) {
        switch (option) {
            case 'o': outputDir = optarg; break;
            case 'h': print_usage(argv[0]); return EXIT_SUCCESS;
            default:  print_usage(argv[0]); return 2;
        }
    }
    if (argc - optind < 2) {
        fprintf(stderr, "[ERROR] A keyword and at least one input file are required.\n");
        print_usage(argv[0]);
        return 2;
    }

    const char *keyword   = argv[optind];
    char      **files     = &argv[optind + 1];
    int         fileCount = argc - optind - 1;

    if (!validate_keyword(keyword) || !validate_output_dir(outputDir)) {
        return 2;
    }

    /* One task (= thread argument structure) per input file. */
    ThreadTask *tasks = calloc((size_t)fileCount, sizeof *tasks);
    if (tasks == NULL) {
        fprintf(stderr, "[ERROR] Not enough memory for %d thread task(s).\n", fileCount);
        return EXIT_FAILURE;
    }

    printf("===== MULTI-THREADED DOCUMENT ANALYZER =====\n\n");
    printf("Search Keyword: %s\n", keyword);
    printf("Number of Input Files: %d\n", fileCount);
    printf("Report Directory: %s\n\n", outputDir);

    for (int i = 0; i < fileCount; i++) {
        if (!init_thread_task(&tasks[i], i + 1, files[i], keyword, outputDir)) {
            tasks[i].status = TASK_REPORT_ERROR;
        }
    }
    make_report_names_unique(tasks, fileCount);

    /* Create every thread first so the files are processed concurrently. */
    for (int i = 0; i < fileCount; i++) {
        ThreadTask *task = &tasks[i];
        if (task->status != TASK_PENDING) {
            printf("[Thread %d] Not started for %s: %s\n", task->threadNumber, task->inputFile, task->errorMessage);
            continue;
        }

        int rc = pthread_create(&task->thread, NULL, process_file, task);
        if (rc != 0) {
            task->status = TASK_THREAD_ERROR;
            snprintf(task->errorMessage, sizeof task->errorMessage,
                     "pthread_create failed: %s", strerror(rc));
            printf("[Thread %d] ERROR: could not create thread for %s (%s)\n",
                   task->threadNumber, task->inputFile, strerror(rc));
            continue;
        }
        task->threadCreated = 1;
        printf("[Thread %d] Created for %s\n", task->threadNumber, task->inputFile);
    }

    /* Wait for every thread that was actually created. */
    for (int i = 0; i < fileCount; i++) {
        if (tasks[i].threadCreated) {
            int rc = pthread_join(tasks[i].thread, NULL);
            if (rc != 0) {
                fprintf(stderr, "[ERROR] pthread_join failed for thread %d: %s\n",
                        tasks[i].threadNumber, strerror(rc));
            }
        }
    }

    display_results(tasks, fileCount);

    int allSucceeded = 1;
    for (int i = 0; i < fileCount; i++) {
        if (tasks[i].status != TASK_SUCCESS) {
            allSucceeded = 0;
        }
        free_thread_task(&tasks[i]);
    }
    free(tasks);
    return allSucceeded ? EXIT_SUCCESS : EXIT_FAILURE;
}
