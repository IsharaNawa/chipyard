# Cross-compile the InterruptAwareMMIOFIFO consumer-wake test for RISC-V Linux.
# Output: bin/test_consumer_wake

CC      = riscv64-linux-gnu-gcc
CFLAGS  = -Wall -O2 -static -pthread
LDFLAGS = -static -pthread

BIN_DIR = bin
TARGET  = $(BIN_DIR)/test_consumer_wake
SRC     = test_consumer_wake.c

.PHONY: all clean

all: $(TARGET)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(TARGET): $(SRC) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

clean:
	rm -rf $(BIN_DIR)
