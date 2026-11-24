/**
 * @file input.c
 * @brief Validated console input helpers (see input.h).
 */
#define _POSIX_C_SOURCE 200809L   /* for isatty() / fileno() */

#include "input.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int g_inputEnded = 0;

/** input_ended() - 1 once stdin has reached EOF. */
int input_ended(void) {
    return g_inputEnded;
}

/** trim() - removes leading and trailing whitespace in place. */
static void trim(char *text) {
    size_t start = 0, len = strlen(text);
    while (start < len && isspace((unsigned char)text[start])) start++;
    while (len > start && isspace((unsigned char)text[len - 1])) len--;
    memmove(text, text + start, len - start);
    text[len - start] = '\0';
}

/**
 * read_line() - prints @prompt and reads one trimmed line.
 * Over-long input is rejected (and the rest of the line discarded) rather
 * than silently truncated.
 */
int read_line(const char *prompt, char *buffer, size_t size) {
    for (;;) {
        printf("%s", prompt);
        fflush(stdout);

        if (fgets(buffer, (int)size, stdin) == NULL) {
            g_inputEnded = 1;
            buffer[0] = '\0';
            printf("\n");
            return 0;
        }
        if (strchr(buffer, '\n') == NULL && !feof(stdin)) {
            int ch;
            while ((ch = getchar()) != '\n' && ch != EOF) { }
            printf("  [ERROR] Input too long (maximum %zu characters).\n", size - 2);
            continue;
        }
        trim(buffer);
        if (!isatty(fileno(stdin))) {
            printf("%s\n", buffer);   /* echo piped input so transcripts are readable */
        }
        return 1;
    }
}

/** parse_long() - strict conversion of the whole string. */
static int parse_long(const char *text, long *out) {
    char *end;
    errno = 0;
    long value = strtol(text, &end, 10);
    if (text[0] == '\0' || errno != 0 || *end != '\0') {
        return 0;
    }
    *out = value;
    return 1;
}

int read_int(const char *prompt, int min, int max, int *out) {
    char buffer[64];
    long value;
    while (read_line(prompt, buffer, sizeof buffer)) {
        if (parse_long(buffer, &value) && value >= min && value <= max) {
            *out = (int)value;
            return 1;
        }
        printf("  [ERROR] Enter a whole number between %d and %d.\n", min, max);
    }
    return 0;
}

int read_int_or_keep(const char *prompt, int min, int max, int *value) {
    char buffer[64];
    long parsed;
    while (read_line(prompt, buffer, sizeof buffer)) {
        if (buffer[0] == '\0') {
            return 1;                                  /* keep current */
        }
        if (parse_long(buffer, &parsed) && parsed >= min && parsed <= max) {
            *value = (int)parsed;
            return 1;
        }
        printf("  [ERROR] Enter a whole number between %d and %d, or press Enter to keep.\n", min, max);
    }
    return 0;
}

int read_double(const char *prompt, double min, double max, double *out) {
    char buffer[64];
    while (read_line(prompt, buffer, sizeof buffer)) {
        char *end;
        errno = 0;
        double value = strtod(buffer, &end);
        if (buffer[0] != '\0' && errno == 0 && *end == '\0' &&
            value >= min && value <= max) {
            *out = value;
            return 1;
        }
        printf("  [ERROR] Enter a number between %.2f and %.2f.\n", min, max);
    }
    return 0;
}

/** contains_separator() - '|' would corrupt the data file. */
static int contains_separator(const char *text) {
    return strchr(text, '|') != NULL;
}

int read_text(const char *prompt, char *buffer, size_t size) {
    while (read_line(prompt, buffer, size)) {
        if (buffer[0] == '\0') {
            printf("  [ERROR] This field cannot be empty.\n");
        } else if (contains_separator(buffer)) {
            printf("  [ERROR] The '|' character is not allowed.\n");
        } else {
            return 1;
        }
    }
    return 0;
}

int read_text_or_keep(const char *prompt, char *buffer, size_t size) {
    char temp[256];
    size_t limit = size < sizeof temp ? size : sizeof temp;
    while (read_line(prompt, temp, limit)) {
        if (temp[0] == '\0') {
            return 1;
        }
        if (contains_separator(temp)) {
            printf("  [ERROR] The '|' character is not allowed.\n");
            continue;
        }
        strcpy(buffer, temp);       /* fits: temp was read with limit <= size */
        return 1;
    }
    return 0;
}

/** is_valid_name() - letters, spaces, hyphens and apostrophes only. */
int is_valid_name(const char *text) {
    if (text[0] == '\0') {
        return 0;
    }
    for (const char *p = text; *p != '\0'; p++) {
        if (!isalpha((unsigned char)*p) && *p != ' ' && *p != '-' && *p != '\'') {
            return 0;
        }
    }
    return 1;
}

int read_name(const char *prompt, char *buffer, size_t size) {
    while (read_line(prompt, buffer, size)) {
        if (is_valid_name(buffer)) {
            return 1;
        }
        printf("  [ERROR] Names may only contain letters, spaces, '-' and '''.\n");
    }
    return 0;
}

int read_name_or_keep(const char *prompt, char *buffer, size_t size) {
    char temp[256];
    size_t limit = size < sizeof temp ? size : sizeof temp;
    while (read_line(prompt, temp, limit)) {
        if (temp[0] == '\0') {
            return 1;
        }
        if (is_valid_name(temp)) {
            strcpy(buffer, temp);
            return 1;
        }
        printf("  [ERROR] Names may only contain letters, spaces, '-' and '''.\n");
    }
    return 0;
}

/** read_course_code() - 2+ letters/digits, stored in upper case. */
int read_course_code(const char *prompt, char *buffer, size_t size) {
    while (read_line(prompt, buffer, size)) {
        size_t len = strlen(buffer);
        int valid = len >= 2;
        for (size_t i = 0; i < len && valid; i++) {
            if (!isalnum((unsigned char)buffer[i])) {
                valid = 0;
            }
            buffer[i] = (char)toupper((unsigned char)buffer[i]);
        }
        if (valid) {
            return 1;
        }
        printf("  [ERROR] Course codes are 2-%zu letters or digits (e.g. CS101).\n", size - 1);
    }
    return 0;
}

int read_yes_no(const char *prompt, int *answer) {
    char buffer[16];
    while (read_line(prompt, buffer, sizeof buffer)) {
        if (buffer[0] == 'y' || buffer[0] == 'Y') { *answer = 1; return 1; }
        if (buffer[0] == 'n' || buffer[0] == 'N') { *answer = 0; return 1; }
        printf("  [ERROR] Please answer y or n.\n");
    }
    return 0;
}
