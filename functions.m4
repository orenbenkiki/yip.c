divert(1)dnl
/* {{{ */
/* Machine implementations. */
divert(-1)

divert(2)dnl
/* }}} */
divert(-1)

define(`LineFeed', `Line_Feed')
define(`BeginEscape', `Begin_Escape')
define(`EndEscape', `End_Escape')

define(`BEGIN_MACHINE', `
divert(1)dnl
static RETURN $2(YIP *yip) {
    for (;;) {
        switch (State) {
divert(-1)
')

define(`END_MACHINE', `
divert(1)dnl
        default:
            assert(0);
            errno = EFAULT;
            return RETURN_ERROR;
        }
    }
}
divert(-1)
')

define(`BEGIN_STATE', `
define(`STATE_INDEX', `$1')
divert(1)dnl
        case $1:
divert(-1)
ifelse($1, `0', `', `
divert(1)dnl
        state_$1:
            yip_invariant(yip);
divert(-1)
')
define(`WHEN_DONE', `assert(0);')
')

define(`TOKEN_ACTION', `
divert(1)dnl
            switch ($1) {
            case RETURN_TOKEN:
                State = $2;
                yip_invariant(yip);
                return RETURN_TOKEN;
            case RETURN_DONE:
                break;
            default:
                assert(0);
                errno = EFAULT;
            case RETURN_ERROR:
                State = STATE_INDEX;
                yip_invariant(yip);
                return RETURN_ERROR;
            }
divert(-1)
')

define(`TEST_ACTION', `
divert(1)dnl
            if ($1 < 0) {
                State = STATE_INDEX;
                yip_invariant(yip);
                return RETURN_ERROR;
            }
divert(-1)
')

define(`VOID_ACTION', `
divert(1)dnl
            $1;
divert(-1)
')

define(`BEGIN_TOKEN', `
TOKEN_ACTION(`begin_token(yip, YIP_`'translit($1, `a-z', `A-Z'))', `$2')
')

define(`END_TOKEN', `
TOKEN_ACTION(`end_token(yip, YIP_`'translit($1, `a-z', `A-Z'))', `$2')
')

define(`EMPTY_TOKEN', `
TOKEN_ACTION(`empty_token(yip, YIP_`'translit($1, `a-z', `A-Z'))', `$2')
')

define(`UNEXPECTED', `
TOKEN_ACTION(`unexpected(yip)', `$1')
')

define(`NEXT_CHAR', `
TEST_ACTION(`next_char(yip)')
')

define(`PREV_CHAR', `
VOID_ACTION(`prev_char(yip)')
')

define(`NEXT_LINE', `
VOID_ACTION(`next_line(yip)')
')

define(`BEGIN_CHOICE', `
TEST_ACTION(`begin_choice(yip, CHOICE_`'translit($1, `a-z', `A-Z'))')
')

define(`END_CHOICE', `
TOKEN_ACTION(`end_choice(yip, CHOICE_`'translit($1, `a-z', `A-Z'))', `$2')
')

define(`COMMIT', `
TOKEN_ACTION(`commit(yip, CHOICE_`'translit($1, `a-z', `A-Z'))', `$2')
')

define(`RESET_COUNTER', `
VOID_ACTION(`I = 0')
')

define(`INCREMENT_COUNTER', `
VOID_ACTION(`I++')
')

define(`NON_POSITIVE_N', `
TOKEN_ACTION(`non_positive_n(yip)', `$1')
')

define(`PUSH_STATE', `
TEST_ACTION(`push_state(yip)')
')

define(`SET_STATE', `
VOID_ACTION(`set_state(yip)')
')

define(`POP_STATE', `
VOID_ACTION(`pop_state(yip)')
')

define(`RESET_STATE', `
VOID_ACTION(`reset_state(yip)')
')

define(`BEGIN_TRANSITIONS', `
define(`TRANSITION_PREFIX', `            ')
')

define(`END_TRANSITIONS', `
ifelse(TRANSITION_PREFIX, `            ', `', `
divert(1)dnl

divert(-1)
')
ifelse(WHEN_DONE, `', `', `
divert(1)dnl
            WHEN_DONE
divert(-1)
')
')

define(`BEGIN_TRANSITION', `
define(`TARGET_STATE', `$2')
define(`IS_ALWAYS', `YES')
')

define(`END_TRANSITION', `
ifelse(IS_ALWAYS, `YES', `
ifelse(TRANSITION_PREFIX, `            ', `
GOTO_STATE(`            ')
', `
divert(1)dnl
TRANSITION_PREFIX`'{
divert(-1)
GOTO_STATE(`                ')
divert(1)dnl
            }
divert(-1)
')
ifelse(WHEN_DONE, `assert(0);', `
define(`WHEN_DONE', `')
')
define(`TRANSITION_PREFIX', `            ')
')
')

define(`COUNTER_LESS_THAN_N', `
define(`IS_ALWAYS', `NO')
divert(1)dnl
TRANSITION_PREFIX`'if (I < N) {
divert(-1)
GOTO_STATE(`                ')
divert(1)dnl
            }dnl
divert(-1)
define(`TRANSITION_PREFIX', ` else ')
')

define(`COUNTER_LESS_EQUAL_N', `
define(`IS_ALWAYS', `NO')
divert(1)dnl
TRANSITION_PREFIX`'if (I <= N) {
divert(-1)
GOTO_STATE(`                ')
divert(1)dnl
            }dnl
divert(-1)
define(`TRANSITION_PREFIX', ` else ')
')

define(`BEGIN_CLASSES', `
define(`IS_ALWAYS', `NO')
define(`PREFIX', `')
divert(1)dnl
TRANSITION_PREFIX`'if (Curr_char->mask & (dnl
divert(-1)
define(`TRANSITION_PREFIX', ` else ')
')

define(`CLASS', `
divert(1)dnl
PREFIX`'(1ll << $1)dnl
divert(-1)
define(`PREFIX', ` | ')
')

define(`END_CLASSES', `
divert(1)dnl
)) {
divert(-1)
GOTO_STATE(`                ')
divert(1)dnl
            }dnl
divert(-1)
')

define(`GOTO_STATE', `
divert(1)dnl
$1`'goto state_`'TARGET_STATE;
divert(-1)
')
')
