CC = gcc
CFLAGS = -O2 -Wall
TARGET = line_info_alarm
SRC = line_info_alarm.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)

.PHONY: all clean
