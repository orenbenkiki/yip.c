divert(1)dnl
/* State arrays. */
divert(-1)

define(`BEGIN_MACHINE', `
define(`MACHINE_ID', `$2')
')

define(`BEGIN_STATES', `
define(`PREFIX', `')
divert(1)dnl

static const STATE MACHINE_ID`'_states[$1] = {
divert(-1)
')

define(`BEGIN_STATE', `
divert(1)dnl
PREFIX    { MACHINE_ID`'_transitions_`'$1 }dnl
divert(-1)
};
divert(-1)
')

define(`END_MACHINE', `
divert(1)dnl

};
divert(-1)
undefine(`PREFIX')
')
