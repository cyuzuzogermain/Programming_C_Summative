/**
 * @file report.c
 * @brief Academic performance analysis.
 *
 * Definitions used in the report:
 *  - Course pass        : marks >= 50% of the course's maximum marks.
 *  - Student average    : total marks obtained / total maximum marks * 100.
 *  - Student passing    : student average >= 50%.
 *  - Overall average    : mean of the averages of students who have at
 *                         least one course (students with no courses are
 *                         reported as "not assessed" and excluded, so there
 *                         is never a division by zero).
 *  - Course average     : mean percentage of every student who took a
 *                         course, grouped by course code.
 */
#include "report.h"

#include <stdlib.h>
#include <string.h>

/** Aggregated statistics for one course code. */
typedef struct {
    const char *code;            /* points into a Course record */
    const char *name;
    double      totalPercent;
    int         students;
    int         passes;
} CourseStat;

/** Number of students in one programme. */
typedef struct {
    const char *programme;       /* points into a Student record */
    int         students;
} ProgrammeStat;

/** Find @code in @stats (case-insensitive). Return: index or -1. */
static int find_course_stat(const CourseStat *stats, int count, const char *code) {
    for (int i = 0; i < count; i++) {
        if (compare_ci(stats[i].code, code) == 0) {
            return i;
        }
    }
    return -1;
}

static int find_programme_stat(const ProgrammeStat *stats, int count, const char *programme) {
    for (int i = 0; i < count; i++) {
        if (compare_ci(stats[i].programme, programme) == 0) {
            return i;
        }
    }
    return -1;
}

Status generate_academic_report(const StudentDB *db, FILE *out) {
    fprintf(out, "==============================================================\n");
    fprintf(out, "              ACADEMIC PERFORMANCE REPORT\n");
    fprintf(out, "==============================================================\n");

    if (db->count <= 0) {
        fprintf(out, "No students registered - nothing to analyse.\n");
        return STATUS_OK;
    }

    /* Worst case: every registration is a distinct course / programme. */
    int totalRegistrations = 0;
    for (int i = 0; i < db->count; i++) {
        totalRegistrations += db->students[i].courseCount;
    }

    CourseStat    *courseStats    = calloc((size_t)(totalRegistrations > 0 ? totalRegistrations : 1),
                                           sizeof *courseStats);
    ProgrammeStat *programmeStats = calloc((size_t)db->count, sizeof *programmeStats);
    if (courseStats == NULL || programmeStats == NULL) {
        free(courseStats);
        free(programmeStats);
        fprintf(out, "[ERROR] Not enough memory to build the report.\n");
        return STATUS_MEMORY;
    }

    int courseStatCount = 0, programmeStatCount = 0;
    int assessed = 0, passing = 0, failing = 0, notAssessed = 0;
    int coursePasses = 0, courseFails = 0;
    double sumOfAverages = 0.0;
    const Student *best = NULL, *worst = NULL;
    double bestAvg = 0.0, worstAvg = 0.0;

    for (int i = 0; i < db->count; i++) {
        const Student *s = &db->students[i];
        double avg;

        /* Programme counts */
        int p = find_programme_stat(programmeStats, programmeStatCount, s->programme);
        if (p < 0) {
            p = programmeStatCount++;
            programmeStats[p].programme = s->programme;
        }
        programmeStats[p].students++;

        /* Student averages, best / worst, pass / fail */
        if (calculate_student_average(s, &avg)) {
            assessed++;
            sumOfAverages += avg;
            if (avg >= PASS_PERCENTAGE) passing++; else failing++;
            if (best == NULL || avg > bestAvg)   { best = s;  bestAvg = avg; }
            if (worst == NULL || avg < worstAvg) { worst = s; worstAvg = avg; }
        } else {
            notAssessed++;
        }

        /* Per-course aggregation */
        for (int c = 0; c < s->courseCount; c++) {
            const Course *course = &s->courses[c];
            int k = find_course_stat(courseStats, courseStatCount, course->code);
            if (k < 0) {
                k = courseStatCount++;
                courseStats[k].code = course->code;
                courseStats[k].name = course->name;
            }
            courseStats[k].totalPercent += course_percentage(course);
            courseStats[k].students++;
            if (course_passed(course)) {
                courseStats[k].passes++;
                coursePasses++;
            } else {
                courseFails++;
            }
        }
    }

    fprintf(out, "Total registered students          : %d\n", db->count);
    fprintf(out, "Total course registrations         : %d\n", totalRegistrations);
    fprintf(out, "Distinct courses offered           : %d\n", courseStatCount);
    if (assessed > 0) {
        fprintf(out, "Overall average performance        : %.2f%%\n", sumOfAverages / assessed);
        fprintf(out, "Highest average                    : %.2f%% - %s %s (ID %d)\n",
                bestAvg, best->firstName, best->lastName, best->id);
        fprintf(out, "Lowest average                     : %.2f%% - %s %s (ID %d)\n",
                worstAvg, worst->firstName, worst->lastName, worst->id);
    } else {
        fprintf(out, "Overall average performance        : N/A (no course records)\n");
    }
    fprintf(out, "Students passing (average >= %.0f%%) : %d\n", PASS_PERCENTAGE, passing);
    fprintf(out, "Students failing (average <  %.0f%%) : %d\n", PASS_PERCENTAGE, failing);
    if (notAssessed > 0) {
        fprintf(out, "Students not assessed (no courses) : %d\n", notAssessed);
    }
    fprintf(out, "Course results passed / failed     : %d / %d\n", coursePasses, courseFails);

    if (courseStatCount > 0) {
        fprintf(out, "\n----------------- Average Performance per Course -----------------\n");
        fprintf(out, "%-10s %-30s %8s %9s %7s\n", "Code", "Course", "Students", "Average", "Passed");
        for (int k = 0; k < courseStatCount; k++) {
            fprintf(out, "%-10s %-30.30s %8d %8.2f%% %4d/%-2d\n",
                    courseStats[k].code, courseStats[k].name, courseStats[k].students,
                    courseStats[k].totalPercent / courseStats[k].students,
                    courseStats[k].passes, courseStats[k].students);
        }
    }

    fprintf(out, "\n------------------ Students per Programme ------------------------\n");
    for (int p = 0; p < programmeStatCount; p++) {
        fprintf(out, "%-40s %d\n", programmeStats[p].programme, programmeStats[p].students);
    }
    fprintf(out, "==============================================================\n");

    free(courseStats);
    free(programmeStats);
    return STATUS_OK;
}
