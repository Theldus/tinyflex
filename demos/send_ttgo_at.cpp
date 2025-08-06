/*
 * flex-fsk-tx: Send FLEX packets over serial using AT commands.
 * Original send_ttgo application Written by Davidson Francis (aka Theldus) and Rodrigo Laneth - 2025.
 * > https://github.com/Theldus/tinyflex
 * send_ttgo adaptation for heltec v3 and standarized AT command renamed to flex-fsk-tx @ geekinsanemx
 * > https://github.com/geekinsanemx/flex-fsk-tx
 * Improved AT Protocol implementation with better retry logic and error handling.
 *
 * This is free and unencumbered software released into the public domain.
 */

#define _DEFAULT_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <inttypes.h>
#include <termios.h>
#include <unistd.h>
#include <poll.h>
#include <limits.h>
#include <ctime>

#include "../tinyflex.h"

/* Default serial parameters */
#define DEFAULT_DEVICE    "/dev/ttyACM0"
#define DEFAULT_BAUDRATE  115200
#define DEFAULT_FREQUENCY 916.0
#define DEFAULT_POWER     2

/* AT Protocol constants */
#define AT_BUFFER_SIZE    1024
#define AT_TIMEOUT_MS     8000
#define AT_MAX_RETRIES    5
#define AT_INTER_CMD_DELAY 200
#define AT_DATA_SEND_TIMEOUT 20000

static int loop_enabled = 0;
static int mail_drop_enabled = 0;
static const char *msg_errors[] = {
	"Invalid provided error pointer",
	"Invalid message buffer",
	"Invalid provided capcode",
	"Invalid provided flex buffer"
};

/* Serial configuration */
struct serial_config {
    double frequency;
    int baudrate;
    const char *device;  // Changed from char* to const char*
    int power;
};

/* Global for TTY restoration */
static struct termios orig_tty;
static int tty_saved = 0;
static int serial_fd = -1;

/* AT Protocol state */
typedef enum {
    AT_RESP_OK,
    AT_RESP_ERROR,
    AT_RESP_DATA,
    AT_RESP_TIMEOUT,
    AT_RESP_INVALID
} at_response_t;

/**
 * Safe string-to-int routine that takes into account:
 * - Overflow and Underflow
 * - No undefined behaviour
 */
static int str2int(int *out, char *s)
{
	char *end;
	if (s[0] == '\0')
		return (-1);
	errno = 0;

	long l = strtol(s, &end, 10);

	if (l > INT_MAX || (errno == ERANGE && l == LONG_MAX))
		return (-1);
	if (l < INT_MIN || (errno == ERANGE && l == LONG_MIN))
		return (-1);
	if (*end != '\0')
		return (-1);

	*out = l;
	return (0);
}

/**
 * @brief Safe string-to-uint64_t routine.
 */
static int str2uint64(uint64_t *out, const char *s)
{
    char *end;
    unsigned long ul;
    const char *p = s;

    if (p[0] == '\0')
        return -1;

    errno = 0;
    ul = strtoull(p, &end, 10);

    if (end == p || errno == ERANGE || *end != '\0' || ul > UINT64_MAX)
        return -1;

    *out = (uint64_t)ul;
    return 0;
}

/**
 * @brief Configures the serial port with the specified baudrate.
 */
static int configure_serial(int fd, int baudrate)
{
	struct termios tty;
	speed_t speed;

	if (tcgetattr(fd, &orig_tty) != 0) {
		perror("tcgetattr");
		return -1;
	}
	tty_saved = 1;
	serial_fd = fd;
	tty       = orig_tty;

	/* Convert baudrate to speed_t */
	switch (baudrate) {
	case 9600:   speed = B9600;   break;
	case 19200:  speed = B19200;  break;
	case 38400:  speed = B38400;  break;
	case 57600:  speed = B57600;  break;
	case 115200: speed = B115200; break;
	case 230400: speed = B230400; break;
	default:
		fprintf(stderr, "Unsupported baudrate: %d\n", baudrate);
		return -1;
	}

	cfsetospeed(&tty, speed);
	cfsetispeed(&tty, speed);
	cfmakeraw(&tty);

	tty.c_cc[VMIN]  = 0;
	tty.c_cc[VTIME] = 5;  // Reduced from 10 to 5 (500ms)
	tty.c_cflag &= ~CSTOPB;
	tty.c_cflag &= ~CRTSCTS;
	tty.c_cflag |= CLOCAL | CREAD;
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);

	if (tcsetattr(fd, TCSANOW, &tty) != 0) {
		perror("tcsetattr");
		return -1;
	}

	return 0;
}

/**
 * @brief Restores original TTY settings if they were saved.
 */
static void restore_tty(void)
{
	if (tty_saved && serial_fd >= 0) {
		tcsetattr(serial_fd, TCSANOW, &orig_tty);
		tty_saved = 0;
	}
}

/**
 * @brief Flush serial buffers completely.
 */
static void flush_serial_buffers(int fd)
{
    tcflush(fd, TCIOFLUSH);
    usleep(100000); // 100ms delay to ensure buffers are clear

    // Read any remaining data
    char dummy[256];
    int attempts = 0;
    while (attempts < 10) {
        ssize_t bytes = read(fd, dummy, sizeof(dummy));
        if (bytes <= 0) break;
        attempts++;
        usleep(10000); // 10ms between attempts
    }
}

/**
 * @brief Send AT command with proper flushing.
 */
static int at_send_command(int fd, const char *command)
{
    printf("Sending: %s", command);

    if (write(fd, command, strlen(command)) < 0) {
        perror("write");
        return -1;
    }
    tcdrain(fd);
    usleep(AT_INTER_CMD_DELAY * 1000); // Convert ms to us

    return 0;
}

/**
 * @brief Read AT response with improved parsing and timeout handling.
 */
static at_response_t at_read_response(int fd, char *buffer, size_t buffer_size,
                                     char *data_buffer, size_t data_buffer_size)
{
    char line_buffer[AT_BUFFER_SIZE];
    int line_pos = 0;
    int total_timeout = AT_TIMEOUT_MS;
    bool got_response = false;
    int empty_reads = 0;

    buffer[0] = '\0';
    if (data_buffer) data_buffer[0] = '\0';

    while (total_timeout > 0 && empty_reads < 20) {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;

        int poll_result = poll(&pfd, 1, 50); // Shorter poll interval

        if (poll_result < 0) {
            perror("poll");
            return AT_RESP_INVALID;
        }

        if (poll_result == 0) {
            total_timeout -= 50;
            empty_reads++;
            continue;
        }

        if (!(pfd.revents & POLLIN)) {
            total_timeout -= 50;
            continue;
        }

        char c;
        ssize_t bytes_read = read(fd, &c, 1);

        if (bytes_read < 0) {
            perror("read");
            return AT_RESP_INVALID;
        }

        if (bytes_read == 0) {
            total_timeout -= 50;
            empty_reads++;
            continue;
        }

        empty_reads = 0; // Reset empty read counter

        // Skip carriage returns
        if (c == '\r') {
            continue;
        }

        if (c == '\n') {
            // End of line
            line_buffer[line_pos] = '\0';

            // Skip empty lines
            if (line_pos == 0) {
                continue;
            }

            printf("Received: '%s'\n", line_buffer);

            // Check for responses
            if (strcmp(line_buffer, "OK") == 0) {
                return AT_RESP_OK;
            }
            else if (strcmp(line_buffer, "ERROR") == 0) {
                return AT_RESP_ERROR;
            }
            else if (strncmp(line_buffer, "+", 1) == 0) {
                // Data response
                if (strlen(line_buffer) < buffer_size) {
                    strcpy(buffer, line_buffer);
                    got_response = true;
                }
                // Continue reading to get OK/ERROR
            }
            else if (strstr(line_buffer, "DEBUG:") != NULL) {
                // Debug message from device
                printf("Device debug: %s\n", line_buffer);
            }
            else if (strstr(line_buffer, "AT READY") != NULL) {
                // Device ready message
                printf("Device ready message: %s\n", line_buffer);
            }

            // Reset line buffer
            line_pos = 0;
        }
        else if (line_pos < sizeof(line_buffer) - 1 && c >= 32 && c <= 126) {
            // Printable character
            line_buffer[line_pos++] = c;
        }
        else if (c < 32 && c != '\r' && c != '\n') {
            // Non-printable character (except CR/LF) - reset line
            if (line_pos > 0) {
                printf("Warning: Non-printable character 0x%02X in response, resetting line\n", (unsigned char)c);
                line_pos = 0;
            }
        }

        // Reset timeout on successful character read
        total_timeout = AT_TIMEOUT_MS;
    }

    if (got_response) {
        return AT_RESP_DATA;
    }

    return AT_RESP_TIMEOUT;
}

/**
 * @brief Send AT command and wait for response with enhanced retry logic.
 */
static int at_execute_command(int fd, const char *command, char *response,
                             size_t response_size)
{
    int retries = AT_MAX_RETRIES;

    while (retries-- > 0) {
        // Clear buffers before sending command
        flush_serial_buffers(fd);

        if (at_send_command(fd, command) < 0) {
            return -1;
        }

        at_response_t result = at_read_response(fd, response, response_size, NULL, 0);

        switch (result) {
        case AT_RESP_OK:
            return 0;
        case AT_RESP_ERROR:
            fprintf(stderr, "AT command failed: %s", command);
            if (retries > 0) {
                printf("Retrying command (%d attempts left)...\n", retries);
                usleep(500000); // 500ms delay between retries
                continue;
            }
            return -1;
        case AT_RESP_TIMEOUT:
            fprintf(stderr, "AT command timeout: %s", command);
            if (retries > 0) {
                printf("Retrying command due to timeout (%d attempts left)...\n", retries);
                // Send AT command to reset device state
                flush_serial_buffers(fd);
                at_send_command(fd, "AT\r\n");
                usleep(200000);
                at_read_response(fd, response, response_size, NULL, 0);
                usleep(500000);
                continue;
            }
            return -1;
        case AT_RESP_INVALID:
            fprintf(stderr, "AT communication error: %s", command);
            if (retries > 0) {
                printf("Retrying command due to communication error (%d attempts left)...\n", retries);
                usleep(1000000); // 1 second delay for communication errors
                continue;
            }
            return -1;
        default:
            return -1;
        }
    }

    return -1;
}

/**
 * @brief Initialize device with AT commands and enhanced error recovery.
 */
static int at_initialize_device(int fd)
{
    char response[AT_BUFFER_SIZE];

    printf("Testing device communication...\n");

    // Give device time to boot up
    flush_serial_buffers(fd);
    usleep(1000000); // 1 second

    // Try to establish communication
    for (int i = 0; i < 10; i++) {
        printf("Communication attempt %d/10...\n", i + 1);

        // Clear buffers thoroughly
        flush_serial_buffers(fd);
        usleep(200000); // 200ms

        // Send basic AT command
        if (at_execute_command(fd, "AT\r\n", response, sizeof(response)) == 0) {
            printf("Device communication established\n");

            // Send one more AT command to ensure stability
            usleep(200000);
            if (at_execute_command(fd, "AT\r\n", response, sizeof(response)) == 0) {
                printf("Device communication confirmed stable\n");
                return 0;
            }
        }

        // Progressive delay between attempts
        usleep(500000 * (i + 1)); // 500ms, 1s, 1.5s, etc.
    }

    fprintf(stderr, "Failed to establish communication after 10 attempts\n");
    return -1;
}

/**
 * @brief Send flex message using AT commands with enhanced retry logic.
 */
static int at_send_flex_message(int fd, struct serial_config *config,
                               const uint8_t *data, size_t size)
{
    char command[128];
    char response[AT_BUFFER_SIZE];
    int send_retries = 3; // Retry sending the entire message up to 3 times

    printf("\nConfiguring radio parameters...\n");

    // Set frequency with retries
    snprintf(command, sizeof(command), "AT+FREQ=%.4f\r\n", config->frequency);
    if (at_execute_command(fd, command, response, sizeof(response)) < 0) {
        fprintf(stderr, "Failed to set frequency after all retries\n");
        return -1;
    }

    // Set power with retries
    snprintf(command, sizeof(command), "AT+POWER=%d\r\n", config->power);
    if (at_execute_command(fd, command, response, sizeof(response)) < 0) {
        fprintf(stderr, "Failed to set power after all retries\n");
        return -1;
    }

    printf("Radio configured successfully.\n");

    // Try to send the message with retries
    while (send_retries-- > 0) {
        printf("\nAttempting to send data (attempt %d/3)...\n", 3 - send_retries);

        // Reset device state before attempting to send
        printf("Resetting device state...\n");
        flush_serial_buffers(fd);
        if (at_execute_command(fd, "AT\r\n", response, sizeof(response)) < 0) {
            printf("Failed to reset device state, continuing anyway...\n");
        }

        // Send the SEND command
        snprintf(command, sizeof(command), "AT+SEND=%zu\r\n", size);
        printf("Sending command: %s", command);

        // Clear buffers before sending
        flush_serial_buffers(fd);

        if (write(fd, command, strlen(command)) < 0) {
            perror("write");
            if (send_retries > 0) {
                printf("Write failed, retrying...\n");
                usleep(1000000);
                continue;
            }
            return -1;
        }
        tcdrain(fd);

        // Wait for device to be ready for data
        printf("Waiting for device to be ready for data...\n");
        at_response_t result = at_read_response(fd, response, sizeof(response), NULL, 0);

        if (result != AT_RESP_DATA || strstr(response, "+SEND: READY") == NULL) {
            fprintf(stderr, "Device not ready for data. Got response type %d: '%s'\n", result, response);
            if (send_retries > 0) {
                printf("Device not ready, retrying entire send operation...\n");
                usleep(2000000); // 2 second delay before retry
                continue;
            }
            return -1;
        }

        printf("Device ready! Sending %zu bytes of binary data...\n", size);

        // Send binary data in smaller chunks with progress tracking
        size_t bytes_sent = 0;
        const size_t CHUNK_SIZE = 32; // Smaller chunks for more reliable transmission
        bool send_success = true;
        unsigned long send_start_time = time(NULL);

        while (bytes_sent < size && send_success) {
            size_t chunk_size = (size - bytes_sent > CHUNK_SIZE) ? CHUNK_SIZE : (size - bytes_sent);

            ssize_t written = write(fd, data + bytes_sent, chunk_size);
            if (written < 0) {
                perror("write binary data");
                send_success = false;
                break;
            }

            bytes_sent += written;
            printf("Sent %zu/%zu bytes (%.1f%%)\r", bytes_sent, size, (float)bytes_sent * 100.0 / size);
            fflush(stdout);

            // Check for timeout
            if ((time(NULL) - send_start_time) > (AT_DATA_SEND_TIMEOUT / 1000)) {
                printf("\nBinary data send timeout\n");
                send_success = false;
                break;
            }

            // Small delay between chunks to avoid overwhelming the device
            usleep(5000); // 5ms
        }

        if (!send_success) {
            if (send_retries > 0) {
                printf("\nBinary data send failed, retrying entire operation...\n");
                usleep(2000000);
                continue;
            }
            return -1;
        }

        printf("\nBinary data sent successfully. Waiting for transmission completion...\n");

        // Ensure all data is transmitted
        tcdrain(fd);

				sleep(5);

        // Wait for final response
        result = at_read_response(fd, response, sizeof(response), NULL, 0);
        if (result != AT_RESP_OK) {
            fprintf(stderr, "Transmission failed. Response type %d: '%s'\n", result, response);
            if (send_retries > 0) {
                printf("Transmission failed, retrying entire operation...\n");
                usleep(2000000);
                continue;
            }
            return -1;
        }

        printf("Transmission completed successfully!\n");
        return 0;
    }

    fprintf(stderr, "Failed to send message after all retry attempts\n");
    return -1;
}

/**
 * @brief Prints the program usage information to stderr and exits.
 */
static void usage(const char *prgname)
{
	fprintf(stderr,
		"%s [options] <capcode> <message>\n"
		"or:\n"
		"%s [options] [-l] [-m] - (from stdin)\n\n"

		"Options:\n"
		"   -d <device>    Serial device (default: %s)\n"
		"   -b <baudrate>  Baudrate (default: %d)\n"
		"   -f <frequency> Frequency in MHz (default: %f)\n"
		"   -p <power>     TX power (default: %d, 2-20)\n"
		"   -l             Loop mode: stays open receiving new lines until EOF\n"
		"   -m             Mail Drop: sets the Mail Drop Flag in the FLEX message\n\n"

		"Stdin mode:\n"
		"   Example:\n"
		"     printf '1234567:MY MESSAGE'               | %s\n"
		"     printf '1234567:MY MSG1\\n1122334:MY MSG2' | %s -l\n"
		"     printf '1234567:MY MESSAGE'               | %s -m\n"
		"     printf '1234567:MY MESSAGE'               | %s -l -m\n\n"

		"Normal mode:\n"
		"   %s 1234567 'MY MESSAGE'\n"
		"   %s -m 1234567 'MY MESSAGE'\n"
		"   %s -d /dev/ttyUSB0 -f 915.5 1234567 'MY MESSAGE'\n",
		prgname, prgname, DEFAULT_DEVICE, DEFAULT_BAUDRATE,
		DEFAULT_FREQUENCY, DEFAULT_POWER, prgname, prgname,
		prgname, prgname, prgname, prgname, prgname);
	exit(1);
}

/**
 * @brief Parses command line options and arguments.
 */
static void read_params(uint64_t *capcode, char *msg, int argc, char **argv,
	struct serial_config *config, int *is_stdin)
{
	int non_opt_start;
	size_t msg_size;
	int opt;

	/* Initialize defaults */
	config->device = strdup(DEFAULT_DEVICE);
	config->baudrate  = DEFAULT_BAUDRATE;
	config->frequency = DEFAULT_FREQUENCY;
	config->power     = DEFAULT_POWER;

	/* Parse options */
	while ((opt = getopt(argc, argv, "d:b:f:p:lm")) != -1) {
		switch (opt) {
		case 'd':
			config->device = optarg;
			break;
		case 'b':
			if (str2int(&config->baudrate, optarg) < 0) {
				fprintf(stderr, "Invalid baudrate: %s\n", optarg);
				usage(argv[0]);
			}
			break;
		case 'f':
			config->frequency = atof(optarg);
			if (config->frequency <= 0) {
				fprintf(stderr, "Invalid frequency: %s\n", optarg);
				usage(argv[0]);
			}
			break;
		case 'p':
			if (str2int(&config->power, optarg) < 0 ||
				config->power < 2 || config->power > 20)
			{
				fprintf(stderr, "Invalid power: %s\n", optarg);
				usage(argv[0]);
			}
			break;
		case 'l':
			loop_enabled = 1;
			break;
		case 'm':
			mail_drop_enabled = 1;
			break;
		default:
			usage(argv[0]);
		}
	}

	non_opt_start = optind;

	/* Check remaining arguments */
	if (argc - non_opt_start == 2) {
		/* Normal mode: capcode and message */
		if (str2uint64(capcode, argv[non_opt_start]) < 0) {
			fprintf(stderr, "Invalid capcode: %s\n",
				argv[non_opt_start]);
			usage(argv[0]);
		}

		if ((msg_size = strlen(argv[non_opt_start + 1])) >= MAX_CHARS_ALPHA) {
			fprintf(stderr, "Message too long (max %d characters).\n",
				MAX_CHARS_ALPHA - 1);
			usage(argv[0]);
		}
		memcpy(msg, argv[non_opt_start + 1], msg_size + 1);
		*is_stdin = 0;
	}
	else if (argc - non_opt_start == 1 && strcmp(argv[non_opt_start], "-") == 0) {
		/* Stdin mode: requires "-" argument */
		*is_stdin = 1;
	}
	else {
		/* No arguments or invalid arguments */
		usage(argv[0]);
	}
}

/**
 * @brief Reads a line from stdin, parses capcode and message.
 */
static int read_stdin_message(uint64_t *capcode_ptr, char *message_buf,
	char **line_ptr, size_t *len_ptr)
{
	char *current_message;
	ssize_t read_len;
	char *colon_pos;
	size_t msg_len;

	read_len = getline(line_ptr, len_ptr, stdin);
	if (read_len == -1)
		return 1; /* EOF or error */

	if (read_len > 0 && (*line_ptr)[read_len - 1] == '\n') {
		(*line_ptr)[read_len - 1] = '\0';
		read_len--;
	}

	colon_pos = strchr(*line_ptr, ':');
	if (colon_pos == NULL) {
		fprintf(stderr,
			"Invalid input: '%s', expected 'capcode:message'\n",
			*line_ptr);
		return 2;
	}
	*colon_pos = '\0';

	if (str2uint64(capcode_ptr, *line_ptr) < 0)  {
		fprintf(stderr, "Invalid capcode in input: '%s'\n", *line_ptr);
		return 2;
	}

	current_message = colon_pos + 1;
	msg_len         = read_len - (current_message - *line_ptr);

	if (msg_len >= MAX_CHARS_ALPHA) {
		fprintf(stderr,
			"Message too long in input: '%s' (max %d chars).\n",
			current_message, MAX_CHARS_ALPHA - 1);
		return 2;
	}
	memcpy(message_buf, current_message, msg_len + 1);
	return 0;
}

/**
 * @brief Signal handler to restore TTY on interrupt.
 */
static void signal_handler(int sig)
{
	(void)sig; /* Unused */
	restore_tty();
	exit(1);
}

/* Main function */
int main(int argc, char **argv)
{
	struct tf_message_config msg_config = {0};
	uint8_t vec[FLEX_BUFFER_SIZE] = {0};
	char message[MAX_CHARS_ALPHA] = {0};
	struct serial_config config;
	uint64_t capcode;
	size_t read_size;
	int is_stdin;
	char *line;
	int status;
	size_t len;
	int err;
	int ret;
	int fd;

	line = NULL;
	len  = 0;
	ret  = 1;
	fd   = -1;

	/* Restore TTY at the end */
	atexit(restore_tty);
	signal(SIGINT,  signal_handler);
	signal(SIGTERM, signal_handler);

	read_params(&capcode, message, argc, argv, &config, &is_stdin);

	/* Open serial device */
	fd = open(config.device, O_RDWR | O_NOCTTY | O_SYNC);
	if (fd < 0) {
		fprintf(stderr, "Unable to open serial device '%s': %s\n",
			config.device, strerror(errno));
		goto error;
	}

	/* Configure serial port */
	if (configure_serial(fd, config.baudrate) < 0) {
		fprintf(stderr, "Failed to configure serial port\n");
		goto error;
	}

	/* Give device time to settle and initialize communication */
	usleep(1000000); // 1 second

	if (at_initialize_device(fd) < 0) {
		fprintf(stderr, "Failed to initialize device\n");
		goto error;
	}

	/* Normal mode */
	if (!is_stdin) {
		msg_config.mail_drop = mail_drop_enabled;
		read_size = tf_encode_flex_message_ex(message, capcode, vec,
			sizeof vec, &err, &msg_config);

		if (err >= 0) {
			if (at_send_flex_message(fd, &config, vec, read_size) < 0)
				goto error;
			printf("Successfully sent flex message\n");
		}
		else {
			fprintf(stderr, "Error encoding message: %s\n",
				msg_errors[-err]);
			goto error;
		}
		goto exit;
	}

	/* Stdin mode */
	do {
		status = read_stdin_message(&capcode, message, &line, &len);
		if (status == 1) /* EOF or read error */
			break;

		if (status == 2) { /* Parsing error */
			if (!loop_enabled)
				goto error;
			continue;
		}

		msg_config.mail_drop = mail_drop_enabled;
		read_size = tf_encode_flex_message_ex(message, capcode, vec,
			sizeof vec, &err, &msg_config);

		if (err >= 0) {
			if (at_send_flex_message(fd, &config, vec, read_size) < 0) {
				if (!loop_enabled)
					goto error;

				/* In loop mode, continue on error */
				fprintf(stderr, "Failed to send message, continuing...\n");
			} else {
				printf("Sent %zu bytes for capcode %" PRId64"\n",
					read_size, capcode);
			}
		}
		else {
			fprintf(stderr, "Error encoding message: %s\n", msg_errors[-err]);
			if (!loop_enabled)
				goto error;
		}
	} while (loop_enabled); /* Continue loop if enabled */

exit:
	ret = 0;
error:
	if (fd >= 0)
		close(fd);
	free(line);
	return ret;
}
