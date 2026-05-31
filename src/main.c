#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>
#include <errno.h>
#include <regex.h>
#include <openssl/sha.h>

int decompress(const unsigned char *file_contents, size_t file_size, unsigned char **raw_buffer, size_t *raw_capacity)
{
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    strm.next_in = (unsigned char *)file_contents;
    strm.avail_in = (uInt)file_size;
    strm.next_out = *raw_buffer;
    strm.avail_out = (uInt)(*raw_capacity);

    if (inflateInit(&strm) != Z_OK)
    {
        fprintf(stderr, "inflateInit failed\n");
        return -1;
    }

    int ret;
    while (1)
    {
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret == Z_STREAM_END)
            break;

        if (ret == Z_OK)
        {
            if (strm.avail_out == 0)
            {
                /* need more output space: grow buffer */
                size_t used = strm.total_out;
                size_t new_cap = (*raw_capacity) * 2;
                unsigned char *new_buf = realloc(*raw_buffer, new_cap);
                if (!new_buf)
                {
                    fprintf(stderr, "memory allocation failed\n");
                    inflateEnd(&strm);
                    return -1;
                }
                *raw_buffer = new_buf;
                *raw_capacity = new_cap;
                strm.next_out = *raw_buffer + used;
                strm.avail_out = (uInt)(*raw_capacity - used);
                continue;
            }
            /* otherwise continue decompressing */
            continue;
        }

        /* handle buffer error by growing if possible */
        if (ret == Z_BUF_ERROR)
        {
            if (strm.avail_out == 0)
            {
                size_t used = strm.total_out;
                size_t new_cap = (*raw_capacity) * 2;
                unsigned char *new_buf = realloc(*raw_buffer, new_cap);
                if (!new_buf)
                {
                    fprintf(stderr, "memory allocation failed\n");
                    inflateEnd(&strm);
                    return -1;
                }
                *raw_buffer = new_buf;
                *raw_capacity = new_cap;
                strm.next_out = *raw_buffer + used;
                strm.avail_out = (uInt)(*raw_capacity - used);
                continue;
            }
        }

        /* any other error is fatal */
        fprintf(stderr, "inflate failed: %d\n", ret);
        inflateEnd(&strm);
        return -1;
    }

    size_t out_len = strm.total_out;
    /* leave buffer as raw bytes (may contain NULs) */

    inflateEnd(&strm);
    return (int)out_len;
}

int find_and_read_object(const char *hash, char *contents_buffer)
{
    char hash_buffer[1024];
    if (strlen(hash) < 3)
    {
        fprintf(stderr, "Hash too small");
        return 1;
    }
    char hash_prefix[3];
    strncpy(hash_prefix, hash, 2);
    hash_prefix[2] = '\0';
    sprintf(hash_buffer, "./.git/objects/%s/%s", hash_prefix, hash + 2);
    FILE *object_file = fopen(hash_buffer, "rb");
    if (object_file == NULL)
    {
        fprintf(stderr, "Object couldnt be opened");
        return 1;
    }
    fseek(object_file, 0, SEEK_END);
    long fsize = ftell(object_file);
    fseek(object_file, 0, SEEK_SET);
    size_t read = fread(contents_buffer, 1, fsize, object_file);
    fclose(object_file);
    if (read != (size_t)fsize)
    {
        fprintf(stderr, "failed to read object file\n");
        free(contents_buffer);
        return 1;
    }

    size_t raw_capacity = 65536;
    unsigned char *raw_buffer = malloc(raw_capacity);
    if (!raw_buffer)
    {
        fprintf(stderr, "memory allocation failed\n");
        free(contents_buffer);
        return 1;
    }

    int out_len = decompress((unsigned char *)contents_buffer, (size_t)fsize, &raw_buffer, &raw_capacity);
    if (out_len < 0)
    {
        free(contents_buffer);
        free(raw_buffer);
        return 1;
    }
    return out_len;
}

int compressFile(const unsigned char *final_file_contents, size_t file_size, unsigned char **raw_buffer, size_t *raw_capacity)
{
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    strm.next_in = (unsigned char *)final_file_contents;
    strm.avail_in = (uInt)file_size;
    strm.next_out = *raw_buffer;
    strm.avail_out = (uInt)(*raw_capacity);

    if (deflateInit(&strm, 1) != Z_OK)
    {
        fprintf(stderr, "deflateInit failed\n");
        return -1;
    }

    int ret;
    do
    {
        ret = deflate(&strm, Z_FINISH);

        if (ret == Z_OK || ret == Z_BUF_ERROR)
        {
            if (strm.avail_out == 0)
            {
                size_t used = strm.total_out;
                size_t new_cap = (*raw_capacity) * 2;
                unsigned char *new_buf = realloc(*raw_buffer, new_cap);
                if (!new_buf)
                {
                    fprintf(stderr, "memory allocation failed\n");
                    deflateEnd(&strm);
                    return -1;
                }
                *raw_buffer = new_buf;
                *raw_capacity = new_cap;
                strm.next_out = *raw_buffer + used;
                strm.avail_out = (uInt)(*raw_capacity - used);
            }
        }
        else if (ret != Z_STREAM_END)
        {
            fprintf(stderr, "deflate failed: %d\n", ret);
            deflateEnd(&strm);
            return -1;
        }

    } while (ret != Z_STREAM_END);

    size_t out_len = strm.total_out;
    deflateEnd(&strm);

    return (int)out_len;
}

int main(int argc, char *argv[])
{
    // Disable output buffering
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    if (argc < 2)
    {
        fprintf(stderr, "Usage: ./your_program.sh <command> [<args>]\n");
        return 1;
    }

    const char *command = argv[1];

    if (strcmp(command, "init") == 0)
    {
        // You can use print statements as follows for debugging, they'll be visible when running tests.
        fprintf(stderr, "Logs from your program will appear here!\n");

        if (mkdir(".git", 0755) == -1 ||
            mkdir(".git/objects", 0755) == -1 ||
            mkdir(".git/refs", 0755) == -1)
        {
            fprintf(stderr, "Failed to create directories: %s\n", strerror(errno));
            return 1;
        }

        FILE *headFile = fopen(".git/HEAD", "w");
        if (headFile == NULL)
        {
            fprintf(stderr, "Failed to create .git/HEAD file: %s\n", strerror(errno));
            return 1;
        }
        fprintf(headFile, "ref: refs/heads/main\n");
        fclose(headFile);

        printf("Initialized git directory\n");
    }
    else if (strcmp(command, "cat-file") == 0)
    {
        if (argc < 4)
        {
            fprintf(stderr, "too few params for the command\n");
            return 1;
        }
        const char *flag = argv[2];
        if (strcmp(flag, "-p") != 0)
        {
            fprintf(stderr, "Wrong flag\n");
            return 1;
        }
        const char *hash = argv[3];
        unsigned long int memory_size = 65536;
        char *contents_buffer = malloc(memory_size);
        int out_len = find_and_read_object(hash, contents_buffer);
        if (!out_len)
        {
            fprintf(stderr, "problem finding the file or decompressing");
            return 0;
        }
        regex_t rx;
        int value;
        regmatch_t match;
        unsigned char *raw_buffer = malloc(1024);
        if (!raw_buffer)
        {
            fprintf(stderr, "memory allocation failed\n");
            free(contents_buffer);
            return 1;
        }
        value = regcomp(&rx, "blob .+[0-9]", REG_EXTENDED);
        if (value != 0)
        {
            fprintf(stderr, "problem compiling regex");
            return 0;
        }
        int whereEnd = regexec(&rx, raw_buffer, 1, &match, 0);
        fwrite(raw_buffer + match.rm_eo + 1, 1, out_len - match.rm_eo - 1, stdout);
        free(contents_buffer);
        free(raw_buffer);
        return 0;
    }
    else if (strcmp(command, "hash-object") == 0)
    {
        if (argc < 4)
        {
            fprintf(stderr, "too few params for the command\n");
            return 1;
        }
        const char *flag = argv[2];
        if (strcmp(flag, "-w") != 0)
        {
            fprintf(stderr, "Wrong flag\n");
            return 1;
        }
        const char *file_name = argv[3];
        FILE *input_file = fopen(file_name, "r");
        if (input_file == NULL)
        {
            fprintf(stderr, "file could not be opened");
            return 0;
        }
        fseek(input_file, 0, SEEK_END);
        long fsize = ftell(input_file);
        fseek(input_file, 0, SEEK_SET);

        char *contents_buffer = malloc(fsize);
        if (!contents_buffer)
        {
            fprintf(stderr, "memory allocation failed\n");
            fclose(input_file);
            return 1;
        }
        size_t read = fread(contents_buffer, 1, fsize, input_file);
        fclose(input_file);
        if (read != (size_t)fsize)
        {
            fprintf(stderr, "failed to read object file\n");
            free(contents_buffer);
            return 1;
        }

        char final_uncompr_file[4096];
        size_t cap = 8192;
        unsigned char *final_compr_file = malloc(cap);
        size_t final_cmpr_len = cap;
        char contents_prefix[128] = {0};
        snprintf(contents_prefix, sizeof(contents_prefix), "blob %d", read);
        size_t final_len = strlen(contents_prefix) + 1 + read;
        memcpy(final_uncompr_file, contents_prefix, strlen(contents_prefix) + 1);
        memcpy(final_uncompr_file + strlen(contents_prefix) + 1, contents_buffer, read);
        unsigned char hash[40];
        char hash_str[100];
        SHA1(final_uncompr_file, final_len, hash);
        for (int i = 0; i < 20; i++)
        {
            sprintf(hash_str + i * 2, "%02x", hash[i]);
        }
        compressFile(final_uncompr_file, final_len, &final_compr_file, &final_cmpr_len);
        hash_str[40] = '\0';
        char hash_prefix[64];
        strncpy(hash_prefix, hash_str, 2);
        char path[1024];
        sprintf(path, ".git/objects/%s/%s", hash_prefix, hash_str + 2);
        char dir_to_write[64];
        sprintf(dir_to_write, ".git/objects/%s", hash_prefix);
        printf("%s", hash_str);
        mkdir(dir_to_write, 0755);
        FILE *fptr = fopen(path, "wb");
        if (fptr == NULL)
        {
            perror("an error occured");
            return 0;
        }
        fwrite(final_compr_file, sizeof(int), final_cmpr_len, fptr);
        return 0;
    }
    else if (strcmp(command, "ls-tree") == 0)
    {
        if (argc < 4)
        {
            fprintf(stderr, "too few params for the command\n");
            return 1;
        }
        const char *flag = argv[2];
        if (strcmp(flag, "--name-only") != 0)
        {
            fprintf(stderr, "Wrong flag\n");
            return 1;
        }
        const char *hash = argv[3];
        unsigned long int memory_size = 65536;
        char *contents_buffer = malloc(memory_size);
        int out_len = find_and_read_object(hash, contents_buffer);
        if (!out_len)
        {
            fprintf(stderr, "problem finding the file or decompressing");
            return 0;
        }
        printf("%s", contents_buffer);
    }
    else
    {
        fprintf(stderr, "Unknown command %s\n", command);
        return 1;
    }

    return 0;
}
