#include "../kernel/elf.h"
#include "abi.h"
#include "compiler-runtime.h"
#include "file.h"
#include "libc/errno.h"
#include "libc/stdio.h"
#include "libc/stdlib.h"
#include "libc/string.h"
#include "libc/unistd.h"
#include "libtcc.h"
static void diagnostic(void *unused, const char *message) {
    (void)unused;
    (void)fprintf(stderr, "%s\n", message);
}
int user_main(const char *arguments) {
    char input[16] = {0}, output[16] = {0};
    unsigned n = 0;
    while (*arguments == ' ')
        ++arguments;
    while (*arguments && *arguments != ' ' && n < 15)
        input[n++] = *arguments++;
    if (!n || (*arguments && *arguments != ' '))
        goto usage;
    while (*arguments == ' ')
        ++arguments;
    n = 0;
    while (*arguments && *arguments != ' ' && n < 15)
        output[n++] = *arguments++;
    while (*arguments == ' ')
        ++arguments;
    if (n < 5 || *arguments || strcmp(output + n - 4, ".nos") || !strcmp(input, output))
        goto usage;
    char temporary[16];
    for (n = 0; n < FS_FILE_COUNT; ++n) {
        (void)snprintf(temporary, sizeof(temporary), "cc-%u.tmp", n);
        if (nos_file_size(temporary) == -ENOENT)
            break;
    }
    if (n == FS_FILE_COUNT) {
        say("CC: no temporary file slot\n");
        return 2;
    }
    TCCState *state = tcc_new();
    if (!state) {
        say("CC: out of memory\n");
        return 3;
    }
    tcc_set_error_func(state, 0, diagnostic);
    int result = 1;
    if (tcc_set_options(state, "-nostdlib -nostdinc -static -Wl,-Ttext=0x400000") < 0 ||
        tcc_set_output_type(state, TCC_OUTPUT_EXE) < 0)
        goto done;
    if(tcc_add_include_path(state,".")<0 || tcc_add_sysinclude_path(state,".")<0)goto done;
    /* Minimal own startup. RDI carries the existing argument string to main;
     * programs may use int main(void) or int main(const char *arguments). */
    const char startup[] =
        "__asm__(\".text\\n.globl _start\\n_start:\\n"
        "xor %ebp,%ebp\\ncall main\\nmov %eax,%edi\\nmov $1,%eax\\nint $0x80\\nud2\\n\");";
    if (tcc_compile_string(state, runtime_source) < 0 || tcc_compile_string(state, startup) < 0 ||
        tcc_add_file(state, input) < 0)
        goto done;
    if(nos_file_size("libnos.a")>=0 && tcc_add_file(state,"libnos.a")<0)goto done;
    if(tcc_output_file(state,temporary)<0)goto done;
    long bytes = nos_file_size(temporary);
    if (bytes < 64 || bytes > FS_FILE_LIMIT) {
        say("CC: output exceeds current file limit\n");
        goto done;
    }
    unsigned char *program = malloc((size_t)bytes);
    if (!program) {
        say("CC: out of memory\n");
        goto done;
    }
    if (nos_file_read(temporary, 0, program, (unsigned)bytes) != bytes) {
        free(program);
        goto done;
    }
    program[7] = 0x4e;
    program[8] = 1;
    struct elf_image image;
    if (!elf_validate(program, (size_t)bytes, &image)) {
        free(program);
        say("CC: executable exceeds current loader limits\n");
        goto done;
    }
    long written = nos_file_write(output, program, (unsigned)bytes);
    free(program);
    if (written != bytes) {
        say("CC: cannot commit output\n");
        goto done;
    }
    (void)printf("COMPILED %s -> %s (%ld bytes)\n", input, output, bytes);
    result = 0;
done:
    tcc_delete(state);
    if (nos_file_size(temporary) >= 0)
        (void)unlink(temporary);
    return result;
usage:
    say("USAGE: cc source.c output.nos\n");
    return 2;
}
