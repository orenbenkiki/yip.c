divert(1)dnl
/* Transition arrays. */
divert(-1)

define(`BEGIN_MACHINE', `
define(`MACHINE_ID', `$2')
')

define(`BEGIN_STATE', `
define(`STATE_ID', `$1')
')

define(`BEGIN_TRANSITIONS', `
divert(1)dnl

static const TRANSITION MACHINE_ID`'_transitions_`'STATE_ID`'[$1 + 1] = {
divert(-1)
')

define(`END_TRANSITIONS', `
divert(1)dnl
    /* ~ */ { 0, 0 }
};
divert(-1)
')

define(`END_TRANSITION', `
ifdef(`ANY_CLASSES', `', `
divert(2)dnl
0dnl
divert(-1)
')
undefine(`ANY_CLASSES')
divert(1)dnl
    /* $1 */ { $2, dnl
undivert(2)dnl
 },
divert(-1)
')
')

define(`CLASS', `
ifdef(`ANY_CLASSES', `
divert(2)dnl
 | dnl
divert(-1)
')
divert(2)dnl
1ll << $1dnl
divert(-1)
define(`ANY_CLASSES')
')
