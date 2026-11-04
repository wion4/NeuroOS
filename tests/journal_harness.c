/* Test-only boundary setup; no counter-setting API is linked into the kernel. */
#include "../kernel/journal.c"
void journal_test_seed(uint64_t sequence) {
    journal_reset();
    last_sequence = sequence;
}
