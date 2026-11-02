#include "abi.h"
#include "file.h"
#include "libc/errno.h"
#include "libc/fcntl.h"
#include "libc/stdio.h"
#include "libc/stdlib.h"
#include "libc/unistd.h"
int test_file_descriptors(void) {
    int fd = open("fd.txt", O_CREAT | O_TRUNC | O_RDWR, 0);
    if (fd < 3 || write(fd, "abc", 3) != 3 || lseek(fd, 1, SEEK_SET) != 1 || write(fd, "X", 1) != 1)
        return 81;
    char data[8] = {0};
    if (lseek(fd, 0, SEEK_SET) || read(fd, data, 8) != 3 || data[0] != 'a' || data[1] != 'X' ||
        data[2] != 'c')
        return 82;
    FILE *f = fdopen(fd, "wb");
    if (!f || fileno(f) != fd || fwrite("+", 1, 1, f) != 1 || fclose(f))
        return 83;
    if (read(fd, data, 1) != -1 || errno != EBADF || nos_file_size("fd.txt") != 4)
        return 84;
    fd = open("fd.txt", O_WRONLY | O_APPEND);
    if (fd < 0 || lseek(fd, 0, SEEK_SET) || write(fd, "!", 1) != 1 || close(fd))
        return 85;
    fd = open("/fd.txt", O_RDONLY);
    if (fd < 0 || fdopen(fd, "wb") || errno != EINVAL || read(fd, data, 8) != 5)
        return 86;
    if (data[0] != 'a' || data[1] != 'X' || data[2] != 'c' || data[3] != '+' || data[4] != '!' ||
        close(fd))
        return 87;
    int handles[8];
    for (unsigned i = 0; i < 8; ++i) {
        handles[i] = open("./fd.txt", O_RDONLY);
        if (handles[i] < 0)
            return 88;
    }
    if (open("fd.txt", O_WRONLY | O_TRUNC) != -1 || errno != EMFILE || nos_file_size("fd.txt") != 5)
        return 89;
    for (unsigned i = 0; i < 8; ++i)
        if (close(handles[i]))
            return 90;
    if (!getcwd(data, sizeof(data)) || data[0] != '/' || data[1] || getcwd(data, 1) ||
        errno != ERANGE)
        return 91;
    char *canonical = realpath("./fd.txt", 0);
    if (!canonical || canonical[0] != '/' || canonical[1] != 'f')
        return 92;
    fd = open(canonical, O_RDONLY);
    free(canonical);
    if (fd < 0 || close(fd) || unlink("fd.txt"))
        return 93;
    if (nos_file_size("fd.txt") != -ENOENT)
        return 94;
    say("C DESCRIPTOR TEST PASS\n");
    return 0;
}
