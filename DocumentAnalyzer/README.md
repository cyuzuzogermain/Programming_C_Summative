# Multi-threaded Document Search and Analysis System

`document_analyzer` searches several text files concurrently using POSIX threads, with **one thread per file**. Each thread counts lines, keyword occurrences and matching lines in its file, then writes its own report.

```bash
make
./document_analyzer "IoT" document1.txt document2.txt document3.txt
./document_analyzer -o reports "IoT" docs/*.txt      # reports go to ./reports/
```

The search is case-insensitive and counts non-overlapping substrings, so `IoT` matches `iot`, `IOT` and `IoT-based`. Exit status is 0 if every file succeeded, 1 if any task failed, and 2 for usage errors.

## Program structure

| File | Contents |
|------|----------|
| `analyzer.h` | The `ThreadTask` and `LineMatch` structures, the `TaskStatus` enum and the function prototypes |
| `analyzer.c` | Thread entry point `process_file()`, file analysis, keyword counting, report generation, result display |
| `main.c` | Argument parsing and validation, task set-up, thread creation and joining, summary, clean-up |

### Thread argument structure

```c
typedef struct {
    /* inputs: set by main before pthread_create() */
    int threadNumber;  const char *inputFile;  const char *keyword;  char reportFile[1024];
    /* outputs: written only by the owning thread */
    long totalLines, totalOccurrences, matchingLines;
    LineMatch *matches;  size_t matchCapacity;      /* dynamic array (realloc) */
    TaskStatus status;  char errorMessage[256];
    /* bookkeeping for main */
    pthread_t thread;  int threadCreated;
} ThreadTask;
```

`main` allocates one `ThreadTask` per file with `calloc` and passes `&tasks[i]` as the thread argument.

### Functions

These map to the brief's "Modular programming" list:

| Function | Role |
|----------|------|
| `init_thread_task()` | Sets up a thread's arguments and builds its report path (`docs/document1.txt` → `./document1_report.txt`) |
| `process_file()` | The pthread start routine. It analyses the file, writes the report, and prints its status. |
| `analyse_file()` | Checks the file with `stat` (it must exist and be a regular file), opens it, and reads it line by line with `getline` so any line length works |
| `count_keyword_occurrences()` | Manual, case-insensitive search that counts non-overlapping occurrences |
| `record_match()` | Stores the line number, the count on that line and a 120-character excerpt in a dynamic array that doubles with `realloc` |
| `generate_report()` | Writes the per-file report |
| `display_results()` | Main thread: prints the processing summary |
| `free_thread_task()` | Frees the excerpts and the match array |

## Concurrency design: why no synchronisation is needed

- Every thread reads its own input file and writes its own report file.
- Every thread writes only into its own `ThreadTask`. The shared data (the keyword string, and the file names in `argv`) is read-only.
- The main thread reads a task's results only after `pthread_join()` on that thread, and the join guarantees all of the thread's writes are visible.
- Each status message is a single `printf` call. glibc locks `stdout` per call, so lines never mix, although their order varies from run to run. That variation is the visible sign that the threads run concurrently.
- Threads use `strerror_r` (thread-safe) instead of `strerror`.
- Two inputs with the same file name in different folders, such as `a/notes.txt` and `b/notes.txt`, would produce the same report name. `main` detects this before starting any threads and gives the later one a unique name (`notes_2_report.txt`), so no two threads write the same file.

`main` first creates all the threads, then joins every thread that was created successfully.

## Report contents

Every input file gets a report, including files that failed:

- input file name, search keyword, thread number and timestamp
- total lines processed
- total keyword occurrences
- number of matching lines
- the line numbers where matches were found, or "none - keyword not found"
- each matching line with its own occurrence count
- **processing status**: `SUCCESS`, or `FAILED - <reason>`

## Error handling

| Case | Behaviour |
|------|-----------|
| Missing file | The thread reports `cannot access file: No such file or directory`, a FAILED report is written, and the other threads continue |
| Directory or other non-regular file | `not a regular file` |
| Permission denied | `cannot open file: Permission denied` |
| Read error | Detected with `ferror` |
| Keyword not found | The file still counts as success. The report and summary both say the keyword was not found. |
| Report cannot be written | The task is marked `Failed (report could not be written)` |
| Out of memory while recording matches | The task is marked `Failed (out of memory)` |
| `pthread_create` fails | The task is marked failed with the error and is **not** joined. All other threads are still created and joined. |
| Bad usage: missing arguments, empty keyword, or an output directory that doesn't exist or isn't writable | Usage message and exit status 2 |
| `calloc` fails for the tasks | An error message, and the program exits |

## Evidence (`samples/`)

| File | Contents |
|------|----------|
| `document1.txt`, `document2.txt`, `document3.txt` | Sample inputs. The third does not contain "IoT". |
| `document*_report.txt` | The reports generated by run 1 |
| `demo_run.txt` | Four runs: (1) the normal run; (2) a missing file, a directory and a permission-denied file in one run; (3) usage errors; (4) duplicate report names resolved |

The build is clean with `-std=c11 -Wall -Wextra -Wpedantic -pthread`. A run with an allocation-counting wrapper showed 0 blocks still allocated at exit.
