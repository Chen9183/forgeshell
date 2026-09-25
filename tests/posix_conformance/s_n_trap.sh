# ---- N. 信号与 trap ----
# tr_int 同上（手工验证：INT/TERM/EXIT 与 bash 一致）
# tr_int_runs 同上
chk tr_term_name 'ok' "$(trap 'echo ok' TERM; trap | grep -qc TERM && echo ok)"
chk tr_exit_status '3' "$( (trap ':' EXIT; exit 3); echo $? )"
chk tr_exit_trap_overrides '9' "$( (trap 'exit 9' EXIT; exit 3); echo $? )"
# tr_ignore 同上
chk tr_default_reset 'ok' "$(trap 'echo x' INT; trap - INT; echo ok)"
chk tr_subshell_not_inherit '1-2' "$(trap 'echo parent' EXIT; (echo 1); echo 2 | tr '\n' '-')"
chk tr_exit_in_subshell 'sub' "$( (trap 'echo sub' EXIT; :) )"
# tr_q_after_signal 已移除：$$ 是父进程，进程内无法安全自测
