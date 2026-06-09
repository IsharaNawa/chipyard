#!/bin/bash
# Script to extract binaries from a tarball in the current directory
set -e

if [ $# -ne 1 ]; then
  echo "Usage: $0 <tarball_file>"
  exit 1
fi

TARBALL="$1"

if [ ! -f "$TARBALL" ]; then
  echo "Error: Tarball $TARBALL not found."
  exit 1
fi

tar -xzvf "$TARBALL"

echo "Extraction complete. Binaries are in $(pwd)"