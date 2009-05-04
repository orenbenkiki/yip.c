divert(-1)

define(`BEGIN_CLASSIFICATION', `
divert(1)
/* Mask for a decoded unicode character. */
static long long int code_mask(int code) {
    if (code == INVALID_CODE) return 0;
    assert(code + 1 >= 0);
    if (code + 1 < numof(low_char_mask)) return low_char_mask[code + 1];
divert(-1)
')

define(`END_CLASSIFICATION', `
divert(1)dnl
    return 0;
};
divert(-1)
')

define(`HIGH_CHAR_RANGE', `
divert(1)dnl
    if ($2 <= code && code <= $3) return 1ll << $1;
divert(-1)
')
