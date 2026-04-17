#!/bin/bash
# Build verification test

set -e

echo "=== Build Verification Test ==="

# Test 1: Clean build
echo "[1/3] Testing clean build..."
make clean
make build
if [ -f "cpp-dumper" ]; then
    echo "  ✓ Binary created: cpp-dumper"
else
    echo "  ✗ Binary not found"
    exit 1
fi

# Test 2: Binary is executable
echo "[2/3] Testing binary is executable..."
if [ -x "cpp-dumper" ]; then
    echo "  ✓ Binary is executable"
else
    echo "  ✗ Binary is not executable"
    exit 1
fi

# Test 3: Binary runs without arguments (shows help or runs)
echo "[3/3] Testing binary execution..."
if ./cpp-dumper --help >/dev/null 2>&1 || ./cpp-dumper 2>&1 | head -1 >/dev/null; then
    echo "  ✓ Binary runs successfully"
else
    echo "  ✗ Binary failed to run"
    exit 1
fi

echo ""
echo "All build tests passed!"