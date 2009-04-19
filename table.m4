divert(-1)

define(`BEGIN_CLASSIFICATION_TABLE', `
define(`PREFIX', `')
divert(1)dnl
/* Low characters classification table. */
static YIP_CODE low_char_class[$1] = {
divert(-1)
')

define(`END_CLASSIFICATION_TABLE', `
undefine(`PREFIX')
divert(1)dnl

};
divert(-1)
')

define(`LOW_CHAR_CLASS', `
divert(1)dnl
PREFIX    /* format(`%3d 0%3o 0x%2x', $1, $1, $1) */ ifelse(`$2', `-1', `NO_CODE', `$2')`'dnl
divert(-1)
define(`PREFIX', `,
')
')
