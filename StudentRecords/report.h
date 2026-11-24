/**
 * @file report.h
 * @brief Academic performance analysis reports.
 */
#ifndef REPORT_H
#define REPORT_H

#include <stdio.h>
#include "student.h"

/**
 * generate_academic_report() - writes the full statistics report to @out
 * (stdout or an open file).
 * Return: STATUS_OK, or STATUS_MEMORY if the temporary tables could not
 *         be allocated.
 */
Status generate_academic_report(const StudentDB *db, FILE *out);

#endif /* REPORT_H */
