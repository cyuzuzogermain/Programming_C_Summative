/**
 * @file input.h
 * @brief Validated console input helpers.
 *
 * Every function re-prompts until the input is valid. They all return 1
 * on success and 0 when standard input has ended (EOF), so callers can
 * stop gracefully instead of looping forever.
 */
#ifndef INPUT_H
#define INPUT_H

#include <stddef.h>

int read_line(const char *prompt, char *buffer, size_t size);
int read_int(const char *prompt, int min, int max, int *out);
int read_int_or_keep(const char *prompt, int min, int max, int *value);
int read_double(const char *prompt, double min, double max, double *out);

/** Text that may not be empty and may not contain the '|' separator. */
int read_text(const char *prompt, char *buffer, size_t size);
/** Like read_text, but an empty answer keeps the current buffer contents. */
int read_text_or_keep(const char *prompt, char *buffer, size_t size);

int read_name(const char *prompt, char *buffer, size_t size);
int read_name_or_keep(const char *prompt, char *buffer, size_t size);
int read_course_code(const char *prompt, char *buffer, size_t size);
int read_yes_no(const char *prompt, int *answer);

int is_valid_name(const char *text);
int input_ended(void);

#endif /* INPUT_H */
