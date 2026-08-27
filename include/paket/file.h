/*
   Declares PAKET's single-file streaming writer. On CP/M it uses one fixed
   FCB and DMA record instead of the general-purpose file-descriptor library.

   GPL 3.0 License (see: LICENSE)
   Copyright (C) 2026 Tomaz Stih
*/

#ifndef PAKET_FILE_H
#define PAKET_FILE_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
#if defined(PAKET_CPM)
    uint8_t fcb[36];
    uint8_t dma[128];
    uint8_t dma_used;
    uint8_t user;
    uint8_t previous_user;
    uint8_t open;
#else
    int descriptor;
    const char *path;
#endif
} paket_file;

/* Create or replace one validated native CP/M 8.3 target. */
int paket_file_create(paket_file *file, const char *path);

/* Append a byte block to the open file. */
int paket_file_write(
    paket_file *file,
    const uint8_t *data,
    size_t data_size
);

/* Flush and close the file. */
int paket_file_close(paket_file *file);

/* Delete the file previously named in paket_file_create(). */
int paket_file_remove(paket_file *file);

#endif
