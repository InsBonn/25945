CC = gcc
CFLAGS = -O2 -Wall
TARGET = ex_time
SRC = ex_time.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)

.PHONY: all clean