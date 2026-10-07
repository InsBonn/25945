CC = gcc
CFLAGS = -O2 -Wall
TARGET = line_info
SRC = line_info.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)

.PHONY: all clean
