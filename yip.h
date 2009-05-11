#ifndef YIP_H
#define YIP_H 1

/**
 * @file
 *
 * @brief YAML Incremental Parser in C.
 */

/**
 * @mainpage
 *
 * @brief YAML Incremental Parser in C.
 *
 * @author Oren Ben-Kiki
 *
 * @version 1.0
 *
 * <b>Modules</b>:
 *
 * - @ref Buffers : Low-level memory #YIP_BUFFER.
 *
 * - @ref Sources : #YIP_SOURCE used as input for parsing.
 *
 * - @ref Tokens : #YIP_TOKEN used as output of parsing.
 *
 * - @ref Parsers : Opaque YIP parsers.
 *
 * <b>General issues:</b>
 *
 * - <em>Run-time errors</em>:
 *   This is a pure C library. Every function that can fail uses the return
 *   value to indicate failure, and sets errno to an appropriate value. Once an
 *   error is detected, all bets are off unless otherwise specified. That said,
 *   closing everything should probably be safe.
 *
 * - <em>Memory consumption</em>:
 *   After the initial creation of the parser, additional memory allocation is
 *   done either by the source or for the several internal stacks maintained in
 *   the parser. In practice, one can expect no memory operations at all once
 *   these stabilize to a sufficient size (which should be very quick), unless
 *   encountering a pathological input file. Testing with valgrind have shown
 *   no leaks or other problems.
 *
 * - <em>Thread safety</em>:
 *   No global variables are used (other than constants), so this should be as
 *   safe to use in a multi-thread environment as the standard C library. That
 *   said, multi-threading has not been tested.
 *
 * <b>License (MIT):</b>
 *
 *  Copyright (c) 2009 Oren Ben-Kiki
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a
 *  copy of this software and associated documentation files (the "Software"),
 *  to deal in the Software without restriction, including without limitation
 *  the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *  and/or sell copies of the Software, and to permit persons to whom the
 *  Software is furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 *  DEALINGS IN THE SOFTWARE.
 */

/**
 * @addtogroup Buffers
 *
 * @brief Low-level in-memory byte buffers.
 *
 * @{
 */

/**
 * @brief A (read only) buffer of bytes in memory.
 *
 * Buffers are specified using an STL-like begin->end pointers range. Buffers
 * are read-only for client code (and most of the implementation code).
 * Therefore the pointers are to const (unsigned) bytes.
 *
 * @see #YIP_SOURCE, #YIP_TOKEN
 */
typedef struct YIP_BUFFER {

    /**
     * @brief The first byte of the buffer.
     * This may but need not be NULL for an empty buffer.
     */
    const unsigned char *begin;

    /**
     * @brief One byte beyond the final available byte of the buffer.
     * This will be equal to begin for an empty buffer.
     */
    const unsigned char *end;
} YIP_BUFFER;

/**
 * @brief Encoding used inside buffers.
 *
 * This is a signed enum so negative values can be used to signify errors.
 *
 * @see #YIP_BUFFER, #YIP_SOURCE, #YIP_TOKEN
 * @see #yip_encoding_name
 */
typedef enum YIP_ENCODING {
    YIP_UTF8,       /**< UTF8 encoding. */
    YIP_UTF16LE,    /**< UTF16 little endian encoding. */
    YIP_UTF16BE,    /**< UTF16 big endian encoding. */
    YIP_UTF32LE,    /**< UTF32 little endian encoding. */
    YIP_UTF32BE,    /**< UTF32 big endian encoding. */
    YIP_ENCODING_SIGNED = -1 /* Force enum to be signed. */
} YIP_ENCODING;

/**
 * @brief Human-readable name of the encoding (e.g. "UTF8").
 *
 * @param encoding
 *    The encoding to fetch the name of.
 *
 * @return
 *    Human-readable name of the encoding (e.g. "UTF8"), or NULL (and sets
 *    errno) if given a bad encoding.
 *
 * @see #YIP_ENCODING
 */
extern const char *yip_encoding_name(YIP_ENCODING encoding);

/**
 * @brief Decode a single Unicode character.
 *
 * This is the low-level method used internally by the parser. It is also
 * provided to external callers who wish to decode the token's bytes.
 *
 * @param encoding
 *    The encoding to use for decoding the character.
 *
 * @param begin
 *    Pointer to the start of the bytes to decode. This pointer is always
 *    incremented to point past the decoded bytes, even if they are invalid.
 *    This allows the following call to recover.
 *
 * @param end
 *    Pointer beyond the last byte of available bytes.
 *
 * @return
 *    The Unicode point of the decoded character, or a negative value (and does
 *    NOT set errno) if encountering an invalid byte sequence.
 */
extern int yip_decode(YIP_ENCODING encoding, 
                      const unsigned char **begin, const unsigned char *end);

/**
 * @brief Decode a single UTF8 Unicode character.
 *
 * This is the low-level method used internally by the parser. It is also
 * provided to external callers who wish to decode the token's bytes.
 *
 * @param begin
 *    Pointer to the start of the bytes to decode. This pointer is always
 *    incremented to point past the decoded bytes, even if they are invalid.
 *    This allows the following call to recover.
 *
 * @param end
 *    Pointer beyond the last byte of available bytes.
 *
 * @return
 *    The Unicode point of the decoded character, or a negative value (and does
 *    NOT set errno) if encountering an invalid byte sequence.
 */
extern int yip_decode_utf8(const unsigned char **begin, const unsigned char *end);

/**
 * @brief Decode a single UTF16 little endian Unicode character.
 *
 * Note this will decode surrogate pairs into a single Unicode point.
 *
 * This is the low-level method used internally by the parser. It is also
 * provided to external callers who wish to decode the token's bytes.
 *
 * @param begin
 *    Pointer to the start of the bytes to decode. This pointer is always
 *    incremented to point past the decoded bytes, even if they are invalid.
 *    This allows the following call to recover.
 *
 * @param end
 *    Pointer beyond the last byte of available bytes.
 *
 * @return
 *    The Unicode point of the decoded character, or a negative value (and does
 *    NOT set errno) if encountering an invalid byte sequence.
 */
extern int yip_decode_utf16le(const unsigned char **begin, const unsigned char *end);

/**
 * @brief Decode a single UTF16 big endian Unicode character.
 *
 * Note this will decode surrogate pairs into a single Unicode point.
 *
 * This is the low-level method used internally by the parser. It is also
 * provided to external callers who wish to decode the token's bytes.
 *
 * @param begin
 *    Pointer to the start of the bytes to decode. This pointer is always
 *    incremented to point past the decoded bytes, even if they are invalid.
 *    This allows the following call to recover.
 *
 * @param end
 *    Pointer beyond the last byte of available bytes.
 *
 * @return
 *    The Unicode point of the decoded character, or a negative value (and does
 *    NOT set errno) if encountering an invalid byte sequence.
 */
extern int yip_decode_utf16be(const unsigned char **begin, const unsigned char *end);

/**
 * @brief Decode a single UTF32 little endian Unicode character.
 *
 * This is the low-level method used internally by the parser. It is also
 * provided to external callers who wish to decode the token's bytes.
 *
 * @param begin
 *    Pointer to the start of the bytes to decode. This pointer is always
 *    incremented to point past the decoded bytes, even if they are invalid.
 *    This allows the following call to recover.
 *
 * @param end
 *    Pointer beyond the last byte of available bytes.
 *
 * @return
 *    The Unicode point of the decoded character, or a negative value (and does
 *    NOT set errno) if encountering an invalid byte sequence.
 */
extern int yip_decode_utf32le(const unsigned char **begin, const unsigned char *end);

/**
 * @brief Decode a single UTF32 big endian Unicode character.
 *
 * This is the low-level method used internally by the parser. It is also
 * provided to external callers who wish to decode the token's bytes.
 *
 * @param begin
 *    Pointer to the start of the bytes to decode. This pointer is always
 *    incremented to point past the decoded bytes, even if they are invalid.
 *    This allows the following call to recover.
 *
 * @param end
 *    Pointer beyond the last byte of available bytes.
 *
 * @return
 *    The Unicode point of the decoded character, or a negative value (and does
 *    NOT set errno) if encountering an invalid byte sequence.
 */
extern int yip_decode_utf32be(const unsigned char **begin, const unsigned char *end);

/**
 * @}
 */

/**
 * @addtogroup Sources
 *
 * @brief Abstract data type for byte sources for parsing.
 *
 * @{
 */

/**
 * @brief Opaque source of bytes for parsing.
 *
 * Since C has no built-in notion of an ADT or an object, this contains the
 * common vtable members that must appear at the start of each implementation
 * struct. Do not directly modify any of the members.
 *
 * To invoke a "method", pass the "this" pointer as the first argument, e.g.:
 *
 * @code
 *    int result = source->more(source, size);
 * @endcode
 *
 * Specific implementations may use additional members, by including
 * #YIP_SOURCE as the first data member. For example, memory mapped byte
 * sources are implemented using the following:
 *
 * @code
 *    typedef struct FD_MMAP_SOURCE {
 *        YIP_SOURCE common[1];
 *        int fd;
 *        int to_close;
 *    } FD_MMAP_SOURCE;
 * @endcode
 *
 * This allows wrapping almost anything as a byte source. Implementation is
 * provided for wrapping memory, stdio files pointers and UNIX file descriptors
 * as byte sources.
 *
 * @see #YIP_BUFFER
 * @see #yip_buffer_source, #yip_string_source, #yip_fp_source,
 *      #yip_fd_read_source, #yip_fd_map_source, #yip_path_source
 */
typedef struct YIP_SOURCE {

    /**
     * @brief Increase #buffer by size (at the end).
     *
     * How the additional bytes are fetched depends on the specific
     * implementation.
     * 
     * Note this may relocate the #buffer.
     *
     * @param source
     *    The source to ask for #more bytes of.
     *
     * @param size
     *    The (non-negative) number of bytes to ask for.
     *
     * @return
     *    The number of additional bytes fetched, 0 if there are no #more bytes
     *    to fetch (reached EOF), or a negative value (and sets errno) if some
     *    error occured.
     */
    int (*more)(struct YIP_SOURCE *source, int size);

    /**
     * @brief Decrease #buffer by size (at the beginning).
     *
     * Depending on the implementation, this may release resources (in
     * particular, memory) used to hold the no-longer-needed bytes.
     *
     * Note this may relocate the #buffer.
     *
     * @param source
     *    The source to ask for #less bytes of.
     *
     * @param size
     *    The (non-negative) number of bytes to release.
     *
     * @return
     *    The number of unneeded bytes released, or a negative value (and sets
     *    errno) if some error occured.
     */
    int (*less)(struct YIP_SOURCE *source, int size);

    /**
     * @brief Close the byte source and release all resources.
     *
     * This should be invoked at the end of processing to avoid a resource
     * leak. It should be safe to invoke even if an error occured.
     *
     * @param source
     *    The source to release. No further use of this should be made.
     *
     * @return
     *    Zero if all is well, or a negative value (and sets errno) if some
     *    error occured.
     */
    int (*close)(struct YIP_SOURCE *source);

    /**
     * @brief Currently available source bytes.
     *
     * In general, this is a subset of the overall source bytes.
     * It is expected to be kept at a reasonable size by calling #more and #less
     * to "slide" the #buffer along.
     *
     * Under no circumstances should this #buffer or its content be modified by
     * the caller.
     */
    YIP_BUFFER buffer[1];

    /**
     * @brief The offset of the first #buffer byte.
     *
     * This is zero-based offset, in bytes, of the first #buffer byte compared
     * to the first actual source byte. Note that this is meaningful even if
     * the #buffer is empty (e.g., when reaching the end of the input).
     */
    long byte_offset;

} YIP_SOURCE;

/**
 * @brief Wrap a memory buffer as a source of bytes for parsing.
 *
 * @param begin
 *    A pointer to the first byte of the buffer. May be NULL.
 *
 * @param end
 *    A pointer just beyond the last byte of the buffer. May be equal to the
 *    begin pointer for an empty buffer.
 *
 * @return
 *    A valid #YIP_SOURCE or NULL (and sets errno) if some error occured.
 *
 * @see #YIP_SOURCE
 */
extern YIP_SOURCE *yip_buffer_source(const void *begin, const void *end);

/* Null terminated string as a source of bytes for parsing. Returns source or
 * NULL with errno. */

/**
 * @brief Wrap a null terminated string as a source of bytes for parsing.
 *
 * @param string
 *    Null terminated string to wrap as a source. May be NULL.
 *
 * @return
 *    A valid #YIP_SOURCE or NULL (and sets errno) if some error occured.
 *
 * @see #YIP_SOURCE
 */
extern YIP_SOURCE *yip_string_source(const char *string);

/**
 * @brief Wrap a file pointer as a source of bytes for parsing using stdio.
 *
 * This does not gain you much over using file descriptors, as YIP will buffer
 * I/O requests anyway.
 *
 * @param fp
 *    File pointer to wrap as a source. May be NULL.
 *
 * @param to_close
 *    If true, the file pointer will be closed when the #YIP_SOURCE is.
 *
 * @return
 *    A valid #YIP_SOURCE or NULL (and sets errno) if some error occured.
 *
 * @see #YIP_SOURCE
 */
extern YIP_SOURCE *yip_fp_source(FILE *fp, int to_close);

/**
 * @brief Wrap a file descriptor as a source of bytes for parsing using UNIX
 * I/O.
 *
 * This is less efficient than using memory mapping, but will work for pipes
 * etc.
 *
 * @param fd
 *    File decsriptor to wrap as a source. May be negative.
 *
 * @param to_close
 *    If true, the file descriptor will be closed when the #YIP_SOURCE is.
 *
 * @return
 *    A valid #YIP_SOURCE or NULL (and sets errno) if some error occured.
 *
 * @see #YIP_SOURCE
 */
extern YIP_SOURCE *yip_fd_read_source(int fd, int to_close);

/**
 * @brief Wrap a file descriptor as a source of bytes for parsing using memory
 * mapping.
 *
 * This is the most efficient way to parse files, but does not work for pipes
 * etc.
 *
 * @param fd
 *    File decsriptor to wrap as a source. May be negative.
 *
 * @param to_close
 *    If true, the file descriptor will be closed when the #YIP_SOURCE is.
 *
 * @return
 *    A valid #YIP_SOURCE or NULL (and sets errno) if some error occured.
 *
 * @see #YIP_SOURCE
 */
extern YIP_SOURCE *yip_fd_map_source(int fd, int to_close);

/**
 * @brief Wrap a file descriptor as a source of bytes for parsing using
 * either memory maps or UNIX I/O.
 *
 * This attempts to use memory mapping, but if it fails (due to the input being
 * a pipe etc.) it falls back to using UNIX I/O.
 *
 * @param fd
 *    File decsriptor to wrap as a source. May be negative.
 *
 * @param to_close
 *    If true, the file descriptor will be closed when the #YIP_SOURCE is.
 *
 * @return
 *    A valid #YIP_SOURCE or NULL (and sets errno) if some error occured.
 *
 * @see #YIP_SOURCE
 */
extern YIP_SOURCE *yip_fd_source(int fd, int to_close);

/* File path as a source of bytes for parsing using memory map if possible or
 * read otherwise. Returns source of NULL with errno. */
/**
 * @brief Open a disk file as a source of bytes for parsing using either memory
 * maps or UNIX I/O.
 *
 * This attempts to use memory mapping, but if it fails (due to the input being
 * a pipe etc.) it falls back to using UNIX I/O.
 *
 * @param path
 *    The path of the file to open. May be NULL. As a special case "-" is taken
 *    to mean standard input.
 *
 * @return
 *    A valid #YIP_SOURCE or NULL (and sets errno) if some error occured.
 *
 * @see #YIP_SOURCE
 */
extern YIP_SOURCE *yip_path_source(const char *path);

/**
 * @}
 */

/**
 * @addtogroup Tokens
 *
 * @brief Results of parsing.
 *
 * @{
 */

/**
 * @brief Parsed YIP token codes (signed).
 *
 * The token codes were all chosen as printable ASCII characters. This allows
 * the generation of printable YIP files, containing one token per line. Such
 * files are used in regression testing of the YIP parser, but also for other
 * uses. For example http://yaml.org/ypaste is built on processing such files.
 *
 * Token codes that come in pairs (#YIP_BEGIN/#YIP_END) use upper case letter
 * for begin, lower case letter for end. The "#" code is allowed in YIP files
 * to indicate a comment line. Such tokens are never generated by the parser,
 * but the code is included here for completeness.
 *
 * This is a signed enum so negative values can be used to signify errors.
 *
 * @see #YIP_CODE_TYPE, #YIP_TOKEN
 * @see #yip_code_type, #yip_code_pair
 */
typedef enum YIP_CODE {
    YIP_BEGIN_ALIAS      = 'R',   /**< Begins alias (reference) */
    YIP_BEGIN_ANCHOR     = 'A',   /**< Begins anchor. */
    YIP_BEGIN_COMMENT    = 'C',   /**< Begins comment. */
    YIP_BEGIN_DIRECTIVE  = 'D',   /**< Begins directive. */
    YIP_BEGIN_DOCUMENT   = 'O',   /**< Begins document. */
    YIP_BEGIN_ESCAPE     = 'E',   /**< Begins escape sequence. */
    YIP_BEGIN_HANDLE     = 'H',   /**< Begins tag handle. */
    YIP_BEGIN_MAPPING    = 'M',   /**< Begins mapping content. */
    YIP_BEGIN_NODE       = 'N',   /**< Begins complete node. */
    YIP_BEGIN_PAIR       = 'X',   /**< Begins mapping key:value pair. */
    YIP_BEGIN_PROPERTIES = 'P',   /**< Begins properties. */
    YIP_BEGIN_SCALAR     = 'S',   /**< Begins scalar content. */
    YIP_BEGIN_SEQUENCE   = 'Q',   /**< Begins sequence content. */
    YIP_BEGIN_TAG        = 'G',   /**< Begins tag. */
    YIP_BOM              = 'U',   /**< Byte order mark. */
    YIP_BREAK            = 'b',   /**< Non-content (separation) line break. */
    YIP_COMMENT          = '#',   /**< Comment (never generated by parser). */
    YIP_DOCUMENT_END     = 'k',   /**< Document end marker. */
    YIP_DOCUMENT_START   = 'K',   /**< Document start marker. */
    YIP_DONE             = '\0',  /**< Parsing is done. */
    YIP_END_ALIAS        = 'r',   /**< Ends alias (reference) */
    YIP_END_ANCHOR       = 'a',   /**< Ends anchor. */
    YIP_END_COMMENT      = 'c',   /**< Ends comment. */
    YIP_END_DIRECTIVE    = 'd',   /**< Ends directive. */
    YIP_END_DOCUMENT     = 'o',   /**< Ends document. */
    YIP_END_ESCAPE       = 'e',   /**< Ends escape sequence. */
    YIP_END_HANDLE       = 'h',   /**< Ends tag handle. */
    YIP_END_MAPPING      = 'm',   /**< Ends mapping content. */
    YIP_END_NODE         = 'n',   /**< Ends complete node. */
    YIP_END_PAIR         = 'x',   /**< Ends mapping key:value pair. */
    YIP_END_PROPERTIES   = 'p',   /**< Ends properties. */
    YIP_END_SCALAR       = 's',   /**< Ends scalar content. */
    YIP_END_SEQUENCE     = 'q',   /**< Ends sequence content. */
    YIP_END_TAG          = 'g',   /**< Ends tag. */
    YIP_ERROR            = '!',   /**< Parsing error. */
    YIP_INDENT           = 'i',   /**< Indentation spaces. */
    YIP_INDICATOR        = 'I',   /**< Character indicating structure. */
    YIP_LINE_FEED        = 'L',   /**< Line break normalized to line feed. */
    YIP_LINE_FOLD        = 'l',   /**< Line break folded to content space. */
    YIP_META             = 't',   /**< Non-content text. */
    YIP_TEXT             = 'T',   /**< Content text. */
    YIP_UNPARSED         = '-',   /**< Unparsed text (due to error). */
    YIP_WHITE            = 'w',   /**< Non-content (separation) white space. */
    YIP_CODE_SIGNED = -1 /* Force enum to be signed. */
} YIP_CODE;

/**
 * @brief Token code types.
 *
 * @see #YIP_CODE
 * @see #yip_code_type, #yip_code_pair
 */
typedef enum YIP_CODE_TYPE {
    YIP_FAKE,   /**< Token contains "fake" (non-input) characters. */
    YIP_BEGIN,  /**< Token begins a group of tokens. */
    YIP_END,    /**< Token ends a group of tokens. */
    YIP_MATCH,  /**< Token matches some input characters. */
    YIP_CODE_TYPE_SIGNED = -1 /* Force enum to be signed. */
} YIP_CODE_TYPE;

/**
 * @brief The type of a token code.
 *
 * @param code
 *    The token code to get the type of.
 *
 * @return
 *    The type of the token code, or a negative value (and sets errno) if given
 *    an invalid token code.
 *
 * @see #YIP_CODE, #YIP_CODE_TYPE
 * @see #yip_code_pair
 */
extern YIP_CODE_TYPE yip_code_type(YIP_CODE code);

/**
 * @brief The paired token code.
 *
 * This only applies to #YIP_BEGIN/#YIP_END tokens. When given one of the token
 * codes, it returns the other.
 *
 * @param code
 *    The token code to get the paired code of.
 *
 * @return
 *    The paired token code, or a negative value (and sets errno) if given an
 *    invalid (non #YIP_BEGIN/#YIP_END) code.
 *
 * @see #YIP_CODE, #YIP_CODE_TYPE
 * @see #yip_code_type
 */
extern YIP_CODE yip_code_pair(YIP_CODE code);

/**
 * @brief A single parsed token.
 *
 * The whole point of the parser is to convert a #YIP_SOURCE into a stream of
 * these tokens.
 */
typedef struct YIP_TOKEN {
    YIP_BUFFER buffer[1];       /**< Token data bytes. */
    long byte_offset;           /**< Zero based offset in source bytes. */
    long char_offset;           /**< Zero based offset in source characters. */
    long line;                  /**< One based source line number. */
    long line_char;             /**< Zero based source character in line. */
    YIP_ENCODING encoding;      /**< Encoding used in buffer bytes. */
    YIP_CODE code;              /**< Parsed token code. */
} YIP_TOKEN;

/**
 * @}
 */

/**
 * @addtogroup Parsers
 *
 * @brief Convert @ref Sources to a stream of @ref Tokens.
 *
 * @{
 */

/**
 * @brief Opaque parser object.
 *
 * This holds the internal state of the parser, including the source.
 */
typedef struct YIP YIP;

/**
 * @brief Identification of a specific production instance.
 *
 * This is used for regression testing specific productions.
 */
typedef struct YIP_PRODUCTION {
    const char *name; /**< Production name. */
    const char *n;    /**< Indentation argument or NULL. */
    const char *c;    /**< Context argument or NULL. */
    const char *t;    /**< Chomp argument of NULL. */
} YIP_PRODUCTION;

/**
 * @brief Initialize a YIP parser object for a production.
 *
 * This is used for regression testing specific productions.
 *
 * @param source
 *    Source of bytes for parsing.
 *
 * @param to_close
 *    If true, the source will be closed when the parser is.
 * 
 * @param production
 *    The production to test, including parameter values.
 *
 * @return
 *    A parser for testing the production, or NULL (and sets errno) if some
 *    error occured.
 *
 * @see #YIP, #YIP_PRODUCTION, #YIP_SOURCE
 */
extern YIP *yip_test(YIP_SOURCE *source, int to_close,
                     const YIP_PRODUCTION *production);

/**
 * @brief Close a parser and release all resources.
 *
 * @param yip
 *    The parser to close.
 *
 * @return
 *    Zero if all is well, or a negative value (and sets errno) if some error
 *    occured.
 *
 * @see YIP
 */
extern int yip_close(YIP *yip);

/**
 * @brief Return the next parsed token.
 *
 * The returned token is only valid until the next call.
 *
 * The parser tries to recover from errors by skipping some parts of the input
 * (e.g., until a less-indented line) and picking up from there. This is
 * reported as #YIP_UNPARSED and #YIP_ERROR tokens. Thus, discarding these
 * tokens as well as all #YIP_FAKE tokens results in a valid YAML stream
 * (missing possibly large parts of the original stream).
 *
 * The tokens nesting structure is properly maintained so that every #YIP_BEGIN
 * token has a matching #YIP_END token, even when errors are detected and parts
 * of the input is skipped.
 *
 * @param yip
 *    The parser to fetch the next token from.
 *
 * @return
 *    The next token, or NULL (and sets errno) if some error occured.
 *
 * @see #YIP, #YIP_TOKEN
 */
extern const YIP_TOKEN *yip_next_token(YIP *yip);

/**
 * @}
 */

#endif /* YIP_H */
