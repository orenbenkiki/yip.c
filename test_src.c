#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "yip.h"

#ifndef O_BINARY
#   define O_BINARY 0
#endif /* O_BINARY */

/* The input data. */
static const char *input_path = "-";
static char *input_data = NULL;
static long input_size = -1;
static FILE *input_fp = NULL;
static int input_fd = -1;

/* Abort execution with errno-based message. */
static void die(const char *where) {
    perror(where);
    exit(1);
}

/* Initialize input_fp according to input_path. */
static void set_input_fp() {
    if (input_fp != NULL) return;
    else if (!strcmp(input_path, "-")) {
        input_fp = stdin;
        return;
    } else {
        input_fp = fopen(input_path, "rb");
        if (input_fp == NULL) die("fopen");
    }
}

/* Initialize input_fd according to input_path. */
static void set_input_fd() {
    if (input_fd >= 0) return;
    else if (!strcmp(input_path, "-")) {
        input_fd = 0;
        return;
    } else {
        input_fd = open(input_path, O_RDONLY|O_BINARY);
        if (input_fd < 0) die("open");
    }
}

/* Initialize input_data (using input_fd). Will allocate memory to hold the
 * whole thing. */
static void set_input_data() {
    static const int BUFFER_SIZE = 8192;
    if (input_data != NULL) return;
    set_input_fd();
    input_size = 0;
    for (;;) {
        input_data = realloc(input_data, input_size + BUFFER_SIZE);
        if (!input_data) die("realloc");
        else {
            int size = read(input_fd, input_data + input_size, BUFFER_SIZE);
            if (size < 0) die("read");
            else if (size == 0) {
                input_data[input_size] = '\0';
                return;
            }
            input_size += size;
        }
    }
}

/* Free allocated input_data for valgrind etc. */
static void clear_input_data() {
    free(input_data);
    input_data = NULL;
}

/* Test reading from a source. Using weird sizes tries to ensure all sort of
 * cases are hit, at least when large files are read. */
static void test_source(YIP_SOURCE *source) {
    static const int MORE_SIZE = 543;
    static const int LESS_SIZE = 321;
    static const int KEEP_SIZE = 432;
    for (;;) {
        int status = source->more(source, MORE_SIZE);
        if (status < 0) die("yip_source_more");
        else if (status == 0) {
            if (write(1, source->buffer->begin, source->buffer->end - source->buffer->begin) < 0)
                die("write");
            if (source->close(source) < 0) die("close");
            return;
        }
        while (source->buffer->end - source->buffer->begin > KEEP_SIZE) {
            long less_size = LESS_SIZE;
            if (write(1, source->buffer->begin, less_size) < 0) die("write");
            if (source->less(source, less_size) < 0) die("yip_source_less");
        }
    }
}

/* Test buffer sources. */
static void test_buf() {
    YIP_SOURCE *source;
    set_input_data();
    source = yip_buffer_source(input_data, input_data + input_size);
    if (source == NULL) die("yip_buffer_source");
    test_source(source);
    clear_input_data();
}

/* Test string sources. If there is a zero byte in the file it will terminate
 * the source bytes. */
static void test_str() {
    YIP_SOURCE *source;
    set_input_data();
    source = yip_string_source(input_data);
    if (source == NULL) die("yip_string_source");
    test_source(source);
    clear_input_data();
}

/* Test file pointer sources. */
static void test_fp() {
    YIP_SOURCE *source;
    set_input_fp();
    source = yip_fp_source(input_fp, 1);
    if (source == NULL) die("yip_fp_source");
    test_source(source);
}

/* Test reading file descriptor sources. */
static void test_fdr() {
    YIP_SOURCE *source;
    set_input_fd();
    source = yip_fd_read_source(input_fd, 1);
    if (source == NULL) die("yip_fd_read_source");
    test_source(source);
}

/* Test mapping file descriptor sources. */
static void test_fdm() {
    YIP_SOURCE *source;
    set_input_fd();
    source = yip_fd_map_source(input_fd, 1);
    if (source == NULL) die("yip_fd_map_source");
    test_source(source);
}

/* Test "best attempt" file descriptor sources. */
static void test_fd() {
    YIP_SOURCE *source;
    set_input_fd();
    source = yip_fd_source(input_fd, 1);
    if (source == NULL) die("yip_fd_source");
    test_source(source);
}

/* Test "best attempt" file path sources. */
static void test_path() {
    YIP_SOURCE *source = yip_path_source(input_path);
    if (source == NULL) die("yip_path_source");
    test_source(source);
}

/* Aborts execution with a helpful message. */
static void usage() {
    fprintf(stderr, "Usage: test_src {str|buf|fp|fdr|fdm|fd|path} [path|-]\n");
    exit(1);
}

/* Invoke a single test according to command line arguments. */
int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 3) usage();
    if (argc == 3 && strcmp(argv[2], "-")) input_path = argv[2];
    if (!strcmp(argv[1], "str"))
        test_str();
    else if (!strcmp(argv[1], "buf"))
        test_buf();
    else if (!strcmp(argv[1], "fp"))
        test_fp();
    else if (!strcmp(argv[1], "fdr"))
        test_fdr();
    else if (!strcmp(argv[1], "fdm"))
        test_fdm();
    else if (!strcmp(argv[1], "fd"))
        test_fd();
    else if (!strcmp(argv[1], "path"))
        test_path();
    else
        usage();
    return 0;
}
