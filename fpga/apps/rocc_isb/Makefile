# Makefile for RoCC ISB Two-Core Communication Test
# Cross-compiles for RISC-V Linux (static binaries)

CC = riscv64-linux-gnu-gcc
CFLAGS = -Wall -O2 -static
LDFLAGS = -static

HEADER = rocc_isb.h
PRODUCER_SRC = core0_producer.c
CONSUMER_SRC = core1_consumer.c

DIAG_SRC = rocc_diag.c
PRODUCER_BIN = core0_producer
CONSUMER_BIN = core1_consumer
DIAG_BIN = rocc_diag

.PHONY: all clean help

all: $(PRODUCER_BIN) $(CONSUMER_BIN) $(DIAG_BIN)

$(PRODUCER_BIN): $(PRODUCER_SRC) $(HEADER)
	@echo "Building Core 0 Producer..."
	$(CC) $(CFLAGS) -o $@ $(PRODUCER_SRC) $(LDFLAGS)
	@echo "Built: $@"

$(CONSUMER_BIN): $(CONSUMER_SRC) $(HEADER)
	@echo "Building Core 1 Consumer..."
	$(CC) $(CFLAGS) -o $@ $(CONSUMER_SRC) $(LDFLAGS)
	@echo "Built: $@"

$(DIAG_BIN): $(DIAG_SRC) $(HEADER)
	@echo "Building RoCC Diagnostic..."
	$(CC) $(CFLAGS) -o $@ $(DIAG_SRC) $(LDFLAGS)
	@echo "Built: $@"

clean:
	rm -f $(PRODUCER_BIN) $(CONSUMER_BIN) $(DIAG_BIN)

help:
	@echo "RoCC ISB Two-Core Test"
	@echo "  make        - Build both binaries"
	@echo "  make clean  - Remove binaries"
	@echo ""
	@echo "Usage on FPGA:"
	@echo "  scp core0_producer core1_consumer run_test.sh root@<fpga>:~/"
	@echo "  ssh root@<fpga> 'chmod +x run_test.sh && ./run_test.sh'"
