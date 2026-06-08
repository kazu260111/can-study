CC      := gcc
CFLAGS  := -Wall -Wextra -I..
BUILD   := build
TARGET  := $(BUILD)/send
OBJS    := $(BUILD)/send.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD)/send.o: send.c ../util.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)

.PHONY: all clean
