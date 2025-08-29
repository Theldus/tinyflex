#!/bin/bash
#
# tinyflex: A minimal, dependency-free, single-header library, FLEX encoder.
# Written by Davidson Francis (aka Theldus) - 2025.
#
# This is free and unencumbered software released into the public domain.
#

set -e

echo "Running tinyflex tests..."

# Test short capcode alphanumeric
echo "[1] Testing short capcode alphanumeric..."
hash1=$(printf "1234567:HACK THE PLANET" | ./encode_file | md5sum | awk '{print $1}')
hash2=$(md5sum ../tests/7digit_output.bin | awk '{print $1}')
if [ "$hash1" = "$hash2" ]; then
	echo "Short capcode alphanumeric: OK"
else
	echo "Short capcode alphanumeric: FAIL"
	exit 1
fi

# Test long capcode alphanumeric
echo "[2] Testing long capcode alphanumeric..."
hash1=$(printf "123456789:HACKING TO THE GATE" | ./encode_file | md5sum | awk '{print $1}')
hash2=$(md5sum ../tests/9digit_output.bin | awk '{print $1}')
if [ "$hash1" = "$hash2" ]; then
	echo "Long capcode alphanumeric: OK"
else
	echo "Long capcode alphanumeric: FAIL"
	exit 1
fi

# Test short capcode numeric
echo "[3] Testing short capcode numeric..."
hash1=$(printf "1234567:12345" | ./encode_file -n | md5sum | awk '{print $1}')
hash2=$(md5sum ../tests/7digit_numeric_output.bin | awk '{print $1}')
if [ "$hash1" = "$hash2" ]; then
	echo "Short capcode numeric: OK"
else
	echo "Short capcode numeric: FAIL"
	exit 1
fi

# Test long capcode numeric
echo "[4] Testing long capcode numeric..."
hash1=$(printf "123456789:987654321" | ./encode_file -n | md5sum | awk '{print $1}')
hash2=$(md5sum ../tests/9digit_numeric_output.bin | awk '{print $1}')
if [ "$hash1" = "$hash2" ]; then
	echo "Long capcode numeric: OK"
else
	echo "Long capcode numeric: FAIL"
	exit 1
fi

# Test short capcode tone-only
echo "[5] Testing short capcode tone-only..."
hash1=$(printf "1234567" | ./encode_file -t | md5sum | awk '{print $1}')
hash2=$(md5sum ../tests/7digit_tone_only_output.bin | awk '{print $1}')
if [ "$hash1" = "$hash2" ]; then
	echo "Short capcode tone-only: OK"
else
	echo "Short capcode tone-only: FAIL"
	exit 1
fi

# Test long capcode tone-only
echo "[6] Testing long capcode tone-only..."
hash1=$(printf "123456789" | ./encode_file -t | md5sum | awk '{print $1}')
hash2=$(md5sum ../tests/9digit_tone_only_output.bin | awk '{print $1}')
if [ "$hash1" = "$hash2" ]; then
	echo "Long capcode tone-only: OK"
else
	echo "Long capcode tone-only: FAIL"
	exit 1
fi

echo "All tests passed!"
