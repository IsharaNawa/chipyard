#!/bin/bash
# Script to create a tarball of the latest binaries in the bin folder
set -e


TARBALL="binaries_$(date +%Y%m%d_%H%M%S).tar.gz"

tar -czvf "$TARBALL" core0_producer core1_consumer run_test.sh

echo "Tarball created: $TARBALL"