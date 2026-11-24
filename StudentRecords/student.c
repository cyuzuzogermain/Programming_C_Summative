/**
 * @file student.c
 * @brief Core student/course operations: CRUD, calculations, manual
 *        searching, manual sorting and console display.
 */
#include "student.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =====================================================================
 *  String helpers
 * ===================================================================== */

/**
 * compare_ci() - case-insensitive string comparison.
 * Return: <0, 0 or >0 like strcmp().
 */
int compare_ci(const char *a, const char *b) {
    while (*a != '\0' && *b != '\0') {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) {
            return ca - cb;
        }
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

/**
 * contains_ci() - case-insensitive substring test (naive manual search).
 * Return: 1 if @needle occurs in @haystack, else 0.
 */
int contains_ci(const char *haystack, const char *needle) {
    size_t hlen = strlen(haystack);
    size_t nlen = strlen(needle);

    if (nlen == 0) {
        return 1;
    }
    for (size_t start = 0; start + nlen <= hlen; start++) {
        size_t i = 0;
        while (i < nlen &&
               tolower((unsigned char)haystack[start + i]) ==
               tolower((unsigned char)needle[i])) {
            i++;
        }
        if (i == nlen) {
            return 1;
        }
    }
    return 0;
}

/* =====================================================================
 *  Database lifetime
 * ===================================================================== */

/** db_init() - start with an empty database (no allocation yet). */
void db_init(StudentDB *db) {
    db->students = NULL;
    db->count    = 0;
}

/** db_free() - release every course array, then the student array. */
void db_free(StudentDB *db) {
    if (db == NULL) {
        return;
    }
    for (int i = 0; i < db->count; i++) {
        free(db->students[i].courses);
        db->students[i].courses = NULL;
    }
    free(db->students);
    db->students = NULL;
    db->count    = 0;
}

/** status_message() - human-readable text for a Status code. */
const char *status_message(Status status) {
    switch (status) {
        case STATUS_OK:           return "Success";
        case STATUS_DUPLICATE:    return "Duplicate record";
        case STATUS_NOT_FOUND:    return "Record not found";
        case STATUS_INVALID:      return "Invalid data";
        case STATUS_LIMIT:        return "Course limit reached";
        case STATUS_MEMORY:       return "Memory allocation failed";
        case STATUS_FILE_MISSING: return "Data file not found";
        case STATUS_FILE_ERROR:   return "File access error";
    }
    return "Unknown error";
}

/* =====================================================================
 *  Student CRUD
 * ===================================================================== */

/**
 * find_student_index() - linear search by ID.
 * Return: array index, or -1 if no student has that ID.
 */
int find_student_index(const StudentDB *db, int id) {
    for (int i = 0; i < db->count; i++) {
        if (db->students[i].id == id) {
            return i;
        }
    }
    return -1;
}

/** find_student() - pointer to the student with @id, or NULL. */
Student *find_student(StudentDB *db, int id) {
    int index = find_student_index(db, id);
    return (index >= 0) ? &db->students[index] : NULL;
}

/**
 * register_student() - append a copy of @student to the database.
 * The array grows by one element with realloc(). Ownership of
 * student->courses moves into the database on success.
 */
Status register_student(StudentDB *db, const Student *student) {
    if (student->id < MIN_STUDENT_ID || student->id > MAX_STUDENT_ID ||
        student->year < MIN_YEAR || student->year > MAX_YEAR ||
        student->firstName[0] == '\0' || student->lastName[0] == '\0' ||
        student->programme[0] == '\0') {
        return STATUS_INVALID;
    }
    if (find_student_index(db, student->id) >= 0) {
        return STATUS_DUPLICATE;
    }

    /* realloc(NULL, n) behaves like malloc(n) for the first student. */
    Student *grown = realloc(db->students, (size_t)(db->count + 1) * sizeof *grown);
    if (grown == NULL) {
        return STATUS_MEMORY;              /* old array is still valid */
    }
    db->students = grown;
    db->students[db->count] = *student;    /* struct copy */
    db->count++;
    return STATUS_OK;
}

/**
 * delete_student() - remove the student with @id, freeing their courses
 * and shrinking the array with realloc().
 */
Status delete_student(StudentDB *db, int id) {
    int index = find_student_index(db, id);
    if (index < 0) {
        return STATUS_NOT_FOUND;
    }

    free(db->students[index].courses);

    /* Shift the following records one place to the left. */
    for (int i = index; i < db->count - 1; i++) {
        db->students[i] = db->students[i + 1];
    }
    db->count--;

    if (db->count == 0) {
        free(db->students);
        db->students = NULL;
        return STATUS_OK;
    }

    Student *shrunk = realloc(db->students, (size_t)db->count * sizeof *shrunk);
    if (shrunk != NULL) {
        db->students = shrunk;
    }
    /* If shrinking fails the larger block is still valid, so nothing is lost. */
    return STATUS_OK;
}

/* =====================================================================
 *  Course records
 * ===================================================================== */

/** validate_course() - checks code, name and mark ranges. */
Status validate_course(const Course *course) {
    if (course->code[0] == '\0' || course->name[0] == '\0') {
        return STATUS_INVALID;
    }
    if (course->maxMarks <= 0.0 || course->maxMarks > MAX_MAX_MARKS) {
        return STATUS_INVALID;
    }
    if (course->marks < 0.0 || course->marks > course->maxMarks) {
        return STATUS_INVALID;
    }
    return STATUS_OK;
}

/** find_course() - course on @student with @code (case-insensitive), or NULL. */
Course *find_course(Student *student, const char *code) {
    for (int i = 0; i < student->courseCount; i++) {
        if (compare_ci(student->courses[i].code, code) == 0) {
            return &student->courses[i];
        }
    }
    return NULL;
}

/**
 * add_course() - validates @course and appends it to @student's record,
 * growing the course array with realloc().
 */
Status add_course(Student *student, const Course *course) {
    Status valid = validate_course(course);
    if (valid != STATUS_OK) {
        return valid;
    }
    if (student->courseCount >= MAX_COURSES) {
        return STATUS_LIMIT;
    }
    if (find_course(student, course->code) != NULL) {
        return STATUS_DUPLICATE;
    }

    Course *grown = realloc(student->courses,
                            (size_t)(student->courseCount + 1) * sizeof *grown);
    if (grown == NULL) {
        return STATUS_MEMORY;
    }
    student->courses = grown;
    student->courses[student->courseCount] = *course;
    student->courseCount++;
    return STATUS_OK;
}

/** update_course_marks() - sets new marks if 0 <= marks <= maxMarks. */
Status update_course_marks(Course *course, double marks) {
    if (marks < 0.0 || marks > course->maxMarks) {
        return STATUS_INVALID;
    }
    course->marks = marks;
    return STATUS_OK;
}

/* =====================================================================
 *  Calculations
 * ===================================================================== */

/** course_percentage() - marks as a percentage of the maximum. */
double course_percentage(const Course *course) {
    if (course->maxMarks <= 0.0) {
        return 0.0;                          /* guarded: never divide by 0 */
    }
    return course->marks / course->maxMarks * 100.0;
}

/** course_passed() - 1 when the mark is at least 50% of the maximum. */
int course_passed(const Course *course) {
    return course_percentage(course) >= PASS_PERCENTAGE;
}

/**
 * calculate_student_average() - overall average as a percentage:
 *     (sum of marks obtained / sum of maximum marks) * 100
 * so a course out of 200 weighs twice as much as one out of 100.
 * Return: 1 and stores the value in *average, or 0 if the student has
 *         no courses (prevents division by zero).
 */
int calculate_student_average(const Student *student, double *average) {
    double obtained = 0.0;
    double maximum  = 0.0;

    for (int i = 0; i < student->courseCount; i++) {
        obtained += student->courses[i].marks;
        maximum  += student->courses[i].maxMarks;
    }
    if (student->courseCount == 0 || maximum <= 0.0) {
        *average = 0.0;
        return 0;
    }
    *average = obtained / maximum * 100.0;
    return 1;
}

/* =====================================================================
 *  Manual searching (linear search; results are array indices)
 * ===================================================================== */

/** search_by_id() - Return: number of matches (0 or 1). */
int search_by_id(const StudentDB *db, int id, int *results) {
    int index = find_student_index(db, id);
    if (index < 0) {
        return 0;
    }
    results[0] = index;
    return 1;
}

/** search_by_name() - partial, case-insensitive match on first OR last name. */
int search_by_name(const StudentDB *db, const char *query, int *results) {
    int found = 0;
    for (int i = 0; i < db->count; i++) {
        const Student *s = &db->students[i];
        if (contains_ci(s->firstName, query) || contains_ci(s->lastName, query)) {
            results[found++] = i;
        }
    }
    return found;
}

/** search_by_programme() - partial, case-insensitive match on programme. */
int search_by_programme(const StudentDB *db, const char *query, int *results) {
    int found = 0;
    for (int i = 0; i < db->count; i++) {
        if (contains_ci(db->students[i].programme, query)) {
            results[found++] = i;
        }
    }
    return found;
}

/* =====================================================================
 *  Manual sorting (insertion sort with a comparator function pointer)
 * ===================================================================== */

typedef int (*StudentComparator)(const Student *a, const Student *b);

static int compare_by_id(const Student *a, const Student *b) {
    return (a->id > b->id) - (a->id < b->id);
}

/** Alphabetical by last name, then first name, then ID as a tie-breaker. */
static int compare_by_name(const Student *a, const Student *b) {
    int result = compare_ci(a->lastName, b->lastName);
    if (result == 0) {
        result = compare_ci(a->firstName, b->firstName);
    }
    return (result != 0) ? result : compare_by_id(a, b);
}

/** By overall average; students with no courses count as -1 (lowest). */
static int compare_by_average(const Student *a, const Student *b) {
    double avgA, avgB;
    if (!calculate_student_average(a, &avgA)) avgA = -1.0;
    if (!calculate_student_average(b, &avgB)) avgB = -1.0;
    if (avgA < avgB) return -1;
    if (avgA > avgB) return 1;
    return compare_by_id(a, b);
}

/** By programme, then by name within the programme. */
static int compare_by_programme(const Student *a, const Student *b) {
    int result = compare_ci(a->programme, b->programme);
    return (result != 0) ? result : compare_by_name(a, b);
}

/**
 * sort_students() - insertion sort, written by hand (no qsort()).
 * Each record is lifted out and shifted left past every record that
 * should come after it. Stable, O(n^2) worst case, fast for nearly
 * sorted data, and records are moved by struct assignment.
 */
void sort_students(StudentDB *db, SortKey key, int ascending) {
    StudentComparator compare;
    switch (key) {
        case SORT_BY_NAME:      compare = compare_by_name;      break;
        case SORT_BY_AVERAGE:   compare = compare_by_average;   break;
        case SORT_BY_PROGRAMME: compare = compare_by_programme; break;
        case SORT_BY_ID:
        default:                compare = compare_by_id;        break;
    }

    for (int i = 1; i < db->count; i++) {
        Student current = db->students[i];
        int j = i - 1;
        while (j >= 0) {
            int order = compare(&db->students[j], &current);
            if (!ascending) {
                order = -order;
            }
            if (order <= 0) {
                break;
            }
            db->students[j + 1] = db->students[j];
            j--;
        }
        db->students[j + 1] = current;
    }
}

/* =====================================================================
 *  Display
 * ===================================================================== */

void print_student_table_header(void) {
    printf("%-10s %-24s %-30s %4s %7s %8s\n",
           "ID", "Name", "Programme", "Year", "Courses", "Average");
    printf("---------------------------------------------------------------------------------------\n");
}

void print_student_row(const Student *student) {
    char fullName[MAX_NAME_LEN * 2];
    double average;

    snprintf(fullName, sizeof fullName, "%s %s", student->firstName, student->lastName);
    printf("%-10d %-24.24s %-30.30s %4d %7d ",
           student->id, fullName, student->programme,
           student->year, student->courseCount);
    if (calculate_student_average(student, &average)) {
        printf("%7.2f%%\n", average);
    } else {
        printf("%8s\n", "N/A");
    }
}

void display_all_students(const StudentDB *db) {
    if (db->count == 0) {
        printf("No students registered yet.\n");
        return;
    }
    print_student_table_header();
    for (int i = 0; i < db->count; i++) {
        print_student_row(&db->students[i]);
    }
    printf("Total: %d student(s)\n", db->count);
}

void display_student_details(const Student *student) {
    printf("\n----------- Student Details -----------\n");
    printf("Student ID   : %d\n", student->id);
    printf("First name   : %s\n", student->firstName);
    printf("Last name    : %s\n", student->lastName);
    printf("Programme    : %s\n", student->programme);
    printf("Year of study: %d\n", student->year);
    printf("Courses      : %d\n", student->courseCount);
    display_academic_performance(student);
}

void display_academic_performance(const Student *student) {
    double average;
    int passed = 0;

    printf("\nAcademic performance of %s %s (ID %d)\n",
           student->firstName, student->lastName, student->id);

    if (student->courseCount == 0) {
        printf("No courses registered - average cannot be calculated.\n");
        return;
    }

    printf("%-10s %-32s %8s %8s %8s %6s\n", "Code", "Course", "Marks", "Max", "Percent", "Result");
    printf("--------------------------------------------------------------------------\n");
    for (int i = 0; i < student->courseCount; i++) {
        const Course *c = &student->courses[i];
        int ok = course_passed(c);
        passed += ok;
        printf("%-10s %-32.32s %8.2f %8.2f %7.2f%% %6s\n",
               c->code, c->name, c->marks, c->maxMarks,
               course_percentage(c), ok ? "PASS" : "FAIL");
    }
    calculate_student_average(student, &average);
    printf("--------------------------------------------------------------------------\n");
    printf("Overall average: %.2f%%  |  Passed %d of %d course(s)  |  Status: %s\n",
           average, passed, student->courseCount,
           average >= PASS_PERCENTAGE ? "PASSING" : "FAILING");
}
