#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>
#include <errno.h>

int decompress(char *file_contents, char *raw_buffer)
{
    printf("%s", file_contents);
    // output buffer
    unsigned char output[4096];

    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    strm.next_in = file_contents;
    strm.avail_in = strlen(file_contents);

    strm.next_out = output;
    strm.avail_out = strlen(output);

    // initialize inflate
    if (inflateInit(&strm) != Z_OK)
    {
        printf("inflateInit failed\n");
        return 0;
    }

    int ret = inflate(&strm, Z_FINISH);
    printf("%s", output);
    if (ret != Z_STREAM_END)
    {
        printf("inflate failed: %d\n", ret);
        inflateEnd(&strm);
        return 0;
    }

    // null terminate if text
    output[strm.total_out] = '\0';

    strcpy(raw_buffer, output);

    printf("Decompressed:\n%s\n", output);

    inflateEnd(&strm);
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
            fprintf(stderr, "too few params for the command");
        }
        const char *flag = argv[2];
        if (!strcmp(flag, "-p") == 0)
        {
            fprintf(stderr, "Wrong flag");
            return 1;
        }
        const char *hash = argv[3];
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
        fseek(object_file, 0, SEEK_SET); /* same as rewind(f); */

        char *contents_buffer = malloc(fsize + 1);
        fread(contents_buffer, fsize, 1, object_file);
        fclose(object_file);

        char *raw_buffer = malloc(4096);
        decompress(contents_buffer, raw_buffer);
        printf("%s", raw_buffer);
        return 1;
    }
    else
    {
        fprintf(stderr, "Unknown command %s\n", command);
        return 1;
    }

    return 0;
}
