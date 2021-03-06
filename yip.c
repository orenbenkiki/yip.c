/*
 * Copyright (c) 2009 Oren Ben-Kiki
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "yip.h"

/* Isn't it lovely we all speak the same language? */
#ifndef O_BINARY
#   define O_BINARY 0
#endif /* O_BINARY */

/* Like sizeof but in elements. */
#define numof(ARRAY) (int)(sizeof(ARRAY) / sizeof(ARRAY[0]))

/* Works for everything with begin/end - both buffers and stacks. */
#define size_of(BUFFER) ((BUFFER)->end - (BUFFER)->begin)

/* Special values. */
#define INVALID_CODE  (-1000)
#define NO_CODE       (-2000)
#define NO_INDENT     (-3000)

/* Mask for start of line (always class 0). */
#define START_OF_LINE_MASK 1ll

/* {{{ */

/* Buffers: */

/* Byte sources: */

/* Return the offset beyond the last available byte of the source. */
static int end_offset(YIP_SOURCE *source) {
    return source->byte_offset + size_of(source->buffer);
}

/* Returns cast buffered byte source. Also asserts invariant always held by buffered byte sources. */
static YIP_SOURCE *source_invariant(const YIP_SOURCE *source) {
    assert(source);
    if (!source->buffer->begin) {
        assert(!source->byte_offset);
        assert(!source->buffer->end);
    } else {
        assert(source->byte_offset >= 0);
        assert(source->buffer->end);
        assert(size_of(source->buffer) >= 0);
    }
    return (YIP_SOURCE *)source;
}

/* Request more bytes from a buffered byte source. */
static int buffer_more(YIP_SOURCE *source, int size) {
    if (!source || size < 0) {
        errno = EINVAL;
        return -1;
    }
    source_invariant(source);
    return 0;
}

/* Release bytes from a buffered byte source. */
static int buffer_less(YIP_SOURCE *source, int size) {
    if (!source || size < 0) {
        errno = EINVAL;
        return -1;
    } else {
        source_invariant(source);
        if (size > size_of(source->buffer)) {
            errno = EINVAL;
            return -1;
        }
        source->byte_offset += size;
        source->buffer->begin += size;
        source_invariant(source);
        return size;
    }
}

/* Close buffered byte source. */
static int buffer_close(YIP_SOURCE *source) {
    if (!source) {
        errno = EINVAL;
        return -1;
    }
    source_invariant(source);
    free(source);
    return 0;
}

/* Return new buffered byte source. */
YIP_SOURCE *yip_buffer_source(const void *begin, const void *end) {
    if (!begin != !end || begin > end) {
        errno = EINVAL;
        return NULL;
    } else {
        YIP_SOURCE *source = calloc(1, sizeof(*source));
        source->more = buffer_more;
        source->less = buffer_less;
        source->close = buffer_close;
        source->buffer->begin = begin;
        source->buffer->end = end;
        source_invariant(source);
        return source;
    }
}

/* Return new string buffered byte source. */
YIP_SOURCE *yip_string_source(const char *string) {
    if (!string) return yip_buffer_source(NULL, NULL);
    else {
        YIP_SOURCE *source = calloc(1, sizeof(*source));
        source->more = buffer_more;
        source->less = buffer_less;
        source->close = buffer_close;
        source->buffer->begin = (const unsigned char *)string;
        source->buffer->end = (const unsigned char *)string + strlen(string);
        source_invariant(source);
        return source;
    }
}

/* }}} */

/* Dynamic buffer size increment. A good match for I/O operation size. */
static const int DYNAMIC_BUFFER_SIZE = 8192;

/* Dynamic buffered byte source. */
typedef struct DYNAMIC_SOURCE {
    YIP_SOURCE common[1];       /* Common members. */
    long size;                  /* Size of physical buffer. */
    const unsigned char *base;  /* Base of physical buffer. */
} DYNAMIC_SOURCE;

/* {{{ */

/* Returns cast dynamic buffered byte source. Also asserts invariant always held by dynamic buffered byte sources. */
static DYNAMIC_SOURCE *dynamic_invariant(const YIP_SOURCE *common) {
    DYNAMIC_SOURCE *source = (DYNAMIC_SOURCE *)source_invariant(common);
    if (!common->buffer->begin) {
        assert(!source->base);
        assert(!source->size);
    } else {
        assert(source->base);
        assert(source->base <= common->buffer->begin);
        assert(source->size >= common->buffer->end - source->base);
    }
    return source;
}

/* Make more space for bytes from a dynamic buffered byte source. Does not actually add the bytes, just makes room for them. */
static int dynamic_more(YIP_SOURCE *common, int size) {
    if (!common || size < 0) {
        errno = EINVAL;
        return -1;
    } else {
        DYNAMIC_SOURCE *source = dynamic_invariant(common);
        long used_size = common->buffer->end - source->base;
        long need_size = used_size + size;
        if (need_size <= source->size) {
            dynamic_invariant(common);
            return size;
        } else {
            long gap_size = common->buffer->begin - source->base;
            long need_buffers = (need_size + DYNAMIC_BUFFER_SIZE - 1) / DYNAMIC_BUFFER_SIZE;
            source->size = need_buffers * DYNAMIC_BUFFER_SIZE;
            source->base = realloc((void *)source->base, source->size);
            if (!source->base) return -1;
            common->buffer->begin = source->base + gap_size;
            common->buffer->end = source->base + used_size;
            dynamic_invariant(common);
            return size;
        }
    }
}

/* Release space from a dynamic buffered byte source. */
static int dynamic_less(YIP_SOURCE *common, int size) {
    if (!common || size < 0) {
        errno = EINVAL;
        return -1;
    } else {
        DYNAMIC_SOURCE *source = dynamic_invariant(common);
        long data_size = size_of(common->buffer);
        if (size > data_size) {
            errno = EINVAL;
            return -1;
        }
        common->buffer->begin += size;
        common->byte_offset += size;
        data_size -= size;
        /* Tricky: move data to start of buffer if it fits in the gap.
         * This allows using the faster memcpy (no overlap) and also ensures linear run-time costs. */
        if (common->buffer->begin - source->base >= data_size) {
            memcpy((void *)source->base, common->buffer->begin, data_size);
            common->buffer->begin = source->base;
            common->buffer->end = common->buffer->begin + data_size;
        }
        dynamic_invariant(common);
        return size;
    }
}

/* }}} */

/* STDIO byte source. Is an extension of a dynamic buffered data source. */
typedef struct FP_READ_SOURCE {
    DYNAMIC_SOURCE dynamic[1];  /* Dynamic byte source members. */
    FILE *fp;                   /* File pointer to read from. */
    int to_close;               /* Whether to automatically close fd. */
} FP_READ_SOURCE;


/* {{{ */
/* Returns cast fp read byte source. Also asserts invariant always held by fp read buffered byte sources. */
static FP_READ_SOURCE *fp_invariant(const YIP_SOURCE *common) {
    FP_READ_SOURCE *source = (FP_READ_SOURCE *)dynamic_invariant(common);
    assert(source->fp);
    return source;
}

/* Request more bytes for an fp read buffered byte source. */
static int fp_more(YIP_SOURCE *common, int size) {
    size = dynamic_more(common, size);
    if (size < 0) return -1;
    else {
        FP_READ_SOURCE *source = fp_invariant(common);
        size = fread((void *)common->buffer->end, 1, size, source->fp);
        if (size < 0) return -1;
        common->buffer->end += size;
        fp_invariant(common);
        return size;
    }
}

/* Close fp read buffered byte source. */
static int fp_close(YIP_SOURCE *common) {
    if (!common) {
        errno = EINVAL;
        return -1;
    } else {
        FP_READ_SOURCE *source = fp_invariant(common);
        FILE *fp = source->fp;
        int to_close = source->to_close;
        free((void *)source->dynamic->base);
        free(source);
        if (to_close) return fclose(fp);
        return 0;
    }
}

/* Return new fp read buffered byte source. */
YIP_SOURCE *yip_fp_source(FILE *fp, int to_close) {
    if (!fp) return yip_buffer_source(NULL, NULL);
    else {
        FP_READ_SOURCE *source = calloc(1, sizeof(*source));
        if (!source) return NULL;
        else {
            YIP_SOURCE *common = source->dynamic->common;
            common->more = fp_more;
            common->less = dynamic_less;
            common->close = fp_close;
            source->fp = fp;
            source->to_close = to_close;
            assert(fp_invariant(common) == source);
            return common;
        }
    }
}

/* }}} */

/* UNIX I/O byte source. Is an extension of a dynamic buffered data source. */
typedef struct FD_READ_SOURCE {
    DYNAMIC_SOURCE dynamic[1];  /* Dynamic byte source members. */
    int fd;                     /* File descriptor to read from. */
    int to_close;               /* Whether to automatically close fd. */
} FD_READ_SOURCE;

/* {{{ */

/* Returns cast fd read byte source. Also asserts invariant always held by fd read buffered byte sources. */
static FD_READ_SOURCE *fd_read_invariant(const YIP_SOURCE *common) {
    FD_READ_SOURCE *source = (FD_READ_SOURCE *)dynamic_invariant(common);
    assert(source->fd >= 0);
    return source;
}

/* Request more bytes for an fd read buffered byte source. */
static int fd_read_more(YIP_SOURCE *common, int size) {
    size = dynamic_more(common, size);
    if (size < 0) return -1;
    else {
        FD_READ_SOURCE *source = fd_read_invariant(common);
        size = read(source->fd, (void *)common->buffer->end, size);
        if (size < 0) return -1;
        common->buffer->end += size;
        fd_read_invariant(common);
        return size;
    }
}

/* Close fd read buffered byte source. */
static int fd_read_close(YIP_SOURCE *common) {
    if (!common) {
        errno = EINVAL;
        return -1;
    } else {
        FD_READ_SOURCE *source = fd_read_invariant(common);
        int fd = source->fd;
        int to_close = source->to_close;
        free((void *)source->dynamic->base);
        free(source);
        if (to_close) return close(fd);
        return 0;
    }
}

/* Return new fd read buffered byte source. */
YIP_SOURCE *yip_fd_read_source(int fd, int to_close) {
    if (fd < 0) return yip_buffer_source(NULL, NULL);
    else {
        FD_READ_SOURCE *source = calloc(1, sizeof(*source));
        if (!source) return NULL;
        else {
            YIP_SOURCE *common = source->dynamic->common;
            common->more = fd_read_more;
            common->less = dynamic_less;
            common->close = fd_read_close;
            source->fd = fd;
            source->to_close = to_close;
            assert(fd_read_invariant(common) == source);
            return common;
        }
    }
}

/* }}} */

/* UNIX mmap byte source. Is an extension of a static buffered data source. */
typedef struct FD_MMAP_SOURCE {
    YIP_SOURCE common[1];       /* Common members. */
    int fd;                     /* File descriptor to read from. */
    int to_close;               /* Whether to automatically close fd. */
} FD_MMAP_SOURCE;

/* {{{ */

/* Returns cast fd mmap byte source. Also asserts invariant always held by fd mmap buffered byte sources. */
static FD_MMAP_SOURCE *fd_mmap_invariant(const YIP_SOURCE *common) {
    FD_MMAP_SOURCE *source = (FD_MMAP_SOURCE *)source_invariant(common);
    assert(source->fd >= 0);
    return source;
}

/* Close fd mmap buffered byte source. */
static int fd_mmap_close(YIP_SOURCE *common) {
    if (!common) {
        errno = EINVAL;
        return -1;
    } else {
        FD_MMAP_SOURCE *source = fd_mmap_invariant(common);
        int fd = source->fd;
        int to_close = source->to_close;
        void *base = (void *)common->buffer->begin - common->byte_offset;
        long size = (void *)common->buffer->end - base;
        free(source);
        if (munmap(base, size) < 0) return -1;
        if (to_close) return close(fd);
        return 0;
    }
}

/* Return new fd map buffered byte source.
 * TODO: Also provide a Windows implementation. */
YIP_SOURCE *yip_fd_map_source(int fd, int to_close) {
    if (fd < 0) return yip_buffer_source(NULL, NULL);
    else {
        long size = lseek(fd, 0, SEEK_END);
        if (size < 0) return NULL;
        else {
            void *base = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
            if (base == MAP_FAILED) return NULL;
            else {
                FD_MMAP_SOURCE *source = calloc(1, sizeof(*source));
                if (!source) {
                    munmap(base, size);
                    return NULL;
                } else {
                    YIP_SOURCE *common = source->common;
                    common->more = buffer_more;
                    common->less = buffer_less;
                    common->close = fd_mmap_close;;
                    common->buffer->begin = base;
                    common->buffer->end = base + size;
                    source->fd = fd;
                    source->to_close = to_close;
                    assert(fd_mmap_invariant(common) == source);
                    return common;
                }
            }
        }
    }
}

/* Return new fd buffered byte source using mmap or read. */
YIP_SOURCE *yip_fd_source(int fd, int to_close) {
    int saved_errno = errno;
    YIP_SOURCE *source = yip_fd_map_source(fd, to_close);
    if (source) return source;
    errno = saved_errno;
    return yip_fd_read_source(fd, to_close);
}

/* Return new path buffered byte source using mmap or read. */
YIP_SOURCE *yip_path_source(const char *path) {
    if (!path) return yip_buffer_source(NULL, NULL);
    if (!strcmp(path, "-")) return yip_fd_source(0, 0);
    else {
        int fd = open(path, O_RDONLY|O_BINARY);
        return yip_fd_source(fd, 1);
    }
}

/* Name of each UTF encoding. */
static const char *encoding_names[] = {
    "UTF-8", "UTF-16LE", "UTF-16BE", "UTF-32LE", "UTF-32BE"
};

/* Returns name of the encoding (e.g. "UTF8"). */
const char *yip_encoding_name(YIP_ENCODING encoding) {
    if (encoding < 0 || numof(encoding_names) <= encoding) {
        errno = EINVAL;
        return NULL;
    }
    return encoding_names[encoding];
}

/* Deduce the encoding based on the first few input bytes */
static int detect_encoding(YIP_SOURCE *source) {
    if (source->more(source, 4) < 0) return -1;
    else {
        unsigned char byte_0    = (size_of(source->buffer) > 0 ? source->buffer->begin[0] : 0xAA);
        unsigned char byte_1    = (size_of(source->buffer) > 1 ? source->buffer->begin[1] : 0xAA);
        unsigned char byte_2    = (size_of(source->buffer) > 2 ? source->buffer->begin[2] : 0xAA);
        unsigned char byte_3    = (size_of(source->buffer) > 3 ? source->buffer->begin[3] : 0xAA);
        unsigned long byte_01   = (byte_0 << 8)  |  byte_1;
        unsigned long byte_012  = (byte_0 << 16) | (byte_1 << 8)  |  byte_2;
        unsigned long byte_123  =                  (byte_1 << 16) | (byte_2 << 8) | byte_3;
        unsigned long byte_0123 = (byte_0 << 24) | (byte_1 << 16) | (byte_2 << 8) | byte_3;
        if (byte_0123 == 0x0000FEFF) return YIP_UTF32BE;
        if (byte_012  == 0x000000  ) return YIP_UTF32BE;
        if (byte_0123 == 0xFFFE0000) return YIP_UTF32LE;
        if ( byte_123 ==   0x000000) return YIP_UTF32LE;
        if (byte_01   == 0xFEFF    ) return YIP_UTF16BE;
        if (byte_0    == 0x00      ) return YIP_UTF16BE;
        if (byte_01   == 0xFFFE    ) return YIP_UTF16LE;
        if ( byte_1   ==   0x00    ) return YIP_UTF16BE;
        if (byte_012  == 0xEFBBBF  ) return YIP_UTF8;
        else                         return YIP_UTF8;
    }
}

/* Returns type of a YEAST token code. */
YIP_CODE_TYPE yip_code_type(YIP_CODE code) {
    switch (code) {
    case YIP_BEGIN_ALIAS:
    case YIP_BEGIN_ANCHOR:
    case YIP_BEGIN_COMMENT:
    case YIP_BEGIN_DIRECTIVE:
    case YIP_BEGIN_DOCUMENT:
    case YIP_BEGIN_ESCAPE:
    case YIP_BEGIN_HANDLE:
    case YIP_BEGIN_MAPPING:
    case YIP_BEGIN_NODE:
    case YIP_BEGIN_PAIR:
    case YIP_BEGIN_PROPERTIES:
    case YIP_BEGIN_SCALAR:
    case YIP_BEGIN_SEQUENCE:
    case YIP_BEGIN_TAG:
        return YIP_BEGIN;
    case YIP_END_ALIAS:
    case YIP_END_ANCHOR:
    case YIP_END_COMMENT:
    case YIP_END_DIRECTIVE:
    case YIP_END_DOCUMENT:
    case YIP_END_ESCAPE:
    case YIP_END_HANDLE:
    case YIP_END_MAPPING:
    case YIP_END_NODE:
    case YIP_END_PAIR:
    case YIP_END_PROPERTIES:
    case YIP_END_SCALAR:
    case YIP_END_SEQUENCE:
    case YIP_END_TAG:
        return YIP_END;
    case YIP_BREAK:
    case YIP_DOCUMENT_END:
    case YIP_DOCUMENT_START:
    case YIP_INDENT:
    case YIP_INDICATOR:
    case YIP_LINE_FEED:
    case YIP_LINE_FOLD:
    case YIP_META:
    case YIP_TEXT:
    case YIP_UNPARSED:
    case YIP_WHITE:
        return YIP_MATCH;
    case YIP_BOM:
    case YIP_COMMENT:
    case YIP_DONE:
    case YIP_ERROR:
        return YIP_FAKE;
    default:
        errno = EINVAL;
        return -1;
    }
}

/* Returns the paired token code. */
YIP_CODE yip_code_pair(YIP_CODE code) {
    switch (code) {
    case YIP_BEGIN_ALIAS:       return YIP_END_ALIAS;
    case YIP_BEGIN_ANCHOR:      return YIP_END_ANCHOR;
    case YIP_BEGIN_COMMENT:     return YIP_END_COMMENT;
    case YIP_BEGIN_DIRECTIVE:   return YIP_END_DIRECTIVE;
    case YIP_BEGIN_DOCUMENT:    return YIP_END_DOCUMENT;
    case YIP_BEGIN_ESCAPE:      return YIP_END_ESCAPE;
    case YIP_BEGIN_HANDLE:      return YIP_END_HANDLE;
    case YIP_BEGIN_MAPPING:     return YIP_END_MAPPING;
    case YIP_BEGIN_NODE:        return YIP_END_NODE;
    case YIP_BEGIN_PAIR:        return YIP_END_PAIR;
    case YIP_BEGIN_PROPERTIES:  return YIP_END_PROPERTIES;
    case YIP_BEGIN_SCALAR:      return YIP_END_SCALAR;
    case YIP_BEGIN_SEQUENCE:    return YIP_END_SEQUENCE;
    case YIP_BEGIN_TAG:         return YIP_END_TAG;
    case YIP_END_ALIAS:         return YIP_BEGIN_ALIAS;
    case YIP_END_ANCHOR:        return YIP_BEGIN_ANCHOR;
    case YIP_END_COMMENT:       return YIP_BEGIN_COMMENT;
    case YIP_END_DIRECTIVE:     return YIP_BEGIN_DIRECTIVE;
    case YIP_END_DOCUMENT:      return YIP_BEGIN_DOCUMENT;
    case YIP_END_ESCAPE:        return YIP_BEGIN_ESCAPE;
    case YIP_END_HANDLE:        return YIP_BEGIN_HANDLE;
    case YIP_END_MAPPING:       return YIP_BEGIN_MAPPING;
    case YIP_END_NODE:          return YIP_BEGIN_NODE;
    case YIP_END_PAIR:          return YIP_BEGIN_PAIR;
    case YIP_END_PROPERTIES:    return YIP_BEGIN_PROPERTIES;
    case YIP_END_SCALAR:        return YIP_BEGIN_SCALAR;
    case YIP_END_SEQUENCE:      return YIP_BEGIN_SEQUENCE;
    case YIP_END_TAG:           return YIP_BEGIN_TAG;
    default:
        errno = EINVAL;
        return -1;
    }
}

/* }}} */

/* Typed stack. */
#define TYPEDEF_STACK(TYPE, NAME) \
typedef struct NAME { \
    TYPE *begin;  /* Pointer to bottom of stack. */ \
    TYPE *end;    /* Pointer beyond last allocated member. */ \
    TYPE *top;    /* Pointer to top element of stack or NULL. */ \
} NAME

/* Stack element function. */
typedef void (*STACK_FUNCTION)(void *data, void *element);

/* Size of element in a typed stack. */
#define STACK_TYPE_SIZE(STACK) sizeof(*(STACK)->begin)

/* Number of used elements in a (typed) stack. */
#define depth_of(STACK) (1 + (STACK)->top - (STACK)->begin)

/* Offset of top element in a stack. */
#define top_offset(STACK) ((char *)(STACK)->top - (char *)(STACK)->begin)

/* Untyped stack for generic code. */
TYPEDEF_STACK(void, VOID_STACK);

/* Apply function to all stack elements. */
#define stack_apply(STACK, FUNCTION, DATA) \
    generic_stack_apply((VOID_STACK *)(STACK), STACK_TYPE_SIZE(STACK), (STACK_FUNCTION)(FUNCTION), (void *)(DATA))
static void generic_stack_apply(const VOID_STACK *stack, size_t type_size, STACK_FUNCTION function, void *data) {
    void *element = stack->begin;
    while (element <= stack->top) {
        function(data, element);
        element += type_size;
    }
}

/* Asserts invariant always held by stacks. */
#define stack_invariant(STACK, INVARIANT, DATA) \
    generic_stack_invariant((VOID_STACK *)(STACK), STACK_TYPE_SIZE(STACK), (STACK_FUNCTION)(INVARIANT), (void *)(DATA))
static void generic_stack_invariant(const VOID_STACK *stack, size_t type_size, STACK_FUNCTION invariant, void *data) {
    assert(stack);
    assert(type_size > 0);
    assert(stack->begin);
    assert(stack->end);
    assert(stack->top);
    assert(stack->begin <= stack->top);
    assert(stack->top < stack->end);
    assert(!(size_of(stack) % type_size));
    assert(!(top_offset(stack) % type_size));
    if (invariant) generic_stack_apply(stack, type_size, invariant, data);
    else           assert(!data);
}

/* Initialize a stack with one allocated element. Returns 0 or -1 with errno. */
#define stack_init(STACK, SIZE) \
    generic_stack_init((VOID_STACK *)(STACK), STACK_TYPE_SIZE(STACK), (SIZE))
static int generic_stack_init(VOID_STACK *stack, size_t type_size, int init_size) {
    assert(init_size > 0);
    size_t size = init_size * type_size;
    stack->begin = malloc(size);
    if (!stack->begin) return -1;
    stack->end = stack->begin + size;
    stack->top = stack->begin;
    generic_stack_invariant(stack, type_size, NULL, NULL);
    return 0;
}

/* Release all resources used by a stack. */
#define stack_close(STACK) \
    generic_stack_close((VOID_STACK *)(STACK), STACK_TYPE_SIZE(STACK))
static void generic_stack_close(VOID_STACK *stack, size_t type_size) {
    generic_stack_invariant(stack, type_size, NULL, NULL);
    free(stack->begin);
}

/* Allocate space for a new uninitialized top element. Return 0 or -1 with errno. */
#define stack_push(STACK) \
    generic_stack_push((VOID_STACK *)(STACK), STACK_TYPE_SIZE(STACK))
static int generic_stack_push(VOID_STACK *stack, size_t type_size) {
    generic_stack_invariant(stack, type_size, NULL, NULL);
    stack->top += type_size;
    if (stack->top == stack->end) {
        int size = size_of(stack);
        stack->begin = realloc(stack->begin, 2 * size);
        if (!stack->begin) return -1;
        stack->top = stack->begin + size;
        stack->end = stack->begin + 2 * size;
    }
    generic_stack_invariant(stack, type_size, NULL, NULL);
    return 0;
}

/* Release top element. Must not release the bottom-most element. */
#define stack_pop(STACK) \
    generic_stack_pop((VOID_STACK *)(STACK), STACK_TYPE_SIZE(STACK))
static void generic_stack_pop(VOID_STACK *stack, size_t type_size) {
    generic_stack_invariant(stack, type_size, NULL, NULL);
    assert(top_offset(stack) >= 0);
    stack->top -= type_size;
    generic_stack_invariant(stack, type_size, NULL, NULL);
}

/* Release bottom elements. Must not release the top-most element.
#define stack_shift(STACK, ELEMENTS) \
    generic_stack_shift((VOID_STACK *)(STACK), STACK_TYPE_SIZE(STACK), ELEMENTS)
static void generic_stack_shift(VOID_STACK *stack, size_t type_size, int elements) {
    int shift_size = elements * type_size;
    int tail_size = top_offset(stack) + type_size - shift_size;
    generic_stack_invariant(stack, type_size, NULL, NULL);
    assert(shift_size >= 0);
    assert(shift_size >= type_size);
    assert(!(shift_size % type_size));
    assert(!(tail_size % type_size));
    memmove(stack->begin, stack->begin + shift_size, tail_size);
    stack->top = stack->begin + tail_size - type_size;
    generic_stack_invariant(stack, type_size, NULL, NULL);
} */

/* Return from an action/machine invocation. */
typedef enum RETURN {
    RETURN_ERROR = -1,  /* Unexpected error aborted action execution. */
    RETURN_DONE = 0,    /* Completed action execution created no token(s) to be returned to called. */
    RETURN_TOKEN = 1    /* Completed action execution created token(s) to be returned to caller. */
} RETURN;

/* Choice points. */
typedef enum CHOICE {
    CHOICE_ESCAPE,  /* Escape sequences top level choice point. */
    CHOICE_ESCAPED  /* Escape sequences nested choice point. */
} CHOICE;

/* Error message for each choice. */
static const char *choices_error[] = {
    "Commit to 'escape' was made outside it",
    "Commit to 'escaped' was made outside it"
};

/* Machine implementation. */
typedef RETURN (*MACHINE)(YIP *yip);

/* Generic struct type for looking machines up by name. */
typedef struct MACHINE_BY_NAME {
    const char *name; /* Machine (production) name. */
    MACHINE machine;  /* Machine (production) implementation. */
} MACHINE_BY_NAME;

/* An input character. */
typedef struct CHAR {
    YIP_TOKEN token[1];     /* Matched character as a token; code is Unicode point. */
    long long int mask;     /* 1 << character class of the character, or -1. */
} CHAR;

/* Stack frame for backtracking. */
typedef struct FRAME {
    CHAR prev[1];           /* Previous character. */
    CHAR curr[1];           /* Current character. */
    int tokens_depth;       /* Depth of tokens stack. */
    int codes_depth;        /* Depth of codes stack. */
} FRAME;

/* Stack of nested token codes. */
TYPEDEF_STACK(YIP_CODE, CODE_STACK);

/* Stack of backtracking states. */
TYPEDEF_STACK(FRAME, FRAME_STACK);

/* Stack of collected tokens. */
TYPEDEF_STACK(YIP_TOKEN, TOKEN_STACK);

/* YIP parser object. */
struct YIP {
    CODE_STACK codes[1];      /* Stack of nested tokens. */
    TOKEN_STACK tokens[1];    /* Stack of collected tokens. */
    FRAME_STACK frames[1];    /* Stack for backtracking. */
    MACHINE machine;          /* State machine implementation. */
    YIP_SOURCE *source;       /* Byte source to parse. */
    YIP_ENCODING encoding;    /* Detected source encoding. */
    int to_close;             /* Whether to automatically close source. */
    int did_see_eof;          /* Did the source report EOF? */
    int state;                /* Current state. */
    int next_return_token;    /* Next token to return to caller. */
    int i;                    /* Loops counter. */
    int n;                    /* Indentation level. */
};

/* Easy access of YIP members. */
#define Codes (yip->codes)
#define Code (*yip->codes->top)
#define Tokens (yip->tokens)
#define Token (yip->tokens->top)
#define Frames (yip->frames)
#define Frame (yip->frames->top)
#define Curr_char (yip->frames->top->curr)
#define Prev_char (yip->frames->top->prev)
#define Curr (yip->frames->top->curr->token)
#define Prev (yip->frames->top->prev->token)
#define Machine (yip->machine)
#define Source (yip->source)
#define Buffer (yip->source->buffer)
#define Encoding (yip->encoding)
#define To_close (yip->to_close)
#define Did_see_eof (yip->did_see_eof)
#define State (yip->state)
#define Next_return_token (yip->next_return_token)
#define I (yip->i)
#define N (yip->n)

#include "table.i"
#include "classify.i"

/* {{{ */

/* Assert invariant always held by tokens (or characaters posing as tokens). */
static void token_char_invariant(const YIP *yip, const YIP_TOKEN *token) {
    assert(token->byte_offset >= 0);
    if (token->code != NO_CODE) {
        assert(token->char_offset >= 0);
        assert(token->line >= 1);
        assert(token->line_char >= 0);
    }
    assert(token->byte_offset <= Source->byte_offset + size_of(Buffer));
    assert(token->char_offset <= token->byte_offset);
    assert(token->buffer->begin);
    if (token->buffer->begin == Curr->buffer->begin && token->code != NO_CODE) {
        assert(token->byte_offset == Curr->byte_offset);
        assert(token->char_offset == Curr->char_offset);
        assert(token->line == Curr->line);
        assert(token->line_char == Curr->line_char);
        assert(!size_of(token->buffer) || token->buffer->end == Curr->buffer->end);
        assert(token->encoding == Encoding);
    }
}

/* Assert invariant always held by tokens. */
static void token_invariant(const YIP *yip, const YIP_TOKEN *token) {
    token_char_invariant(yip, token);
    if (size_of(token->buffer)) assert(' ' < token->code && token->code <= '~');
    if (yip_code_type(token->code) != YIP_FAKE) {
        assert(token->byte_offset + size_of(token->buffer) <= Source->byte_offset + size_of(Source->buffer));
        assert(token->encoding == Encoding);
        assert(token->buffer->begin >= Source->buffer->begin);
        assert(token->buffer->end <= Source->buffer->end);
        assert(token->byte_offset - Source->byte_offset == token->buffer->begin - Source->buffer->begin);
    }
}

/* Assert invariant always held by characters posing as tokens. */
static void char_invariant(const YIP *yip, const YIP_TOKEN *token) {
    token_char_invariant(yip, token);
    if (!size_of(token->buffer)) assert(token->code == NO_CODE || token->code == EOF);
    else {
        assert(token->code >= 0 || token->code == INVALID_CODE);
        assert(token->byte_offset + size_of(token->buffer) <= Source->byte_offset + size_of(Source->buffer));
        assert(token->encoding == Encoding);
        assert(token->buffer->begin >= Source->buffer->begin);
        assert(token->buffer->end <= Source->buffer->end);
        assert(token->byte_offset - Source->byte_offset == token->buffer->begin - Source->buffer->begin);
    }
}

/* Assert invariant always held by stack frames. */
static void frame_invariant(const YIP *yip, const FRAME *frame) {
    const CHAR *curr_char = frame->curr;
    const CHAR *prev_char = frame->prev;
    const YIP_TOKEN *curr = curr_char->token;
    const YIP_TOKEN *prev = prev_char->token;
    char_invariant(yip, curr);
    char_invariant(yip, prev);
    if (curr->buffer->begin == prev->buffer->begin) {
        if (!size_of(curr->buffer)) assert(!size_of(prev->buffer));
        else if (size_of(prev->buffer)) assert(!memcmp(curr_char, prev_char, sizeof(*curr_char)));
    }
    if (frame == Frame) {
        assert(frame->tokens_depth == -1);
        assert(frame->codes_depth == -1);
    } else {
        assert(frame->tokens_depth > 0);
        assert(frame->codes_depth > 0);
        assert(frame->tokens_depth <= depth_of(Tokens));
        assert(frame->codes_depth <= depth_of(Codes));
    }
}

/*
#define HERE fprintf(stderr, "%s: %d: %s:\n", __FILE__, __LINE__, __FUNCTION__)
#define yip_invariant(Y) yip__invariant(Y, __FILE__, __LINE__, __FUNCTION__)
*/

/* Asserts invariant always held by parsers. */
static void yip_invariant(const YIP *yip/*, const char *file, int line, const char *function*/) {
    /*fprintf(stderr, "%s: %d: %s:", file, line, function);*/
    assert(yip);
    assert(Source);
    assert(Machine);
    source_invariant(Source);
    stack_invariant(Codes, NULL, NULL);
    stack_invariant(Tokens, token_invariant, yip);
    stack_invariant(Frames, frame_invariant, yip);
    if (yip_code_type(Token->code) != YIP_FAKE) assert(Token->buffer->end == Curr->buffer->begin);
    assert(Next_return_token <= depth_of(Tokens));
    assert(Next_return_token >= 0 || Token->code == YIP_UNPARSED || Token->code == Code);
    /*fprintf(stderr, " OK\n");*/
}

/* Consume the next byte from a buffer ending at "end" into a buffer starting at "*begin". */
static int next_byte(const unsigned char **begin, const unsigned char *end) {
    if (*begin >= end) return -1;
    else {
        int code = **begin;
        ++*begin;
        return code;
    }
}

/* Consume a UTF8 character from the buffer starting at "*begin" and ending at "end". */
int yip_decode_utf8(const unsigned char **begin, const unsigned char *end) {
    int code;
    int continuations;
    code = next_byte(begin, end);
    if (code < 0) return INVALID_CODE;
    if (!(code & 0x80)) return code;        /* 0xxxxxxx. */
    if ((code & 0xE0) == 0xC0) {            /* 110xxxxx 10xxxxxx X 1 */
        code &= 0x1F;
        continuations = 1;
    } else if ((code & 0xF0) == 0xE0) {     /* 1110xxxx 10xxxxxx X 2 */
        code &= 0xF;
        continuations = 2;
    } else if ((code & 0xF8) == 0xF0) {     /* 11110xxx 10xxxxxx X 3 */
        code &= 0x7;
        continuations = 3;
    } else if ((code & 0xFC) == 0xF8) {     /* 111110xx 10xxxxxx X 4 */
        code &= 0x3;
        continuations = 4;
    } else if ((code & 0xFE) == 0xFC) {     /* 1111110x 10xxxxxx X 5 */
        code &= 0x1;
        continuations = 5;
    } else return INVALID_CODE;
    while (continuations-- > 0) {
        if (*begin >= end) return INVALID_CODE;
        else {
            int next = next_byte(begin, end);
            if (next < 0) return INVALID_CODE;
            if ((next & 0xC0) != 0x80) return INVALID_CODE;
            code <<= 6;
            code |= next & 0x3F;
        }
    }
    return code;
}

/* Consume a UTF16LE character from the buffer starting at "*begin" and ending at "end". */
int yip_decode_utf16le(const unsigned char **begin, const unsigned char *end) {
    int code0 = next_byte(begin, end);
    if (code0 < 0) return INVALID_CODE;
    else {
        int code1 = next_byte(begin, end);
        if (code1 < 0) return INVALID_CODE;
        else {
            int code01 = code0 | code1 << 8;
            if (0xDC00 <= code01 && code01 < 0xE000) return INVALID_CODE;
            if (code01 < 0xD800 || 0xDC00 <= code01) return code01;
            else {
                int code2 = next_byte(begin, end);
                if (code2 < 0) return INVALID_CODE;
                else {
                    int code3 = next_byte(begin, end);
                    if (code3 < 0) return INVALID_CODE;
                    else {
                        int code23 = code2 | code3 << 8;
                        if (code23 < 0xDC00 || 0xE000 <= code23) return INVALID_CODE;
                        return (code01 << 10) + code23 + 0x10000 - (0xD800 << 10) - 0xDC00;
                    }
                }
            }
        }
    }
}

/* Consume a UTF16BE character from the buffer starting at "*begin" and ending at "end". */
int yip_decode_utf16be(const unsigned char **begin, const unsigned char *end) {
    int code0 = next_byte(begin, end);
    if (code0 < 0) return INVALID_CODE;
    else {
        int code1 = next_byte(begin, end);
        if (code1 < 0) return INVALID_CODE;
        else {
            int code01 = code0 << 8 | code1;
            if (0xDC00 <= code01 && code01 < 0xE000) return INVALID_CODE;
            if (code01 < 0xD800 || 0xDC00 <= code01) return code01;
            else {
                int code2 = next_byte(begin, end);
                if (code2 < 0) return INVALID_CODE;
                else {
                    int code3 = next_byte(begin, end);
                    if (code3 < 0) return INVALID_CODE;
                    else {
                        int code23 = code2 << 8 | code3;
                        if (code23 < 0xDC00 || 0xE000 <= code23) return INVALID_CODE;
                        return (code01 << 10) + code23 + 0x10000 - (0xD800 << 10) - 0xDC00;
                    }
                }
            }
        }
    }
}

/* Consume a UTF32LE character from the buffer starting at "*begin" and ending at "end". */
int yip_decode_utf32le(const unsigned char **begin, const unsigned char *end) {
    int code0 = next_byte(begin, end);
    if (code0 < 0) return INVALID_CODE;
    else {
        int code1 = next_byte(begin, end);
        if (code1 < 0) return INVALID_CODE;
        else {
            int code2 = next_byte(begin, end);
            if (code2 < 0) return INVALID_CODE;
            else {
                int code3 = next_byte(begin, end);
                if (code3 < 0) return INVALID_CODE;
                else {
                    return code0 | code1 << 8 | code2 << 16 | code3 << 24;
                }
            }
        }
    }
}

/* Consume a UTF32BE character from the buffer starting at "*begin" and ending at "end". */
int yip_decode_utf32be(const unsigned char **begin, const unsigned char *end) {
    int code0 = next_byte(begin, end);
    if (code0 < 0) return INVALID_CODE;
    else {
        int code1 = next_byte(begin, end);
        if (code1 < 0) return INVALID_CODE;
        else {
            int code2 = next_byte(begin, end);
            if (code2 < 0) return INVALID_CODE;
            else {
                int code3 = next_byte(begin, end);
                if (code3 < 0) return INVALID_CODE;
                else {
                    return code0 << 24 | code1 << 16 | code2 << 8 | code3;
                }
            }
        }
    }
}

/* Consume a Unicode character from the buffer starting at "*begin" and ending at "end". */
int yip_decode(YIP_ENCODING encoding, const unsigned char **begin, const unsigned char *end) {
    switch (encoding) {
    case YIP_UTF8:    return yip_decode_utf8(begin, end);
    case YIP_UTF16LE: return yip_decode_utf16le(begin, end);
    case YIP_UTF16BE: return yip_decode_utf16be(begin, end);
    case YIP_UTF32LE: return yip_decode_utf32le(begin, end);
    case YIP_UTF32BE: return yip_decode_utf32be(begin, end);
    default:          assert(0); errno = EINVAL; return INVALID_CODE;
    }
}

/* Data for rebasing buffers. */
typedef struct REBASE {
    YIP_BUFFER before[1]; /* Buffer before rebase. */
    YIP_BUFFER after[1];  /* Buffer after rebase. */
} REBASE;

static void rebase_invariant(const REBASE *rebase) {
    assert(rebase->before->begin != rebase->after->begin);
    assert(rebase->before->begin && rebase->before->end);
    assert(rebase->after->begin && rebase->after->end);
    assert(rebase->before->begin < rebase->before->end);
    assert(rebase->after->begin < rebase->after->end);
    assert(size_of(rebase->after) > size_of(rebase->before));
}

/* Move a relative pointer to "the same place" after a buffer was reallocated to a new address. */
static const unsigned char *rebase_pointer(const unsigned char *old_pointer, const REBASE *rebase) {
    const unsigned char *new_pointer = rebase->after->begin + (old_pointer - rebase->before->begin);
    assert(old_pointer);
    assert(new_pointer);
    assert(rebase->before->begin <= old_pointer && old_pointer <= rebase->before->end);
    assert(rebase->after->begin <= new_pointer && new_pointer <= rebase->after->end);
    return new_pointer;
}

/* Move token's relative pointers to "the same place" after the buffer was realocated to a new address. */
static void rebase_token(YIP_TOKEN *token, const REBASE *rebase) {
    token->buffer->begin = rebase_pointer(token->buffer->begin, rebase);
    token->buffer->end = rebase_pointer(token->buffer->end, rebase);
}

/* Move stack frame's relative pointers to "the same place" after the buffer was realocated to a new address. */
static void rebase_frame(FRAME *frame, const REBASE *rebase) {
    rebase_token(frame->curr->token, rebase);
    rebase_token(frame->prev->token, rebase);
}

/* Move to the next input character. */
static int next_char(YIP *yip) {
    static const int MAX_UTF_SIZE = 6; /* UTF8 goes up to 6 bytes. */
    REBASE rebase[1] = { { { { Source->buffer->begin, Source->buffer->end } }, { { NULL, NULL } } } };
    if (Curr->code != NO_CODE) yip_invariant(yip);
    if (Curr->code == EOF) return 0;
    assert(Token->buffer->end == Curr->buffer->begin);
    assert(Token->code != NO_CODE || Curr->code == NO_CODE);
    *Prev_char = *Curr_char;
    Curr->byte_offset += size_of(Curr->buffer);
    Curr->char_offset++;
    Curr->line_char++;
    Curr->buffer->begin = Curr->buffer->end;
    Token->buffer->end = Curr->buffer->begin;
    if (!Did_see_eof && Curr->byte_offset + MAX_UTF_SIZE > end_offset(Source) && Source->more(Source, DYNAMIC_BUFFER_SIZE) < 0) return -1;
    if (rebase->before->begin != Source->buffer->begin) {
        rebase->after->begin = Source->buffer->begin;
        rebase->after->end = Source->buffer->end;
        rebase_invariant(rebase);
        stack_apply(Tokens, rebase_token, rebase);
        stack_apply(Frames, rebase_frame, rebase);
    }
    if (Curr->buffer->begin == Source->buffer->end) {
        Did_see_eof = 1;
        Curr->code = EOF;
    } else {
        Curr->code = yip_decode(Encoding, &Curr->buffer->end, Source->buffer->end);
    }
    Curr_char->mask = code_mask(Curr->code);
    if ((Prev->code < 0 || Prev->code == 0xFFFF) && Prev_char->mask & START_OF_LINE_MASK) Curr_char->mask |= START_OF_LINE_MASK;
    if (Prev->code != NO_CODE) yip_invariant(yip);
    return 0;
}

/* Move to the previous character. */
/* TODO:
static void prev_char(YIP *yip) {
    yip_invariant(yip);
    assert(Prev->code != NO_CODE);
    *Curr_char = *Prev_char;
    Token->buffer->end = Curr->buffer->begin;
    yip_invariant(yip);
}
*/

/* Move to the next input line. */
static void next_line(YIP *yip) {
    Curr_char->mask |= START_OF_LINE_MASK;
    Curr->line_char = 0;
    Curr->line++;
}

/* Initialize YIP parser object. */
static YIP *yip_init(YIP_SOURCE *source, int to_close, MACHINE machine, const YIP_PRODUCTION *production) {
    if (!source || !machine) {
        errno = EINVAL;
        return NULL;
    } else {
        YIP *yip = (YIP *)calloc(sizeof(*yip), 1);
        if (!yip) return NULL;
        if ((Encoding = detect_encoding(source)) < 0) {
            errno = EILSEQ;
            yip_close(yip);
            return NULL;
        }
        Source = source;
        Machine = machine;
        To_close = to_close;
        Next_return_token = -1;
        I = NO_INDENT;
        N = production && production->n ? atoi(production->n) : NO_INDENT;
        Encoding = detect_encoding(Source);
        if (Encoding < 0
         || stack_init(Codes, production ? 1 : 128) < 0
         || stack_init(Tokens, production ? 1 : 128) < 0
         || stack_init(Frames, production ? 1 : 128) < 0) {
            yip_close(yip);
            return NULL;
        }
        Code = YIP_UNPARSED;
        Frame->tokens_depth = -1;
        Frame->codes_depth = -1;
        Curr->byte_offset = 0;
        Curr->char_offset = -1;
        Curr->line = 1;
        Curr->line_char = -1;
        Curr->buffer->begin = Curr->buffer->end = Source->buffer->begin;
        Curr->encoding = Encoding;
        Curr->code = NO_CODE;
        Curr_char->mask = START_OF_LINE_MASK;
        *Prev_char = *Curr_char;
        *Token = *Curr;
        if (next_char(yip) < 0) {
            yip_close(yip);
            return NULL;
        }
        *Token = *Curr;
        Token->code = YIP_UNPARSED;
        Token->buffer->end = Token->buffer->begin;
        yip_invariant(yip);
        return yip;
    }
}

/* Start collecting characters to a new token. */
static RETURN begin_token(YIP *yip, YIP_CODE code) {
    yip_invariant(yip);
    assert(Next_return_token < 0);
    assert(yip_code_type(code) == YIP_MATCH || code == YIP_BOM);
    if (stack_push(Codes) < 0) return RETURN_ERROR;
    Code = code;
    if (!size_of(Token->buffer)) {
        Token->code = code;
        yip_invariant(yip);
        return RETURN_DONE;
    }
    if (depth_of(Frames) == 1) {
        assert(depth_of(Tokens) == 1);
        Next_return_token = 0;
        yip_invariant(yip);
        return RETURN_TOKEN;
    }
    if (stack_push(Tokens) < 0) return RETURN_ERROR;
    *Token = *Curr;
    Token->buffer->end = Token->buffer->begin;
    Token->code = code;
    return RETURN_DONE;
}

/* End collecting characters to a token. */
static RETURN end_token(YIP *yip, YIP_CODE code) {
    yip_invariant(yip);
    assert(Next_return_token < 0);
    assert(code == Token->code || code == YIP_UNPARSED);
    if (depth_of(Codes) == 1) assert(Code == YIP_UNPARSED);
    else stack_pop(Codes);
    if (!size_of(Token->buffer)) {
        Token->code = Code;
        yip_invariant(yip);
        return RETURN_DONE;
    }
    Token->code = code;
    if (Token->code == YIP_BOM) {
        Token->buffer->begin = (const unsigned char *)encoding_names[Token->encoding] + 1;
        Token->buffer->end = Token->buffer->begin + strlen((const char *)Token->buffer->begin);
        Token->encoding = YIP_UTF8;
    }
    if (depth_of(Frames) == 1) {
        assert(depth_of(Tokens) == 1);
        Next_return_token = 0;
        yip_invariant(yip);
        return RETURN_TOKEN;
    }
    if (stack_push(Tokens) < 0) return RETURN_ERROR;
    *Token = *Curr;
    Token->buffer->end = Token->buffer->begin;
    Token->code = Code;
    yip_invariant(yip);
    return RETURN_DONE;
}

/* Return a fake token to the caller. */
static RETURN fake_token(YIP *yip, YIP_CODE code, const char *text) {
    yip_invariant(yip);
    assert(Next_return_token < 0);
    if (text) assert(yip_code_type(code) == YIP_FAKE);
    else      assert(code == YIP_DONE || yip_code_type(code) == YIP_BEGIN || yip_code_type(code) == YIP_END);
    if (size_of(Token->buffer)) {
        if (stack_push(Tokens) < 0) return RETURN_ERROR;
        *Token = *Curr;
        Token->buffer->end = Token->buffer->begin;
    }
    Token->code = code;
    if (text) {
        Token->buffer->begin = (const unsigned char *)text;
        Token->buffer->end = Token->buffer->begin + strlen(text);
    }
    if (depth_of(Frames) == 1) {
        assert(depth_of(Tokens) <= 2);
        Next_return_token = 0;
        yip_invariant(yip);
        return RETURN_TOKEN;
    }
    if (stack_push(Tokens) < 0) return RETURN_ERROR;
    *Token = *Curr;
    Token->buffer->end = Token->buffer->begin;
    Token->code = Code;
    yip_invariant(yip);
    return RETURN_DONE;
}

/* Return an empty token to the caller. */
static RETURN empty_token(YIP *yip, YIP_CODE code) {
    return fake_token(yip, code, NULL);
}

/* Return an error for an unexpected input character. */
static RETURN unexpected(YIP *yip) {
    static char buffer[24];
    if (Curr->code == INVALID_CODE) return fake_token(yip, YIP_ERROR, "Invalid byte sequence");
    if (Curr->code == EOF)          return fake_token(yip, YIP_ERROR, "Unexpected end of input");
    if (Curr->code == '\'')         return fake_token(yip, YIP_ERROR, "Unexpected \"'\"");
    if (' ' <= Curr->code && Curr->code <= '~') sprintf((char *)buffer, "Unexpected '%c'", Curr->code);
    else if (Curr->code <= 0xFF)                sprintf((char *)buffer, "Unexpected '\\x%02x'", Curr->code);
    else if (Curr->code <= 0xFFFF)              sprintf((char *)buffer, "Unexpected '\\u%04x'", Curr->code);
    else                                        sprintf((char *)buffer, "Unexpected '\\U%08x'", Curr->code);
    return fake_token(yip, YIP_ERROR, buffer);
}

/* Prevent further named backtracking. */
static RETURN commit(YIP *yip, CHOICE choice) {
    return fake_token(yip, YIP_ERROR, choices_error[choice]);
}

/* Push current state for backtracking. */
static int push_state(YIP *yip) {
    yip_invariant(yip);
    assert(!size_of(Token->buffer));
    assert(Token->code == YIP_UNPARSED);
    if (stack_push(Frames) < 0) return -1;
    Frame[0] = Frame[-1];
    Frame[-1].tokens_depth = depth_of(Tokens);
    Frame[-1].codes_depth = depth_of(Codes);
    yip_invariant(yip);
    return 0;
}

/* Update the state for backtracking. */
static RETURN set_state(YIP *yip) {
    yip_invariant(yip);
    assert(depth_of(Frames) > 1);
    assert(!size_of(Token->buffer));
    assert(Token->code == YIP_UNPARSED);
    Frame[-1] = Frame[0];
    Frame[-1].codes_depth = depth_of(Codes);
    if (depth_of(Frames) > 1 || depth_of(Tokens) == 1) {
        Frame[-1].tokens_depth = depth_of(Tokens);
        yip_invariant(yip);
        return RETURN_DONE;
    }
    Frame[-1].tokens_depth = 1;
    stack_pop(Tokens);
    Next_return_token = 0;
    yip_invariant(yip);
    return RETURN_TOKEN;
}

/* Backtrack to a previous state. */
static void reset_state(YIP *yip) {
    yip_invariant(yip);
    assert(!size_of(Token->buffer));
    assert(Token->code == YIP_UNPARSED);
    assert(depth_of(Frames) > 1);
    Frame[0] = Frame[-1];
    Codes->top = Codes->begin + Frame->codes_depth - 1;
    Token = Tokens->begin + Frame->tokens_depth - 1;
    *Token = *Curr;
    Token->buffer->end = Token->buffer->begin;
    Token->code = Code;
    Frame->tokens_depth = -1;
    Frame->codes_depth = -1;
    yip_invariant(yip);
}

/* End backtracking keeping the current state. */
static RETURN pop_state(YIP *yip) {
    yip_invariant(yip);
    assert(!size_of(Token->buffer));
    assert(Token->code == YIP_UNPARSED);
    assert(depth_of(Frames) > 1);
    Frame[-1] = Frame[0];
    Frame--;
    Frame->tokens_depth = -1;
    Frame->codes_depth = -1;
    if (depth_of(Frames) > 1 || depth_of(Tokens) == 1) {
        yip_invariant(yip);
        return RETURN_DONE;
    }
    stack_pop(Tokens);
    Next_return_token = 0;
    yip_invariant(yip);
    return RETURN_TOKEN;
}

/* Test whether current state has changed from pushed state. */
static int is_same_state(YIP *yip) {
    yip_invariant(yip);
    assert(depth_of(Frames) > 1);
    return Curr->buffer->begin == Frame[-1].curr->token->buffer->begin;
}

/* }}} */

#include "functions.i"
#include "by_name.i"

/* {{{ */

/* Locate production list by parameters. */
static const MACHINE_BY_NAME *machine_by_parameters(const YIP_PRODUCTION *production) {
    if (!production->n && !production->t) return machines;
    if ( production->n && !production->t) return machines_with_n;
    if (!production->n &&  production->t) return machines_with_t;
    if (production->n  &&  production->t) return machines_with_nt;
    errno = EINVAL;
    return NULL;
}

/* Locate production machine by name. */
static MACHINE machine_by_name(const MACHINE_BY_NAME *by_name, const char *name, const char *context) {
    int name_length = strlen(name);
    int context_length = context ? (int)strlen(context) : -1;
    for (; by_name->name; by_name++) {
        if (context) {
            int by_name_length = strlen(by_name->name);
            if (by_name_length == name_length + 1 + context_length
             && !strncmp(by_name->name, name, name_length)
             && !strcmp(by_name->name + name_length + 1, context)) {
                return by_name->machine;
            }
        } else if (!strcmp(by_name->name, name)) {
            return by_name->machine;
        }
    }
    return NULL;
}

/* Initialize a YIP parser object for a production with no parameters. */
YIP *yip_test(YIP_SOURCE *source, int to_close, const YIP_PRODUCTION *production) {
    const MACHINE_BY_NAME *by_name = machine_by_parameters(production);
    if (!by_name) {
        if (to_close) source->close(source);
        return NULL;
    } else {
        MACHINE machine = machine_by_name(by_name, production->name, production->c);
        if (!machine) {
            if (to_close) source->close(source);
            return NULL;
        }
        return yip_init(source, to_close, machine, production);
    }
}

/* Release a YIP parser object and release all allocated resources. */
int yip_close(YIP *yip) {
    YIP_SOURCE *source = Source;
    int to_close = To_close;
    yip_invariant(yip);
    stack_close(Codes);
    stack_close(Frames);
    stack_close(Tokens);
    free(yip);
    if (to_close) return source->close(source);
    return 0;
}

/* Return the next prepared token to the caller. */
static const YIP_TOKEN *next_token(YIP *yip) {
    const YIP_TOKEN *token = Tokens->begin + Next_return_token;
    yip_invariant(yip);
    assert(depth_of(Frames) == 1);
    assert(Next_return_token >= 0);
    if (token->code == YIP_DONE) return token;
    Next_return_token++;
    yip_invariant(yip);
    return token;
}

/* Reset after returning the last token. */
static void last_token(YIP *yip) {
    yip_invariant(yip);
    assert(depth_of(Frames) == 1);
    Next_return_token = -1;
    Token = Tokens->begin;
    *Token = *Curr;
    Token->buffer->end = Token->buffer->begin;
    Token->code = Code;
    yip_invariant(yip);
}

/* Return the next parsed token, or Null with errno. */
const YIP_TOKEN *yip_next_token(YIP *yip) {
    yip_invariant(yip);
    if (Next_return_token >= depth_of(Tokens)) last_token(yip);
    else if (Next_return_token >= 0) return next_token(yip);
    switch ((*Machine)(yip)) {
    case RETURN_ERROR:
        return NULL;
    case RETURN_TOKEN:
        return next_token(yip);
    default:
        assert(0);
        errno = EFAULT;
        return NULL;
    }
}

/* }}} */
