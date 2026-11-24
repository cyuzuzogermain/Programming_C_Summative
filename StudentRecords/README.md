# Student Academic Records and Performance Analytics System

A menu-driven C11 application for an unknown number of students. It supports CRUD on students and their courses, manual searching and sorting, performance analytics, and persistence to a text file.

```bash
make                         # builds ./student_records
./student_records            # uses students.txt
./student_records other.txt  # uses a different data file
```

## Files

| File | Responsibility |
|------|----------------|
| `student.h` / `student.c` | Structures, student and course CRUD, average calculations, manual search, manual sort, console display |
| `storage.h` / `storage.c` | Save and load the text data file |
| `report.h` / `report.c` | Academic performance report |
| `input.h` / `input.c` | Validated input: integers, decimals, names, course codes, y/n |
| `main.c` | Menu and handlers: register, update, delete, add course, update marks, search, sort, report, save |
| `students.txt` | The working data file, loaded at start-up |
| `samples/` | Sample data, demo transcript, sample report and a corrupted-file example |

## 1. Structure definitions and relationships

```
StudentDB ──owns──► Student[count] ──each owns──► Course[courseCount]
 (students, count)    (heap array)                  (heap array)
```

- **`Course`**: `code`, `name`, `marks`, `maxMarks`.
- **`Student`**: `id` (unique), `firstName`, `lastName`, `programme`, `year`, `courseCount`, and `Course *courses`.
- **`StudentDB`**: `Student *students` and `count`.
- **`Status`**: an enum of result codes (`STATUS_OK`, `STATUS_DUPLICATE`, `STATUS_NOT_FOUND`, `STATUS_INVALID`, `STATUS_LIMIT`, `STATUS_MEMORY`, `STATUS_FILE_MISSING`, `STATUS_FILE_ERROR`). Every operation that can fail returns one, and `status_message()` converts it to text.

## 2. Pointer usage

- Every operation receives a pointer to the data it works on, for example `register_student(StudentDB *db, const Student *s)`, `add_course(Student *student, const Course *course)` and `update_course_marks(Course *course, double marks)`. The `const` keyword marks read-only access.
- `find_student()` and `find_course()` return pointers into the dynamic arrays, so updates change the stored record directly.
- Because `realloc` can move the array, `main.c` looks the student up again after any call that may add records, instead of keeping a stale pointer.
- `sort_students()` selects its comparator through a function pointer (`StudentComparator`).
- Searches write matching array indices into a caller-provided `int *results` buffer.

## 3. Dynamic memory allocation and deallocation

| Action | Code |
|--------|------|
| First student | `realloc(NULL, size)`, which behaves like `malloc` |
| Add a student | `realloc` grows the array by one element |
| Delete a student | The student's `courses` array is freed, later records shift left, and `realloc` shrinks the array by one. When the count reaches 0 the array is freed and set to `NULL`. |
| Add a course | `realloc` grows `student->courses` |
| Search buffers | `malloc(count * sizeof(int))`, freed after use |
| Report tables | `calloc` for the per-course and per-programme statistics, freed at the end of the report |
| Exit | `db_free()` frees every course array, then the student array |

Every allocation is checked. If `realloc` fails, the old block is still valid, so the function returns `STATUS_MEMORY` and nothing is lost. A run with an allocation-counting wrapper showed 0 blocks still allocated at exit.

## 4. Searching algorithms (manual)

All searches are linear (O(n)) and written by hand:

- **By ID**: `find_student_index()` compares each ID.
- **By name**: `contains_ci()` is a hand-written, case-insensitive substring search. It matches against both first and last names.
- **By programme**: the same partial, case-insensitive match on the programme field.

## 5. Sorting algorithm (manual)

`sort_students()` uses **insertion sort**, with no `qsort()`. Each record is taken out and earlier records are shifted right until the record's place is found. Ascending or descending order flips the sign of the comparator result. The sort is stable and O(n²), which is fine for class-sized data.

| Key | Comparator | Tie-breakers |
|-----|-----------|--------------|
| Student ID | `compare_by_id` | – |
| Name | `compare_by_name` | Last name, then first name, then ID; case-insensitive |
| Overall average | `compare_by_average` | Students with no courses sort as lowest |
| Programme | `compare_by_programme` | Then name |

## 6. File handling

The data file is plain text with fields separated by `|`. The `|` character is rejected in all user input so it cannot corrupt the file.

```
STUDENT|1003|Aline|Mukamana|Software Engineering|2|3
COURSE|CS101|Introduction to Programming|85.00|100.00
...
```

- **Save** writes `students.txt.tmp` first, checks every write and `fclose()`, then renames the file over `students.txt`. A failed save never destroys the previous file.
- **Load** runs at start-up. Each line goes through `register_student()` and `add_course()`, which rebuilds the dynamic arrays and applies the same validation as typed input. Malformed lines, duplicate IDs, out-of-range marks and orphan course lines are reported with their line numbers and skipped. A missing file starts an empty database, and other open or read errors are reported.
- Unsaved changes are flagged in the menu header. On exit, the program asks whether to save them.

## 7. Academic performance calculations

| Metric | Definition |
|--------|------------|
| Course percentage | marks ÷ maxMarks × 100 |
| Course pass | percentage ≥ 50% |
| Student average | Σ marks ÷ Σ maxMarks × 100, so a course out of 200 counts twice as much as one out of 100 |
| Student passing or failing | Student average ≥ 50%, or below it |
| Overall average | Mean of the averages of students who have at least one course |
| Highest and lowest | The student with the maximum and minimum average |
| Average per course | Mean percentage per course code across every student who took it, plus the pass count |
| Students per programme | Case-insensitive grouping |

Students with no courses show "N/A" and are counted as "not assessed". `calculate_student_average()` returns 0 instead of dividing by zero.

## 8. Error handling and testing

Input validation:

- IDs must be integers from 1 to 99,999,999, and duplicate IDs are rejected.
- Names may contain only letters, spaces, `-` and `'`.
- Year of study must be 1–7, and at most 20 courses per student.
- Course codes must be 2–11 alphanumeric characters, unique per student.
- Max marks must be 1–1000, and marks must be between 0 and the maximum.
- Non-numeric input, over-long lines, out-of-range menu choices, and operations on non-existent students or courses are all rejected with a message.
- End of input causes a clean exit.

Testing:

- `samples/demo_run.txt` is a scripted session. Piped input is echoed so the transcript shows what was typed. It covers:
  - duplicate ID, invalid name, year and marks
  - registering with courses, viewing, updating and deleting (cancelled, confirmed, and on a missing ID)
  - adding a course and updating marks
  - all three searches and all four sorts
  - the report, save, invalid menu input, and a reload in a second run
- The program loads `samples/corrupted_example.txt` and reports each bad line.
- A run with an allocation-counting wrapper reported **0 blocks still allocated** after the full demo.
- The build is clean with `-std=c11 -Wall -Wextra -Wpedantic`.

## Evidence (`samples/`)

`students.txt` (sample data), `demo_run.txt`, `sample_academic_report.txt`, `students_after_demo.txt` (the file saved by the demo), `corrupted_example.txt`.
