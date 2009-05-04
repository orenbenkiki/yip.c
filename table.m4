divert(-1)

define(`BEGIN_CLASSIFICATION_TABLE', `
define(`PREFIX', `')
divert(1)dnl
/* Low characters classification table. */
static long long low_char_mask[$1] = {
divert(-1)
')

define(`END_CLASSIFICATION_TABLE', `
undefine(`PREFIX')
divert(1)dnl

};
divert(-1)
')

define(`LOW_CHAR_CLASS', `
ifelse(`$1', `-1', `
divert(1)dnl
PREFIX    /* format(`%3d     EOF   ', $1) */ ifelse(`$2', `-1', `0', `1ll << $2')`'dnl
divert(-1)
', `
divert(1)dnl
PREFIX    /* format(`%3d 0o%03o 0x%02x', $1, $1, $1) */ ifelse(`$2', `-1', `0', `1ll << $2')`'dnl
divert(-1)
')
define(`PREFIX', `,
')
')
