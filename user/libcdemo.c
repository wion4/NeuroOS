extern int test_c_library(void);
int user_main(const char *arguments) {
    (void)arguments;
    return test_c_library();
}
