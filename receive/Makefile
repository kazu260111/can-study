CC      := gcc
CFLAGS  := -Wall -Wextra -I..
BUILD   := build
TARGET  := $(BUILD)/receiver
OBJS    := $(BUILD)/receiver.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD)/receiver.o: receiver.c ../util.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)

.PHONY: all clean
