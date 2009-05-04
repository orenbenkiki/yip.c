#ifndef YIP_H
#define YIP_H 1

/* Opaque source of bytes for parsing. Is actually the common vtable and
 * members that must appear at the start of each implementation struct,
 * followed by additional implementation-specific members. Each "method" must
 * be given the "this" pointer as the first argument. */
typedef struct YIP_SOURCE {

    /* Increase window by size (at the end). Returns actual increase or -1 with
     * errno. */
    int (*more)(struct YIP_SOURCE *source, int size);

    /* Decrease window by size (at the beginning). Returns actual decrease or
     * -1 with errno. */
    int (*less)(struct YIP_SOURCE *source, int size);

    /* Close the byte source and release all resources. Returns 0 or -1 with
     * errno. */
    int (*close)(struct YIP_SOURCE *source);

    /* The first available source byte. Do not modify! */
    const unsigned char *begin;

    /* One beyond the last availabe source byte. Do not modify! */
    const unsigned char *end;

    /* The offset of the byte begin points to. Do not modify! */
    long byte_offset;

} YIP_SOURCE;

/* Wrap a memory buffer as a source of bytes for parsing. Returns source or
 * NULL with errno. */
extern YIP_SOURCE *yip_buffer_source(const void *begin, const void *end);

/* Null terminated string as a source of bytes for parsing. Returns source or
 * NULL with errno. */
extern YIP_SOURCE *yip_string_source(const char *string);

/* File pointer as a source of bytes for parsing using stdio. Returns source of
 * NULL with errno. If to_close, fp will be closed on an explicit close. */
extern YIP_SOURCE *yip_fp_source(FILE *fp, int to_close);

/* File descriptor as a source of bytes for parsing using UNIX I/O. Returns
 * source of NULL with errno. If to_close, fd will be closed on an explicit
 * close. */
extern YIP_SOURCE *yip_fd_read_source(int fd, int to_close);

/* File descriptor as a source of bytes for parsing using memory map. Returns
 * source of NULL with errno. If to_close, fd will be closed on an explicit
 * close. */
extern YIP_SOURCE *yip_fd_map_source(int fd, int to_close);

/* File descriptor as a source of bytes for parsing using memory map if
 * possible or read otherwise. Returns source of NULL with errno. If to_close,
 * fd will be closed on an explicit close. */
extern YIP_SOURCE *yip_fd_source(int fd, int to_close);

/* File path as a source of bytes for parsing using memory map if possible or
 * read otherwise. Returns source of NULL with errno. */
extern YIP_SOURCE *yip_path_source(const char *path);

/* Encoding used in parsed bytes. */
typedef enum YIP_ENCODING {
    YIP_UTF8,
    YIP_UTF16LE, YIP_UTF16BE,
    YIP_UTF32LE, YIP_UTF32BE,
    YIP_ENCODING_SIGNED = -1  /* Force it to be signed. */
} YIP_ENCODING;

/* Returns name of the encoding (e.g. "UTF8"). */
extern const char *yip_encoding_name(YIP_ENCODING encoding);

/* Parsed YEAST token codes. */
typedef enum YIP_CODE {
    YIP_DONE             = '\0', /* Parsing is done. */
    YIP_BOM              = 'U',  /* Byte order mark. */
    YIP_TEXT             = 'T',  /* Content text. */
    YIP_META             = 't',  /* Non-content text. */
    YIP_BREAK            = 'b',  /* Non-content (separation) line break. */
    YIP_LINE_FEED        = 'L',  /* Line break normalized to line feed. */
    YIP_LINE_FOLD        = 'l',  /* Line break folded to content space. */
    YIP_INDICATOR        = 'I',  /* Character indicating structure. */
    YIP_WHITE            = 'w',  /* Non-content (separation) white space. */
    YIP_INDENT           = 'i',  /* Indentation spaces. */
    YIP_DOCUMENT_START   = 'K',  /* Document start marker. */
    YIP_DOCUMENT_END     = 'k',  /* Document end marker. */
    YIP_BEGIN_ESCAPE     = 'E',  /* Begins escape sequence. */
    YIP_END_ESCAPE       = 'e',  /* Ends escape sequence. */
    YIP_BEGIN_COMMENT    = 'C',  /* Begins comment. */
    YIP_END_COMMENT      = 'c',  /* Ends comment. */
    YIP_BEGIN_DIRECTIVE  = 'D',  /* Begins directive. */
    YIP_END_DIRECTIVE    = 'd',  /* Ends directive. */
    YIP_BEGIN_TAG        = 'G',  /* Begins tag. */
    YIP_END_TAG          = 'g',  /* Ends tag. */
    YIP_BEGIN_HANDLE     = 'H',  /* Begins tag handle. */
    YIP_END_HANDLE       = 'h',  /* Ends tag handle. */
    YIP_BEGIN_ANCHOR     = 'A',  /* Begins anchor. */
    YIP_END_ANCHOR       = 'a',  /* Ends anchor. */
    YIP_BEGIN_PROPERTIES = 'P',  /* Begins properties. */
    YIP_END_PROPERTIES   = 'p',  /* Ends properties. */
    YIP_BEGIN_ALIAS      = 'R',  /* Begins alias (reference) */
    YIP_END_ALIAS        = 'r',  /* Ends alias (reference) */
    YIP_BEGIN_SCALAR     = 'S',  /* Begins scalar content. */
    YIP_END_SCALAR       = 's',  /* Ends scalar content. */
    YIP_BEGIN_SEQUENCE   = 'Q',  /* Begins sequence content. */
    YIP_END_SEQUENCE     = 'q',  /* Ends sequence content. */
    YIP_BEGIN_MAPPING    = 'M',  /* Begins mapping content. */
    YIP_END_MAPPING      = 'm',  /* Ends mapping content. */
    YIP_BEGIN_NODE       = 'N',  /* Begins complete node. */
    YIP_END_NODE         = 'n',  /* Ends complete node. */
    YIP_BEGIN_PAIR       = 'X',  /* Begins mapping key:value pair. */
    YIP_END_PAIR         = 'x',  /* Ends mapping key:value pair. */
    YIP_BEGIN_DOCUMENT   = 'O',  /* Begins document. */
    YIP_END_DOCUMENT     = 'o',  /* Ends document. */
    YIP_ERROR            = '!',  /* Parsing error. */
    YIP_UNPARSED         = '-',  /* Unparsed text (due to error). */
    YIP_CODE_SIGNED      = -1    /* Force it to be signed. */
} YIP_CODE;

/* The of each YEAST token code. */
typedef enum YIP_CODE_TYPE {
    YIP_BEGIN,                    /* Token begins a group of tokens. */
    YIP_END,                      /* Token ends a group of tokens. */
    YIP_MATCH,                    /* Token matches some input characters. */
    YIP_FAKE,                     /* Token contains non-input characters. */
    YIP_CODE_TYPE_SIGNED = -1     /* Force it to be signed. */
} YIP_CODE_TYPE;

/* Returns type of a YEAST token code. */
extern YIP_CODE_TYPE yip_code_type(YIP_CODE code);

/* A single parsed token. */
typedef struct YIP_TOKEN {
    long byte_offset;           /* Zero based offset in bytes. */
    long char_offset;           /* Zero based offset in characters. */
    long line;                  /* One based line number. */
    long line_char;             /* Zero based character in line. */
    const unsigned char *begin; /* Pointer to first data byte, or NULL */
    const unsigned char *end;   /* Pointer after last data byte, or NULL */
    YIP_ENCODING encoding;      /* Encoding used in data bytes. */
    YIP_CODE code;              /* Parsed token code. */
} YIP_TOKEN;

/* Encapsulated YIP parser object. */
typedef struct YIP YIP;

/* Identification of a specific production. */
typedef struct YIP_PRODUCTION {
    const char *name; /* Production name. */
    const char *n;    /* Indentation argument or NULL. */
    const char *c;    /* Context argument or NULL. */
    const char *t;    /* Chomp argument of NULL. */
} YIP_PRODUCTION;

/* Initialize a YIP parser object for a production with no parameters. */
extern YIP *yip_test(YIP_SOURCE *source, int to_close,
                     const YIP_PRODUCTION *production);

/* Release a YIP parser object and release all allocated resources. */
extern int yip_free(YIP *yip);

/* Return the next parsed token, or Null with errno. */
extern const YIP_TOKEN *yip_next_token(YIP *yip);

/* Decode a single charcter from a buffer. The begin pointer is repositioned to
 * after the decoded bytes. Returns the unicode code point, or a negative
 * number if encountering an invalid byte sequence (further call may recover).
 * The caller is responsible for ensuring there is a sufficient number of bytes
 * available for this to work (6 bytes for UTF8, 4 bytes for UTF16 and UTF32).
 * TODO: Provide yip_decode_2, yip_decode_utf8_2, ... variants that return
 * 16-bit characters (using UTF16 surrogate pairs), to supports code that
 * doesn't recognize Unicode is a 32-bit character set. */
extern int yip_decode(YIP_ENCODING encoding,
                      const unsigned char **begin, const unsigned char *end);
extern int yip_decode_utf8(const unsigned char **begin, const unsigned char *end);
extern int yip_decode_utf16le(const unsigned char **begin, const unsigned char *end);
extern int yip_decode_utf16be(const unsigned char **begin, const unsigned char *end);
extern int yip_decode_utf32le(const unsigned char **begin, const unsigned char *end);
extern int yip_decode_utf32be(const unsigned char **begin, const unsigned char *end);


#endif /* YIP_H */
