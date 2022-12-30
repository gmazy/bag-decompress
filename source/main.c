#include "main.h"

#define SUPPORT_07
#define WRITE_ERR_LOG
//#define DEBUG_MODE
#ifdef DEBUG_MODE
#define DBG(fmt, ...) printf(fmt, ## __VA_ARGS__)
#else
#define DBG(fmt, ...) 
#endif // DEBUG

// Lz4 decompression is based on http://fastcompression.blogspot.co.uk/2011/05/lz4-explained.html
// and inspired by https://github.com/MaxxWyndham/Breckfest/blob/master/breckFest/LZ4Decompress.cs

int _mode = 0;
int _count = 0;
int _rc = ERR_OK;
int _pOffset = 0;
FILE *_dump = NULL;
char _procDir[PATH_LEN];
const char *_outDir = ".";
char _dirBuf[PATH_LEN];
char _pattern[PATH_LEN];
uint8_t *buf = NULL;
uint8_t *data = NULL;


int fileWrite(const char *path, const char *mode, uint8_t *buf, uint32_t size)
{
    DBG("writing %s\n", path);
    FILE *file = fopen(path, mode);
    size_t ret = fwrite((void *)buf, 1, size, file);
    fclose(file);
    return ret == size;
}

void errLog(const char *filepath)
{
    char reason[32];
    if (_rc == ERR_MEM)
        sprintf(reason, "out of buffer");
    else if (_rc == ERR_IO)
        sprintf(reason, "can't read file");
    else if (_rc == ERR_ENC)
        sprintf(reason, "encrypted data");
    else if (_rc == ERR_FMT)
        sprintf(reason, "invalid data");
    else if (_rc == ERR_PATH)
        sprintf(reason, "path does not exist");
    else
        sprintf(reason, "unspecified");


    char buf[PATH_LEN * 2];
    snprintf(buf, sizeof(buf) - 1, "ERROR %d | Failed to decompress %s... %s\n", _rc, strrchr(filepath, '/'), reason);
#ifdef WRITE_ERR_LOG
    char errPath[PATH_LEN * 2];
    memset(errPath, 0, sizeof(errPath));
    snprintf(errPath, sizeof(errPath) - 1, "%s/error.txt", _procDir);
    fileWrite(errPath, "a+", buf, (uint32_t)strlen(buf));
#endif // WRITE_ERR_LOG
    fprintf(stderr, "%s", buf);
    fflush(stderr);
}

int ddsOffset(const char *buf, uint32_t size)
{
    uint8_t token[5] = { 'D', 'D', 'S', 0x20, 0x7C };
    int found = 0;
    for (uint32_t i = 0; i < size; i++)
    {
        if (buf[i] == token[found++])
        {
            if (found == sizeof(token))
                return ++i - sizeof(token);
            continue;
        }
        found = 0;
    }
    return -1;
}

int writeToDisk(bagFile_t *file)
{
    DBG("writing %s %s %s\n", file->path, file->name, _outDir);
    size_t pathLen = snprintf(file->path, sizeof(file->path) - 1, "%s%s", file->root, file->name);
    DBG("writing %s %s %s\n", file->path, file->name, _outDir);
    file->ext = strrchr(file->path, '.');
    if (_mode & MODE_DUMP)
    {
        DBG("writing to dump file\n");
        fwrite((void *)buf, 1, file->inflated, _dump);
    }
    else if (!strcmp(file->ext, ".bmap"))
    {
        DBG("writing bmap -> dds\n");
        int offset = ddsOffset(buf, file->size);
        if (offset == -1)
            goto err_fmt;
        strcpy(&file->path[pathLen - EXT_LEN], ".dds");
        if (!fileWrite(file->path, "wb+", (void *)&buf[offset], file->inflated - offset))
            goto err_io;
    }
    else
    {
        DBG("writing file -> raw\n");
        char suffix[64];
        snprintf(suffix, sizeof(suffix) - 1, ".raw%s", file->ext);
        snprintf(&file->path[pathLen - EXT_LEN], sizeof(file->path), "%s", suffix);
        if (!fileWrite(file->path, "wb+", buf, file->inflated))
            goto err_io;
    }
    return ERR_OK;

err_io:
    return _rc = ERR_IO;
err_fmt:
    return _rc = ERR_FMT;
}

char *getDirectory(char *buf, char *name)
{
    char *check = strrchr(buf, '/');
    if (check == NULL)
        return NULL;
    if (name != NULL)
        name = check + 1;
    buf[check - buf] = 0;
    return check;
}

void slashConvert(char *buf)
{
    for (int i = 0; i < strlen(buf); i++)
    {
        if (buf[i] == '\\')
            buf[i] = '/';
    }
}

int invalid(bagFile_t *file)
{
    return _rc = access(file->path, 0) ? ERR_PATH : ERR_OK;
}

void sanitize(const char *filepath, bagFile_t *file)
{
    char buf[PATH_LEN];
    size_t len = snprintf(buf, sizeof(buf) - 1, "%s", filepath);
    file->ext = strrchr(buf, '.');
    memset(file->path, 0, PATH_LEN);

    uint32_t start = 0;
    const char *sq = strchr(buf, '\'');
    if (sq != NULL)
    {
        const size_t len = strlen(buf);
        for (uint32_t i = 0; i < len; i++)
        {
            if (buf[i] != '\'' && (buf[i] >= 33 && buf[i] <= 126))
            {
                start = i;
                break;
            }
        }
    }
    memcpy(file->path, &buf[start], (file->ext - buf) - start + EXT_LEN);

    slashConvert(file->path);
    DBG("sanitize -> %s\n", file->path);
    file->ext = strrchr(file->path, '.');
    const char *check = strrchr(file->path, '/');
    if (check == NULL)
    {
        strcpy(file->root, "./");
        memcpy(file->name, file->path, NAME_LEN);
        file->name[NAME_LEN - 1] = 0;
        DBG("1set name to %s\n", file->name);
        return;
    }
    memcpy(file->root, file->path, check - file->path);
    strcpy(file->name, check);
    DBG("2set name to %s\n", file->name);
}

void dumpOpen()
{
    if (!(_mode & MODE_DUMP) || _dump != NULL)
        return;

    snprintf(_dirBuf, sizeof(_dirBuf) - 1, "%s/%s", _outDir, DUMP_NAME);
    _dump = fopen(_dirBuf, "wb");
}

void dumpClose()
{
    if (!(_mode & MODE_DUMP) || _dump == NULL)
        return;

    uint32_t fsize = ftell(_dump);
    fclose(_dump);
    if (fsize != 0)
        return;

    if (!strcmp(_outDir, "."))
        snprintf(_dirBuf, sizeof(_dirBuf) - 1, "%s", DUMP_NAME);
    else
        snprintf(_dirBuf, sizeof(_dirBuf) - 1, "%s/%s", _outDir, DUMP_NAME);
    remove(_dirBuf);
}

int decompress(bagFile_t *file)
{
    FILE *f = fopen(file->path, "rb");
    fseek(f, 0, SEEK_END);
    file->size = ftell(f);
    rewind(f);
    fread(data, file->size, 1, f);
    fclose(f);

    DBG("decompressing %s\n", file->path);
    uint32_t blockOffset = 1;
    uint32_t FILE_POS = 12;
    file->inflated = FILE_POS;
    memcpy(buf, data, FILE_POS);
    if (buf[0] == 0x08)
        return _rc = ERR_ENC;
#ifdef SUPPORT_07
    else if (buf[0] == 0x07)
        blockOffset = 2;
#endif // SUPPORT_07A
    else if (buf[0] != 0x04)
        return _rc = ERR_FMT;
    buf[0] = 0x01;

    while (FILE_POS < file->size)
    {
        uint32_t blockSize = *(uint32_t *)&data[FILE_POS];
        ADV_FILE_POS(blockOffset << 2);
        uint32_t blockEnd = blockSize + FILE_POS;

        uint32_t blockPos = 0;
        while (1)
        {
            uint32_t literalLength = (data[FILE_POS] & 0xF0) >> 4;
            uint32_t matchLength = (data[FILE_POS] & 0x0F) + 4;
            ADV_FILE_POS(1);
            if (literalLength == 0x0F)
            {
                do
                {
                    literalLength += data[FILE_POS];
                    ADV_FILE_POS(1);
                } while (data[FILE_POS - 1] == 0xFF);
            }

            uint32_t temp = file->inflated + blockPos;
            if (temp + literalLength > BUFFER_LEN * 2)
                return _rc = ERR_MEM;
            memcpy(&buf[temp], &data[FILE_POS], literalLength);
            ADV_FILE_POS(literalLength);
            blockPos += literalLength;

            if (FILE_POS == blockEnd)
                break;

            uint16_t offset = *(uint16_t *)&data[FILE_POS];
            ADV_FILE_POS(2);

            if (matchLength == 0x0F + 4)
            {
                do
                {
                    matchLength += data[FILE_POS];
                    ADV_FILE_POS(1);
                } while (data[FILE_POS - 1] == 0xFF);
            }

            for (uint32_t i = 0; i < matchLength; i++)
            {
                uint32_t x = file->inflated + blockPos + i;
                buf[x] = buf[x - offset];
            }

            blockPos += matchLength;
        }
        file->inflated += blockPos;
    }

    DBG("decompressed %d\n", file->inflated);
    writeToDisk(file);
    return ERR_OK;
}

void process(const char *filepath)
{
    DBG("process %s\n", filepath);
    _rc = ERR_OK;

    static bagFile_t bagFile;
    sanitize(filepath, &bagFile);
    if (invalid(&bagFile) == ERR_OK)
        decompress(&bagFile);

    if (_rc >= ERR_OK) {
        _count++;
    	printf("done\n");
        fflush(stdout);
    }
    else {
        errLog(filepath);
    }
}


void paramOut(const int params, const char *arg[], int *i)
{
    if (*i == params - 1)
        return;
    ++*i;

#ifdef __linux__  
    DIR *d = opendir(arg[*i]);
    if (!d)
        return;
    _outDir = arg[*i];
    closedir(d);
#else
    char *name = NULL;
    char pattern[PATH_LEN];
    snprintf(pattern, sizeof(pattern) - 1, "%s", arg[*i]);
    slashConvert(pattern);
    getDirectory(pattern, name);
    snprintf(&pattern[strlen(pattern)], sizeof(pattern) - 1, "/%s", name);

    struct _finddata_t c_file;
    intptr_t exists = _findfirst(pattern, &c_file);
    if (exists && c_file.size == 0)
        _outDir = arg[*i];
#endif // __linux__
}

void paramPath(const char *arg, int *i)
{
    if (i == NULL)
        return;
    if (arg[0] == '-')
    {
        if (!strcmp(arg, FLAG_OUT))
            *i += 2;
        return;
    }

#ifdef __linux__  
    DIR *d = opendir(arg);
    if (!d)
    {
        process(arg);
        return;
    }

    struct dirent *dir;
    while ((dir = readdir(d)) != NULL)
    {
        const char *ext = strrchr(dir->d_name, '.');
        if (ext == NULL || strlen(ext) != EXT_LEN)
            continue;
        char temp[PATH_LEN * 2];
        snprintf(temp, sizeof(temp) - 1, "%s/%s", _dirBuf, dir->d_name);
        process(temp);
    }
    closedir(d);
#else
    struct _finddata_t c_file;
    intptr_t handle = _findfirst(arg, &c_file);
    if (handle && c_file.size > 0)
    {
        process(arg);
        return;
    }

    snprintf(_pattern, sizeof(_pattern) - 1, "%s", arg);
    slashConvert(_pattern);
    getDirectory(_pattern, NULL);
    strcpy(_dirBuf, _pattern);
    strcat(_pattern, "/*");
    handle = _findfirst(_pattern, &c_file);
    if (!handle)
        return;

    do
    {
        if (c_file.size == 0)
            continue;
        const char *ext = strrchr(c_file.name, '.');
        if (ext == NULL || strlen(ext) != EXT_LEN)
            continue;
        char temp[PATH_LEN * 2];
        snprintf(temp, sizeof(temp) - 1, "%s/%s", _dirBuf, c_file.name);
        process(temp);
    } while (!_findnext(handle, &c_file));
#endif // __linux__
}

void handleInteractive()
{
    if (_count != 0 && !(_mode & MODE_MULTI))
        return;

    printf("Enter filepath to be decompressed, or drag/drop into console.\n");
    printf("Enter %s to exit.\n", FLAG_DONE);
    char buf[PATH_LEN];
    while (1)
    {
        printf("path?\n");
        fflush(stdout);
        memset(buf, 0, sizeof(buf));

        if (fgets(buf, sizeof(buf) - 1, stdin) == NULL)
            continue;
        buf[strcspn(buf, "\r\n")] = 0;
        if (strlen(buf) <= EXT_LEN)
            continue;
        if (strcmp(buf, FLAG_DONE))
            paramPath(buf, NULL);
        else
            break;
    }
}

void getProcDir(const char *arg)
{
    snprintf(_procDir, sizeof(_procDir) - 1, "%s", arg);
    slashConvert(_procDir);
    getDirectory(_procDir, NULL);
}

void getFlags(const int p, const char *arg[])
{
    for (int i = 1; i < p; i++)
    {
        if (!strcmp(arg[i], FLAG_MULTI))
        {
            _mode |= MODE_MULTI;

        }
        else if (!strcmp(arg[i], FLAG_DUMP))
            _mode |= MODE_DUMP;
        else if (!strcmp(arg[i], FLAG_OUT))
            paramOut(p, arg, &i);
    }
}

int main(const int p, const char *arg[])
{
    buf = (uint8_t *)malloc(BUFFER_LEN << 1);
    data = (uint8_t *)malloc(BUFFER_LEN << 1);
    if (data == NULL || buf == NULL)
        return ERR_MEM;

    getProcDir(arg[0]);
    getFlags(p, arg);
    dumpOpen();
    for (int i = 1; i < p; i++)
    {
        DBG("arg %s\n", arg[i]);
        paramPath(arg[i], i);
    }
    handleInteractive();
    dumpClose();

    return ERR_OK;
}
