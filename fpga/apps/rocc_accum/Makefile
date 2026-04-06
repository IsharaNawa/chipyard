# Makefile for AccumulatorExample RoCC Linux test
# Cross-compiles for RISC-V Linux (static binary)

CC      = riscv64-linux-gnu-gcc
CFLAGS  = -Wall -O2 -static
LDFLAGS = -static

HEADER  = rocc_accum.h
SRC     = rocc_accum_test.c
BIN     = rocc_accum_test

.PHONY: all clean help

all: $(BIN)

$(BIN): $(SRC) $(HEADER)
	@echo "Building AccumulatorExample RoCC test..."
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)
	@echo "Built: $@"

clean:
	rm -f $(BIN)

help:
	@echo "AccumulatorExample RoCC Linux Test"
	@echo "  make        - Build the test binary"
	@echo "  make clean  - Remove binary"
	@echo ""
	@echo "Usage on FPGA:"
	@echo "  scp rocc_accum_test root@<fpga>:~/"
	@echo "  ssh root@<fpga> './rocc_accum_test'"
