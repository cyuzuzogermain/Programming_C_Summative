/**
 * @file input.c
 * @brief Validated console input helpers (see input.h).
 */
#define _POSIX_C_SOURCE 200809L   /* isatty(), fileno(), localtime_r() */

#include "input.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int g_inputEnded = 0;

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
 * read_line() - prints @prompt, reads one trimmed line. Over-long input
 * is rejected instead of silently truncated. Piped input is echoed so
 * that recorded transcripts show what was typed.
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
            printf("%s\n", buffer);
        }
        return 1;
    }
}

static int parse_long(const char *text, long *out) {
    char *end;
    errno = 0;
    long value = strtol(text, &end, 10);
    if (text[0] == '\0' || errno != 0 || *end != '\0') return 0;
    *out = value;
    return 1;
}

static int parse_double(const char *text, double *out) {
    char *end;
    errno = 0;
    double value = strtod(text, &end);
    if (text[0] == '\0' || errno != 0 || *end != '\0' || value != value /* NaN */) return 0;
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
        if (buffer[0] == '\0') return 1;
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
    double value;
    while (read_line(prompt, buffer, sizeof buffer)) {
        if (parse_double(buffer, &value) && value >= min && value <= max) {
            *out = value;
            return 1;
        }
        printf("  [ERROR] Enter a number between %.2f and %.2f.\n", min, max);
    }
    return 0;
}

int read_double_or_keep(const char *prompt, double min, double max, double *value) {
    char buffer[64];
    double parsed;
    while (read_line(prompt, buffer, sizeof buffer)) {
        if (buffer[0] == '\0') return 1;
        if (parse_double(buffer, &parsed) && parsed >= min && parsed <= max) {
            *value = parsed;
            return 1;
        }
        printf("  [ERROR] Enter a number between %.2f and %.2f, or press Enter to keep.\n", min, max);
    }
    return 0;
}

int read_text(const char *prompt, char *buffer, size_t size) {
    while (read_line(prompt, buffer, size)) {
        if (buffer[0] == '\0') {
            printf("  [ERROR] This field cannot be empty.\n");
        } else if (strchr(buffer, '|') != NULL) {
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
        if (temp[0] == '\0') return 1;
        if (strchr(temp, '|') != NULL) {
            printf("  [ERROR] The '|' character is not allowed.\n");
            continue;
        }
        strcpy(buffer, temp);
        return 1;
    }
    return 0;
}

/**
 * is_valid_date() - strict YYYY-MM-DD check (years 2000-2100) including
 * month lengths and leap years.
 */
int is_valid_date(const char *text) {
    static const int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (strlen(text) != 10 || text[4] != '-' || text[7] != '-') return 0;
    for (int i = 0; i < 10; i++) {
        if (i != 4 && i != 7 && !isdigit((unsigned char)text[i])) return 0;
    }
    int year  = atoi(text);
    int month = atoi(text + 5);
    int day   = atoi(text + 8);
    if (year < 2000 || year > 2100 || month < 1 || month > 12 || day < 1) return 0;

    int maxDay = daysInMonth[month - 1];
    int leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    if (month == 2 && leap) maxDay = 29;
    return day <= maxDay;
}

/**
 * read_date() - reads YYYY-MM-DD. "t" or "today" uses the current date.
 * With @allowKeep, an empty answer keeps @buffer unchanged.
 */
int read_date(const char *prompt, char *buffer, size_t size, int allowKeep) {
    char temp[32];
    while (read_line(prompt, temp, sizeof temp)) {
        if (temp[0] == '\0' && allowKeep) return 1;
        if (strcmp(temp, "t") == 0 || strcmp(temp, "today") == 0) {
            time_t now = time(NULL);
            struct tm local;
            if (localtime_r(&now, &local) != NULL) {
                strftime(temp, sizeof temp, "%Y-%m-%d", &local);
            }
        }
        if (is_valid_date(temp) && strlen(temp) < size) {
            strcpy(buffer, temp);
            return 1;
        }
        printf("  [ERROR] Enter a valid date as YYYY-MM-DD (2000-2100), or 't' for today.\n");
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
