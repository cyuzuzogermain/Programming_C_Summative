/**
 * @file input.h
 * @brief Validated console input helpers.
 *
 * Every reader re-prompts until the input is valid and returns 1 on
 * success, or 0 when standard input has ended (EOF) so the caller can
 * stop cleanly. The *_or_keep variants leave the value unchanged when
 * the user just presses Enter.
 */
#ifndef INPUT_H
#define INPUT_H

#include <stddef.h>

int read_line(const char *prompt, char *buffer, size_t size);
int read_int(const char *prompt, int min, int max, int *out);
int read_int_or_keep(const char *prompt, int min, int max, int *value);
int read_double(const char *prompt, double min, double max, double *out);
int read_double_or_keep(const char *prompt, double min, double max, double *value);
int read_text(const char *prompt, char *buffer, size_t size);
int read_text_or_keep(const char *prompt, char *buffer, size_t size);
int read_date(const char *prompt, char *buffer, size_t size, int allowKeep);
int read_yes_no(const char *prompt, int *answer);

int is_valid_date(const char *text);
int input_ended(void);

#endif /* INPUT_H */
