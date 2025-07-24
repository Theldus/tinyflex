#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <libhackrf/hackrf.h>
#include <netinet/in.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>
#include "../../tinyflex.h"
#include "json.hpp"

#ifndef M_TAU
// Why calculate 2 * PI when we can just use a constant?
#define M_TAU 6.28318530717958647692
#endif

/**
 * Generates an FSK modulated signal based on binary data.
 * This function uses a Numerically Controlled Oscillator (NCO) to generate
 * the FSK signal by varying the frequency based on the binary data.
 * Each bit is represented by a number of samples defined by samples_per_symbol.
 * The output is a vector of doubles representing the FSK signal.
 *
 * @param binary_data           A vector of integers (0s and 1s) representing the binary data to be modulated.
 * @param freq_0                The frequency for binary '0'.
 * @param freq_1                The frequency for binary '1'.
 * @param sample_rate           The sample rate at which the signal is generated (samples per second).
 * @param samples_per_symbol    The number of samples per symbol,
 *                              which determines the duration of each bit in the output signal.
 *
 * @return std::vector<double>  A vector of doubles representing the FSK modulated signal.
 */
std::vector<double> generate_fsk_signal(
        const std::vector<int>& binary_data,
        double                  freq_0,
        double                  freq_1,
        double                  sample_rate,
        int                     samples_per_symbol
) {
    std::vector<double> output_signal;

    // NCO phase accumulator
    double phase = 0.0;

    // Precompute per-sample frequency steps for each bit
    double freq_step_0 = M_TAU * freq_0 / sample_rate;
    double freq_step_1 = M_TAU * freq_1 / sample_rate;

    // Generate the FSK signal
    for (int bit : binary_data) {
        // Select frequency step based on the bit value
        double freq_step = (bit == 0) ? freq_step_0 : freq_step_1;

        // Generate samples for this bit
        for (int i = 0; i < samples_per_symbol; ++i) {
            // Calculate the I/Q values based on the current phase
            output_signal.push_back(std::sin(phase));
            // For Q channel, we can use cos(phase) if needed
            phase += freq_step;
            // Normalize phase to keep it within [0, 2π]
            if (phase > M_TAU) phase -= M_TAU;
            // Ensure phase is non-negative
            if (phase < 0) phase += M_TAU;
        }
    }

    return output_signal;
}

int main(int argc, char* argv[]) {
    // Parse CLI arguments for --debug
    bool debug_mode = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--debug") {
            debug_mode = true;
        }
    }

    // Load our configuration from config.json
    std::ifstream config_file("config.json");
    nlohmann::json config;
    if (!config_file.is_open()) {
        std::cerr << "Failed to open config.json!" << std::endl;
        return 1;
    }

    try {
        config_file >> config;
    } catch (const std::exception& e) {
        std::cerr << "Failed to parse config.json: " << e.what() << std::endl;
        return 1;
    }

    int PORT        = config["PORT"];
    int SAMPLE_RATE = config["SAMPLE_RATE"];
    int BITRATE     = config["BITRATE"];
    int AMPLITUDE   = config["AMPLITUDE"];
    int FREQ_DEV    = config["FREQ_DEV"];
    int TX_GAIN     = config["TX_GAIN"];

    // TCP server setup
    struct sockaddr_in address; // FIX: declare address as sockaddr_in
    int    server_fd;
    int    client_fd;
    int    opt          = 1;
    int    addrLen      = sizeof(address);
    char   buffer[2048] = {0};

    // Create socket file descriptor.
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        return 1;
    }

    // Forcefully attach socket to the port
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        return 1;
    }

    // Define the type of socket address, in this case IPv4
    address.sin_family = AF_INET;
    // Allow connections from any IP address
    address.sin_addr.s_addr = INADDR_ANY;
    // Set the port number
    address.sin_port = htons(PORT);

    // Bind the socket to the address and port
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        return 1;
    }

    // Listen for incoming connections
    if (listen(server_fd, 1) < 0) {
        perror("listen");
        return 1;
    }

    // Constant loop, waiting for a connection.
    printf("Waiting for client on port %d...\n", PORT);
    while (true) {
        // Accept a new connection
        if ((client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrLen)) < 0) {
            perror("accept");
            break;
        }
        printf("Client connected!\n");

        // Read input from client
        int valRead = read(client_fd, buffer, sizeof(buffer) - 1);
        if (valRead <= 0) {
            perror("read");
            close(client_fd);
            continue;
        }
        buffer[valRead] = '\0';

        // Parse input: {CAPCODE}|{MESSAGE}|{FREQUENCY IN HZ}
        std::string input(buffer);
        size_t pos1 = input.find('|');
        size_t pos2 = input.rfind('|');
        if (pos1 == std::string::npos || pos2 == std::string::npos || pos1 == pos2) {
            printf("Invalid input format.\n");
            close(client_fd);
            continue;
        }

        std::string capcode_str = input.substr(0, pos1);
        std::string message     = input.substr(pos1 + 1, pos2 - pos1 - 1);
        std::string freq_str    = input.substr(pos2 + 1);

        int capcode = std::stoi(capcode_str);
        int frequency = std::stoi(freq_str);
        printf("Received: CAPCODE=%d, MESSAGE='%s', FREQUENCY=%d\n", capcode, message.c_str(), frequency);

        // Encode message using TinyFlex
        uint8_t flex_buffer[1024];
        memset(flex_buffer, 0, sizeof(flex_buffer));
        int error = 0;
        size_t flex_len = tf_encode_flex_message(message.c_str(), capcode, flex_buffer, sizeof(flex_buffer), &error);
        if (flex_len == 0 || error != 0) {
            printf("tf_encode_flex_message failed! Error code: %d\n", error);
            close(client_fd);
            continue;
        }

        // This should only show if --debug was in the argument for starting the server.
        if (debug_mode) {
            printf("Encoded FLEX (%zu bytes): ", flex_len);
            for (size_t i = 0; i < flex_len; ++i) {
                printf("%02X ", flex_buffer[i]);
            }
            printf("\n");
        }

        // --- HackRF transmit setup ---
        // Init the HackRF device, if it fails, we just skip this.
        // Could put this in a queue, but for now we just skip it.
        hackrf_device *device = nullptr;
        int result = hackrf_init();
        if (result != HACKRF_SUCCESS) {
            printf("hackrf_init() failed: %s\n", hackrf_error_name((hackrf_error)result));
            close(client_fd);
            continue;
        }

        // Open the HackRF device
        result = hackrf_open(&device);
        if (result != HACKRF_SUCCESS) {
            printf("hackrf_open() failed: %s\n", hackrf_error_name((hackrf_error)result));
            hackrf_exit();
            close(client_fd);
            continue;
        }

        // Set HackRF parameters
        result = hackrf_set_sample_rate(device, SAMPLE_RATE);
        if (result != HACKRF_SUCCESS) {
            printf("hackrf_set_sample_rate() failed\n");
        }

        result = hackrf_set_freq(device, (uint64_t)frequency);
        if (result != HACKRF_SUCCESS) {
            printf("hackrf_set_freq() failed\n");
        }

        result = hackrf_set_txvga_gain(device, TX_GAIN); // 0-47 dB
        if (result != HACKRF_SUCCESS) {
            printf("hackrf_set_txvga_gain() failed\n");
        }

        // --- FSK modulation using generate_fsk_signal ---
        // Convert flex_buffer to binary vector
        std::vector<int> binary_data;
        for (size_t i = 0; i < flex_len; ++i) {
            for (int b = 7; b >= 0; --b) {
                binary_data.push_back((flex_buffer[i] >> b) & 0x01);
            }
        }

        double samples_per_bit = (double)SAMPLE_RATE / BITRATE;
        int samples_per_symbol = (int)samples_per_bit;
        double freq_0 = -FREQ_DEV; // Centered at 0, deviation -/+ FREQ_DEV
        double freq_1 = +FREQ_DEV;

        // Generate proper I/Q FSK signal
        std::vector<int8_t> iq_samples;
        // NCO phase accumulator
        double phase = 0.0;
        double freq_step_0 = M_TAU * freq_0 / SAMPLE_RATE;
        double freq_step_1 = M_TAU * freq_1 / SAMPLE_RATE;
        for (int bit : binary_data) {
            double freq_step = (bit == 0) ? freq_step_0 : freq_step_1;
            for (int i = 0; i < samples_per_symbol; ++i) {
                double I = std::cos(phase);
                double Q = std::sin(phase);
                int val_I = static_cast<int>(std::round(AMPLITUDE * I));
                int val_Q = static_cast<int>(std::round(AMPLITUDE * Q));
                if (val_I > 127) val_I = 127;
                if (val_I < -127) val_I = -127;
                if (val_Q > 127) val_Q = 127;
                if (val_Q < -127) val_Q = -127;
                iq_samples.push_back(static_cast<int8_t>(val_I)); // I
                iq_samples.push_back(static_cast<int8_t>(val_Q)); // Q
                phase += freq_step;
                if (phase > M_TAU) phase -= M_TAU;
                if (phase < 0) phase += M_TAU;
            }
        }

        // --- Write IQ samples to file for analysis ---
        const char* iq_filename = "flexserver_output.iq";
        FILE* iq_file = fopen(iq_filename, "wb");
        if (iq_file) {
            fwrite(iq_samples.data(), sizeof(int8_t), iq_samples.size(), iq_file);
            fclose(iq_file);
            printf("Wrote %zu IQ samples to %s\n", iq_samples.size() / 2, iq_filename);
        } else {
            printf("Failed to open %s for writing!\n", iq_filename);
        }

        // --- Transmit IQ samples ---
        if (debug_mode) {
            printf("%zu IQ samples...\n", iq_samples.size() / 2);
        }

        // Only transmit if not in debug mode.
        if (!debug_mode) {
            struct TxState {
                const int8_t* data;
                size_t total;
                size_t sent;
            };
            TxState tx_state = { iq_samples.data(), iq_samples.size(), 0 };

            auto tx_callback = [](hackrf_transfer* transfer) -> int {
                TxState* state = reinterpret_cast<TxState*>(transfer->tx_ctx);
                size_t to_copy = transfer->buffer_length;
                if (state->sent + to_copy > state->total) {
                    to_copy = state->total - state->sent;
                }
                if (to_copy > 0) {
                    memcpy(transfer->buffer, state->data + state->sent, to_copy);
                    state->sent += to_copy;
                    // If done, fill rest with zeros
                    if (to_copy < (size_t)transfer->buffer_length) {
                        memset(transfer->buffer + to_copy, 0, transfer->buffer_length - to_copy);
                    }
                } else {
                    memset(transfer->buffer, 0, transfer->buffer_length);
                }
                // Stop when all samples sent
                return (state->sent >= state->total) ? 1 : 0;
            };

            result = hackrf_start_tx(device, tx_callback, &tx_state);
            if (result != HACKRF_SUCCESS) {
                printf("hackrf_start_tx() failed\n");
            } else {
                // Wait for transmission to finish
                while (tx_state.sent < tx_state.total) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                hackrf_stop_tx(device);
            }
            printf("Transmission complete.\n");
        } else {
            printf("Debug mode active, skipping HackRF transmission.\n");
        }

        hackrf_close(device);
        hackrf_exit();
        close(client_fd);
    }

    close(server_fd);
    return 0;
}