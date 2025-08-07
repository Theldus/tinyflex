# tinyflex <img align="right" src="https://i.imgur.com/GMhpOXw.png" />
[![License: Unlicense](https://img.shields.io/badge/License-Unlicense-8af7ff.svg)](https://opensource.org/licenses/Unlicense)
[![Build Status](https://github.com/Theldus/tinyflex/actions/workflows/c-cpp.yml/badge.svg)](https://github.com/Theldus/tinyflex/actions/workflows/c-cpp.yml)
<br/>
<br/>
tinyflex is a **single-header**, dependency-free FLEX™ protocol encoder written
in C99. It produces fully-valid FLEX paging messages with zero dynamic
allocations, no I/O, and no platform-specific dependencies, making it suitable
for freestanding environments.

## Features
As a compact library (given the protocol's complexity), tinyflex doesn't attempt
to implement the entire FLEX specification but focuses on a subset that provides
'good enough' functionality for most use cases:

- **Speed**: 1600bps / 2-FSK
- **Frame:** Single-frame support
- **Messages type**: Alphanumeric messages (ASCII) up to 248 characters
- **Capcode**: Short and long addresses supported!

## Usage
Usage is straightforward and centers around a single public function:

```c
size_t
tf_encode_flex_message(const char *msg, uint64_t cap_code,
    uint8_t *flex_pckt, size_t flex_size, int *error);
```

as follows:
```c
#include "tinyflex.h"

int main(void) {
    uint8_t vec[FLEX_BUFFER_SIZE] = {0};
    size_t size;
    int err;

    size = tf_encode_flex_message(
        "HELLO, WORLD!", 1234567, vec, sizeof vec, &err);

    if (!err)
        /* error. */
    else
        /* do something with your output. */
}
```

tinyflex outputs raw binary data that must be transmitted at 1600 bps / 2-FSK
with a deviation of ±4800 Hz (as per the FLEX specification, Section 3.1
Modulation).

The concept is simple: once you obtain the raw FLEX-encoded data, you can use an
external tool to transmit it.

The easiest way to transmit, if you have a LoRa32-OLED v2.1.6 board, is using
[ttgo-fsk-tx]. For transmitting with an SDR, see the GNU Radio flowchart
available under `demos/gnuradio`.

[ttgo-fsk-tx]: https://github.com/rlaneth/ttgo-fsk-tx/

## Companion demos
The `demo` directory contains companion programs to facilitate library usage:
`encode_file`, `send_ttgo`, `hackrf_tcp_server`, and `webasm`, plus a `gnuradio` 
subdirectory with transmission examples.

### `encode_file`
Designed for transmission tools that work with file input and/or communicate
via pipes, `encode_file` provides an easy way to generate FLEX packets:

```bash
./encode_file <capcode> <message> <output_file>
or:
./encode_file [-l] [-m] (from stdin/stdout)

Options:
   -l Loop mode: stays open receiving new lines of messages until EOF
   -m Mail Drop: sets the Mail Drop Flag in the FLEX message

Stdin/stdout mode:
   Example:
     printf '1234567:MY MESSAGE'               | ./encode_file (no loop mode)
     printf '1234567:MY MSG1\n1122334:MY MSG2' | ./encode_file -l (loop mode)
     printf '1234567:MY MESSAGE'               | ./encode_file -m (mail drop)
     printf '1234567:MY MESSAGE'               | ./encode_file -l -m (both)
   (binary output goes to stdout!)

   Note: On loop mode, each output is preceded by a line indicating
         how many bytes follows, ex:
   795\n   <binary output>
   600\n   <binary output>

Normal mode:
   ./encode_file 1234567 'MY MESSAGE' output.bin
   ./encode_file -m 1234567 'MY MESSAGE' output.bin (with mail drop)

```

`encode_file` allows you to save packets to an output file or generate new
packets continuously via stdin/stdout, enabling seamless integration with
external tools.

### send_ttgo
`send_ttgo` is a companion tool designed to work with [ttgo-fsk-tx] firmware,
enabling transmission of FLEX packets to pagers through a Lilygo TTGO LoRa32-OLED
development board.

Its usage is similar to `encode_file`, supporting both command-line arguments
and pipes:

```bash
./send_ttgo [options] <capcode> <message>
or:
./send_ttgo [options] [-l] [-m] - (from stdin)

Options:
   -d <device>    Serial device (default: /dev/ttyACM0)
   -b <baudrate>  Baudrate (default: 115200)
   -f <frequency> Frequency in MHz (default: 916.000000)
   -p <power>     TX power (default: 2, 2-17)
   -l             Loop mode: stays open receiving new lines until EOF
   -m             Mail Drop: sets the Mail Drop Flag in the FLEX message

Stdin mode:
   Example:
     printf '1234567:MY MESSAGE'               | ./send_ttgo
     printf '1234567:MY MSG1\n1122334:MY MSG2' | ./send_ttgo -l
     printf '1234567:MY MESSAGE'               | ./send_ttgo -m
     printf '1234567:MY MESSAGE'               | ./send_ttgo -l -m

Normal mode:
   ./send_ttgo 1234567 'MY MESSAGE'
   ./send_ttgo -m 1234567 'MY MESSAGE'
   ./send_ttgo -d /dev/ttyUSB0 -f 915.5 1234567 'MY MESSAGE'

```

### `hackrf_tcp_server`
`hackrf_tcp_server` is a C++ server that provides real-time FLEX transmission
using HackRF SDR devices. It accepts messages over TCP and immediately transmits
them without requiring file intermediates.

```bash
cd demos/hackrf_tcp_server
make
./hackrf_tcp_server

# Send a simple message
echo '001122334|Hello World|925516000' | nc localhost 16175
```

The server listens for TCP connections and expects messages in the format:
`CAPCODE|MESSAGE|FREQUENCY`.

### WebAssembly Demo
The `demos/webasm` directory contains a WebAssembly port of tinyflex that
serves as an online FLEX encoder, without requiring extra tooling.

```bash
cd demos/webasm
make                    # Requires Emscripten
```

Link: https://blog.theldus.moe/tinyflex/

### GNU Radio
The `demos/gnuradio` directory contains a flowchart demonstrating how to
transmit a single file encoded by `encode_file` using a HackRF (and can be
easily adapted for other devices).

To run the flowchart as-is:
1. Compile the tools under `demos`
2. Run `encode_file` with appropriate parameters to generate your message
3. Place the encoded file as `encoded.bin` under the `gnuradio` directory
4. Run the flowchart

Example:
```bash
cd demos
make
./encode_file 1234567 'HELLO WORLD' gnuradio/encoded.bin
cd gnuradio
gnuradio-companion hackrf_file_tx_demo.grc
```

## Projects using tinyflex

### [Flex HTTP Server]
An enhanced HTTP REST server that extends the basic HackRF TCP server
functionality, enabling Grafana alerts to be sent directly to
pagers, whether via HackRF or TTGO Lora32. Features dual protocol support
(TCP and HTTP JSON API), authentication,  configuration management, and a
dedicated Grafana webhook service for seamless alert forwarding.

[Flex HTTP Server]: https://github.com/geekinsanemx/flex-http-server

### [flex-fsk-tx]
A command-line FSK transmitter application that sends FLEX pager messages
over serial using AT commands.
Originally based on [tinyflex](https://github.com/Theldus/tinyflex),
and [ttgo-fsk-tx](https://github.com/rlaneth/ttgo-fsk-tx)
adapted for Heltec WiFi Lora32 v3 boards with chip SX1262 LoRa32

[flex-fsk-tx]: https://github.com/geekinsanemx/flex-fsk-tx

## Acknowledgements
This project came from discussions and experiments on paging that I have worked
on along with [@rlaneth]. He's gifted me a Motorola Advisor Elite pager and a
TTGO LoRa32-OLED board that have made this project possible.

[@rlaneth]: https://github.com/rlaneth

## Contributing
tinyflex welcomes community contributions of all kinds, including issues,
documentation improvements, testing, new features, bug fixes, typos, and more.
Welcome aboard!

## License
tinyflex is released into the public domain under the Unlicense license.
