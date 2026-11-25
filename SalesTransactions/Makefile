CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -O2
TARGET  = sales_system
OBJS    = main.o sales.o processing.o storage.o input.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

main.o:       main.c sales.h processing.h storage.h input.h
sales.o:      sales.c sales.h input.h
processing.o: processing.c processing.h sales.h
storage.o:    storage.c storage.h sales.h
input.o:      input.c input.h

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
