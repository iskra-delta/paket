/*
   Implements PAKET.COM user-visible behavior: catalog listing, wildcard
   search, package information display, destination conversion, and streamed
   download with partial-file cleanup on failure.

   GPL 3.0 License (see: LICENSE)
   Copyright (C) 2026 Tomaz Stih
*/

#include "paket/application.h"

#include "paket/command.h"
#include "paket/file.h"
#include "paket/messages.h"
#include "paket/output.h"

#include <stdint.h>
#include <string.h>

typedef struct {
    paket_file file;
} download_writer_context;

static paket_package_info package_info;
static char target_path[PAKET_TARGET_SIZE];

#define paket_screen_width 78U
#define paket_catalog_id_width 22U
#define paket_field_width 13U

static char next_partner_character(const unsigned char **position)
{
    const unsigned char *cursor = *position;
    char character = '?';

    if (*cursor == 0U) {
        return '\0';
    }
    if (*cursor < 0x80U) {
        character = (char)*cursor++;
    } else if ((cursor[0] == 0xc4U) &&
               ((cursor[1] == 0x8cU) || (cursor[1] == 0x86U))) {
        character = 'C';
        cursor += 2;
    } else if ((cursor[0] == 0xc4U) &&
               ((cursor[1] == 0x8dU) || (cursor[1] == 0x87U))) {
        character = 'c';
        cursor += 2;
    } else if ((cursor[0] == 0xc5U) && (cursor[1] == 0xa0U)) {
        character = 'S';
        cursor += 2;
    } else if ((cursor[0] == 0xc5U) && (cursor[1] == 0xa1U)) {
        character = 's';
        cursor += 2;
    } else if ((cursor[0] == 0xc5U) && (cursor[1] == 0xbdU)) {
        character = 'Z';
        cursor += 2;
    } else if ((cursor[0] == 0xc5U) && (cursor[1] == 0xbeU)) {
        character = 'z';
        cursor += 2;
    } else {
        ++cursor;
        while ((*cursor & 0xc0U) == 0x80U) {
            ++cursor;
        }
    }
    *position = cursor;
    return character;
}

static void output_spaces(unsigned int count)
{
    while (count-- > 0U) {
        paket_output_character(' ');
    }
}

static unsigned int partner_text_width(const char *text)
{
    const unsigned char *cursor = (const unsigned char *)text;
    unsigned int width = 0U;

    if (text == NULL) {
        return 0U;
    }
    while (*cursor != 0U) {
        (void)next_partner_character(&cursor);
        ++width;
    }
    return width;
}

static void print_partner_text(const char *text)
{
    const unsigned char *cursor = (const unsigned char *)text;

    if (text == NULL) {
        return;
    }
    while (*cursor != 0U) {
        paket_output_character(next_partner_character(&cursor));
    }
}

static void print_partner_column(const char *text, unsigned int width)
{
    const unsigned char *cursor = (const unsigned char *)text;
    unsigned int text_width = partner_text_width(text);
    unsigned int limit = width;
    unsigned int written = 0U;

    if (text_width > width && width > 3U) {
        limit = width - 3U;
    }
    while ((cursor != NULL) && (*cursor != 0U) && (written < limit)) {
        paket_output_character(next_partner_character(&cursor));
        ++written;
    }
    if (text_width > width) {
        while (written < width) {
            paket_output_character('.');
            ++written;
        }
    } else {
        output_spaces(width - written);
    }
}

static void print_partner_text_wrapped(
    const char *text,
    unsigned int current_column,
    unsigned int continuation_indent
)
{
    const unsigned char *cursor = (const unsigned char *)text;

    if ((text == NULL) || (*text == '\0')) {
        paket_output_character('\n');
        return;
    }
    while (*cursor != 0U) {
        const unsigned char *word_end = NULL;
        const unsigned char *word_cursor = NULL;
        unsigned int word_width = 0U;
        unsigned int separator = 0U;

        while ((*cursor == ' ') || (*cursor == '\t')) {
            ++cursor;
        }
        if ((*cursor == '\r') || (*cursor == '\n')) {
            unsigned char newline = *cursor++;

            if ((*cursor != 0U) && (*cursor != newline) &&
                ((*cursor == '\r') || (*cursor == '\n'))) {
                ++cursor;
            }
            paket_output_character('\n');
            output_spaces(continuation_indent);
            current_column = continuation_indent;
            continue;
        }
        if (*cursor == 0U) {
            break;
        }
        word_end = cursor;
        while ((*word_end != 0U) && (*word_end != ' ') &&
               (*word_end != '\t') && (*word_end != '\r') &&
               (*word_end != '\n')) {
            (void)next_partner_character(&word_end);
            ++word_width;
        }
        separator = current_column > continuation_indent ? 1U : 0U;
        if ((current_column > continuation_indent) &&
            (current_column + separator + word_width > paket_screen_width)) {
            paket_output_character('\n');
            output_spaces(continuation_indent);
            current_column = continuation_indent;
            separator = 0U;
        }
        if (separator != 0U) {
            paket_output_character(' ');
            ++current_column;
        }
        word_cursor = cursor;
        while (word_cursor < word_end) {
            paket_output_character(next_partner_character(&word_cursor));
            ++current_column;
        }
        cursor = word_end;
    }
    paket_output_character('\n');
}

static void print_section(const char *title)
{
    paket_output_character('\n');
    paket_output_line(title);
}

static void print_field_prefix(const char *label)
{
    print_partner_column(label, paket_field_width);
    paket_output_text(": ");
}

static const char *rating_text(uint8_t rating)
{
    switch (rating) {
    case 1U:
        return paket_message(PAKET_MSG_RATING_CURIOUS);
    case 2U:
        return paket_message(PAKET_MSG_RATING_AVERAGE);
    case 3U:
        return paket_message(PAKET_MSG_RATING_GREAT);
    case 4U:
        return paket_message(PAKET_MSG_RATING_LEGENDARY);
    default:
        return paket_message(PAKET_MSG_RATING_UNKNOWN);
    }
}

void paket_print_usage(void)
{
    print_section(paket_message(PAKET_MSG_USAGE));
    paket_output_line(paket_message(PAKET_MSG_USAGE_LIST));
    paket_output_line(paket_message(PAKET_MSG_USAGE_SEARCH));
    paket_output_line(paket_message(PAKET_MSG_USAGE_INFO));
    paket_output_line(paket_message(PAKET_MSG_USAGE_DOWNLOAD));
    print_section(paket_message(PAKET_MSG_CONNECTION_OPTIONS_TITLE));
    paket_output_line(paket_message(PAKET_MSG_PORTS));
    paket_output_line(paket_message(PAKET_MSG_COMMUNICATION));
    paket_output_line(paket_message(PAKET_MSG_COMMUNICATION_DEFAULT));
    print_section(paket_message(PAKET_MSG_DESTINATION_OPTIONS_TITLE));
    paket_output_line(paket_message(PAKET_MSG_DESTINATIONS));
}

static int print_catalog_entry(
    void *context,
    const paket_catalog_entry *entry
)
{
    (void)context;
    print_partner_column(entry->id, paket_catalog_id_width);
    paket_output_text("  ");
    print_partner_text(entry->name);
    paket_output_character('\n');
    return 0;
}

static int show_catalog(paket_client *client, const char *pattern)
{
    unsigned int count = 0U;
    int result = 0;

    print_section(paket_message(
        pattern == NULL ? PAKET_MSG_CATALOG_TITLE : PAKET_MSG_SEARCH_TITLE
    ));
    if (pattern != NULL) {
        print_field_prefix(paket_message(PAKET_MSG_PATTERN));
        print_partner_text_wrapped(
            pattern,
            paket_field_width + 2U,
            paket_field_width + 2U
        );
        paket_output_character('\n');
    }
    print_partner_column(
        paket_message(PAKET_MSG_CATALOG_ID_HEADER),
        paket_catalog_id_width
    );
    paket_output_text("  ");
    print_partner_text(paket_message(PAKET_MSG_CATALOG_NAME_HEADER));
    paket_output_character('\n');

    result = paket_visit_catalog(
        client,
        pattern,
        print_catalog_entry,
        NULL,
        &count
    );

    if (result != 0) {
        paket_output_format(
            paket_message(PAKET_MSG_ERROR_FORMAT),
            paket_result_text(result)
        );
        return 1;
    }
    if (count == 0U) {
        paket_output_line(paket_message(PAKET_MSG_NO_PACKAGES));
    } else {
        paket_message_id count_message = PAKET_MSG_PACKAGE_COUNT_FORMAT;
        unsigned int ending = count % 100U;

        if (ending == 1U) {
            count_message = PAKET_MSG_PACKAGE_COUNT_ONE_FORMAT;
        } else if (ending == 2U) {
            count_message = PAKET_MSG_PACKAGE_COUNT_TWO_FORMAT;
        } else if ((ending == 3U) || (ending == 4U)) {
            count_message = PAKET_MSG_PACKAGE_COUNT_FEW_FORMAT;
        }
        paket_output_format(
            paket_message(count_message),
            count
        );
        paket_output_line(paket_message(PAKET_MSG_CATALOG_HINT));
    }
    return 0;
}

static void print_field(const char *label, const char *value)
{
    if ((value == NULL) || (*value == '\0')) {
        return;
    }
    print_field_prefix(label);
    print_partner_text_wrapped(
        value,
        paket_field_width + 2U,
        paket_field_width + 2U
    );
}

static void print_unsigned_field(const char *label, unsigned int value)
{
    print_field_prefix(label);
    paket_output_format(
        paket_message(PAKET_MSG_YEAR_FORMAT),
        value
    );
}

static void print_size_field(const char *label, uint32_t value)
{
    print_field_prefix(label);
    if (value == UINT32_MAX) {
        paket_output_line(paket_message(PAKET_MSG_SIZE_UNKNOWN));
    } else {
        paket_output_format(
            paket_message(PAKET_MSG_BYTES_FORMAT),
            (unsigned long)value
        );
        paket_output_character('\n');
    }
}

static void print_package_info(const paket_package_info *info)
{
    unsigned int index = 0U;

    print_section(paket_message(PAKET_MSG_INFO_TITLE));
    print_field(paket_message(PAKET_MSG_PACKAGE), info->name);
    print_field(paket_message(PAKET_MSG_IDENTIFIER), info->id);
    print_field(paket_message(PAKET_MSG_VERSION), info->version);
    print_field(paket_message(PAKET_MSG_VENDOR), info->vendor);
    print_field(paket_message(PAKET_MSG_PLATFORM), info->platform_name);
    if (info->release_year != 0U) {
        print_unsigned_field(
            paket_message(PAKET_MSG_YEAR),
            (unsigned int)info->release_year
        );
    }
    print_field_prefix(paket_message(PAKET_MSG_RATING));
    if ((info->rating >= 1U) && (info->rating <= 4U)) {
        paket_output_format(
            paket_message(PAKET_MSG_RATING_FORMAT),
            rating_text(info->rating),
            (unsigned int)info->rating
        );
    } else {
        paket_output_line(rating_text(info->rating));
    }

    if (info->description[0] != '\0') {
        print_section(paket_message(PAKET_MSG_DESCRIPTION_TITLE));
        print_partner_text_wrapped(info->description, 0U, 0U);
    }

    print_section(paket_message(PAKET_MSG_DOWNLOAD_OPTIONS_TITLE));
    if (info->download_count == 0U) {
        paket_output_line(paket_message(PAKET_MSG_DOWNLOADS_NONE));
        return;
    }
    for (index = 0U; index < info->stored_download_count; ++index) {
        const paket_download_choice *choice = &info->downloads[index];

        paket_output_format(
            paket_message(PAKET_MSG_DOWNLOAD_CHOICE_FORMAT),
            index + 1U
        );
        print_partner_text(choice->label);
        paket_output_character('\n');
        print_field(paket_message(PAKET_MSG_FORMAT), choice->format);
        print_size_field(
            paket_message(PAKET_MSG_SIZE),
            choice->aggregate_size
        );
        print_unsigned_field(
            paket_message(PAKET_MSG_FILES),
            (unsigned int)choice->file_count
        );
        paket_output_character('\n');
    }
    if (info->download_count > info->stored_download_count) {
        paket_output_format(
            paket_message(PAKET_MSG_MORE_CHOICES_FORMAT),
            (unsigned int)(
                info->download_count - info->stored_download_count
            )
        );
    }
    paket_output_format(
        paket_message(PAKET_MSG_DOWNLOAD_HINT_FORMAT),
        info->id
    );
}

static int resolve_and_fetch(
    paket_client *client,
    const char *package_name,
    paket_catalog_entry *entry
)
{
    int result = paket_resolve_package(client, package_name, entry);

    if (result == 0) {
        result = paket_fetch_info(client, entry->id, &package_info);
    }
    if (result != 0) {
        paket_output_format(
            paket_message(PAKET_MSG_ERROR_FORMAT),
            paket_result_text(result)
        );
    }
    return result;
}

static int write_download_chunk(
    void *context,
    const uint8_t *data,
    size_t data_size,
    uint32_t offset,
    uint32_t total_size
)
{
    download_writer_context *writer = context;
    uint32_t completed = 0U;
    uint32_t remaining = 0U;

    if ((writer == NULL) ||
        (paket_file_write(&writer->file, data, data_size) != 0)) {
        return -1;
    }
    completed = offset + (uint32_t)data_size;
    remaining = completed < total_size ? total_size - completed : 0U;

    /* CR redraws one fixed-width status line on both Partner consoles. Keep
       this free of terminal escapes: GDP and CRT use different terminal
       profiles, but both implement carriage return. */
    paket_output_character('\r');
    print_field_prefix(paket_message(PAKET_MSG_STATUS));
    paket_output_format(
        paket_message(PAKET_MSG_REMAINING_FORMAT),
        (unsigned long)remaining
    );
    output_spaces(10U);
    return 0;
}

static int download_package(
    paket_client *client,
    const char *package_name,
    const char *destination
)
{
    paket_catalog_entry entry;
    const paket_download_choice *choice = NULL;
    download_writer_context writer;
    int result = 0;

    if (paket_has_wildcards(package_name)) {
        paket_output_line(paket_message(PAKET_MSG_NO_DOWNLOAD_WILDCARDS));
        return 1;
    }
    result = resolve_and_fetch(client, package_name, &entry);
    if (result != 0) {
        return 1;
    }
    if (package_info.stored_download_count == 0U) {
        paket_output_line(paket_message(PAKET_MSG_NO_DOWNLOADS));
        return 1;
    }
    choice = &package_info.downloads[0];
    if (paket_make_target_path(
        destination,
        package_name,
        choice->format,
        choice->file_count,
        target_path,
        sizeof(target_path)
    ) != 0) {
        paket_output_line(paket_message(PAKET_MSG_INVALID_DESTINATION));
        return 1;
    }

    print_section(paket_message(PAKET_MSG_DOWNLOAD_TITLE));
    print_field(paket_message(PAKET_MSG_PACKAGE), package_info.name);
    print_field(paket_message(PAKET_MSG_IDENTIFIER), entry.id);
    print_field(paket_message(PAKET_MSG_DESTINATION), target_path);
    print_field(paket_message(PAKET_MSG_SELECTED_OPTION), choice->label);
    print_field(paket_message(PAKET_MSG_FORMAT), choice->format);
    print_size_field(paket_message(PAKET_MSG_SIZE), choice->aggregate_size);
    print_unsigned_field(
        paket_message(PAKET_MSG_FILES),
        (unsigned int)choice->file_count
    );

    if (paket_file_create(&writer.file, target_path) != 0) {
        paket_output_format(
            paket_message(PAKET_MSG_CANNOT_CREATE_FORMAT),
            target_path
        );
        return 1;
    }

    print_field_prefix(paket_message(PAKET_MSG_STATUS));
    paket_output_text(paket_message(PAKET_MSG_STATUS_DOWNLOADING));
    result = paket_fetch_download(
        client,
        entry.id,
        choice->id,
        write_download_chunk,
        &writer
    );
    paket_output_character('\n');
    if (paket_file_close(&writer.file) != 0) {
        result = PAKET_ERROR_WRITE;
    }

    if (result != 0) {
        (void)paket_file_remove(&writer.file);
        paket_output_format(
            paket_message(PAKET_MSG_DOWNLOAD_FAILED_FORMAT),
            paket_result_text(result)
        );
        paket_output_line(paket_message(PAKET_MSG_PARTIAL_REMOVED));
        return 1;
    }
    paket_output_line(paket_message(PAKET_MSG_DOWNLOAD_COMPLETE));
    return 0;
}

int paket_application_run(
    paket_client *client,
    int argc,
    char **argv
)
{
    paket_catalog_entry entry;
    int result = 0;

    if ((client == NULL) || (argv == NULL) || (argc < 1) || (argc > 3)) {
        paket_print_usage();
        return 1;
    }
    if (argc == 1) {
        return show_catalog(client, NULL);
    }
    if (argc == 2) {
        if (paket_has_wildcards(argv[1])) {
            return show_catalog(client, argv[1]);
        }
        result = resolve_and_fetch(client, argv[1], &entry);
        if (result != 0) {
            return 1;
        }
        print_package_info(&package_info);
        return 0;
    }
    return download_package(client, argv[1], argv[2]);
}
