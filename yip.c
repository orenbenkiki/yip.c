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

#define E_CT_
#define O_CT_
#define U_CT_
#define X_CT_

#ifndef O_BINARY
#   define O_BINARY 0
#endif /* O_BINARY */

/* Like sizeof but in elements. */
#define numof(ARRAY) (int)(sizeof(ARRAY) / sizeof(ARRAY[0]))

/* Works for anything with begin and end members. */
#define sizeoff(PTR)  ((PTR)->end - (PTR)->begin)

/* Works for anything with begin, end and byte_offset members. */
#define endoff(PTR)   ((PTR)->byte_offset + sizeoff(PTR))

/* Invalid values for enumerated types. */
#define NO_ENCODING  ((YIP_ENCODING)-1)
#define NO_CODE     ((YIP_CODE)-1)
#define EOF_CODE    ((YIP_CODE)-2)

/* {{{ */

/* Byte sources: */

/* Returns cast buffered byte source. Also asserts invariant always held by
 * buffered byte sources. */
static YIP_SOURCE *source_invariant(YIP_SOURCE *source) {
    assert(source);
    if (!source->begin) {
        assert(!source->byte_offset);
        assert(!source->end);
    } else {
        assert(source->byte_offset >= 0);
        assert(source->end);
        assert(source->end >= source->begin);
    }
    return source;
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
        if (size > sizeoff(source)) {
            errno = EINVAL;
            return -1;
        }
        source->byte_offset += size;
        source->begin += size;
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
        source->begin = begin;
        source->end = end;
        source_invariant(source);
        return source;
    }
}

/* Return new string buffered byte source. */
YIP_SOURCE *yip_string_source(const char *string) {
    if (!string) {
        errno = EINVAL;
        return NULL;
    } else {
        YIP_SOURCE *source = calloc(1, sizeof(*source));
        source->more = buffer_more;
        source->less = buffer_less;
        source->close = buffer_close;
        source->begin = (const unsigned char *)string;
        source->end = (const unsigned char *)string + strlen(string);
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

/* Returns cast dynamic buffered byte source. Also asserts invariant always
 * held by dynamic buffered byte sources. */
static DYNAMIC_SOURCE *dynamic_invariant(YIP_SOURCE *common) {
    DYNAMIC_SOURCE *source = (DYNAMIC_SOURCE *)source_invariant(common);
    if (!common->begin) {
        assert(!source->base);
        assert(!source->size);
    } else {
        assert(source->base);
        assert(source->base <= common->begin);
        assert(source->size >= common->end - source->base);
    }
    return source;
}

/* Make more space for bytes from a dynamic buffered byte source. Does not
 * actually add the bytes, just makes room for them. */
static int dynamic_more(YIP_SOURCE *common, int size) {
    if (!common || size < 0) {
        errno = EINVAL;
        return -1;
    } else {
        DYNAMIC_SOURCE *source = dynamic_invariant(common);
        long used_size = common->end - source->base;
        long need_size = used_size + size;
        if (need_size <= source->size) {
            dynamic_invariant(common);
            return size;
        } else {
            long gap_size = common->begin - source->base;
            long need_buffers = (need_size + DYNAMIC_BUFFER_SIZE - 1)
                              / DYNAMIC_BUFFER_SIZE;
            source->size = need_buffers * DYNAMIC_BUFFER_SIZE;
            source->base = realloc((void *)source->base, source->size);
            if (!source->base) {
                return -1;
            }
            common->begin = source->base + gap_size;
            common->end = source->base + used_size;
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
        long data_size = sizeoff(common);
        if (size > data_size) {
            errno = EINVAL;
            return -1;
        }
        common->begin += size;
        common->byte_offset += size;
        data_size -= size;
        /* Tricky: move data to start of buffer if it fits in the gap. This
         * allows using the faster memcpy (no overlap) and also ensures linear
         * run-time costs. */
        if (common->begin - source->base >= data_size) {
            memcpy((void *)source->base, common->begin, data_size);
            common->begin = source->base;
            common->end = common->begin + data_size;
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
/* Returns cast fp read byte source. Also asserts invariant always held by fp
 * read buffered byte sources. */
static FP_READ_SOURCE *fp_invariant(YIP_SOURCE *common) {
    FP_READ_SOURCE *source = (FP_READ_SOURCE *)dynamic_invariant(common);
    assert(source->fp);
    return source;
}

/* Request more bytes for an fp read buffered byte source. */
static int fp_more(YIP_SOURCE *common, int size) {
    size = dynamic_more(common, size);
    if (size < 0) {
        return -1;
    } else {
        FP_READ_SOURCE *source = fp_invariant(common);
        size = fread((void *)common->end, 1, size, source->fp);
        if (size < 0) {
            return -1;
        }
        common->end += size;
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
        if (to_close) {
            return fclose(fp);
        } else {
            return 0;
        }
    }
}

/* Return new fp read buffered byte source. */
YIP_SOURCE *yip_fp_source(FILE *fp, int to_close) {
    if (!fp) {
        errno = EINVAL;
        return NULL;
    } else {
        FP_READ_SOURCE *source = calloc(1, sizeof(*source));
        if (!source) {
            return NULL;
        }
        source->dynamic->common->more = fp_more;
        source->dynamic->common->less = dynamic_less;
        source->dynamic->common->close = fp_close;
        source->fp = fp;
        source->to_close = to_close;
        assert(fp_invariant(source->dynamic->common) == source);
        return source->dynamic->common;
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

/* Returns cast fd read byte source. Also asserts invariant always held by fd
 * read buffered byte sources. */
static FD_READ_SOURCE *fd_read_invariant(YIP_SOURCE *common) {
    FD_READ_SOURCE *source = (FD_READ_SOURCE *)dynamic_invariant(common);
    assert(source->fd >= 0);
    return source;
}

/* Request more bytes for an fd read buffered byte source. */
static int fd_read_more(YIP_SOURCE *common, int size) {
    size = dynamic_more(common, size);
    if (size < 0) {
        return -1;
    } else {
        FD_READ_SOURCE *source = fd_read_invariant(common);
        size = read(source->fd, (void *)common->end, size);
        if (size < 0) {
            return -1;
        }
        common->end += size;
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
        if (to_close) {
            return close(fd);
        } else {
            return 0;
        }
    }
}

/* Return new fd read buffered byte source. */
YIP_SOURCE *yip_fd_read_source(int fd, int to_close) {
    if (fd < 0) {
        errno = EINVAL;
        return NULL;
    } else {
        FD_READ_SOURCE *source = calloc(1, sizeof(*source));
        if (!source) {
            return NULL;
        }
        source->dynamic->common->more = fd_read_more;
        source->dynamic->common->less = dynamic_less;
        source->dynamic->common->close = fd_read_close;
        source->fd = fd;
        source->to_close = to_close;
        assert(fd_read_invariant(source->dynamic->common) == source);
        return source->dynamic->common;
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

/* Returns cast fd mmap byte source. Also asserts invariant always held by fd
 * mmap buffered byte sources. */
static FD_MMAP_SOURCE *fd_mmap_invariant(YIP_SOURCE *common) {
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
        void *base = (void *)common->begin - common->byte_offset;
        long size = (void *)common->end - base;
        free(source);
        if (munmap(base, size) < 0) {
            return -1;
        }
        if (to_close) {
            return close(fd);
        } else {
            return 0;
        }
    }
}

/* Return new fd map buffered byte source. TODO: Also provide a Windows
 * implementation. */
YIP_SOURCE *yip_fd_map_source(int fd, int to_close) {
    if (fd < 0) {
        errno = EINVAL;
        return NULL;
    } else {
        long size = lseek(fd, 0, SEEK_END);
        if (size < 0) {
            return NULL;
        } else {
            void *base = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
            if (base == MAP_FAILED) {
                return NULL;
            } else {
                FD_MMAP_SOURCE *source = calloc(1, sizeof(*source));
                if (!source) {
                    munmap(base, size);
                    return NULL;
                }
                source->common->more = buffer_more;
                source->common->less = buffer_less;
                source->common->close = fd_mmap_close;;
                source->common->begin = base;
                source->common->end = base + size;
                source->fd = fd;
                source->to_close = to_close;
                assert(fd_mmap_invariant(source->common) == source);
                return source->common;
            }
        }
    }
}

/* Return new fd buffered byte source using mmap or read. */
YIP_SOURCE *yip_fd_source(int fd, int to_close) {
    YIP_SOURCE *source = yip_fd_map_source(fd, to_close);
    if (source) {
        return source;
    }
    errno = 0;
    return yip_fd_read_source(fd, to_close);
}

/* Return new path buffered byte source using mmap or read. */
YIP_SOURCE *yip_path_source(const char *path) {
    int fd = open(path, O_RDONLY|O_BINARY);
    return yip_fd_source(fd, 1);
}

/* Name of each UTF encoding. */
static const char *encoding_names[] = {
    "UTF-8", "UTF-16LE", "UTF-16BE", "UTF-32LE", "UTF-32BE"
};

/* Returns name of the encoding (e.g. "UTF8"). */
const char *yip_encoding_name(YIP_ENCODING encoding) {
    if (encoding > numof(encoding_names)) {
        errno = EINVAL;
        return NULL;
    }
    return encoding_names[encoding];
}

/* Deduce the encoding based on the first few input bytes */
static int detect_encoding(YIP_SOURCE *source) {
    if (source->more(source, 4) < 0) {
        return NO_ENCODING;
    } else {
        unsigned char byte_0    = (sizeoff(source) > 0 ? source->begin[0] : 0xAA);
        unsigned char byte_1    = (sizeoff(source) > 1 ? source->begin[1] : 0xAA);
        unsigned char byte_2    = (sizeoff(source) > 2 ? source->begin[2] : 0xAA);
        unsigned char byte_3    = (sizeoff(source) > 3 ? source->begin[3] : 0xAA);
        unsigned long byte_01   = (byte_0 << 8)  |  byte_1;
        unsigned long byte_012  = (byte_0 << 16) | (byte_1 << 8)  |  byte_2;
        unsigned long byte_123  =                  (byte_1 << 16) | (byte_2 << 8) | byte_3;
        unsigned long byte_0123 = (byte_0 << 24) | (byte_1 << 16) | (byte_2 << 8) | byte_3;
        if      (byte_0123 == 0x0000FEFF) return YIP_UTF32BE;
        else if (byte_012  == 0x000000  ) return YIP_UTF32BE;
        else if (byte_0123 == 0xFFFE0000) return YIP_UTF32LE;
        else if ( byte_123 ==   0x000000) return YIP_UTF32LE;
        else if (byte_01   == 0xFEFF    ) return YIP_UTF16BE;
        else if (byte_0    == 0x00      ) return YIP_UTF16BE;
        else if (byte_01   == 0xFFFE    ) return YIP_UTF16LE;
        else if ( byte_1   ==   0x00    ) return YIP_UTF16BE;
        else if (byte_012  == 0xEFBBBF  ) return YIP_UTF8;
        else                              return YIP_UTF8;
    }
}

/* Returns type of a YEAST token code. */
YIP_CODE_TYPE yip_code_type(YIP_CODE code) {
    switch (code) {
    default:
        errno = EINVAL;
        return NO_CODE;
    case YIP_BEGIN_ESCAPE:
    case YIP_BEGIN_COMMENT:
    case YIP_BEGIN_DIRECTIVE:
    case YIP_BEGIN_TAG:
    case YIP_BEGIN_HANDLE:
    case YIP_BEGIN_ANCHOR:
    case YIP_BEGIN_PROPERTIES:
    case YIP_BEGIN_ALIAS:
    case YIP_BEGIN_SCALAR:
    case YIP_BEGIN_SEQUENCE:
    case YIP_BEGIN_MAPPING:
    case YIP_BEGIN_NODE:
    case YIP_BEGIN_PAIR:
    case YIP_BEGIN_DOCUMENT:
        return YIP_BEGIN;
    case YIP_END_ESCAPE:
    case YIP_END_COMMENT:
    case YIP_END_DIRECTIVE:
    case YIP_END_TAG:
    case YIP_END_HANDLE:
    case YIP_END_ANCHOR:
    case YIP_END_PROPERTIES:
    case YIP_END_ALIAS:
    case YIP_END_SCALAR:
    case YIP_END_SEQUENCE:
    case YIP_END_MAPPING:
    case YIP_END_NODE:
    case YIP_END_PAIR:
    case YIP_END_DOCUMENT:
        return YIP_END;
    case YIP_DONE:
    case YIP_BOM:
    case YIP_TEXT:
    case YIP_META:
    case YIP_BREAK:
    case YIP_LINE_FEED:
    case YIP_LINE_FOLD:
    case YIP_INDICATOR:
    case YIP_WHITE:
    case YIP_INDENT:
    case YIP_DOCUMENT_START:
    case YIP_DOCUMENT_END:
    case YIP_ERROR:
    case YIP_TEST:
        return YIP_MATCH;
    }
}

/* }}} */

/* Return from a machine invocation. */
typedef enum RETURN {
    RETURN_TOKEN,       /* To caller, then call again. */
    RETURN_UNEXPECTED,  /* Seen unexpected input. */
    RETURN_ERROR,       /* Unexpected error (I/O etc.) */
    RETURN_DONE         /* Completed machine execution. */
} RETURN;

/* Token generation status. */
typedef enum HAS_TOKEN {
    HAS_NO_TOKEN,         /* Have no (complete) token. */
    HAS_NEW_TOKEN,        /* Have complete token, not returned to caller yet. */
    HAS_OLD_TOKEN         /* Have complete token, which was returned to caller. */
  } HAS_TOKEN;

/* Choice points. */
typedef enum CHOICE {
    CHOICE_ESCAPE,        /* Escape sequences top level choice point. */
    CHOICE_ESCAPED        /* Escape sequences nested choice point. */
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

/* YIP parser object. */
struct YIP {
    YIP_SOURCE *source;   /* Byte source to parse. */
    int to_close;         /* Whether to automatically close source. */
    YIP_TOKEN token[1];   /* Current token being built. */
    YIP_TOKEN prev[1];    /* Previous character; code member is the class. */
    YIP_TOKEN match[1];   /* Current character; code member is the class. */
    YIP_CODE bom_class;   /* Class of byte order mark. */
    MACHINE machine;      /* Machine implementation. */
    HAS_TOKEN has_token;  /* Whether we have a complete token. */
    int choices;          /* TODO: Make into a stack. */
    int state;            /* Current state. */
    int did_see_eof;      /* Did the source report EOF? */
    int is_start_of_line; /* Is current the start of a line? */
    int is_test;          /* Automatically wrap in a test token? */
    int i;                /* Loops counter. */
    int n;                /* Indentation level. */
};

#include "table.i"
#include "classify.i"

/* {{{ */

static void token_invariant(YIP_SOURCE *source, YIP_TOKEN *token) {
    assert(token->byte_offset >= 0);
    assert(token->char_offset >= 0);
    assert(token->char_offset <= token->byte_offset);
    assert(token->line >= 1);
    assert(token->line_char >= 0);
    if (!token->begin) {
        assert(!token->end);
    } else {
        assert(token->end);
        if (token->code != YIP_BOM && token->code != YIP_ERROR) {
            assert(token->begin >= source->begin);
            assert(token->end <= source->end);
            assert(token->byte_offset == source->byte_offset + (token->begin - source->begin));
        }
    }
}

/* Asserts invariant always held by parsers. */
static void yip_invariant(YIP *yip) {
    assert(yip);
    assert(yip->source);
    source_invariant(yip->source);
    token_invariant(yip->source, yip->token);
    token_invariant(yip->source, yip->match);
    assert(yip->token->code == EOF_CODE || yip->token->code == NO_CODE || yip->token->code == YIP_DONE
        || (' ' < yip->token->code && yip->token->code <= '~'));
   if (yip->token->code != YIP_BOM && yip->token->code != YIP_ERROR) {
        assert(yip->match->byte_offset - yip->token->byte_offset == sizeoff(yip->token));
        assert(yip->token->encoding == yip->match->encoding);
    }
    if (yip->token->code == YIP_DONE) {
        assert(!yip->token->begin);
        assert(yip->token->byte_offset <= yip->source->byte_offset + sizeoff(yip->source));
    }
    if (!yip->match->begin) {
        assert(yip->match->code == NO_CODE);
        assert(!yip->match->byte_offset);
    }
    switch (yip->match->code) {
    case EOF_CODE:
        assert(yip->did_see_eof);
        assert(yip->match->byte_offset == yip->source->byte_offset + sizeoff(yip->source));
        assert(yip->match->begin);
        break;
    case NO_CODE:
        break;
    default:
        assert(yip->match->begin);
        assert(yip->match->begin < yip->match->end);
        break;
    }
    assert(yip->machine);
    if (!yip->is_test) {
        assert(yip->token->code != YIP_TEST);
    }
    if (!yip->did_see_eof) {
        assert(yip->match->code != EOF_CODE);
    }
    if (yip->has_token == HAS_NEW_TOKEN) {
        assert(yip->token->code != NO_CODE);
    }
}

static int next_byte(const unsigned char **begin, const unsigned char *end) {
    int code;
    assert(*begin < end);
    code = **begin;
    ++*begin;
    return code;
}

int yip_decode_utf8(const unsigned char **begin, const unsigned char *end) {
    int code;
    int continuations;
    if (*begin >= end) goto illegal;
    code = next_byte(begin, end);
    if (code < 0) {
        return -1;
    } else if ((code & 0x80) == 0) {           /* 0xxxxxxx. */
        return code;
    } else if ((code & 0xE0) == 0xC0) { /* 110xxxxx 10xxxxxx X 1 */
        code &= 0x1F;
        continuations = 1;
    } else if ((code & 0xF0) == 0xE0) { /* 1110xxxx 10xxxxxx X 2 */
        code &= 0xF;
        continuations = 2;
    } else if ((code & 0xF8) == 0xF0) { /* 11110xxx 10xxxxxx X 3 */
        code &= 0x7;
        continuations = 3;
    } else if ((code & 0xFC) == 0xF8) { /* 111110xx 10xxxxxx X 4 */
        code &= 0x3;
        continuations = 4;
    } else if ((code & 0xFE) == 0xFC) { /* 1111110x 10xxxxxx X 5 */
        code &= 0x1;
        continuations = 5;
    } else goto illegal;
    while (continuations-- > 0) {
        if (*begin >= end) goto illegal;
        else {
            int next = next_byte(begin, end);
            if ((next & 0xC0) != 0x80) goto illegal;
            code <<= 6;
            code |= next & 0x3F;
        }
    }
    return code;
illegal:
    errno = EILSEQ;
    return -1;
}

/* Decode the next single UTF16LE character. */
int yip_decode_utf16le(const unsigned char **begin, const unsigned char *end) {
    if (*begin + 2 <= end) {
        int code = next_byte(begin, end);
        code |= next_byte(begin, end) << 8;
        if (code < 0xDC00 || 0xE000 <= code) {
            if (code < 0xD800 || 0xDC00 <= code) {
                return code;
            } else if (*begin + 2 <= end) {
                int code2 = next_byte(begin, end);
                code2 |= next_byte(begin, end) << 8;
                if (0xDC00 <= code2 && code2 < 0xE000) {
                    code = (code << 10) + code2 + 0x10000 - (0xD800 << 10) - 0xDC00;
                    return code;
                }
            }
        }
    }
    errno = EILSEQ;
    return -1;
}

/* Decode the next single UTF16BE character. */
int yip_decode_utf16be(const unsigned char **begin, const unsigned char *end) {
    if (*begin + 2 <= end) {
        int code = next_byte(begin, end) << 8;
        code |= next_byte(begin, end);
        if (code < 0xDC00 || 0xE000 <= code) {
            if (code < 0xD800 || 0xDC00 <= code) {
                return code;
            } else if (*begin + 2 <= end) {
                int code2 = next_byte(begin, end) << 8;
                code2 |= next_byte(begin, end);
                if (0xDC00 <= code2 && code2 < 0xE000) {
                    code = (code << 10) + code2 + 0x10000 - (0xD800 << 10) - 0xDC00;
                    return code;
                }
            }
        }
    }
    errno = EILSEQ;
    return -1;
}

/* Decode the next single UTF32LE character. */
int yip_decode_utf32le(const unsigned char **begin, const unsigned char *end) {
    if (*begin + 4 <= end) {
        int code = next_byte(begin, end);
        code |= next_byte(begin, end) << 8;
        code |= next_byte(begin, end) << 16;
        code |= next_byte(begin, end) << 24;
        if (code >= 0) {
            return code;
        }
    }
    errno = EILSEQ;
    return -1;
}

/* Decode the next single UTF32BE character. */
int yip_decode_utf32be(const unsigned char **begin, const unsigned char *end) {
    if (*begin + 4 <= end) {
        int code = next_byte(begin, end) << 24;
        code |= next_byte(begin, end) << 16;
        code |= next_byte(begin, end) << 8;
        code |= next_byte(begin, end);
        if (code >= 0) {
            return code;
        }
    }
    errno = EILSEQ;
    return -1;
}

/* Decode the next character in a specified encoding. */
int yip_decode(YIP_ENCODING encoding, const unsigned char **begin, const unsigned char *end) {
    switch (encoding) {
    case YIP_UTF8:    return yip_decode_utf8(begin, end);
    case YIP_UTF16LE: return yip_decode_utf16le(begin, end);
    case YIP_UTF16BE: return yip_decode_utf16be(begin, end);
    case YIP_UTF32LE: return yip_decode_utf32le(begin, end);
    case YIP_UTF32BE: return yip_decode_utf32be(begin, end);
    default:          errno = EINVAL; return -1;
    }
}

/* Move a relative pointer to "the same place" after a buffer was reallocated
 * to a new address. */
static const unsigned char *rebase_pointer(const unsigned char *pointer,
                                           const unsigned char *old_begin,
                                           const unsigned char *old_end,
                                           const unsigned char *new_begin) {
    assert(pointer);
    assert(old_begin <= pointer && pointer < old_end);
    return new_begin + (pointer - old_begin);
}

/* Move token's relative pointers to "the same place" after the buffer was
 * realocated to a new address. */
static void rebase_token(YIP_TOKEN *token,
                         const unsigned char *old_begin,
                         const unsigned char *old_end,
                         YIP_SOURCE *source) {
    const unsigned char *new_begin = source->begin;
    assert(new_begin != old_begin);
    assert(token->begin);
    token->begin = rebase_pointer(token->begin, old_begin, old_end, new_begin);
    token->end = rebase_pointer(token->end, old_begin, old_end, new_begin);
    token_invariant(source, token);
}

/* Move to the next input character. */
static int next_char(YIP *yip) {
    static const int MAX_UTF_SIZE = 6; /* UTF8 goes up to 6. */
    const unsigned char *old_begin = yip->source->begin;
    const unsigned char *old_end = yip->source->end;
    yip_invariant(yip);
    *yip->prev = *yip->match;
    assert(yip->has_token == HAS_NO_TOKEN);
    if (yip->match->code == EOF_CODE) {
        return 0;
    }
    if (!yip->match->begin) { /* First time. */
        assert(yip->match->code == NO_CODE);
        assert(!yip->token->begin);
        yip->match->end = yip->source->begin;
        yip->match->begin = yip->source->begin;
        yip->token->end = yip->source->begin;
        yip->token->begin = yip->source->begin;
    } else { /* Have a character. */
        if (!yip->token->begin) {
            *yip->token = *yip->match;
            yip->token->code = NO_CODE;
        } else {
            assert(yip->token->end == yip->match->begin);
            yip->token->end = yip->match->end;
        }
        if (yip->match->code != yip->bom_class) {
            yip->is_start_of_line = 0;
        }
        yip->match->byte_offset += sizeoff(yip->match);
        yip->match->char_offset++;
        yip->match->line_char++;
        yip->match->begin = yip->match->end;
    }
    if (!yip->did_see_eof
     && endoff(yip->source) < yip->match->byte_offset + MAX_UTF_SIZE
     && yip->source->more(yip->source, DYNAMIC_BUFFER_SIZE) < 0) return -1;
    if (old_begin != yip->source->begin) {
        rebase_token(yip->token, old_begin, old_end, yip->source);
        rebase_token(yip->match, old_begin, old_end, yip->source);
    }
    if (yip->match->begin == yip->source->end) {
        yip->did_see_eof = 1;
        yip->match->code = EOF_CODE;
    } else {
        int code = yip_decode(yip->match->encoding, &yip->match->end, yip->source->end);
        if (code < 0) {
            return -1;
        }
        yip->match->code = char_class(code);
    }
    yip_invariant(yip);
    return 0;
}

/* Move to the previous character. */
static void prev_char(YIP *yip) {
    int code = yip->token->code;
    yip_invariant(yip);
    assert(yip->prev->code != NO_CODE);
    assert(yip->has_token == HAS_NO_TOKEN);
    *yip->token = *yip->match = *yip->prev;
    yip->token->code = code;
    yip->token->begin = yip->token->end = yip->match->begin;
    yip_invariant(yip);
}

/* Move to the next input line. */
static void next_line(YIP *yip) {
    yip->match->line++;
    yip->match->line_char = 0;
}

/* Initialize YIP parser object. */
static YIP *yip_init(YIP_SOURCE *source, int to_close,
                     MACHINE machine, const YIP_PRODUCTION *production) {
    if (!source || !machine) {
        errno = EINVAL;
        return NULL;
    } else {
        YIP *yip = (YIP *)malloc(sizeof(*yip));
        if (!yip) {
            return NULL;
        }
        else {
            yip->source = source;
            yip->to_close = to_close;
            yip->token->byte_offset = 0;
            yip->token->char_offset = 0;
            yip->token->line = 1;
            yip->token->line_char = 0;
            yip->token->begin = NULL;
            yip->token->end =  NULL;
            if ((yip->token->encoding = detect_encoding(source)) == NO_ENCODING) {
                errno = EILSEQ;
                free(yip);
                return NULL;
            }
            yip->token->code = NO_CODE;
            *yip->match = *yip->token;
            *yip->prev = *yip->token;
            yip->bom_class = char_class(0xFEFF);
            yip->machine = machine;
            yip->choices = 0;
            yip->state = 0;
            yip->did_see_eof = 0;
            yip->is_start_of_line = 1;
            yip->is_test = !!production;
            if (production) {
                if (production->n) {
                    yip->n = atoi(production->n);
                } else {
                    yip->n = -9999;
                }
            }
            yip->has_token = HAS_NO_TOKEN;
            if (next_char(yip) < 0) {
                free(yip);
                return NULL;
            }
            return yip;
        }
    }
}

static RETURN begin_token(YIP *yip, YIP_CODE code) {
    yip_invariant(yip);
    assert(yip->token->begin == yip->token->end);
    yip->token->code = code;
    yip_invariant(yip);
    return RETURN_DONE;
}

static RETURN end_token(YIP *yip, YIP_CODE code) {
    yip_invariant(yip);
    if (yip->has_token == HAS_OLD_TOKEN) {
        yip->has_token = HAS_NO_TOKEN;
        assert(yip->token->code == NO_CODE);
        return RETURN_DONE;
    }
    assert(yip->has_token == HAS_NO_TOKEN);
    assert(yip->token->code == code);
    if (yip->token->begin == yip->token->end) {
        yip->has_token = HAS_NO_TOKEN;
        yip->token->code = NO_CODE;
        return RETURN_DONE;
    }
    yip->has_token = HAS_NEW_TOKEN;
    if (yip->token->code == YIP_BOM) {
        yip->token->begin = (const unsigned char *)encoding_names[yip->token->encoding] + 1;
        yip->token->end = yip->token->begin + strlen((const char *)yip->token->begin);
        yip->token->encoding = YIP_UTF8;
    }
    yip_invariant(yip);
    return RETURN_TOKEN;
}

static RETURN empty_token(YIP *yip, YIP_CODE code) {
    yip_invariant(yip);
    if (yip->has_token == HAS_OLD_TOKEN) {
        yip->has_token = HAS_NO_TOKEN;
        assert(yip->token->code == NO_CODE);
        return RETURN_DONE;
    }
    assert(yip->has_token == HAS_NO_TOKEN);
    yip->has_token = HAS_NEW_TOKEN;
    yip->token->code = code;
    yip->token->begin = yip->token->end = NULL;
    yip_invariant(yip);
    return RETURN_TOKEN;
}

static void begin_choice(YIP *yip, CHOICE choice) {
    yip_invariant(yip);
    assert(!(yip->choices & (1 << choice)));
    yip->choices |= (1 << choice);
}

static RETURN end_choice(YIP *yip, CHOICE choice) {
    yip_invariant(yip);
    assert(yip->choices & (1 << choice));
    yip->choices &= ~(1 << choice);
    return RETURN_DONE;
}

static RETURN commit(YIP *yip, CHOICE choice) {
    yip_invariant(yip);
    if (yip->choices & (1 << choice)) {
        return RETURN_DONE;
    }
    if (yip->has_token == HAS_OLD_TOKEN) {
        yip->has_token = HAS_NO_TOKEN;
        assert(yip->token->code == NO_CODE);
        return RETURN_DONE;
    }
    assert(yip->has_token == HAS_NO_TOKEN);
    yip->has_token = HAS_NEW_TOKEN;
    yip->token->code = YIP_ERROR;
    yip->token->begin = (const unsigned char *)choices_error[choice];
    yip->token->end = yip->token->begin + strlen((const char *)yip->token->begin);
    yip->token->encoding = YIP_UTF8;
    yip_invariant(yip);
    return RETURN_TOKEN;
}

/* Fake states. */
#define STATE_DONE  -1  /* Return EOF tokens from now on. */

/* Complain about impossible loops ("never happens"). */
static int non_positive_n(YIP *yip) {
    static const unsigned char message[] = "Fewer than 0 repetitions";
    yip_invariant(yip);
    yip->token->code = YIP_ERROR;
    yip->token->begin = message;
    yip->token->end = yip->token->begin + sizeof(message) - 1;
    yip->token->encoding = YIP_UTF8;
    yip->state = STATE_DONE;
    yip->has_token = HAS_OLD_TOKEN;
    yip_invariant(yip);
    return RETURN_TOKEN;
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
            if (by_name_length == name_length + 3 + context_length
             && !strncmp(by_name->name, name, name_length)
             && !strcmp(by_name->name + name_length + 3, context)) {
                return by_name->machine;
            }
        } else if (!strcmp(by_name->name, name)) {
            return by_name->machine;
        }
    }
    return NULL;
}

/* Initialize a YIP parser object for a production with no parameters. */
YIP *yip_test(YIP_SOURCE *source, int to_close,
              const YIP_PRODUCTION *production) {
    const MACHINE_BY_NAME *by_name = machine_by_parameters(production);
    if (!by_name) {
        return NULL;
    } else {
        MACHINE machine = machine_by_name(by_name, production->name, production->c);
        if (!machine) {
          return NULL;
        } else {
          return yip_init(source, to_close, machine, production);
        }
    }
}

/* Release a YIP parser object and release all allocated resources. */
int yip_free(YIP *yip) {
    YIP_SOURCE *source = yip->source;
    int to_close = yip->to_close;
    yip_invariant(yip);
    free(yip);
    if (to_close) {
        return source->close(source);
    } else {
        return 0;
    }
}

/* Return all uncollected characters as a "test" token. */
static const YIP_TOKEN *unexpected(YIP *yip) {
    YIP_TOKEN tmp;
    yip_invariant(yip);
    tmp = *yip->match;
    if (yip->match->code != EOF_CODE && next_char(yip) < 0) {
        return NULL;
    }
    *yip->token = tmp;
    if (yip->token->code == EOF_CODE) {
        static const unsigned char message[] = "Unexpected end of input";
        yip->token->begin = message;
        yip->token->end = yip->token->begin + sizeof(message) - 1;
        yip->token->encoding = YIP_UTF8;
    }
    yip->token->code = YIP_ERROR;
    yip->state = STATE_DONE;
    yip->has_token = HAS_OLD_TOKEN;
    yip_invariant(yip);
    return yip->token;
}

static const YIP_TOKEN *done(YIP *yip) {
    yip_invariant(yip);
    assert(yip->state == STATE_DONE);
    assert(yip->has_token == HAS_NO_TOKEN);
    if (!yip->is_test) {
        assert(yip->token->end == yip->token->begin);
    }
    if (yip->token->begin != yip->token->end) {
        yip->token->code = YIP_TEST;
        yip->has_token = HAS_OLD_TOKEN;
    } else if (yip->match->code != EOF_CODE) {
        static const unsigned char message[] = "Expected end of input";
        yip->token->begin = message;
        yip->token->end = yip->token->begin + sizeof(message) - 1;
        yip->token->encoding = YIP_UTF8;
        yip->token->code = YIP_ERROR;
    } else {
        yip->token->code = YIP_DONE;
        yip->token->begin = yip->token->end = NULL;
    }
    yip_invariant(yip);
    return yip->token;
}

/* Return the next parsed token, or Null with errno. */
const YIP_TOKEN *yip_next_token(YIP *yip) {
    yip_invariant(yip);
    if (yip->has_token != HAS_NO_TOKEN) {
        *yip->token = *yip->match;
        yip->token->end = yip->token->begin;
        yip->token->code = NO_CODE;
        yip->has_token = yip->has_token == HAS_OLD_TOKEN ? HAS_NO_TOKEN : HAS_OLD_TOKEN;
        yip_invariant(yip);
    }
    if (yip->state == STATE_DONE) {
        yip->token->code = YIP_DONE;
        yip->token->begin = yip->token->end = NULL;
        yip_invariant(yip);
        return yip->token;
    }
    switch ((*yip->machine)(yip)) {
    case RETURN_ERROR:
        return NULL;
    case RETURN_TOKEN:
        yip_invariant(yip);
        return yip->token;
    case RETURN_UNEXPECTED:
        return unexpected(yip);
    case RETURN_DONE:
        return done(yip);
    default:
        assert(0);
        errno = EFAULT;
        return NULL;
    }
}

/* }}} */
