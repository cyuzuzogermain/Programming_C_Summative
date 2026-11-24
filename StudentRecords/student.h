/**
 * @file student.h
 * @brief Data structures and core operations for the Student Academic
 *        Records and Performance Analytics System.
 *
 * Relationships:
 *   StudentDB  1 ──► N  Student   (dynamic array, grown/shrunk with realloc)
 *   Student    1 ──► N  Course    (dynamic array, grown with realloc)
 */
#ifndef STUDENT_H
#define STUDENT_H

#include <stddef.h>

/* ---- Limits --------------------------------------------------------- */
#define MAX_NAME_LEN          32   /* first / last name (incl. '\0')      */
#define MAX_PROGRAMME_LEN     64
#define MAX_CODE_LEN          12   /* course code, e.g. "CS101"           */
#define MAX_COURSE_NAME_LEN   64
#define MAX_COURSES           20   /* per student                         */
#define MIN_STUDENT_ID        1
#define MAX_STUDENT_ID        99999999
#define MIN_YEAR              1
#define MAX_YEAR              7
#define MAX_MAX_MARKS         1000.0
#define PASS_PERCENTAGE       50.0 /* pass when mark >= 50% of maximum    */

/* ---- Structures ----------------------------------------------------- */

/** One course on a student's academic record. */
typedef struct {
    char   code[MAX_CODE_LEN];
    char   name[MAX_COURSE_NAME_LEN];
    double marks;                 /* marks obtained, 0..maxMarks          */
    double maxMarks;              /* maximum attainable marks, > 0        */
} Course;

/** One student and their dynamically allocated list of courses. */
typedef struct {
    int     id;                   /* unique identifier                    */
    char    firstName[MAX_NAME_LEN];
    char    lastName[MAX_NAME_LEN];
    char    programme[MAX_PROGRAMME_LEN];
    int     year;                 /* year of study                        */
    int     courseCount;          /* number of courses registered         */
    Course *courses;              /* heap array of courseCount items      */
} Student;

/** The whole collection of students. */
typedef struct {
    Student *students;            /* heap array of count items            */
    int      count;
} StudentDB;

/** Result codes returned by operations that can fail. */
typedef enum {
    STATUS_OK = 0,
    STATUS_DUPLICATE,             /* ID or course code already exists     */
    STATUS_NOT_FOUND,             /* student or course does not exist     */
    STATUS_INVALID,               /* value out of range / malformed       */
    STATUS_LIMIT,                 /* too many courses                     */
    STATUS_MEMORY,                /* malloc / realloc failed              */
    STATUS_FILE_MISSING,          /* data file does not exist yet         */
    STATUS_FILE_ERROR             /* cannot open / read / write a file    */
} Status;

/** Keys the student list can be sorted by. */
typedef enum {
    SORT_BY_ID = 1,
    SORT_BY_NAME,
    SORT_BY_AVERAGE,
    SORT_BY_PROGRAMME
} SortKey;

/* ---- Database lifetime ---------------------------------------------- */
void        db_init(StudentDB *db);
void        db_free(StudentDB *db);
const char *status_message(Status status);

/* ---- Student CRUD --------------------------------------------------- */
Status   register_student(StudentDB *db, const Student *student);
Status   delete_student(StudentDB *db, int id);
int      find_student_index(const StudentDB *db, int id);
Student *find_student(StudentDB *db, int id);

/* ---- Course records ------------------------------------------------- */
Status  validate_course(const Course *course);
Status  add_course(Student *student, const Course *course);
Course *find_course(Student *student, const char *code);
Status  update_course_marks(Course *course, double marks);

/* ---- Calculations --------------------------------------------------- */
int    calculate_student_average(const Student *student, double *average);
double course_percentage(const Course *course);
int    course_passed(const Course *course);

/* ---- Manual searching (results = indices, caller-provided array) ---- */
int search_by_id(const StudentDB *db, int id, int *results);
int search_by_name(const StudentDB *db, const char *query, int *results);
int search_by_programme(const StudentDB *db, const char *query, int *results);

/* ---- Manual sorting ------------------------------------------------- */
void sort_students(StudentDB *db, SortKey key, int ascending);

/* ---- Display -------------------------------------------------------- */
void print_student_table_header(void);
void print_student_row(const Student *student);
void display_all_students(const StudentDB *db);
void display_student_details(const Student *student);
void display_academic_performance(const Student *student);

/* ---- String helpers (case-insensitive, implemented manually) -------- */
int compare_ci(const char *a, const char *b);
int contains_ci(const char *haystack, const char *needle);

#endif /* STUDENT_H */
