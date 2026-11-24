/**
 * @file main.c
 * @brief Menu-driven interface for the Student Academic Records and
 *        Performance Analytics System.
 *
 * Usage: ./student_records [data_file]      (default: students.txt)
 *
 * Records are loaded from the data file on start-up. Changes are kept in
 * memory until the user saves (menu option 12); on exit the program asks
 * to save any unsaved changes.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "input.h"
#include "report.h"
#include "storage.h"
#include "student.h"

#define DEFAULT_DATA_FILE   "students.txt"
#define REPORT_FILE         "academic_report.txt"

/* Shared state for the menu handlers. */
typedef struct {
    StudentDB   db;
    const char *dataFile;
    int         unsavedChanges;
} AppState;

/* ---------------------------------------------------------------------
 *  Small helpers
 * ------------------------------------------------------------------- */

static void print_status_error(Status status) {
    printf("  [ERROR] %s.\n", status_message(status));
}

/**
 * prompt_existing_student() - asks for an ID and returns that student,
 * or NULL (with a message) when it does not exist.
 */
static Student *prompt_existing_student(AppState *app) {
    int id;
    if (app->db.count == 0) {
        printf("No students registered yet.\n");
        return NULL;
    }
    if (!read_int("Enter student ID: ", MIN_STUDENT_ID, MAX_STUDENT_ID, &id)) {
        return NULL;
    }
    Student *student = find_student(&app->db, id);
    if (student == NULL) {
        printf("  [ERROR] No student with ID %d exists.\n", id);
    }
    return student;
}

/**
 * prompt_course() - reads and validates one course record.
 * The code must not already be on @student's record.
 */
static int prompt_course(Student *student, Course *course) {
    memset(course, 0, sizeof *course);
    for (;;) {
        if (!read_course_code("  Course code: ", course->code, sizeof course->code)) {
            return 0;
        }
        if (find_course(student, course->code) == NULL) {
            break;
        }
        printf("  [ERROR] %s is already on this student's record.\n", course->code);
    }
    return read_text("  Course name: ", course->name, sizeof course->name) &&
           read_double("  Maximum marks (1-1000): ", 1.0, MAX_MAX_MARKS, &course->maxMarks) &&
           read_double("  Marks obtained: ", 0.0, course->maxMarks, &course->marks);
}

/* ---------------------------------------------------------------------
 *  Menu handlers
 * ------------------------------------------------------------------- */

static void handle_register_student(AppState *app) {
    Student s;
    memset(&s, 0, sizeof s);

    printf("\n--- Register New Student ---\n");
    for (;;) {
        if (!read_int("Student ID (1-99999999): ", MIN_STUDENT_ID, MAX_STUDENT_ID, &s.id)) return;
        if (find_student_index(&app->db, s.id) < 0) break;
        printf("  [ERROR] Student ID %d already exists. IDs must be unique.\n", s.id);
    }
    if (!read_name("First name: ", s.firstName, sizeof s.firstName) ||
        !read_name("Last name: ", s.lastName, sizeof s.lastName) ||
        !read_text("Programme of study: ", s.programme, sizeof s.programme) ||
        !read_int("Year of study (1-7): ", MIN_YEAR, MAX_YEAR, &s.year)) {
        return;
    }

    Status status = register_student(&app->db, &s);
    if (status != STATUS_OK) {
        print_status_error(status);
        return;
    }
    app->unsavedChanges = 1;
    printf("Student %d registered.\n", s.id);

    int courses;
    if (!read_int("How many courses to add now? (0-20): ", 0, MAX_COURSES, &courses)) return;
    for (int i = 0; i < courses; i++) {
        /* Look the student up again: the array may have moved. */
        Student *student = find_student(&app->db, s.id);
        Course course;
        printf(" Course %d of %d\n", i + 1, courses);
        if (!prompt_course(student, &course)) return;
        status = add_course(student, &course);
        if (status != STATUS_OK) {
            print_status_error(status);
            return;
        }
    }
}

static void handle_view_student(AppState *app) {
    Student *student = prompt_existing_student(app);
    if (student != NULL) {
        display_student_details(student);
    }
}

static void handle_update_student(AppState *app) {
    Student *student = prompt_existing_student(app);
    if (student == NULL) return;

    printf("Press Enter to keep the current value.\n");
    char prompt[128];
    Student updated = *student;              /* edit a copy, commit at the end */

    snprintf(prompt, sizeof prompt, "First name [%s]: ", updated.firstName);
    if (!read_name_or_keep(prompt, updated.firstName, sizeof updated.firstName)) return;
    snprintf(prompt, sizeof prompt, "Last name [%s]: ", updated.lastName);
    if (!read_name_or_keep(prompt, updated.lastName, sizeof updated.lastName)) return;
    snprintf(prompt, sizeof prompt, "Programme [%s]: ", updated.programme);
    if (!read_text_or_keep(prompt, updated.programme, sizeof updated.programme)) return;
    snprintf(prompt, sizeof prompt, "Year of study [%d]: ", updated.year);
    if (!read_int_or_keep(prompt, MIN_YEAR, MAX_YEAR, &updated.year)) return;

    *student = updated;                      /* courses pointer is unchanged */
    app->unsavedChanges = 1;
    printf("Student %d updated.\n", student->id);
}

static void handle_delete_student(AppState *app) {
    Student *student = prompt_existing_student(app);
    if (student == NULL) return;

    int id = student->id, confirm;
    char question[160];
    snprintf(question, sizeof question, "Delete %s %s (ID %d) and all their courses? (y/n): ",
             student->firstName, student->lastName, id);
    if (!read_yes_no(question, &confirm)) return;
    if (!confirm) {
        printf("Deletion cancelled.\n");
        return;
    }
    Status status = delete_student(&app->db, id);
    if (status != STATUS_OK) {
        print_status_error(status);
        return;
    }
    app->unsavedChanges = 1;
    printf("Student %d deleted.\n", id);
}

static void handle_add_course(AppState *app) {
    Student *student = prompt_existing_student(app);
    if (student == NULL) return;
    if (student->courseCount >= MAX_COURSES) {
        print_status_error(STATUS_LIMIT);
        return;
    }
    Course course;
    if (!prompt_course(student, &course)) return;

    Status status = add_course(student, &course);
    if (status != STATUS_OK) {
        print_status_error(status);
        return;
    }
    app->unsavedChanges = 1;
    printf("Course %s added to student %d.\n", course.code, student->id);
}

static void handle_update_marks(AppState *app) {
    Student *student = prompt_existing_student(app);
    if (student == NULL) return;
    if (student->courseCount == 0) {
        printf("  [ERROR] This student has no registered courses.\n");
        return;
    }

    display_academic_performance(student);
    char code[MAX_CODE_LEN];
    if (!read_course_code("Course code to update: ", code, sizeof code)) return;

    Course *course = find_course(student, code);
    if (course == NULL) {
        printf("  [ERROR] Course %s is not on this student's record.\n", code);
        return;
    }

    char prompt[96];
    double marks;
    snprintf(prompt, sizeof prompt, "New marks for %s (0-%.2f) [current %.2f]: ",
             course->code, course->maxMarks, course->marks);
    if (!read_double(prompt, 0.0, course->maxMarks, &marks)) return;

    Status status = update_course_marks(course, marks);
    if (status != STATUS_OK) {
        print_status_error(status);
        return;
    }
    app->unsavedChanges = 1;
    printf("Marks for %s updated to %.2f / %.2f.\n", course->code, course->marks, course->maxMarks);
}

static void handle_show_performance(AppState *app) {
    Student *student = prompt_existing_student(app);
    if (student != NULL) {
        display_academic_performance(student);
    }
}

static void handle_search(AppState *app) {
    if (app->db.count == 0) {
        printf("No students registered yet.\n");
        return;
    }
    printf("\nSearch by:\n 1. Student ID\n 2. First or last name\n 3. Programme of study\n");
    int choice;
    if (!read_int("Choice: ", 1, 3, &choice)) return;

    int *results = malloc((size_t)app->db.count * sizeof *results);
    if (results == NULL) {
        print_status_error(STATUS_MEMORY);
        return;
    }

    int found = 0;
    char query[MAX_PROGRAMME_LEN];
    if (choice == 1) {
        int id;
        if (read_int("Student ID: ", MIN_STUDENT_ID, MAX_STUDENT_ID, &id)) {
            found = search_by_id(&app->db, id, results);
        }
    } else if (read_text(choice == 2 ? "Name (or part of it): " : "Programme (or part of it): ",
                         query, sizeof query)) {
        found = (choice == 2) ? search_by_name(&app->db, query, results)
                              : search_by_programme(&app->db, query, results);
    }

    if (found == 0) {
        printf("No matching students found.\n");
    } else {
        printf("%d matching student(s):\n", found);
        print_student_table_header();
        for (int i = 0; i < found; i++) {
            print_student_row(&app->db.students[results[i]]);
        }
    }
    free(results);
}

static void handle_sort(AppState *app) {
    if (app->db.count < 2) {
        printf("At least two students are needed to sort.\n");
        return;
    }
    printf("\nSort by:\n 1. Student ID\n 2. Name (alphabetical)\n 3. Overall average\n 4. Programme of study\n");
    int key, order;
    if (!read_int("Choice: ", 1, 4, &key)) return;
    if (!read_int("Order: 1 = ascending, 2 = descending: ", 1, 2, &order)) return;

    sort_students(&app->db, (SortKey)key, order == 1);
    app->unsavedChanges = 1;                 /* the saved order changes too */
    printf("Students sorted.\n");
    display_all_students(&app->db);
}

static void handle_report(AppState *app) {
    generate_academic_report(&app->db, stdout);

    int save;
    if (!read_yes_no("Save this report to " REPORT_FILE "? (y/n): ", &save) || !save) return;

    FILE *file = fopen(REPORT_FILE, "w");
    if (file == NULL) {
        perror("  [ERROR] Cannot create " REPORT_FILE);
        return;
    }
    generate_academic_report(&app->db, file);
    if (fclose(file) != 0) {
        perror("  [ERROR] Failed to write " REPORT_FILE);
        return;
    }
    printf("Report saved to %s.\n", REPORT_FILE);
}

static void handle_save(AppState *app) {
    Status status = save_students(&app->db, app->dataFile);
    if (status != STATUS_OK) {
        print_status_error(status);
        return;
    }
    app->unsavedChanges = 0;
    printf("%d student record(s) saved to %s.\n", app->db.count, app->dataFile);
}

/** handle_exit() - Return: 1 if the program should really exit. */
static int handle_exit(AppState *app) {
    if (!app->unsavedChanges || input_ended()) {
        if (app->unsavedChanges) {
            printf("[WARNING] Input ended - unsaved changes were discarded.\n");
        }
        return 1;
    }
    int save;
    if (!read_yes_no("You have unsaved changes. Save before exiting? (y/n): ", &save)) {
        return 1;
    }
    if (save) {
        handle_save(app);
        if (app->unsavedChanges) {
            int quit;
            if (!read_yes_no("Saving failed. Exit anyway? (y/n): ", &quit)) return 1;
            return quit;
        }
    }
    return 1;
}

static void print_menu(const AppState *app) {
    printf("\n========= STUDENT ACADEMIC RECORDS SYSTEM =========\n");
    printf("  Students: %d   Data file: %s%s\n", app->db.count, app->dataFile,
           app->unsavedChanges ? "   (unsaved changes)" : "");
    printf(" 1. Register a new student\n");
    printf(" 2. Display all students\n");
    printf(" 3. View student details\n");
    printf(" 4. Update student information\n");
    printf(" 5. Delete a student\n");
    printf(" 6. Add a course to a student\n");
    printf(" 7. Update course marks\n");
    printf(" 8. Display a student's academic performance\n");
    printf(" 9. Search students\n");
    printf("10. Sort students\n");
    printf("11. Generate academic report\n");
    printf("12. Save records to file\n");
    printf(" 0. Exit\n");
}

int main(int argc, char *argv[]) {
    AppState app;
    db_init(&app.db);
    app.dataFile       = (argc > 1) ? argv[1] : DEFAULT_DATA_FILE;
    app.unsavedChanges = 0;

    Status status = load_students(&app.db, app.dataFile);
    if (status == STATUS_OK) {
        printf("Loaded %d student record(s) from %s.\n", app.db.count, app.dataFile);
    } else if (status == STATUS_FILE_MISSING) {
        printf("No data file '%s' found - starting with an empty database.\n", app.dataFile);
    } else {
        fprintf(stderr, "[ERROR] Could not load '%s': %s.\n", app.dataFile, status_message(status));
        if (status == STATUS_MEMORY) {
            db_free(&app.db);
            return EXIT_FAILURE;
        }
    }

    int running = 1;
    while (running) {
        print_menu(&app);
        int choice;
        if (!read_int("Select an option: ", 0, 12, &choice)) {
            running = !handle_exit(&app);
            continue;
        }
        switch (choice) {
            case 1:  handle_register_student(&app);      break;
            case 2:  display_all_students(&app.db);      break;
            case 3:  handle_view_student(&app);          break;
            case 4:  handle_update_student(&app);        break;
            case 5:  handle_delete_student(&app);        break;
            case 6:  handle_add_course(&app);            break;
            case 7:  handle_update_marks(&app);          break;
            case 8:  handle_show_performance(&app);      break;
            case 9:  handle_search(&app);                break;
            case 10: handle_sort(&app);                  break;
            case 11: handle_report(&app);                break;
            case 12: handle_save(&app);                  break;
            case 0:  running = !handle_exit(&app);       break;
        }
        if (input_ended()) {
            running = !handle_exit(&app);
        }
    }

    db_free(&app.db);
    printf("Goodbye!\n");
    return EXIT_SUCCESS;
}
