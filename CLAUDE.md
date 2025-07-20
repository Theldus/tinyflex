# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with
code in this repository.

## Project Overview

tinyflex is a single-header, dependency-free FLEX™ protocol encoder library
written in C99. FLEX is a radio paging protocol for transmitting messages to
pagers. The library produces fully-valid FLEX paging messages with zero
dynamic allocations, making it suitable for embedded/freestanding environments.

## Development Commands

### Building Demo Programs
```bash
cd demos && make
```

### Testing
The project uses MD5 hash comparison against reference binary outputs:
```bash
# Build first
cd demos && make

# Test 7-digit capcode encoding
printf "1234567:HACK THE PLANET" | ./encode_file | md5sum
# Expected: matches md5sum of tests/7digit_output.bin

# Test 9-digit capcode encoding  
printf "123456789:HACKING TO THE GATE" | ./encode_file | md5sum
# Expected: matches md5sum of tests/9digit_output.bin
```

### Cleaning Build Artifacts
```bash
cd demos && make clean
```

## Architecture

### Core Library
- **Main file**: `tinyflex.h` - Single header containing the entire implementation
- **Public API**: Single function `tf_encode_flex_message()`
- **Buffer size**: Use `FLEX_BUFFER_SIZE` constant for output buffer allocation
- **Standards compliance**: C99, works in freestanding environments

### Demo Programs (demos/)
- **encode_file.c**: Converts messages to FLEX packets (file or stdin/stdout)
- **send_ttgo.c**: Transmits FLEX packets via TTGO LoRa32-OLED hardware

### Test Data (tests/)
- **7digit_output.bin**: Reference output for 7-digit capcode test
- **9digit_output.bin**: Reference output for 9-digit capcode test

## Protocol Specifications

### FLEX Protocol Constraints
- **Speed**: 1600bps / 2-FSK
- **Frame**: Single-frame support only
- **Messages**: Alphanumeric (ASCII) up to 248 characters
- **Capcodes**: Both short (7-digit) and long (9-digit) addresses supported

### Key Constants
- `FLEX_BUFFER_SIZE`: Minimum required buffer size for output
- `BAUDRATE`: 1600 bps
- `BLOCKS_PER_FRAME`: 11
- `WORDS_PER_BLOCK`: 8 (at 1600 bps)

## API Usage

The main encoding function:
```c
size_t tf_encode_flex_message(const char *msg, uint64_t cap_code,
                             uint8_t *flex_pckt, size_t flex_size, int *error);
```

Basic usage pattern:
```c
#include "tinyflex.h"

uint8_t vec[FLEX_BUFFER_SIZE] = {0};
size_t size;
int err;

size = tf_encode_flex_message("HELLO, WORLD!", 1234567, vec, sizeof vec, &err);
if (!err) {
    /* Handle error */
} else {
    /* Use encoded output in vec[] */
}
```

## Build Configuration

- **Compiler**: Any C89/C99 compatible compiler
- **Flags**: `-Wall -Wextra -std=c89`
- **Dependencies**: None (uses only standard C library)
- **Include path**: Add parent directory (`-I$(CURDIR)/..`) for demos

## CI/CD

GitHub Actions CI runs on every push/PR to master:
1. Builds demo programs
2. Tests encoding correctness for both short and long capcodes
3. Compares MD5 hashes of output against reference files