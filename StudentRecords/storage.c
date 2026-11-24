/**
 * @file storage.c
 * @brief Text-file persistence for the student database.
 *
 * Saving writes to "<path>.tmp" first and renames it over the real file,
 * so a failed write never destroys the previous data.
 *
 * Loading rebuilds every dynamic array through the same functions the
 * menu uses (register_student / add_course), so loaded records are
 * validated exactly like typed-in ones. Bad lines are reported with their
 * line number and skipped instead of aborting the whole load.
 */
#include "storage.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_BUFFER   512
#define MAX_FIELDS    8

/**
 * split_fields() - splits @line in place on '|' (empty fields are kept,
 * unlike strtok). Return: number of fields stored in @fields.
 */
static int split_fields(char *line, char **fields, int maxFields) {
    int count = 0;
    char *cursor = line;

    fields[count++] = cursor;
    while (*cursor != '\0') {
        if (*cursor == '|') {
            *cursor = '\0';
            if (count == maxFields) {
                return maxFields + 1;            /* too many fields */
            }
            fields[count++] = cursor + 1;
        }
        cursor++;
    }
    return count;
}

/** parse_int() - strict string-to-int; Return: 1 on success. */
static int parse_int(const char *text, int *out) {
    char *end;
    errno = 0;
    long value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < -2147483647L || value > 2147483647L) {
        return 0;
    }
    *out = (int)value;
    return 1;
}

/** parse_double() - strict string-to-double; Return: 1 on success. */
static int parse_double(const char *text, double *out) {
    char *end;
    errno = 0;
    double value = strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0') {
        return 0;
    }
    *out = value;
    return 1;
}

/** copy_field() - bounded copy; Return: 0 if empty or too long. */
static int copy_field(char *dest, size_t size, const char *src) {
    size_t len = strlen(src);
    if (len == 0 || len >= size) {
        return 0;
    }
    memcpy(dest, src, len + 1);
    return 1;
}

/** strip_newline() - removes a trailing "\n" or "\r\n". */
static void strip_newline(char *line) {
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        line[--len] = '\0';
    }
}

Status save_students(const StudentDB *db, const char *path) {
    char tmpPath[1024];
    if (snprintf(tmpPath, sizeof tmpPath, "%s.tmp", path) >= (int)sizeof tmpPath) {
        return STATUS_FILE_ERROR;
    }

    FILE *file = fopen(tmpPath, "w");
    if (file == NULL) {
        fprintf(stderr, "[ERROR] Cannot open '%s' for writing: %s\n", tmpPath, strerror(errno));
        return STATUS_FILE_ERROR;
    }

    int failed = 0;
    failed |= fprintf(file, "# Student Academic Records data file\n") < 0;
    failed |= fprintf(file, "# STUDENT|id|first|last|programme|year|courseCount\n") < 0;
    failed |= fprintf(file, "# COURSE|code|name|marks|maxMarks\n") < 0;

    for (int i = 0; i < db->count && !failed; i++) {
        const Student *s = &db->students[i];
        failed |= fprintf(file, "STUDENT|%d|%s|%s|%s|%d|%d\n", s->id, s->firstName,
                          s->lastName, s->programme, s->year, s->courseCount) < 0;
        for (int c = 0; c < s->courseCount && !failed; c++) {
            const Course *course = &s->courses[c];
            failed |= fprintf(file, "COURSE|%s|%s|%.2f|%.2f\n", course->code,
                              course->name, course->marks, course->maxMarks) < 0;
        }
    }

    if (fclose(file) != 0) {
        failed = 1;
    }
    if (failed) {
        fprintf(stderr, "[ERROR] Failed while writing '%s'. Previous data kept.\n", tmpPath);
        remove(tmpPath);
        return STATUS_FILE_ERROR;
    }
    if (rename(tmpPath, path) != 0) {
        fprintf(stderr, "[ERROR] Cannot replace '%s': %s\n", path, strerror(errno));
        remove(tmpPath);
        return STATUS_FILE_ERROR;
    }
    return STATUS_OK;
}

Status load_students(StudentDB *db, const char *path) {
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        if (errno == ENOENT) {
            return STATUS_FILE_MISSING;
        }
        fprintf(stderr, "[ERROR] Cannot open '%s': %s\n", path, strerror(errno));
        return STATUS_FILE_ERROR;
    }

    char line[LINE_BUFFER];
    char *fields[MAX_FIELDS];
    int lineNumber = 0;
    int skipped = 0;
    Student *current = NULL;      /* student that COURSE lines attach to */
    int coursesExpected = 0;
    Status result = STATUS_OK;

    while (fgets(line, sizeof line, file) != NULL) {
        lineNumber++;

        if (strchr(line, '\n') == NULL && !feof(file)) {
            /* Line longer than the buffer: discard the rest of it. */
            int ch;
            while ((ch = fgetc(file)) != '\n' && ch != EOF) { }
            fprintf(stderr, "[WARNING] Line %d too long - skipped\n", lineNumber);
            skipped++;
            continue;
        }
        strip_newline(line);
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        int n = split_fields(line, fields, MAX_FIELDS);

        if (strcmp(fields[0], "STUDENT") == 0) {
            if (coursesExpected > 0) {
                fprintf(stderr, "[WARNING] Line %d: previous student is missing %d course line(s)\n",
                        lineNumber, coursesExpected);
            }
            current = NULL;
            coursesExpected = 0;

            Student s;
            memset(&s, 0, sizeof s);
            int declaredCourses;
            if (n != 7 || !parse_int(fields[1], &s.id) ||
                !copy_field(s.firstName, sizeof s.firstName, fields[2]) ||
                !copy_field(s.lastName, sizeof s.lastName, fields[3]) ||
                !copy_field(s.programme, sizeof s.programme, fields[4]) ||
                !parse_int(fields[5], &s.year) ||
                !parse_int(fields[6], &declaredCourses) ||
                declaredCourses < 0 || declaredCourses > MAX_COURSES) {
                fprintf(stderr, "[WARNING] Line %d: malformed STUDENT record - skipped\n", lineNumber);
                skipped++;
                continue;
            }

            Status status = register_student(db, &s);
            if (status == STATUS_MEMORY) {
                result = STATUS_MEMORY;
                break;
            }
            if (status != STATUS_OK) {
                fprintf(stderr, "[WARNING] Line %d: student %d rejected (%s) - skipped\n",
                        lineNumber, s.id, status_message(status));
                skipped++;
                continue;
            }
            current = &db->students[db->count - 1];
            coursesExpected = declaredCourses;

        } else if (strcmp(fields[0], "COURSE") == 0) {
            Course c;
            memset(&c, 0, sizeof c);
            if (current == NULL || coursesExpected == 0) {
                fprintf(stderr, "[WARNING] Line %d: COURSE without a matching STUDENT - skipped\n", lineNumber);
                skipped++;
                continue;
            }
            coursesExpected--;
            if (n != 5 || !copy_field(c.code, sizeof c.code, fields[1]) ||
                !copy_field(c.name, sizeof c.name, fields[2]) ||
                !parse_double(fields[3], &c.marks) ||
                !parse_double(fields[4], &c.maxMarks)) {
                fprintf(stderr, "[WARNING] Line %d: malformed COURSE record - skipped\n", lineNumber);
                skipped++;
                continue;
            }
            Status status = add_course(current, &c);
            if (status == STATUS_MEMORY) {
                result = STATUS_MEMORY;
                break;
            }
            if (status != STATUS_OK) {
                fprintf(stderr, "[WARNING] Line %d: course %s rejected (%s) - skipped\n",
                        lineNumber, c.code, status_message(status));
                skipped++;
            }
        } else {
            fprintf(stderr, "[WARNING] Line %d: unknown record type '%s' - skipped\n",
                    lineNumber, fields[0]);
            skipped++;
        }
    }

    if (result == STATUS_OK && ferror(file)) {
        fprintf(stderr, "[ERROR] Read error in '%s'\n", path);
        result = STATUS_FILE_ERROR;
    }
    fclose(file);

    if (skipped > 0) {
        fprintf(stderr, "[WARNING] %d invalid line(s) were skipped while loading.\n", skipped);
    }
    return result;
}
