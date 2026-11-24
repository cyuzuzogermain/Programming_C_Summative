CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -O2
TARGET  = student_records
OBJS    = main.o student.o storage.o report.o input.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

main.o:    main.c student.h storage.h report.h input.h
student.o: student.c student.h
storage.o: storage.c storage.h student.h
report.o:  report.c report.h student.h
input.o:   input.c input.h

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
