# HackRF TCP Server for FLEX Paging

A TCP server that receives paging messages and transmits them using HackRF One with FLEX protocol encoding.

## Features

- **FLEX Protocol Support**: Encodes messages using TinyFlex library
- **EMR Support**: Automatically sends Emergency Message Resynchronization messages
- **Flexible Configuration**: Supports both config files and environment variables
- **Multiple Output Modes**: Debug, verbose, and normal operation modes
- **Network Binding**: Configurable bind address for network interfaces

## Configuration

The server supports two configuration methods:

### 1. Configuration File (Recommended)

Create a `config.ini` file:

```ini
# HackRF TCP Server Configuration
BIND_ADDRESS=127.0.0.1
PORT=16175
SAMPLE_RATE=2000000
BITRATE=1600
AMPLITUDE=127
FREQ_DEV=2400
TX_GAIN=0
```

### 2. Environment Variables

If `config.ini` doesn't exist, the server will use environment variables:

```bash
export BIND_ADDRESS="127.0.0.1"
export PORT="16175"
export SAMPLE_RATE="2000000"
export BITRATE="1600"
export AMPLITUDE="127"
export FREQ_DEV="2400"
export TX_GAIN="0"
```

### Configuration Parameters

| Parameter | Description | Default | Range/Notes |
|-----------|-------------|---------|-------------|
| `BIND_ADDRESS` | IP address to bind TCP server to | `127.0.0.1` | Use `0.0.0.0` for all interfaces |
| `PORT` | TCP port to listen on | `16175` | "page" in phone keypad |
| `SAMPLE_RATE` | HackRF sample rate (Hz) | `2000000` | Minimum 2MHz for HackRF |
| `BITRATE` | FSK bitrate (bps) | `1600` | Minimum 1600 for 2FSK FLEX |
| `AMPLITUDE` | Software amplitude scaling | `127` | Range: -127 to 127 |
| `FREQ_DEV` | Frequency deviation (Hz) | `2400` | FLEX 2FSK uses ±2400Hz = 4800Hz total |
| `TX_GAIN` | HackRF hardware gain (dB) | `0` | Range: 0-47 dB |

## Building

```bash
make clean && make
```

## Usage

```bash
./hackrf_tcp_server [OPTIONS]
```

### Command Line Options

| Option | Description |
|--------|-------------|
| `--help`, `-h` | Show detailed help message and configuration info |
| `--debug`, `-d` | Debug mode: prints raw bytes, creates IQ file, skips transmission |
| `--verbose`, `-v` | Verbose mode: detailed output with actual transmission |

### Examples

```bash
# Show help and configuration options
./hackrf_tcp_server --help

# Run with verbose output
./hackrf_tcp_server --verbose

# Run in debug mode (no RF transmission)
./hackrf_tcp_server --debug

# Run with environment variables
source vars.sh && ./hackrf_tcp_server --verbose
```

## Message Protocol

Send messages via TCP connection in the format:
```
{CAPCODE}|{MESSAGE}|{FREQUENCY_IN_HZ}
```

### Examples

Using netcat:
```bash
# Send a simple message
echo '001122334|Hello World|925516000' | nc localhost 16175

# Send to different frequency
echo '123456789|Emergency Alert|929500000' | nc localhost 16175

# Send long message
echo '555123456|This is a longer message for testing purposes|925516000' | nc localhost 16175
```

Using telnet:
```bash
telnet localhost 16175
# Then type: 001122334|Test Message|925516000
```

## EMR (Emergency Message Resynchronization)

The server automatically handles EMR protocol requirements:

- **First Message**: Always sends an EMR message before the first transmission
- **Idle Timeout**: Sends EMR message if more than 10 minutes have passed since last transmission
- **Purpose**: Ensures pager receivers maintain proper synchronization

EMR behavior:
- Debug mode: Shows "Would send EMR messages here" without transmission
- Verbose mode: Shows detailed EMR transmission progress
- Normal mode: Sends EMR silently in background

## Debug Features

### Debug Mode (`--debug`)
- Prints encoded FLEX data in hexadecimal
- Generates `flexserver_output.iq` file for signal analysis
- Shows EMR message notifications
- **Does not transmit RF signals** (safe for development)

### Verbose Mode (`--verbose`)
- Shows configuration on startup
- Displays client connection details
- Reports EMR transmission status
- Shows sample generation and transmission progress
- **Performs actual RF transmission**

### IQ File Analysis
In debug mode, the server creates `flexserver_output.iq` containing the generated I/Q samples. This file can be analyzed with tools like:
- GNU Radio
- SDR#
- GQRX
- Custom analysis scripts

## Error Handling

The server validates all inputs and provides detailed error messages:

### Capcode Validation
- Must be valid FLEX capcode format
- Range checking performed
- Invalid format returns error to client

### Frequency Validation  
- Range: 1 MHz to 6 GHz
- Invalid frequencies rejected with error message

### Message Encoding
- Uses TinyFlex library for FLEX encoding
- Encoding errors reported with error codes
- Buffer overflow protection

## Network Configuration

### Local Access Only (Default)
```ini
BIND_ADDRESS=127.0.0.1
```

### All Network Interfaces
```ini
BIND_ADDRESS=0.0.0.0
```

### Specific Interface
```ini
BIND_ADDRESS=192.168.1.100
```

## Development

### File Structure
```
├── main.cpp                 # Main server application
├── include/
│   ├── config.hpp          # Configuration management
│   ├── tcp_util.hpp        # TCP server utilities
│   ├── hackrf_util.hpp     # HackRF device management
│   ├── fsk.hpp             # FSK signal generation
│   ├── flex_util.hpp       # FLEX encoding wrapper
│   └── iq_util.hpp         # I/Q file utilities
├── config.ini              # Configuration file
├── vars.sh                 # Environment variables example
└── README.md               # This file
```

### Dependencies
- libhackrf
- TinyFlex library
- Standard C++ libraries
- POSIX networking

## Troubleshooting

### Common Issues

**Server won't start:**
- Check if port is already in use: `netstat -ln | grep 16175`
- Verify bind address is valid
- Ensure HackRF permissions are correct

**No RF output:**
- Verify HackRF is connected and detected
- Check TX_GAIN setting (try increasing from 0)
- Ensure frequency is within HackRF range
- Verify antenna is connected

**Connection refused:**
- Check if server is running
- Verify port and bind address
- Test with: `telnet localhost 16175`

**EMR not working:**
- EMR is automatic - no manual intervention needed
- Check verbose output for EMR transmission status
- Verify 10-minute timeout behavior

### Debugging Steps

1. **Test with debug mode:**
   ```bash
   ./hackrf_tcp_server --debug
   ```

2. **Check IQ file generation:**
   ```bash
   ls -la flexserver_output.iq
   ```

3. **Test message format:**
   ```bash
   echo '123456789|Test|925516000' | nc localhost 16175
   ```

4. **Verify configuration:**
   ```bash
   ./hackrf_tcp_server --verbose
   ```
