# Makefile for 2-Core Pipelined Squares App (interrupt-aware)
#
# Builds two static RISC-V binaries:
#   bin/core1_producer  - produces 10000 squared values via /dev/uio0
#   bin/core2_consumer  - consumes them in 20 bursts of 500 via /dev/uio1
#
# Both block in read(uio_fd) on the InterruptAwareMMIOFIFO producer-wake /
# consumer-wake IRQs (no spin-polling MMIO).
#
# Requires: the dual-IRQ isb_uio.ko kernel module to be loaded on the board.

CC      = riscv64-linux-gnu-gcc
CFLAGS  = -Wall -O2 -static -pthread
LDFLAGS = -static -pthread

BIN_DIR     = bin
CORE1_BIN   = $(BIN_DIR)/core1_producer
CORE2_BIN   = $(BIN_DIR)/core2_consumer
CORE1_SRC   = core1_producer.c
CORE2_SRC   = core2_consumer.c

.PHONY: all clean

all: $(CORE1_BIN) $(CORE2_BIN)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(CORE1_BIN): $(CORE1_SRC) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(CORE1_SRC) $(LDFLAGS)

$(CORE2_BIN): $(CORE2_SRC) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(CORE2_SRC) $(LDFLAGS)

clean:
	rm -rf $(BIN_DIR) output.txt
