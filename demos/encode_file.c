/*
 * tinyflex: A minimal, dependency-free, single-header library, FLEX encoder.
 * Written by Davidson Francis (aka Theldus) - 2025.
 *
 * This is free and unencumbered software released into the public domain.
 */

#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>
#include <limits.h>
#include <getopt.h>

#include "tinyflex.h"

static int loop_enabled = 0;
static int mail_drop_enabled = 0;
static int numeric_mode = 0;
static int tone_only_mode = 0;
static const char *msg_errors[] = {
	"Invalid provided error pointer",
	"Invalid message buffer",
	"Invalid provided capcode",
	"Invalid provided flex buffer"
};

/*
 * @brief Safe string-to-uint64_t routine.
 * Handles overflow, invalid characters, and rejects negative input.
 *
 * @param out Pointer to uint64_t.
 * @param s String to be converted.
 *
 * @return Returns 0 on success, -1 on error.
 */
static int str2uint64(uint64_t *out, const char *s)
{
    char *end;
    unsigned long ul;
    const char *p = s;

    /* Check for empty. */
    if (p[0] == '\0')
        return -1;

    errno = 0;
    ul = strtoull(p, &end, 10);

    /* Check if:
     * - No digits were found
     * - Overflow
     * - Extra chars at the end
     * - If fits into uint64_t
     */
    if (end == p || errno == ERANGE || *end != '\0' || ul > UINT64_MAX)
        return -1;

    *out = (uint64_t)ul;
    return 0;
}

/**
 * @brief Prints the program usage information to stderr and exits.
 *
 * @param prgname The name of the executable program (argv[0]).
 */
static void usage(const char *prgname)
{
	fprintf(stderr,
		"%s <capcode> <message> <output_file>\n"
		"or:\n"
		"%s [-l] [-m] [-n] [-t] (from stdin/stdout)\n\n"
		
		"Options:\n"
		"   -l Loop mode: stays open receiving new lines of "
		"messages until EOF\n"
		"   -m Mail Drop: sets the Mail Drop Flag in the FLEX "
		"message\n"
		"   -n Numeric mode: encode as numeric message (0-9 -_[]\\sU)\n"
		"   -t Tone-only mode: send tone-only message (capcode only)\n\n"
		
		"Stdin/stdout mode:\n"
		"   Example:\n"
		"     printf '1234567:MY MESSAGE'               | %s "
		"(no loop mode)\n"
		"     printf '1234567:MY MSG1\\n1122334:MY MSG2' | %s -l "
		"(loop mode)\n"
		"     printf '1234567:MY MESSAGE'               | %s -m "
		"(mail drop)\n"
		"     printf '1234567:12345'                    | %s -n "
		"(numeric)\n"
		"     printf '1234567'                          | %s -t "
		"(tone-only)\n"
		"     printf '1234567\\n1122334'                | %s -t -l "
		"(tone-only loop)\n"
		"     printf '1234567:MY MESSAGE'               | %s -l -m "
		"(both)\n"
		"   (binary output goes to stdout!)\n\n"

		"   Note: On loop mode, each output is preceded by a line "
		"indicating\n"
		"         how many bytes follows, ex:\n"
		"   795\\n"
		"   <binary output>\n"
		"   600\\n"
		"   <binary output>\n\n"

		"Normal mode:\n"
		"   %s 1234567 'MY MESSAGE' output.bin\n"
		"   %s -m 1234567 'MY MESSAGE' output.bin (with mail drop)\n"
		"   %s -n 1234567 '12345' output.bin (numeric message)\n"
		"   %s -t 1234567 output.bin (tone-only message)\n",
		prgname, prgname, prgname, prgname, prgname, prgname,
		prgname, prgname, prgname, prgname, prgname, prgname,
		prgname);
	exit(1);
}

/**
 * @brief Parses cmd arguments for normal mode or sets up stdin/stdout mode.
 *
 * @param capcode  Capcode destination pointer.
 * @param msg      Pointer storing message to be sent.
 * @param argc     duh
 * @param argv     duh
 * @param out_file Output file name (normal-mode) or NULL if stdin mode.    
 */
static void read_params(uint64_t *capcode, char *msg, int argc, char **argv,
	char **out_file)
{
	size_t msg_size;
	int opt;
	int non_opt_start;

	/* Parse options using getopt */
	while ((opt = getopt(argc, argv, "lmnt")) != -1) {
		switch (opt) {
		case 'l':
			loop_enabled = 1;
			break;
		case 'm':
			mail_drop_enabled = 1;
			break;
		case 'n':
			numeric_mode = 1;
			break;
		case 't':
			tone_only_mode = 1;
			break;
		default:
			usage(argv[0]);
		}
	}

	non_opt_start = optind;

	/* Check remaining arguments */
	if (argc - non_opt_start == 3) {
		/* Normal mode: capcode, message, output file */
		if (tone_only_mode) {
			fprintf(stderr, "Tone-only mode (-t) does not accept message argument\n");
			usage(argv[0]);
		}

		if (str2uint64(capcode, argv[non_opt_start]) < 0) {
			fprintf(stderr, "Invalid capcode: %s\n", 
				argv[non_opt_start]);
			usage(argv[0]);
		}

		msg_size = strlen(argv[non_opt_start + 1]);
		if (numeric_mode) {
			if (msg_size >= MAX_CHARS_NUMERIC) {
				fprintf(stderr, "Numeric message too long (max %d characters).\n",
					MAX_CHARS_NUMERIC - 1);
				usage(argv[0]);
			}
		} else {
			if (msg_size >= MAX_CHARS_ALPHA) {
				fprintf(stderr,
					"Message too long (max %d characters).\n",
					MAX_CHARS_ALPHA - 1);
				usage(argv[0]);
			}
		}
		memcpy(msg, argv[non_opt_start + 1], msg_size + 1);

		*out_file = argv[non_opt_start + 2];
		return;
	}
	
	/* Tone-only normal mode: capcode and output file */
	if (argc - non_opt_start == 2 && tone_only_mode) {
		if (str2uint64(capcode, argv[non_opt_start]) < 0) {
			fprintf(stderr, "Invalid capcode: %s\n", 
				argv[non_opt_start]);
			usage(argv[0]);
		}
		*out_file = argv[non_opt_start + 1];
		return;
	}

	/* Stdin/stdout mode: no non-option arguments */
	if (argc - non_opt_start == 0) {
		*out_file = NULL; /* Indicate output to stdout */
		return;
	}

	/* Invalid number of arguments, show usage */
	usage(argv[0]);
}

/**
 * @brief Reads a line from stdin, parses capcode and message.
 *
 * @param capcode_ptr Capcode destination pointer.
 * @param message_buf Destination parsed message buffer.
 * @param line_ptr    Line read pointer.
 * @param len_ptr     Line read length.
 * @return Returns 0 if success, 1 if EOF and 2 if parsing error.
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
		if (tone_only_mode) {
			/* In tone-only mode, no colon is expected - just capcode */
			if (str2uint64(capcode_ptr, *line_ptr) < 0)  {
				fprintf(stderr, "Invalid capcode in input: '%s'\n", *line_ptr);
				return 2;
			}
			message_buf[0] = '\0'; /* Empty message for tone-only */
			return 0;
		} else {
			fprintf(stderr,
				"Invalid input: '%s', expected 'capcode:message'\n",
				*line_ptr);
			return 2;
		}
	}

	if (tone_only_mode) {
		fprintf(stderr,
			"Tone-only mode (-t) does not accept message in input: '%s'\n",
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

	if (numeric_mode) {
		if (msg_len >= MAX_CHARS_NUMERIC) {
			fprintf(stderr,
				"Numeric message too long in input: '%s' (max %d chars).\n",
				current_message, MAX_CHARS_NUMERIC - 1);
			return 2;
		}
	} else {
		if (msg_len >= MAX_CHARS_ALPHA) {
			fprintf(stderr,
				"Message too long in input: '%s' (max %d chars).\n",
				current_message, MAX_CHARS_ALPHA - 1);
			return 2;
		}
	}
	memcpy(message_buf, current_message, msg_len + 1);
	return 0;
}

/* Main =). */
int main(int argc, char **argv)
{
	struct tf_message_config config = {0};
	uint8_t vec[FLEX_BUFFER_SIZE] = {0};
	char message[MAX_CHARS_ALPHA] = {0};
	uint64_t capcode;
	size_t read_size;
	char *out_file;
	char *line;
	int status;
	size_t len;
	int err;
	int ret;
	int fd;

	out_file = NULL;
	line     = NULL;
	len      = 0;
	ret      = 1;
	fd       = -1;

	read_params(&capcode, message, argc, argv, &out_file);

	/* Normal mode. */
	if (out_file != NULL) {
		fd = creat(out_file, S_IRUSR|S_IWUSR);
		if (fd < 0) {
			fprintf(stderr, "Unable to open output file '%s'!\n",
				out_file);
			goto error;
		}

		if (tone_only_mode) {
			read_size = tf_encode_flex_tone_only_msg(capcode, vec,
				sizeof vec, &err);
		} else {
			config.mail_drop = mail_drop_enabled;
			if (numeric_mode) {
				read_size = tf_encode_flex_numeric_message(message, capcode, vec,
					sizeof vec, &err);
			} else {
				read_size = tf_encode_flex_message_ex(message, capcode, vec,
					sizeof vec, &err, &config);
			}
		}
		
		if (err >= 0)
			write(fd, vec, read_size);
		else
			fprintf(stderr, "Error encoding message: %s\n",
				msg_errors[-err]);

		goto exit;
	}

	/* Stdin/stdout mode. */
	do {
		status = read_stdin_message(&capcode, message, &line, &len);
		if (status == 1) /* EOF or read error */
			break;

		if (status == 2) { /* Parsing error */
			if (!loop_enabled)
				goto error;
			continue;
		}

		if (tone_only_mode) {
			read_size = tf_encode_flex_tone_only_msg(capcode, vec,
				sizeof vec, &err);
		} else {
			config.mail_drop = mail_drop_enabled;
			if (numeric_mode) {
				read_size = tf_encode_flex_numeric_message(message, capcode, vec,
					sizeof vec, &err);
			} else {
				read_size = tf_encode_flex_message_ex(message, capcode, vec,
					sizeof vec, &err, &config);
			}
		}
		
		if (err >= 0) {
			if (loop_enabled)
				printf("%zu\n", read_size);
			write(STDOUT_FILENO, vec, read_size);
		} 
		else {
			fprintf(stderr, "Error encoding message: %s\n",
				msg_errors[-err]);
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
