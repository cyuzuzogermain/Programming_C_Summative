/**
 * ============================================================================
 *  Smart Occupancy Monitoring and Access Control System
 * ============================================================================
 *  Platform : Arduino Uno (Tinkercad simulation)
 *
 *  Description
 *  -----------
 *  Tracks how many people are inside a facility that has a fixed maximum
 *  capacity. Two push buttons stand in for people entering and leaving.
 *
 *    - ENTRY button : +1 person (rejected when the facility is full)
 *    - EXIT  button : -1 person (rejected when the facility is empty)
 *    - 16x2 LCD     : current occupancy and remaining capacity
 *    - Green LED    : capacity available
 *    - Red LED      : maximum occupancy reached
 *    - Serial       : statistics, entry/exit events and error messages
 *
 *  Programming concepts shown
 *  --------------------------
 *    Structures      : OccupancyRecord, EventLog, OccupancyEvent, DebouncedButton
 *    Pointers        : every function receives a pointer to the record it
 *                      reads or changes (OccupancyRecord *, EventLog *, ...)
 *    Dynamic memory  : the record is created with malloc(), the event log
 *                      buffer with calloc(); both are validated for NULL
 *    Memory release  : free() is called when the system is reset from the
 *                      Serial Monitor (command 'r') before re-allocating
 *    Non-blocking    : millis() drives debouncing, LCD refresh and the
 *                      temporary "denied" message; delay() is never used
 *
 *  Serial Monitor commands (9600 baud)
 *  -----------------------------------
 *    s : print occupancy statistics
 *    l : print the recent event log
 *    r : reset the system (frees and re-allocates all records)
 *    h : print help
 *
 *  Wiring (see README.md for the full table)
 *  -----------------------------------------
 *    LCD RS=12, E=11, D4=5, D5=4, D6=3, D7=2, RW=GND
 *    Entry button -> pin 7  (10k pull-down to GND, other leg to 5V)
 *    Exit  button -> pin 8  (10k pull-down to GND, other leg to 5V)
 *    Green LED    -> pin 9  through 220 ohm
 *    Red LED      -> pin 10 through 220 ohm
 * ============================================================================
 */

#include <LiquidCrystal.h>
#include <stdlib.h>

/* ---------------------------------------------------------------------------
 *  Configuration
 * ------------------------------------------------------------------------- */

#define MAX_CAPACITY          10      /* Maximum people allowed inside       */
#define EVENT_LOG_SIZE        10      /* Number of recent events remembered  */

#define PIN_ENTRY_BUTTON      7
#define PIN_EXIT_BUTTON       8
#define PIN_GREEN_LED         9
#define PIN_RED_LED           10

#define PIN_LCD_RS            12
#define PIN_LCD_EN            11
#define PIN_LCD_D4            5
#define PIN_LCD_D5            4
#define PIN_LCD_D6            3
#define PIN_LCD_D7            2

#define LCD_COLUMNS           16
#define LCD_ROWS              2

#define DEBOUNCE_DELAY_MS     50UL    /* Input must be stable this long      */
#define LCD_REFRESH_MS        250UL   /* Periodic LCD redraw interval        */
#define NOTICE_DURATION_MS    2000UL  /* How long "DENIED" stays on the LCD  */

/* Buttons are wired with external pull-down resistors: pressed = HIGH. */
#define BUTTON_PRESSED_LEVEL  HIGH

/* ---------------------------------------------------------------------------
 *  Data types
 * ------------------------------------------------------------------------- */

/** Kinds of event the system records. */
typedef enum {
    EVENT_ENTRY = 0,       /* A person entered successfully            */
    EVENT_EXIT,            /* A person left successfully               */
    EVENT_ENTRY_DENIED,    /* Entry rejected: facility full            */
    EVENT_EXIT_DENIED      /* Exit rejected: facility already empty    */
} EventType;

/** Result codes returned by the entry / exit operations. */
typedef enum {
    OP_SUCCESS = 0,
    OP_ERROR_FULL,
    OP_ERROR_EMPTY,
    OP_ERROR_NULL_RECORD
} OperationResult;

/** Main occupancy record: everything we know about the facility. */
typedef struct OccupancyRecord {
    int           currentOccupancy;   /* People inside right now           */
    int           maxCapacity;        /* Maximum people allowed            */
    int           peakOccupancy;      /* Highest occupancy seen            */
    unsigned long totalEntries;       /* Successful entries                */
    unsigned long totalExits;         /* Successful exits                  */
    unsigned long deniedEntries;      /* Entries rejected (full)           */
    unsigned long deniedExits;        /* Exits rejected (empty)            */
    bool          entryDeniedAlert;   /* Last action was a denied entry    */
} OccupancyRecord;

/** One entry in the event log. */
typedef struct OccupancyEvent {
    EventType     type;               /* What happened                     */
    int           occupancyAfter;     /* Occupancy after the event         */
    unsigned long timestampMs;        /* millis() when it happened         */
} OccupancyEvent;

/** Circular buffer of recent events (buffer is allocated with calloc). */
typedef struct EventLog {
    OccupancyEvent *events;           /* Dynamically allocated array       */
    int             capacity;         /* Number of slots in the array      */
    int             count;            /* Slots currently used              */
    int             nextIndex;        /* Where the next event is written   */
} EventLog;

/** State needed to debounce one push button without delay(). */
typedef struct DebouncedButton {
    uint8_t       pin;
    int           stableState;        /* Debounced level                   */
    int           lastReading;        /* Raw level seen last loop          */
    unsigned long lastChangeMs;       /* When the raw level last changed   */
} DebouncedButton;

/** State for the non-blocking LCD refresh. */
typedef struct DisplayState {
    unsigned long lastRefreshMs;      /* When the LCD was last redrawn     */
    unsigned long noticeUntilMs;      /* Show notice until this time       */
    const char   *noticeText;         /* Temporary second-line message     */
} DisplayState;

/* ---------------------------------------------------------------------------
 *  Global objects
 *  (only pointers to the dynamic records are global; the data lives on the heap)
 * ------------------------------------------------------------------------- */

LiquidCrystal lcd(PIN_LCD_RS, PIN_LCD_EN, PIN_LCD_D4, PIN_LCD_D5, PIN_LCD_D6, PIN_LCD_D7);

OccupancyRecord *occupancy = NULL;
EventLog        *eventLog  = NULL;

DebouncedButton entryButton;
DebouncedButton exitButton;
DisplayState    display;

/* ---------------------------------------------------------------------------
 *  Function prototypes
 * ------------------------------------------------------------------------- */

/*
 * Note: signatures use "struct X *" and int for the enum parameters on purpose.
 * The Arduino/Tinkercad build step auto-generates prototypes and may place
 * them above the typedefs; "struct X" is still valid there, a bare typedef
 * name is not.
 */

/* Memory management */
struct OccupancyRecord *createOccupancyRecord(int maxCapacity);
void             destroyOccupancyRecord(struct OccupancyRecord **record);
struct EventLog        *createEventLog(int capacity);
void             destroyEventLog(struct EventLog **log);
bool             initialiseSystemMemory(void);
void             releaseSystemMemory(void);
void             haltWithError(const char *message);

/* Occupancy logic */
int              processEntry(struct OccupancyRecord *record, struct EventLog *log);
int              processExit(struct OccupancyRecord *record, struct EventLog *log);
int              getRemainingCapacity(const struct OccupancyRecord *record);
bool             isFacilityFull(const struct OccupancyRecord *record);
void             logEvent(struct EventLog *log, int type, int occupancyAfter);

/* Input */
void             initButton(struct DebouncedButton *button, uint8_t pin);
bool             wasButtonPressed(struct DebouncedButton *button, unsigned long now);
void             handleSerialCommands(void);

/* Output */
void             updateStatusLeds(const struct OccupancyRecord *record);
void             updateLcd(const struct OccupancyRecord *record, struct DisplayState *state, unsigned long now);
void             forceLcdRefresh(struct DisplayState *state);
void             showLcdNotice(struct DisplayState *state, const char *text, unsigned long now);
void             printLcdLine(uint8_t row, const char *text);
void             printStatistics(const struct OccupancyRecord *record);
void             printEventLog(const struct EventLog *log);
void             printEventMessage(int type, const struct OccupancyRecord *record);
const char      *eventTypeToString(int type);
void             printHelp(void);
void             resetSystem(void);

/* ===========================================================================
 *  Arduino entry points
 * ========================================================================= */

/**
 * setup() - configures pins, allocates the records and draws the first screen.
 */
void setup() {
    Serial.begin(9600);

    pinMode(PIN_GREEN_LED, OUTPUT);
    pinMode(PIN_RED_LED, OUTPUT);
    initButton(&entryButton, PIN_ENTRY_BUTTON);
    initButton(&exitButton, PIN_EXIT_BUTTON);

    lcd.begin(LCD_COLUMNS, LCD_ROWS);

    if (!initialiseSystemMemory()) {
        haltWithError("Memory allocation failed");
    }

    display.lastRefreshMs = 0;
    display.noticeUntilMs = 0;
    display.noticeText    = NULL;

    Serial.println(F("=========================================="));
    Serial.println(F(" Smart Occupancy Monitoring System Ready"));
    Serial.println(F("=========================================="));
    printHelp();
    printStatistics(occupancy);

    updateStatusLeds(occupancy);
    forceLcdRefresh(&display);
    updateLcd(occupancy, &display, millis());
}

/**
 * loop() - non-blocking main loop. Each pass reads the buttons, handles
 * Serial commands, updates the LEDs and refreshes the LCD when due.
 */
void loop() {
    unsigned long now = millis();

    if (wasButtonPressed(&entryButton, now)) {
        if (processEntry(occupancy, eventLog) == OP_ERROR_FULL) {
            showLcdNotice(&display, "FULL-ENTRY DENY", now);
        }
        forceLcdRefresh(&display);
    }

    if (wasButtonPressed(&exitButton, now)) {
        if (processExit(occupancy, eventLog) == OP_ERROR_EMPTY) {
            showLcdNotice(&display, "EMPTY-EXIT DENY", now);
        }
        forceLcdRefresh(&display);
    }

    handleSerialCommands();
    updateStatusLeds(occupancy);
    updateLcd(occupancy, &display, now);
}

/* ===========================================================================
 *  Memory management
 * ========================================================================= */

/**
 * createOccupancyRecord() - allocates and initialises an occupancy record.
 * @maxCapacity: maximum number of people allowed (must be > 0).
 * Return: pointer to the new record, or NULL if allocation failed.
 */
struct OccupancyRecord *createOccupancyRecord(int maxCapacity) {
    if (maxCapacity <= 0) {
        Serial.println(F("[ERROR] Max capacity must be greater than zero"));
        return NULL;
    }

    OccupancyRecord *record = (OccupancyRecord *)malloc(sizeof(OccupancyRecord));
    if (record == NULL) {
        Serial.println(F("[ERROR] malloc() failed for OccupancyRecord"));
        return NULL;
    }

    record->currentOccupancy = 0;
    record->maxCapacity      = maxCapacity;
    record->peakOccupancy    = 0;
    record->totalEntries     = 0;
    record->totalExits       = 0;
    record->deniedEntries    = 0;
    record->deniedExits      = 0;
    record->entryDeniedAlert = false;

    return record;
}

/**
 * destroyOccupancyRecord() - frees a record and clears the caller's pointer
 * so it cannot be used after being freed.
 * @record: address of the pointer to free.
 */
void destroyOccupancyRecord(struct OccupancyRecord **record) {
    if (record != NULL && *record != NULL) {
        free(*record);
        *record = NULL;
    }
}

/**
 * createEventLog() - allocates an event log with @capacity slots.
 * The slots array uses calloc() so every event starts zeroed.
 * Return: pointer to the new log, or NULL if allocation failed.
 */
struct EventLog *createEventLog(int capacity) {
    if (capacity <= 0) {
        return NULL;
    }

    EventLog *log = (EventLog *)malloc(sizeof(EventLog));
    if (log == NULL) {
        Serial.println(F("[ERROR] malloc() failed for EventLog"));
        return NULL;
    }

    log->events = (OccupancyEvent *)calloc(capacity, sizeof(OccupancyEvent));
    if (log->events == NULL) {
        Serial.println(F("[ERROR] calloc() failed for event buffer"));
        free(log);                      /* do not leak the half-built log */
        return NULL;
    }

    log->capacity  = capacity;
    log->count     = 0;
    log->nextIndex = 0;
    return log;
}

/**
 * destroyEventLog() - frees the event buffer, then the log itself,
 * and clears the caller's pointer.
 */
void destroyEventLog(struct EventLog **log) {
    if (log != NULL && *log != NULL) {
        free((*log)->events);
        (*log)->events = NULL;
        free(*log);
        *log = NULL;
    }
}

/**
 * initialiseSystemMemory() - allocates every dynamic structure.
 * If any allocation fails, whatever was allocated is released.
 * Return: true on success, false on failure.
 */
bool initialiseSystemMemory(void) {
    occupancy = createOccupancyRecord(MAX_CAPACITY);
    eventLog  = createEventLog(EVENT_LOG_SIZE);

    if (occupancy == NULL || eventLog == NULL) {
        releaseSystemMemory();
        return false;
    }
    return true;
}

/**
 * releaseSystemMemory() - frees every dynamic structure (safe to call twice).
 */
void releaseSystemMemory(void) {
    destroyOccupancyRecord(&occupancy);
    destroyEventLog(&eventLog);
}

/**
 * haltWithError() - fatal error handler. Reports the problem on Serial and
 * the LCD, lights the red LED and stops normal operation.
 */
void haltWithError(const char *message) {
    Serial.print(F("[FATAL] "));
    Serial.println(message);
    Serial.println(F("[FATAL] System halted. Reset the board."));

    digitalWrite(PIN_GREEN_LED, LOW);
    digitalWrite(PIN_RED_LED, HIGH);
    printLcdLine(0, "SYSTEM ERROR");
    printLcdLine(1, message);

    while (true) {
        /* Nothing can run safely without the records. */
    }
}

/* ===========================================================================
 *  Occupancy logic
 * ========================================================================= */

/**
 * processEntry() - handles one entry attempt.
 * Adds one person unless the facility is already full.
 * @record: occupancy record to update (through the pointer).
 * @log:    event log that receives the result.
 * Return: OP_SUCCESS, OP_ERROR_FULL or OP_ERROR_NULL_RECORD.
 */
int processEntry(struct OccupancyRecord *record, struct EventLog *log) {
    if (record == NULL) {
        Serial.println(F("[ERROR] processEntry(): record is NULL"));
        return OP_ERROR_NULL_RECORD;
    }

    if (isFacilityFull(record)) {
        record->deniedEntries++;
        record->entryDeniedAlert = true;
        logEvent(log, EVENT_ENTRY_DENIED, record->currentOccupancy);
        printEventMessage(EVENT_ENTRY_DENIED, record);
        return OP_ERROR_FULL;
    }

    record->currentOccupancy++;
    record->totalEntries++;
    record->entryDeniedAlert = false;
    if (record->currentOccupancy > record->peakOccupancy) {
        record->peakOccupancy = record->currentOccupancy;
    }

    logEvent(log, EVENT_ENTRY, record->currentOccupancy);
    printEventMessage(EVENT_ENTRY, record);
    return OP_SUCCESS;
}

/**
 * processExit() - handles one exit attempt.
 * Removes one person unless the facility is already empty.
 * Return: OP_SUCCESS, OP_ERROR_EMPTY or OP_ERROR_NULL_RECORD.
 */
int processExit(struct OccupancyRecord *record, struct EventLog *log) {
    if (record == NULL) {
        Serial.println(F("[ERROR] processExit(): record is NULL"));
        return OP_ERROR_NULL_RECORD;
    }

    if (record->currentOccupancy <= 0) {
        record->deniedExits++;
        logEvent(log, EVENT_EXIT_DENIED, record->currentOccupancy);
        printEventMessage(EVENT_EXIT_DENIED, record);
        return OP_ERROR_EMPTY;
    }

    record->currentOccupancy--;
    record->totalExits++;
    record->entryDeniedAlert = false;

    logEvent(log, EVENT_EXIT, record->currentOccupancy);
    printEventMessage(EVENT_EXIT, record);
    return OP_SUCCESS;
}

/**
 * getRemainingCapacity() - number of people who can still enter.
 */
int getRemainingCapacity(const struct OccupancyRecord *record) {
    if (record == NULL) {
        return 0;
    }
    return record->maxCapacity - record->currentOccupancy;
}

/**
 * isFacilityFull() - true when occupancy has reached the maximum.
 */
bool isFacilityFull(const struct OccupancyRecord *record) {
    if (record == NULL) {
        return true;  /* treat an invalid record as "no entry allowed" */
    }
    return record->currentOccupancy >= record->maxCapacity;
}

/**
 * logEvent() - stores an event in the circular log, overwriting the oldest
 * entry once the buffer is full.
 */
void logEvent(struct EventLog *log, int type, int occupancyAfter) {
    if (log == NULL || log->events == NULL) {
        return;
    }

    OccupancyEvent *slot = &log->events[log->nextIndex];
    slot->type           = (EventType)type;
    slot->occupancyAfter = occupancyAfter;
    slot->timestampMs    = millis();

    log->nextIndex = (log->nextIndex + 1) % log->capacity;
    if (log->count < log->capacity) {
        log->count++;
    }
}

/* ===========================================================================
 *  Input handling
 * ========================================================================= */

/**
 * initButton() - sets up a button pin and its debounce state.
 */
void initButton(struct DebouncedButton *button, uint8_t pin) {
    button->pin          = pin;
    pinMode(pin, INPUT);              /* external 10k pull-down resistor */
    button->stableState  = digitalRead(pin);
    button->lastReading  = button->stableState;
    button->lastChangeMs = 0;
}

/**
 * wasButtonPressed() - non-blocking debounce using millis().
 * The input must hold a new level for DEBOUNCE_DELAY_MS before it is
 * accepted. Returns true once per press (on the press edge only), so
 * holding the button down counts as a single person.
 */
bool wasButtonPressed(struct DebouncedButton *button, unsigned long now) {
    int reading = digitalRead(button->pin);

    if (reading != button->lastReading) {
        button->lastChangeMs = now;   /* input changed: restart the timer */
        button->lastReading  = reading;
    }

    if ((now - button->lastChangeMs) >= DEBOUNCE_DELAY_MS &&
        reading != button->stableState) {
        button->stableState = reading;
        if (button->stableState == BUTTON_PRESSED_LEVEL) {
            return true;
        }
    }
    return false;
}

/**
 * handleSerialCommands() - reads one-letter commands from the Serial Monitor.
 */
void handleSerialCommands(void) {
    while (Serial.available() > 0) {
        char command = (char)Serial.read();

        switch (command) {
            case 's': case 'S': printStatistics(occupancy); break;
            case 'l': case 'L': printEventLog(eventLog);    break;
            case 'r': case 'R': resetSystem();              break;
            case 'h': case 'H': printHelp();                break;
            case '\n': case '\r': case ' ':                  break;
            default:
                Serial.print(F("[ERROR] Unknown command '"));
                Serial.print(command);
                Serial.println(F("'. Type 'h' for help."));
                break;
        }
    }
}

/**
 * resetSystem() - frees all records and allocates fresh ones, returning the
 * facility to zero occupancy with cleared statistics.
 */
void resetSystem(void) {
    Serial.println(F("[INFO] Resetting system: releasing memory..."));
    releaseSystemMemory();

    if (!initialiseSystemMemory()) {
        haltWithError("Re-alloc failed");
    }

    Serial.println(F("[INFO] Memory re-allocated. Occupancy reset to 0."));
    display.noticeText = NULL;
    forceLcdRefresh(&display);
    printStatistics(occupancy);
}

/* ===========================================================================
 *  Output: LEDs, LCD and Serial
 * ========================================================================= */

/**
 * updateStatusLeds() - green while there is room, red once full
 * (including straight after a denied entry, which can only happen when full).
 */
void updateStatusLeds(const struct OccupancyRecord *record) {
    bool full = isFacilityFull(record);
    digitalWrite(PIN_GREEN_LED, full ? LOW : HIGH);
    digitalWrite(PIN_RED_LED,   full ? HIGH : LOW);
}

/**
 * forceLcdRefresh() - makes the next updateLcd() call redraw immediately.
 */
void forceLcdRefresh(struct DisplayState *state) {
    state->lastRefreshMs = millis() - LCD_REFRESH_MS;
}

/**
 * showLcdNotice() - shows @text on the second LCD row for
 * NOTICE_DURATION_MS, without blocking.
 */
void showLcdNotice(struct DisplayState *state, const char *text, unsigned long now) {
    state->noticeText    = text;
    state->noticeUntilMs = now + NOTICE_DURATION_MS;
}

/**
 * updateLcd() - redraws the LCD every LCD_REFRESH_MS milliseconds.
 *   Row 0: "Inside: 3/10"
 *   Row 1: "Available: 7" / "FULL - NO ENTRY" / a temporary notice
 */
void updateLcd(const struct OccupancyRecord *record, struct DisplayState *state, unsigned long now) {
    if ((now - state->lastRefreshMs) < LCD_REFRESH_MS || record == NULL) {
        return;
    }
    state->lastRefreshMs = now;

    char line[LCD_COLUMNS + 1];

    snprintf(line, sizeof(line), "Inside: %d/%d",
             record->currentOccupancy, record->maxCapacity);
    printLcdLine(0, line);

    bool noticeActive = state->noticeText != NULL &&
                        (long)(state->noticeUntilMs - now) > 0;

    if (noticeActive) {
        printLcdLine(1, state->noticeText);
    } else {
        state->noticeText = NULL;
        if (isFacilityFull(record)) {
            printLcdLine(1, "FULL - NO ENTRY");
        } else {
            snprintf(line, sizeof(line), "Available: %d",
                     getRemainingCapacity(record));
            printLcdLine(1, line);
        }
    }
}

/**
 * printLcdLine() - writes @text on @row, padded with spaces to the full
 * width so old characters are overwritten without calling lcd.clear()
 * (which flickers).
 */
void printLcdLine(uint8_t row, const char *text) {
    char padded[LCD_COLUMNS + 1];
    snprintf(padded, sizeof(padded), "%-16s", text);
    lcd.setCursor(0, row);
    lcd.print(padded);
}

/**
 * printEventMessage() - reports one entry/exit event on the Serial Monitor.
 */
void printEventMessage(int type, const struct OccupancyRecord *record) {
    Serial.print(F("["));
    Serial.print(millis());
    Serial.print(F(" ms] "));

    switch (type) {
        case EVENT_ENTRY:
            Serial.print(F("ENTRY  : Person entered."));
            break;
        case EVENT_EXIT:
            Serial.print(F("EXIT   : Person left."));
            break;
        case EVENT_ENTRY_DENIED:
            Serial.print(F("ERROR  : Entry denied - maximum capacity ("));
            Serial.print(record->maxCapacity);
            Serial.print(F(") reached."));
            break;
        case EVENT_EXIT_DENIED:
            Serial.print(F("ERROR  : Exit denied - facility is already empty."));
            break;
    }

    Serial.print(F(" | Inside: "));
    Serial.print(record->currentOccupancy);
    Serial.print(F("/"));
    Serial.print(record->maxCapacity);
    Serial.print(F(" | Available: "));
    Serial.println(getRemainingCapacity(record));

    if (type == EVENT_ENTRY && isFacilityFull(record)) {
        Serial.println(F("[ALERT] Facility is now FULL. Red LED on."));
    }
}

/**
 * printStatistics() - full occupancy report on the Serial Monitor.
 */
void printStatistics(const struct OccupancyRecord *record) {
    if (record == NULL) {
        Serial.println(F("[ERROR] No occupancy record available"));
        return;
    }

    int usagePercent = (record->currentOccupancy * 100) / record->maxCapacity;

    Serial.println(F("------------ Occupancy Statistics ------------"));
    Serial.print(F(" Current occupancy : ")); Serial.print(record->currentOccupancy);
    Serial.print(F(" / ")); Serial.println(record->maxCapacity);
    Serial.print(F(" Available capacity: ")); Serial.println(getRemainingCapacity(record));
    Serial.print(F(" Utilisation       : ")); Serial.print(usagePercent); Serial.println(F(" %"));
    Serial.print(F(" Peak occupancy    : ")); Serial.println(record->peakOccupancy);
    Serial.print(F(" Total entries     : ")); Serial.println(record->totalEntries);
    Serial.print(F(" Total exits       : ")); Serial.println(record->totalExits);
    Serial.print(F(" Denied entries    : ")); Serial.println(record->deniedEntries);
    Serial.print(F(" Denied exits      : ")); Serial.println(record->deniedExits);
    Serial.print(F(" Status            : "));
    Serial.println(isFacilityFull(record) ? F("FULL (red LED)") : F("OPEN (green LED)"));
    Serial.println(F("----------------------------------------------"));
}

/**
 * eventTypeToString() - short label for an event type.
 */
const char *eventTypeToString(int type) {
    switch (type) {
        case EVENT_ENTRY:        return "ENTRY";
        case EVENT_EXIT:         return "EXIT";
        case EVENT_ENTRY_DENIED: return "ENTRY DENIED";
        case EVENT_EXIT_DENIED:  return "EXIT DENIED";
    }
    return "UNKNOWN";
}

/**
 * printEventLog() - prints the stored events, oldest first.
 */
void printEventLog(const struct EventLog *log) {
    if (log == NULL || log->events == NULL) {
        Serial.println(F("[ERROR] Event log not available"));
        return;
    }

    Serial.println(F("--------------- Recent Events ----------------"));
    if (log->count == 0) {
        Serial.println(F(" (no events yet)"));
    }

    /* Oldest event is at nextIndex once the buffer has wrapped. */
    int start = (log->count < log->capacity) ? 0 : log->nextIndex;

    for (int i = 0; i < log->count; i++) {
        const OccupancyEvent *event = &log->events[(start + i) % log->capacity];
        Serial.print(F(" "));
        Serial.print(i + 1);
        Serial.print(F(". ["));
        Serial.print(event->timestampMs);
        Serial.print(F(" ms] "));
        Serial.print(eventTypeToString(event->type));
        Serial.print(F(" -> inside: "));
        Serial.println(event->occupancyAfter);
    }
    Serial.println(F("----------------------------------------------"));
}

/**
 * printHelp() - lists the Serial Monitor commands.
 */
void printHelp(void) {
    Serial.println(F("Commands: s=statistics  l=event log  r=reset  h=help"));
}