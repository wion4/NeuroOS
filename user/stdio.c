#include "libc/stdio.h"
#include "abi.h"
#include "file.h"
#include "heap.h"
#include "libc/errno.h"
#include "libc/fcntl.h"
#include "libc/unistd.h"
#include <limits.h>
/* Unbuffered streams: successful writes have already committed through the service.
 * There is one client thread; errno and stream state are not thread-local. */
struct nos_stream {
    char name[16];
    unsigned position;
    bool used, read, write, append, terminal, eof, error, push_decremented;
    int pushed;
};
static FILE streams[8];
static FILE consoles[3] = {{.used = true, .terminal = true, .pushed = EOF},
                           {.used = true, .write = true, .terminal = true, .pushed = EOF},
                           {.used = true, .write = true, .terminal = true, .pushed = EOF}};
FILE *stdin = &consoles[0], *stdout = &consoles[1], *stderr = &consoles[2];
int errno;
static int fail(FILE *s, int error) {
    errno = error;
    if (s)
        s->error = true;
    return EOF;
}
static bool mode_flags(const char *mode, bool *plus) {
    if (!mode || (mode[0] != 'r' && mode[0] != 'w' && mode[0] != 'a')) {
        errno = EINVAL;
        return false;
    }
    *plus = false;
    bool binary = false;
    for (unsigned i = 1; mode[i]; ++i) {
        if (mode[i] == '+' && !*plus)
            *plus = true;
        else if (mode[i] == 'b' && !binary)
            binary = true;
        else {
            errno = EINVAL;
            return false;
        }
    }
    return true;
}
FILE *fopen(const char *name, const char *mode) {
    bool plus;
    if (!mode_flags(mode, &plus))
        return 0;
    name = nos_file_path(name);
    if (!name) {
        errno = EINVAL;
        return 0;
    }
    unsigned length = 0;
    while (name[length] && length < 15)
        ++length;
    if (!length || name[length]) {
        errno = EINVAL;
        return 0;
    }
    FILE *s = 0;
    for (unsigned i = 0; i < 8; ++i)
        if (!streams[i].used) {
            s = &streams[i];
            break;
        }
    if (!s) {
        errno = EMFILE;
        return 0;
    }
    long size = nos_file_size(name);
    long e = 0;
    if (mode[0] == 'w' || (mode[0] == 'a' && size == -ENOENT))
        e = nos_file_write(name, 0, 0);
    else if (size < 0)
        e = size;
    if (e < 0) {
        errno = (int)-e;
        return 0;
    }
    *s = (FILE){.used = true,
                .read = mode[0] == 'r' || plus,
                .write = mode[0] != 'r' || plus,
                .append = mode[0] == 'a',
                .pushed = EOF};
    for (unsigned i = 0; i <= length; ++i)
        s->name[i] = name[i];
    if (s->append && size > 0)
        s->position = (unsigned)size;
    return s;
}
int fclose(FILE *s) {
    if (!s || !s->used)
        return fail(0, EBADF);
    s->used = false;
    return 0;
}
size_t fread(void *data, size_t size, size_t count, FILE *s) {
    if (!size || !count)
        return 0;
    if (!s || !s->used || !s->read) {
        fail(s, EBADF);
        return 0;
    }
    if (count > (size_t)-1 / size) {
        fail(s, EOVERFLOW);
        return 0;
    }
    size_t bytes = count * size, done = 0;
    if (s->pushed != EOF) {
        ((unsigned char *)data)[done++] = (unsigned char)s->pushed;
        s->pushed = EOF;
        if (s->push_decremented)
            ++s->position;
    }
    while (done < bytes) {
        unsigned chunk = bytes - done > FS_FILE_LIMIT ? FS_FILE_LIMIT : (unsigned)(bytes - done);
        long n = nos_file_read(s->name, s->position, (char *)data + done, chunk);
        if (n < 0) {
            fail(s, (int)-n);
            break;
        }
        s->position += (unsigned)n;
        done += (size_t)n;
        if ((unsigned)n < chunk) {
            s->eof = true;
            break;
        }
    }
    return done / size;
}
size_t fwrite(const void *data, size_t size, size_t count, FILE *s) {
    if (!size || !count)
        return 0;
    if (!s || !s->used || !s->write) {
        fail(s, EBADF);
        return 0;
    }
    if (count > (size_t)-1 / size) {
        fail(s, EOVERFLOW);
        return 0;
    }
    size_t bytes = size * count;
    if (s->terminal) {
        size_t done = 0;
        while (done < bytes) {
            size_t n = bytes - done > 128 ? 128 : bytes - done;
            long e = call3(6, (uintptr_t)((const char *)data + done), n, 0);
            if (e < 0) {
                fail(s, (int)-e);
                break;
            }
            done += n;
        }
        return done / size;
    }
    if (s->append) {
        long n = nos_file_size(s->name);
        if (n < 0) {
            fail(s, (int)-n);
            return 0;
        }
        s->position = (unsigned)n;
    }
    if (bytes > FS_FILE_LIMIT || s->position > FS_FILE_LIMIT - bytes) {
        fail(s, EFBIG);
        return 0;
    }
    long n = nos_file_pwrite(s->name, s->position, data, (unsigned)bytes);
    if (n < 0) {
        fail(s, (int)-n);
        return 0;
    }
    s->position += (unsigned)n;
    s->pushed = EOF;
    return (size_t)n / size;
}
int fseek(FILE *s, long offset, int origin) {
    if (!s || !s->used || s->terminal)
        return fail(s, EBADF);
    long base = 0;
    if (origin == SEEK_CUR)
        base = s->position;
    else if (origin == SEEK_END) {
        base = nos_file_size(s->name);
        if (base < 0)
            return fail(s, (int)-base);
    } else if (origin != SEEK_SET)
        return fail(s, EINVAL);
    if (offset < -base || offset > (long)UINT_MAX - base)
        return fail(s, EINVAL);
    s->position = (unsigned)(base + offset);
    s->eof = false;
    s->pushed = EOF;
    return 0;
}
long ftell(FILE *s) {
    if (!s || !s->used || s->terminal)
        return fail(s, EBADF);
    return s->position;
}
int feof(FILE *s) { return s && s->eof; }
int ferror(FILE *s) { return s && s->error; }
void clearerr(FILE *s) {
    if (s)
        s->error = s->eof = false;
}
void rewind(FILE *s) {
    (void)fseek(s, 0, SEEK_SET);
    clearerr(s);
}
int fflush(FILE *s) { return !s || s->used ? 0 : fail(s, EBADF); }
int fgetc(FILE *s) {
    unsigned char c;
    return fread(&c, 1, 1, s) == 1 ? c : EOF;
}
int fputc(int c, FILE *s) {
    unsigned char b = (unsigned char)c;
    return fwrite(&b, 1, 1, s) == 1 ? b : EOF;
}
int ungetc(int c, FILE *s) {
    if (!s || !s->used || !s->read || c == EOF || s->pushed != EOF)
        return EOF;
    s->pushed = (unsigned char)c;
    s->push_decremented = s->position != 0;
    if (s->push_decremented)
        --s->position;
    s->eof = false;
    return s->pushed;
}
char *fgets(char *buffer, int count, FILE *s) {
    if (count <= 0)
        return 0;
    int n = 0, c;
    while (n < count - 1 && (c = fgetc(s)) != EOF) {
        buffer[n++] = (char)c;
        if (c == '\n')
            break;
    }
    buffer[n] = 0;
    return n || count == 1 ? buffer : 0;
}
int fputs(const char *text, FILE *s) {
    size_t n = 0;
    while (text[n])
        ++n;
    return fwrite(text, 1, n, s) == n ? 0 : EOF;
}
int puts(const char *text) { return fputs(text, stdout) == EOF ? EOF : fputc('\n', stdout); }
static FILE *descriptor(int fd) {
    FILE *s = fd >= 0 && fd < 3 ? &consoles[fd] : fd >= 3 && fd < 11 ? &streams[fd - 3] : 0;
    if (!s || !s->used) {
        errno = EBADF;
        return 0;
    }
    return s;
}
int fileno(FILE *s) {
    if (s && s->used) {
        for (unsigned i = 0; i < 3; ++i)
            if (s == &consoles[i])
                return (int)i;
        for (unsigned i = 0; i < 8; ++i)
            if (s == &streams[i])
                return (int)i + 3;
    }
    errno = EBADF;
    return -1;
}
FILE *fdopen(int fd, const char *mode) {
    bool plus;
    if (!mode_flags(mode, &plus))
        return 0;
    FILE *s = descriptor(fd);
    if (!s)
        return 0;
    bool rd = mode[0] == 'r' || plus, wr = mode[0] != 'r' || plus;
    if ((rd && !s->read) || (wr && !s->write)) {
        errno = EINVAL;
        return 0;
    }
    s->read = rd;
    s->write = wr;
    s->append |= mode[0] == 'a';
    return s;
}
int open(const char *path, int flags, ...) {
    unsigned access = (unsigned)flags & 3;
    if (access == 3 || (flags & ~(3 | O_CREAT | O_TRUNC | O_APPEND)) ||
        ((flags & O_TRUNC) && access == O_RDONLY)) {
        errno = EINVAL;
        return -1;
    }
    /* Reserve a stream before touching the file, so EMFILE cannot truncate it. */
    bool available = false;
    for (unsigned i = 0; i < 8; ++i)
        if (!streams[i].used)
            available = true;
    if (!available) {
        errno = EMFILE;
        return -1;
    }
    long size = nos_file_size(path);
    if (size == -ENOENT && (flags & O_CREAT))
        size = nos_file_write(path, 0, 0);
    if (size < 0) {
        errno = (int)-size;
        return -1;
    }
    FILE *s = fopen(path, (flags & O_TRUNC) ? "w+b" : access == O_RDONLY ? "rb" : "r+b");
    if (!s)
        return -1;
    s->read = access != O_WRONLY;
    s->write = access != O_RDONLY;
    s->append = (flags & O_APPEND) != 0;
    return fileno(s);
}
ssize_t read(int fd, void *buffer, size_t bytes) {
    FILE *s = descriptor(fd);
    if (!s)
        return -1;
    if (bytes > LONG_MAX) {
        errno = EINVAL;
        return -1;
    }
    clearerr(s);
    size_t n = fread(buffer, 1, bytes, s);
    return !n && ferror(s) ? -1 : (ssize_t)n;
}
ssize_t write(int fd, const void *buffer, size_t bytes) {
    FILE *s = descriptor(fd);
    if (!s)
        return -1;
    if (bytes > LONG_MAX) {
        errno = EINVAL;
        return -1;
    }
    clearerr(s);
    size_t n = fwrite(buffer, 1, bytes, s);
    return !n && ferror(s) ? -1 : (ssize_t)n;
}
off_t lseek(int fd, off_t offset, int origin) {
    FILE *s = descriptor(fd);
    return !s || fseek(s, offset, origin) ? -1 : ftell(s);
}
int close(int fd) {
    FILE *s = descriptor(fd);
    return s ? fclose(s) : -1;
}
int unlink(const char *path) {
    struct fs_message q = {.op = FS_DELETE}, r;
    path = nos_file_path(path);
    unsigned n = 0;
    if (!path) {
        errno = EINVAL;
        return -1;
    }
    while (path[n] && n < 15) {
        q.name[n] = path[n];
        ++n;
    }
    if (!n || path[n]) {
        errno = EINVAL;
        return -1;
    }
    long e = nos_fs_request(&q, &r);
    if (e) {
        errno = (int)-e;
        return -1;
    }
    return 0;
}
char *getcwd(char *buffer, size_t bytes) {
    if (bytes < 2) {
        errno = ERANGE;
        return 0;
    }
    if (!buffer) {
        buffer = malloc(bytes);
        if (!buffer) {
            errno = ENOMEM;
            return 0;
        }
    }
    buffer[0] = '/';
    buffer[1] = 0;
    return buffer;
}
char *realpath(const char *path, char *resolved) {
    if (!path || !*path) {
        errno = EINVAL;
        return 0;
    }
    /* The current filesystem has one flat root, no directory traversal. */
    path = nos_file_path(path);
    if (!*path) {
        if (!resolved) {
            resolved = malloc(2);
            if (!resolved) {
                errno = ENOMEM;
                return 0;
            }
        }
        resolved[0] = '/';
        resolved[1] = 0;
        return resolved;
    }
    unsigned n = 0;
    while (path[n] && n < 15) {
        if (path[n] == '/') {
            errno = ENOENT;
            return 0;
        }
        ++n;
    }
    if (!n || path[n]) {
        errno = EINVAL;
        return 0;
    }
    long size = nos_file_size(path);
    if (size < 0) {
        errno = (int)-size;
        return 0;
    }
    if (!resolved) {
        resolved = malloc(n + 2);
        if (!resolved) {
            errno = ENOMEM;
            return 0;
        }
    }
    resolved[0] = '/';
    for (unsigned i = 0; i <= n; ++i)
        resolved[i + 1] = path[i];
    return resolved;
}
