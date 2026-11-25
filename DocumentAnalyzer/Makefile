CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -O2 -pthread
LDFLAGS = -pthread
TARGET  = document_analyzer
OBJS    = main.o analyzer.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

main.o:     main.c analyzer.h
analyzer.o: analyzer.c analyzer.h

clean:
	rm -f $(OBJS) $(TARGET) *_report.txt

.PHONY: all clean
