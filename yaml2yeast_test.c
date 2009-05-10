#include <alloca.h>
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "yip.h"

/* {{{ */

static int passed = 0;
static int failed = 0;
static int missing = 0;
static int unimplemented = 0;

static char *set_suffix(char *path, const char *suffix) {
    char *dot = path + strlen(path);
    while (*dot != '.') {
        dot--;
        assert(dot >= path);
    }
    strcpy(dot, suffix);
    return path;
}

/* }}} */
/* {{{ */

static YIP_SOURCE *read_path(char *path) {
    YIP_SOURCE *source = yip_path_source(path);
    if (!source) return NULL;
    else {
        int status;
        while ((status = source->more(source, 8192))) {
            if (status < 0) {
                perror(path);
                exit(1);
            }
        }
        return source;
    }
}

static int are_identical(YIP_SOURCE *left, YIP_SOURCE *right) {
    long left_size = left->end - left->begin;
    long right_size = right->end - right->begin;
    if (left_size != right_size) return 0;
    return !memcmp(left->begin, right->begin, left_size);
}

static void check_test_results(char *path) {
    YIP_SOURCE *output_src = read_path(set_suffix(path, ".output"));
    if (!output_src) {
        missing++;
        fprintf(stderr, "unknown: missing output\n");
        errno = 0;
        return;
    } else {
        YIP_SOURCE *error_src = read_path(set_suffix(path, ".error"));
        if (!are_identical(error_src, output_src)) {
            failed++;
            fprintf(stderr, "failed: unexpected output\n");
        } else {
            passed++;
            fprintf(stderr, "passed\n");
        }
        if (error_src->close(error_src) < 0) {
            perror(set_suffix(path, ".error"));
            exit(1);
        }
        if (output_src->close(output_src) < 0) {
            perror(set_suffix(path, ".output"));
            exit(1);
        }
    }
}

static void run_test_file(YIP *yip, char *path) {
    char *error_path = set_suffix(path, ".error");
    FILE *error_fp = fopen(error_path, "w");
    if (!error_fp) {
        perror(error_path);
        exit(1);
    }
    for (;;) {
        const YIP_TOKEN *token = yip_next_token(yip);
        if (token->code == YIP_DONE) break;
        fprintf(error_fp, "# B: %ld, C: %ld, L: %ld, c: %ld\n", token->byte_offset, token->char_offset, token->line, token->line_char);
        fputc(token->code, error_fp);
        if (token->begin) {
            unsigned const char *begin = token->begin;
            while (begin != token->end) {
                int code = yip_decode(token->encoding, &begin, token->end);
                assert(code >= 0);
                if (' ' <= code && (token->code == YIP_ERROR || code != '\\') && code <= '~') fputc(code, error_fp);
                else if (code <= 0xFF)   fprintf(error_fp, "\\x%02x", code);
                else if (code <= 0xFFFF) fprintf(error_fp, "\\u%04x", code);
                else                     fprintf(error_fp, "\\U%08x", code);
            }
        }
        fputc('\n', error_fp);
    }
    if (fclose(error_fp) < 0) {
        perror(error_path);
        exit(1);
    }
}

static char *next_dot(char *text) {
    assert(text);
    assert(*text);
    while (*text != '.') {
        text++;
        if (!*text) {
            errno = EFAULT;
            perror("next_dot");
            exit(1);
        }
    }
    *text++ = '\0';
    return text;
}

static YIP_PRODUCTION parse_file_name(char *file) {
    YIP_PRODUCTION production = { file, NULL, NULL, NULL };
    char *text = next_dot(file);
    if (!strncmp(text, "n=", 2)) {
        production.n = text + 2;
        text = next_dot(text + 2);
    }
    if (!strncmp(text, "c=", 2)) {
        production.c = text + 2;
        text = next_dot(text + 2);
    }
    if (!strncmp(text, "t=", 2)) {
        production.t = text + 2;
        text = next_dot(text + 2);
    }
    return production;
}

static void confirmed_test_file(char *path, char *file) {
    YIP_SOURCE *source = yip_path_source(path);
    if (!source) {
        perror(path);
        exit(1);
    } else {
        YIP_PRODUCTION production = parse_file_name(file);
        YIP *yip = yip_test(source, 1, &production);
        if (!yip) {
            unimplemented++;
            fprintf(stderr, "unknown: not implemented\n");
            return;
        }
        run_test_file(yip, path);
        if (yip_free(yip) < 0) {
            perror(file);
            exit(1);
        }
        check_test_results(path);
    }
}

static void candidate_test_file(const char *directory, char *file) {
    if (*file == '.' || strlen(file) < 6 || strcmp(file + strlen(file) - 6, ".input")) return;
    else {
        char *path = alloca(strlen(directory) + strlen(file) + 3);
        if (!path) {
            perror("alloca");
            exit(1);
        }
        sprintf(path, "%s/%s", directory, file);
        fprintf(stderr, "%s: ", path);
        confirmed_test_file(path, file);
    }
}

static void run_directory_tests(const char *path) {
    DIR *dir = opendir(path);
    struct dirent *entry;
    if (!dir) {
        perror(path);
        exit(1);
    }
    while ((entry = readdir(dir))) {
        candidate_test_file(path, entry->d_name);
    }
    if (errno || closedir(dir) < 0) {
        perror(path);
        exit(1);
    }
}

int main(int argc, char *argv[]) {
    int i;
    for (i = 1; i < argc; i++) run_directory_tests(argv[i]);
    printf("Total %d, passed %d, failed %d, missing %d, not implemented %d\n",
           passed + failed + missing + unimplemented,
           passed, failed, missing, unimplemented);
    return failed + missing + unimplemented;
}

/* }}} */
