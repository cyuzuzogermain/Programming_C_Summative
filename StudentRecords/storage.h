/**
 * @file storage.h
 * @brief Saving and loading student records to/from a text file.
 *
 * File format (one record per line, fields separated by '|'):
 *
 *   # comment lines start with '#'
 *   STUDENT|<id>|<first>|<last>|<programme>|<year>|<courseCount>
 *   COURSE|<code>|<name>|<marks>|<maxMarks>          (courseCount times)
 */
#ifndef STORAGE_H
#define STORAGE_H

#include "student.h"

Status save_students(const StudentDB *db, const char *path);
Status load_students(StudentDB *db, const char *path);

#endif /* STORAGE_H */
