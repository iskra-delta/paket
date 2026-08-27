/*
   Implements PAKET command-line helpers. It extracts serial options, matches
   wildcards without allocation, and maps Partner destination notation onto
   the CP/M 3 libc's drive/name/user syntax.

   GPL 3.0 License (see: LICENSE)
   Copyright (C) 2026 Tomaz Stih
*/

#include "paket/command.h"

#include <string.h>

#define search_anchor_max 10U

static char ascii_upper(char character)
{
    if ((character >= 'a') && (character <= 'z')) {
        return (char)(character - 'a' + 'A');
    }
    return character;
}

static int ascii_alpha(char character)
{
    character = ascii_upper(character);
    return (character >= 'A') && (character <= 'Z');
}

static int ascii_digit(char character)
{
    return (character >= '0') && (character <= '9');
}

static int path_separator(char character)
{
    return (character == '/') || (character == '\\');
}

static int file_character(char character)
{
    return ascii_alpha(character) || ascii_digit(character) ||
        (character == '_') || (character == '-') || (character == '$');
}

static int parse_communication(
    const char *text,
    paket_communication *communication
)
{
    char parity;

    if ((text == NULL) || (communication == NULL) ||
        (strlen(text) != 8U)) {
        return -1;
    }
    if ((text[4] != '-') || (text[6] != '-') ||
        ((text[7] != '1') && (text[7] != '2')) ||
        (text[8] != '\0')) {
        return -1;
    }
    if ((text[0] == '2') && (text[1] == '4') &&
        (text[2] == '0') && (text[3] == '0')) {
        communication->baud_rate = 2400U;
    } else if ((text[0] == '4') && (text[1] == '8') &&
               (text[2] == '0') && (text[3] == '0')) {
        communication->baud_rate = 4800U;
    } else if ((text[0] == '9') && (text[1] == '6') &&
               (text[2] == '0') && (text[3] == '0')) {
        communication->baud_rate = 9600U;
    } else {
        return -1;
    }
    parity = ascii_upper(text[5]);
    switch (parity) {
    case 'N':
        communication->parity = PAKET_PARITY_NONE;
        break;
    case 'O':
        communication->parity = PAKET_PARITY_ODD;
        break;
    case 'E':
        communication->parity = PAKET_PARITY_EVEN;
        break;
    default:
        return -1;
    }
    communication->stop_bits = (unsigned int)(text[7] - '0');
    return 0;
}

static int parse_payload_bytes(const char *text, unsigned int *payload_bytes)
{
    unsigned int value = 0U;

    if ((text == NULL) || (payload_bytes == NULL) || (*text == '\0')) {
        return -1;
    }
    while (*text != '\0') {
        if (!ascii_digit(*text)) {
            return -1;
        }
        value = value * 10U + (unsigned int)(*text - '0');
        if (value > PAKET_PAYLOAD_MAX) {
            return -1;
        }
        ++text;
    }
    if (value < PAKET_PAYLOAD_MIN) {
        return -1;
    }
    *payload_bytes = value;
    return 0;
}

int paket_parse_options(
    int *argc,
    char **argv,
    paket_options *options
)
{
    int read_index = 1;
    int write_index = 1;
    int found_port = 0;
    int found_communication = 0;
    int found_payload = 0;

    if ((argc == NULL) || (*argc < 1) || (argv == NULL) ||
        (options == NULL)) {
        return -1;
    }
    options->serial_port = PAKET_SERIAL_PORT_DEFAULT;
    options->payload_bytes = PAKET_PAYLOAD_DEFAULT;
    options->has_communication = 0;
    while (read_index < *argc) {
        if (argv[read_index] == NULL) {
            return -1;
        }
        if ((argv[read_index][0] == '-') &&
            (ascii_upper(argv[read_index][1]) == 'P') &&
            (argv[read_index][2] == '\0')) {
            const char *value = NULL;

            if (found_port || (read_index + 1 >= *argc)) {
                return -1;
            }
            value = argv[read_index + 1];
            if ((value == NULL) || (value[1] != '\0') ||
                (value[0] < '2') || (value[0] > '4')) {
                return -1;
            }
            options->serial_port = (unsigned int)(value[0] - '0');
            found_port = 1;
            read_index += 2;
        } else if ((argv[read_index][0] == '-') &&
                   (ascii_upper(argv[read_index][1]) == 'C') &&
                   (argv[read_index][2] == '\0')) {
            const char *value = NULL;

            if (found_communication || (read_index + 1 >= *argc)) {
                return -1;
            }
            value = argv[read_index + 1];
            if (parse_communication(value, &options->communication) != 0) {
                return -1;
            }
            options->has_communication = 1;
            found_communication = 1;
            read_index += 2;
        } else if ((argv[read_index][0] == '-') &&
                   (ascii_upper(argv[read_index][1]) == 'M') &&
                   (argv[read_index][2] == '\0')) {
            const char *value = NULL;

            if (found_payload || (read_index + 1 >= *argc)) {
                return -1;
            }
            value = argv[read_index + 1];
            if (parse_payload_bytes(value, &options->payload_bytes) != 0) {
                return -1;
            }
            found_payload = 1;
            read_index += 2;
        } else {
            argv[write_index++] = argv[read_index++];
        }
    }
    argv[write_index] = NULL;
    *argc = write_index;
    return 0;
}

int paket_has_wildcards(const char *pattern)
{
    if (pattern == NULL) {
        return 0;
    }
    while (*pattern != '\0') {
        if ((*pattern == '*') || (*pattern == '?')) {
            return 1;
        }
        ++pattern;
    }
    return 0;
}

int paket_wildcard_match(const char *pattern, const char *text)
{
    const char *star = NULL;
    const char *retry = NULL;

    if ((pattern == NULL) || (text == NULL)) {
        return 0;
    }

    while (*text != '\0') {
        if ((*pattern == '?') ||
            (ascii_upper(*pattern) == ascii_upper(*text))) {
            ++pattern;
            ++text;
        } else if (*pattern == '*') {
            star = pattern++;
            retry = text;
        } else if (star != NULL) {
            pattern = star + 1;
            text = ++retry;
        } else {
            return 0;
        }
    }

    while (*pattern == '*') {
        ++pattern;
    }
    return *pattern == '\0';
}

size_t paket_search_anchor(
    const char *pattern,
    char *output,
    size_t output_size
)
{
    const char *cursor = pattern;
    const char *best = NULL;
    size_t best_size = 0U;
    size_t copy_size = 0U;

    if ((pattern == NULL) || (output == NULL) || (output_size == 0U)) {
        return 0U;
    }

    while (*cursor != '\0') {
        const char *start = cursor;
        size_t run_size = 0U;

        while ((*cursor != '\0') && (*cursor != '*') && (*cursor != '?')) {
            ++cursor;
            ++run_size;
        }
        if (run_size > best_size) {
            best = start;
            best_size = run_size;
        }
        if (*cursor != '\0') {
            ++cursor;
        }
    }

    copy_size = best_size;
    if (copy_size > search_anchor_max) {
        copy_size = search_anchor_max;
    }
    if (copy_size >= output_size) {
        copy_size = output_size - 1U;
    }
    if ((best != NULL) && (copy_size > 0U)) {
        memcpy(output, best, copy_size);
    }
    output[copy_size] = '\0';
    return copy_size;
}

static int copy_explicit_name(
    const char *name,
    char *output,
    size_t output_size
)
{
    size_t base_size = 0U;
    size_t extension_size = 0U;
    size_t position = 0U;

    if ((name == NULL) || (*name == '\0') ||
        (output == NULL) || (output_size < 2U)) {
        return -1;
    }

    while ((*name != '\0') && (*name != '.')) {
        if (path_separator(*name) || !file_character(*name) ||
            (base_size >= 8U) || (position + 1U >= output_size)) {
            return -1;
        }
        output[position++] = ascii_upper(*name++);
        ++base_size;
    }
    if (base_size == 0U) {
        return -1;
    }

    if (*name == '.') {
        if (position + 1U >= output_size) {
            return -1;
        }
        output[position++] = *name++;
        while (*name != '\0') {
            if ((*name == '.') || path_separator(*name) ||
                !file_character(*name) || (extension_size >= 3U) ||
                (position + 1U >= output_size)) {
                return -1;
            }
            output[position++] = ascii_upper(*name++);
            ++extension_size;
        }
        if (extension_size == 0U) {
            return -1;
        }
    }

    output[position] = '\0';
    return 0;
}

static int make_suggested_name(
    const char *package_name,
    const char *format,
    unsigned int file_count,
    char *output,
    size_t output_size
)
{
    const char *extension = format;
    size_t position = 0U;
    size_t extension_size = 0U;

    if ((package_name == NULL) || (output == NULL) || (output_size < 6U)) {
        return -1;
    }

    while ((*package_name != '\0') && (position < 8U)) {
        if (ascii_alpha(*package_name) || ascii_digit(*package_name)) {
            output[position++] = ascii_upper(*package_name);
        }
        ++package_name;
    }
    if (position == 0U) {
        memcpy(output, "PACKAGE", 7U);
        position = 7U;
    }

    output[position++] = '.';
    if (file_count > 1U) {
        extension = "ZIP";
    }
    if (extension != NULL) {
        while ((*extension != '\0') && (extension_size < 3U)) {
            if (ascii_alpha(*extension) || ascii_digit(*extension)) {
                output[position++] = ascii_upper(*extension);
                ++extension_size;
            }
            ++extension;
        }
    }
    if (extension_size == 0U) {
        memcpy(output + position, "PAK", 3U);
        position += 3U;
    }
    if (position >= output_size) {
        return -1;
    }
    output[position] = '\0';
    return 0;
}

int paket_make_target_path(
    const char *destination,
    const char *package_name,
    const char *format,
    unsigned int file_count,
    char *output,
    size_t output_size
)
{
    const char *cursor = destination;
    const char *file_name = NULL;
    char native_name[13];
    unsigned int user = 0U;
    size_t position = 0U;
    int directory = 0;

    if ((destination == NULL) || (*destination == '\0') ||
        (output == NULL) || (output_size == 0U)) {
        return -1;
    }

    if (ascii_alpha(cursor[0]) && (cursor[1] == ':')) {
        if (position + 2U >= output_size) {
            return -1;
        }
        output[position++] = ascii_upper(cursor[0]);
        output[position++] = ':';
        cursor += 2;
    }

    if (path_separator(*cursor)) {
        ++cursor;
        if (*cursor == '\0') {
            directory = 1;
        } else if (ascii_digit(*cursor)) {
            user = 0U;
            while (ascii_digit(*cursor)) {
                user = user * 10U + (unsigned int)(*cursor - '0');
                if (user > 15U) {
                    return -1;
                }
                ++cursor;
            }
            if (*cursor == '\0') {
                directory = 1;
            } else if (path_separator(*cursor)) {
                ++cursor;
                if (*cursor == '\0') {
                    directory = 1;
                } else {
                    file_name = cursor;
                }
            } else {
                return -1;
            }
        } else {
            file_name = cursor;
        }
    } else if (*cursor == '\0') {
        directory = 1;
    } else {
        file_name = cursor;
    }

    if (directory) {
        if (make_suggested_name(
            package_name,
            format,
            file_count,
            native_name,
            sizeof(native_name)
        ) != 0) {
            return -1;
        }
    } else if (copy_explicit_name(
        file_name,
        native_name,
        sizeof(native_name)
    ) != 0) {
        return -1;
    }

    if (position + strlen(native_name) + 4U +
        (user >= 10U ? 1U : 0U) > output_size) {
        return -1;
    }
    memcpy(output + position, native_name, strlen(native_name));
    position += strlen(native_name);
    output[position++] = '[';
    if (user >= 10U) {
        output[position++] = (char)('0' + (user / 10U));
    }
    output[position++] = (char)('0' + (user % 10U));
    output[position++] = ']';
    output[position] = '\0';
    return 0;
}
