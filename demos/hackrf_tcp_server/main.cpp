#include <cstring>
#include <iostream>
#include <libhackrf/hackrf.h>
#include <netinet/in.h>
#include <string>
#include <unistd.h>
#include <cstdlib>
#include <chrono>
#include <thread>
#include "../../tinyflex.h"
#include "include/config.hpp"
#include "include/fsk.hpp"
#include "include/hackrf_util.hpp"
#include "include/flex_util.hpp"
#include "include/tcp_util.hpp"
#include "include/iq_util.hpp"

#ifndef M_TAU
// Why calculate 2 * PI when we can just use a constant?
#define M_TAU 6.28318530717958647692
#endif

void print_help() {
    std::cout << "hackrf_tcp_server - FLEX paging TCP server for HackRF\n\n";
    std::cout << "USAGE:\n";
    std::cout << "  hackrf_tcp_server [OPTIONS]\n\n";
    std::cout << "OPTIONS:\n";
    std::cout << "  --help, -h     Show this help message and exit\n";
    std::cout << "  --debug, -d    Enable debug mode (prints raw bytes, creates IQ file, skips transmission)\n";
    std::cout << "  --verbose, -v  Enable verbose output (detailed transmission info)\n\n";
    std::cout << "CONFIGURATION:\n";
    std::cout << "  The server reads configuration from config.ini (preferred) or falls back to\n";
    std::cout << "  environment variables if the file doesn't exist.\n\n";
    std::cout << "  Configuration parameters:\n";
    std::cout << "    BIND_ADDRESS  - IP address to bind to (default: 127.0.0.1)\n";
    std::cout << "    PORT          - TCP port to listen on (default: 16175)\n";
    std::cout << "    SAMPLE_RATE   - HackRF sample rate (default: 2000000)\n";
    std::cout << "    BITRATE       - FSK bitrate (default: 1600)\n";
    std::cout << "    AMPLITUDE     - Signal amplitude (default: 127)\n";
    std::cout << "    FREQ_DEV      - Frequency deviation (default: 2400)\n";
    std::cout << "    TX_GAIN       - HackRF TX gain in dB, 0-47 (default: 0)\n\n";
    std::cout << "PROTOCOL:\n";
    std::cout << "  Send messages via TCP in format: {CAPCODE}|{MESSAGE}|{FREQUENCY_HZ}\n";
    std::cout << "  Example: echo '001122334|Hello World|925516000' | nc localhost 16175\n\n";
    std::cout << "EMR (Emergency Message Resynchronization):\n";
    std::cout << "  If this is the first message or no messages have been sent for more than\n";
    std::cout << "  10 minutes, the server will automatically send an EMR message before\n";
    std::cout << "  transmitting the actual message to ensure proper synchronization.\n\n";
}

struct ConnectionState {
    std::chrono::steady_clock::time_point last_transmission;
    bool first_message;

    ConnectionState() : first_message(true) {}
};

bool should_send_emr(ConnectionState& state) {
    if (state.first_message) {
        return true;
    }

    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::minutes>(
        now - state.last_transmission);

    return duration.count() >= 10;
}

void send_emr_messages(hackrf_device* device, const Config& config, bool verbose_mode) {
    if (verbose_mode) {
        std::cout << "Sending EMR (Emergency Message Resynchronization) message...\n";
    }

    // EMR message is typically a short synchronization burst
    // Using a standard EMR pattern for FLEX
    uint8_t emr_buffer[] = {0xA5, 0x5A, 0xA5, 0x5A}; // Simple sync pattern

    std::vector<int8_t> emr_iq_samples = generate_fsk_iq_samples(
        emr_buffer,
        sizeof(emr_buffer),
        config.SAMPLE_RATE,
        config.BITRATE,
        config.AMPLITUDE,
        config.FREQ_DEV
    );

    if (verbose_mode) {
        std::cout << "Transmitting EMR message...\n";
    }

    transmit_hackrf(device, emr_iq_samples);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    if (verbose_mode) {
        std::cout << "EMR transmission complete.\n";
    }
}

int main(int argc, char* argv[]) {
    // Parse CLI arguments
    bool debug_mode = false;
    bool verbose_mode = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--debug" || arg == "-d") {
            debug_mode = true;
        } else if (arg == "--verbose" || arg == "-v") {
            verbose_mode = true;
        } else if (arg == "--help" || arg == "-h") {
            print_help();
            return 0;
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            std::cerr << "Use --help for usage information." << std::endl;
            return 1;
        }
    }

    Config config;
    bool config_loaded = false;

    // Try to load config.ini first
    if (load_config("config.ini", config)) {
        config_loaded = true;
        if (verbose_mode) {
            std::cout << "Configuration loaded from config.ini\n";
        }
    } else {
        // Fall back to environment variables
        if (verbose_mode) {
            std::cout << "config.ini not found, using environment variables\n";
        }

        const char* env_bind = getenv("BIND_ADDRESS");
        const char* env_port = getenv("PORT");
        const char* env_sample_rate = getenv("SAMPLE_RATE");
        const char* env_bitrate = getenv("BITRATE");
        const char* env_amplitude = getenv("AMPLITUDE");
        const char* env_freq_dev = getenv("FREQ_DEV");
        const char* env_tx_gain = getenv("TX_GAIN");

        // Set defaults
        config.BIND_ADDRESS = env_bind ? std::string(env_bind) : "127.0.0.1";
        config.PORT = env_port ? std::stoul(env_port) : 16175;
        config.SAMPLE_RATE = env_sample_rate ? std::stoull(env_sample_rate) : 2000000;
        config.BITRATE = env_bitrate ? std::stoul(env_bitrate) : 1600;
        config.AMPLITUDE = env_amplitude ? static_cast<int8_t>(std::stoi(env_amplitude)) : 127;
        config.FREQ_DEV = env_freq_dev ? std::stoul(env_freq_dev) : 2400;
        config.TX_GAIN = env_tx_gain ? static_cast<uint8_t>(std::stoi(env_tx_gain)) : 0;

        config_loaded = true;
    }

    if (!config_loaded) {
        std::cerr << "Failed to load configuration!" << std::endl;
        return 1;
    }

    if (verbose_mode) {
        std::cout << "Configuration:\n";
        std::cout << "  BIND_ADDRESS: " << config.BIND_ADDRESS << "\n";
        std::cout << "  PORT: " << config.PORT << "\n";
        std::cout << "  SAMPLE_RATE: " << config.SAMPLE_RATE << "\n";
        std::cout << "  BITRATE: " << config.BITRATE << "\n";
        std::cout << "  AMPLITUDE: " << static_cast<int>(config.AMPLITUDE) << "\n";
        std::cout << "  FREQ_DEV: " << config.FREQ_DEV << "\n";
        std::cout << "  TX_GAIN: " << static_cast<int>(config.TX_GAIN) << "\n";
    }

    // TCP server setup with bind address
    struct sockaddr_in address;
    int server_fd = setup_tcp_server(config.PORT, address, config.BIND_ADDRESS);
    if (server_fd < 0) {
        return 1;
    }

    int client_fd;
    int addrLen = sizeof(address);
    char buffer[2048] = {0};
    ConnectionState conn_state;

    // Constant loop, waiting for a connection.
    printf("Waiting for client on %s:%d...\n", config.BIND_ADDRESS.c_str(), config.PORT);

    while (true) {
        // Accept a new connection
        if ((client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrLen)) < 0) {
            perror("accept");
            break;
        }

        if (verbose_mode) {
            std::cout << "Client connected from " << inet_ntoa(address.sin_addr) << "\n";
        } else {
            printf("Client connected!\n");
        }

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

        uint64_t capcode;
        long frequency;

        try {
            capcode = std::stoull(capcode_str);
            int is_long;
            if (!is_capcode_valid(capcode, &is_long)) {
                throw std::invalid_argument("Invalid capcode: " + capcode_str);
            }
        } catch (const std::invalid_argument& e) {
            std::string error_msg = "Invalid capcode: " + capcode_str;
            printf("%s\n", error_msg.c_str());
            send(client_fd, error_msg.c_str(), error_msg.size(), 0);
            close(client_fd);
            continue;
        } catch (const std::out_of_range& e) {
            std::string error_msg = "Capcode out of range: " + capcode_str;
            printf("%s\n", error_msg.c_str());
            send(client_fd, error_msg.c_str(), error_msg.size(), 0);
            close(client_fd);
            continue;
        }

        try {
            frequency = std::stoul(freq_str);
            if (frequency < 1000000 || frequency > 6000000000) {
                throw std::out_of_range("Frequency out of valid range: " + freq_str);
            }
        } catch (const std::invalid_argument& e) {
            std::string error_msg = "Invalid frequency: " + freq_str;
            printf("%s\n", error_msg.c_str());
            send(client_fd, error_msg.c_str(), error_msg.size(), 0);
            close(client_fd);
            continue;
        } catch (const std::out_of_range& e) {
            std::string error_msg = "Frequency out of valid range: " + freq_str;
            printf("%s\n", error_msg.c_str());
            send(client_fd, error_msg.c_str(), error_msg.size(), 0);
            close(client_fd);
            continue;
        }

        if (verbose_mode) {
            printf("Received message - CAPCODE=%llu, MESSAGE='%s', FREQUENCY=%ld Hz\n",
                   (unsigned long long)capcode, message.c_str(), frequency);
        } else {
            printf("Received: CAPCODE=%llu, MESSAGE='%s', FREQUENCY=%ld\n",
                   (unsigned long long)capcode, message.c_str(), frequency);
        }

        // Encode message using TinyFlex
        uint8_t flex_buffer[1024];
        int error = 0;
        size_t flex_len = 0;
        if (!encode_flex_message(message, capcode, flex_buffer, sizeof(flex_buffer), flex_len, error)) {
            std::string error_msg = "Error encoding message: " + std::to_string(error);
            printf("%s\n", error_msg.c_str());
            send(client_fd, error_msg.c_str(), error_msg.size(), 0);
            close(client_fd);
            continue;
        }

        if (debug_mode || verbose_mode) {
            printf("Encoded FLEX (%zu bytes): ", flex_len);
            for (size_t i = 0; i < flex_len; ++i) {
                printf("%02X ", flex_buffer[i]);
            }
            printf("\n");
        }

        // --- HackRF transmitter setup ---
        hackrf_device* device = setup_hackrf((uint64_t)frequency, config.SAMPLE_RATE, config.TX_GAIN);
        if (!device) {
            close(client_fd);
            continue;
        }

        // Check if we need to send EMR messages
        bool need_emr = should_send_emr(conn_state);
        if (need_emr && !debug_mode) {
            send_emr_messages(device, config, verbose_mode);
        } else if (need_emr && debug_mode) {
            std::cout << "Debug mode: Would send EMR messages here\n";
        }

        // Generate FSK IQ samples from FLEX buffer
        std::vector<int8_t> iq_samples = generate_fsk_iq_samples(
            flex_buffer,
            flex_len,
            config.SAMPLE_RATE,
            config.BITRATE,
            config.AMPLITUDE,
            config.FREQ_DEV
        );

        if (debug_mode || verbose_mode) {
            printf("Generated %zu IQ samples for transmission\n", iq_samples.size() / 2);
        }

        // --- Write IQ samples to file for analysis (debug mode) ---
        if (debug_mode) {
            write_iq_file("flexserver_output.iq", iq_samples);
        }

        // --- Transmit IQ samples ---
        if (!debug_mode) {
            if (verbose_mode) {
                std::cout << "Starting transmission...\n";
            }
            transmit_hackrf(device, iq_samples);

            // Update connection state
            conn_state.last_transmission = std::chrono::steady_clock::now();
            conn_state.first_message = false;
        } else {
            printf("Debug mode active, skipping HackRF transmission.\n");
        }

        // Send success message back to client
        std::string success_msg = "Message sent successfully!";
        send(client_fd, success_msg.c_str(), success_msg.size(), 0);
        close_hackrf(device);
        close(client_fd);

        if (verbose_mode) {
            std::cout << "Client connection closed.\n";
        }
    }

    close(server_fd);
    return 0;
}
