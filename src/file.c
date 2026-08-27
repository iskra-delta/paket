/*
   Implements PAKET's compact streaming file writer. The CP/M version talks
   directly to BDOS using one FCB and one 128-byte DMA record.

   GPL 3.0 License (see: LICENSE)
   Copyright (C) 2026 Tomaz Stih
*/

#include "paket/file.h"
#include <string.h>

#if defined(PAKET_CPM)

#include <sys/bdos.h>

#define FCB_DRIVE 0U
#define FCB_NAME  1U
#define FCB_TYPE  9U

static int ascii_alpha(char value)
{
    return ((value >= 'A') && (value <= 'Z')) ||
           ((value >= 'a') && (value <= 'z'));
}

static char ascii_upper(char value)
{
    if ((value >= 'a') && (value <= 'z')) {
        return (char)(value - ('a' - 'A'));
    }
    return value;
}

static int parse_target(paket_file *file, const char *path)
{
    const char *cursor = path;
    unsigned int position = 0U;
    unsigned int user = 0U;

    if ((file == NULL) || (path == NULL) || (*path == '\0')) {
        return -1;
    }
    memset(file->fcb, 0, sizeof(file->fcb));
    memset(file->fcb + FCB_NAME, ' ', 11U);

    if (ascii_alpha(cursor[0]) && (cursor[1] == ':')) {
        file->fcb[FCB_DRIVE] =
            (uint8_t)(ascii_upper(cursor[0]) - 'A' + 1);
        cursor += 2;
    }
    while ((*cursor != '\0') && (*cursor != '.') && (*cursor != '[')) {
        if ((position >= 8U) || (*cursor <= ' ') || (*cursor >= 0x7f)) {
            return -1;
        }
        file->fcb[FCB_NAME + position++] =
            (uint8_t)ascii_upper(*cursor++);
    }
    if ((position == 0U) || (*cursor != '.')) {
        return -1;
    }
    ++cursor;
    position = 0U;
    while ((*cursor != '\0') && (*cursor != '[')) {
        if ((position >= 3U) || (*cursor <= ' ') || (*cursor >= 0x7f)) {
            return -1;
        }
        file->fcb[FCB_TYPE + position++] =
            (uint8_t)ascii_upper(*cursor++);
    }
    if ((position == 0U) || (*cursor++ != '[')) {
        return -1;
    }
    if ((*cursor < '0') || (*cursor > '9')) {
        return -1;
    }
    do {
        user = user * 10U + (unsigned int)(*cursor++ - '0');
        if (user > 15U) {
            return -1;
        }
    } while ((*cursor >= '0') && (*cursor <= '9'));
    if ((*cursor++ != ']') || (*cursor != '\0')) {
        return -1;
    }
    file->user = (uint8_t)user;
    return 0;
}

static int flush_record(paket_file *file)
{
    bdos(F_DMAOFF, (uint16_t)file->dma);
    if (bdos(F_WRITE, (uint16_t)file->fcb) != BDOS_SUCCESS) {
        return -1;
    }
    file->dma_used = 0U;
    return 0;
}

int paket_file_create(paket_file *file, const char *path)
{
    if (parse_target(file, path) != 0) {
        return -1;
    }
    file->dma_used = 0U;
    file->open = 0U;
    file->previous_user = bdos(F_USERNUM, 0xffU);
    bdos(F_USERNUM, file->user);

    (void)bdos(F_DELETE, (uint16_t)file->fcb);
    if (bdos(F_MAKE, (uint16_t)file->fcb) == BDOS_FAILURE) {
        bdos(F_USERNUM, file->previous_user);
        return -1;
    }
    file->open = 1U;
    return 0;
}

int paket_file_write(
    paket_file *file,
    const uint8_t *data,
    size_t data_size
)
{
    size_t copied = 0U;
    size_t available = 0U;

    if ((file == NULL) || (file->open == 0U) ||
        ((data == NULL) && (data_size != 0U))) {
        return -1;
    }
    while (copied < data_size) {
        available = 128U - file->dma_used;
        if (available > data_size - copied) {
            available = data_size - copied;
        }
        memcpy(file->dma + file->dma_used, data + copied, available);
        file->dma_used = (uint8_t)(file->dma_used + available);
        copied += available;
        if ((file->dma_used == 128U) && (flush_record(file) != 0)) {
            return -1;
        }
    }
    return 0;
}

int paket_file_close(paket_file *file)
{
    int result = 0;

    if ((file == NULL) || (file->open == 0U)) {
        return -1;
    }
    if (file->dma_used != 0U) {
        memset(file->dma + file->dma_used, 0x1a, 128U - file->dma_used);
        if (flush_record(file) != 0) {
            result = -1;
        }
    }
    if (bdos(F_CLOSE, (uint16_t)file->fcb) == BDOS_FAILURE) {
        result = -1;
    }
    file->open = 0U;
    bdos(F_USERNUM, file->previous_user);
    return result;
}

int paket_file_remove(paket_file *file)
{
    uint8_t previous_user = 0U;
    int result = 0;

    if (file == NULL) {
        return -1;
    }
    previous_user = bdos(F_USERNUM, 0xffU);
    bdos(F_USERNUM, file->user);
    if (bdos(F_DELETE, (uint16_t)file->fcb) == BDOS_FAILURE) {
        result = -1;
    }
    bdos(F_USERNUM, previous_user);
    return result;
}

#else

#include <fcntl.h>
#include <unistd.h>

int paket_file_create(paket_file *file, const char *path)
{
    if ((file == NULL) || (path == NULL)) {
        return -1;
    }
    file->path = path;
    file->descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    return file->descriptor < 0 ? -1 : 0;
}

int paket_file_write(
    paket_file *file,
    const uint8_t *data,
    size_t data_size
)
{
    if ((file == NULL) || (file->descriptor < 0) ||
        ((data == NULL) && (data_size != 0U))) {
        return -1;
    }
    return write(file->descriptor, data, data_size) ==
        (ssize_t)data_size ? 0 : -1;
}

int paket_file_close(paket_file *file)
{
    int descriptor = 0;

    if ((file == NULL) || (file->descriptor < 0)) {
        return -1;
    }
    descriptor = file->descriptor;
    file->descriptor = -1;
    return close(descriptor);
}

int paket_file_remove(paket_file *file)
{
    if ((file == NULL) || (file->path == NULL)) {
        return -1;
    }
    return unlink(file->path);
}

#endif
