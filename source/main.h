#ifndef MAIN_H
#define MAIN_H

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <wchar.h>
#ifdef __linux__
#include <unistd.h>
#include <dirent.h>
#else
#include <io.h>
#define access _access
#endif // __linux__

#define EXT_LEN 5
#define PATH_LEN 1024
#define NAME_LEN 256
#define BUFFER_LEN 134217728
#define FILE_POS dataRead
#define ADV_FILE_POS(x) { if (FILE_POS + x >= BUFFER_LEN) return ERR_MEM; FILE_POS += x; }
#define DUMP_NAME "importer-dump.scne"

#define FLAG_DONE "--done"
#define FLAG_MULTI "--multi"
#define FLAG_DUMP "--dump"
#define FLAG_OUT "--out"

#define MODE_DUMP (1 << 0)
#define MODE_MULTI (1 << 1)

#define ERR_OK 0
#define ERR_IO -1
#define ERR_MEM -2
#define ERR_ENC -3
#define ERR_FMT -4
#define ERR_PATH -5

typedef struct bagFile {
    char path[PATH_LEN * 2];
    char root[PATH_LEN];
    char name[NAME_LEN];
    const char *ext;
    uint32_t size;
    uint32_t inflated;
} bagFile_t;

#endif // MAIN_H
