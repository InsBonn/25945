CC = gcc
CFLAGS = -O2 -Wall
TARGET = strings
SRC = strings.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)

.PHONY: all clean
