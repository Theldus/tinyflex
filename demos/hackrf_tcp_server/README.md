# TCP for Hackrf

## config.json
```
{
    "PORT": 16175,
    "SAMPLE_RATE": 2000000,
    "BITRATE": 1600,
    "AMPLITUDE": 127,
    "FREQ_DEV": 2400,
    "TX_GAIN": 0
}
```

### PORT 
The TCP port we want to listen to. In this case 16175 translates to "page".

### SAMPLE_RATE
The sample rate you want to run at for Hackrf. 2M is the lowest.

### BITRATE
1600 is the lowest for 2FSK Flex

### AMPLITUDE
How much do we need to amplify this in software? 127 is the highest. -127 being the lowest?

### FREQ_DEV
Our +- frequency deviation. Flex 2FSK is 4800Hz, so +-2400 = 4800

### TX_GAIN
Set the hardware gain in dB for the Hackrf. Min:0 Max:47 Step:1

### Building
make clean && make

## Usage
./hackrf_tcp_server [--debug]

There is a debug mode that you can enable with the --debug argument. This will print out the raw bytes being sent to Hackrf and create an IQ file.

To send a message, you can use the following command (using netcat):
```bash
printf '{CAPCODE}|{MESSAGE}|{FREQ IN HZ}' | nc localhost 16175
printf '001122334|Communicating like its the 90s|925516000' | nc localhost 16175
                                                
```