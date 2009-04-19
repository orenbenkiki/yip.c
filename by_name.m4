divert(1)
/* Productions with no additional arguments. */
static const MACHINE_BY_NAME machines[] = {
divert(2)dnl
    { NULL, NULL }
};

/* Productions with N argument. */
static const MACHINE_BY_NAME machines_with_n[] = {
divert(3)dnl
    { NULL, NULL }
};

/* Productions with T argument. */
static const MACHINE_BY_NAME machines_with_t[] = {
divert(4)dnl
    { NULL, NULL }
};

/* Productions with N and T arguments. */
static const MACHINE_BY_NAME machines_with_nt[] = {
divert(5)dnl
    { NULL, NULL }
};
divert(-1)

define(`BEGIN_MACHINE', `
define(`MACHINE_ID', `$2')
divert(8)dnl
    { "$3", $2 },
divert(-1)
')

define(`BEGIN_PARAMETERS', `
undefine(`USED_n')
undefine(`USED_t')
')

define(`PARAMETER', `
define(`USED_$1', `')
')

define(`END_PARAMETERS', `
ifdef(`USED_n',
      `ifdef(`USED_t',
             `WITH_nt',
             `WITH_n')',
      `ifdef(`USED_t',
             `WITH_t',
             `WITH_out')')
')

define(`NO_PARAMETERS', `WITH_out')

define(`WITH_out', `
divert(1)dnl
undivert(8)dnl
divert(-1)
')

define(`WITH_n', `
divert(2)dnl
undivert(8)dnl
divert(-1)
')

define(`WITH_t', `
divert(4)dnl
undivert(8)dnl
divert(-1)
')

define(`WITH_nt', `
divert(5)dnl
undivert(8)dnl
divert(-1)
')
