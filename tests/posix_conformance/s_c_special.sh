# ---- C. 特殊参数 ----
chk sp_hash '0' "$(set --; echo $#)"
chk sp_hash_args '2' "$(set -- a b; echo $#)"
chk sp_q '0' "$(true; echo $?)"
chk sp_q_false '1' "$(false; echo $?)"
chk sp_dollar_pid "$(echo $$ | grep -cE '^[0-9]+$')" '1'
chk sp_bang_empty '' "$(echo "$!")"
chk sp_zero_nonempty "$( [ -n "$0" ] && echo y )" 'y'
chk sp_star_join 'a b' "$(set -- a b; echo "$*")"
chk sp_at_join 'a b' "$(set -- a b; echo "$@")"
chk sp_at_count '2' "$(set -- a b; printf '%s\n' "$@" | wc -l | tr -d ' ')"
chk sp_at_empty '0' "$(set --; echo $#)"
chk sp_dash_has_e 'yes' "$(set -e; case "$-" in *e*) echo yes;; *) echo no;; esac)"
chk sp_dash_no_e 'no' "$(case "$-" in *e*) echo yes;; *) echo no;; esac)"
chk sp_shift 'b' "$(set -- a b; shift; echo "$1")"
chk sp_shift_n 'c' "$(set -- a b c; shift 2; echo "$1")"
chk sp_shift_rc '1' "$(set -- a; shift 2 2>/dev/null; echo $?)"
chk sp_at_in_func 'x y' "$(f() { echo "$@"; }; f x y)"
chk sp_hash_in_func '2' "$(f() { echo $#; }; f a b)"
