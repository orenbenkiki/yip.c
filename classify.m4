divert(1)
/* Classify a unicode character. */
static YIP_CODE char_class(int char_code) {
    if (char_code < numof(low_char_class)) return low_char_class[char_code];
divert(3)dnl
    return NO_CODE;
};
divert(-1)

define(`HIGH_CHAR_RANGE', `
divert(2)dnl
    if ($2 <= char_code && char_code <= $3) return $1;
divert(-1)
')
