#!/bin/bash
# Script to create a tarball of the latest binaries in the bin folder
set -e

BIN_DIR="$(dirname "$0")/bin"
TARBALL="binaries_$(date +%Y%m%d_%H%M%S).tar.gz"

if [ ! -d "$BIN_DIR" ]; then
  echo "Error: bin directory not found at $BIN_DIR"
  exit 1
fi

cd "$BIN_DIR"
tar -czvf "$TARBALL" core1_producer core2_consumer ../timing_test.sh

echo "Tarball created: $BIN_DIR/$TARBALL"