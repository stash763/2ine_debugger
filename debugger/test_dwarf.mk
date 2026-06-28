CC = gcc
CFLAGS = -g -Wall -I/usr/include/libdwarf
LDFLAGS = -ldwarf

TARGET = test_dwarf
SRC = test_dwarf.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean