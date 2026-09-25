# ---- H. 重定向 ----
D=/tmp/pc_rd.$$
chk rd_out 'x' "$(echo x > $D; cat $D; rm -f $D)"
chk rd_append 'xy' "$(echo x > $D; echo y >> $D; tr -d '\n' < $D; rm -f $D)"
chk rd_in 'line' "$(echo line > $D; cat < $D; rm -f $D)"
chk rd_in_wordcount '2' "$(printf 'a\nb\n' > $D; wc -l < $D | tr -d ' '; rm -f $D)"
chk rd_err_to_out '' "$(echo e 2>&1 >/dev/null | cat)"
chk rd_out_to_err '' "$(echo x >&2 2>/dev/null)"
chk rd_close '1' "$(echo x >&-; echo $?)"
chk rd_dup_fd 'hi' "$(echo hi > $D; exec 3< $D; cat <&3; exec 3<&-; rm -f $D)"
chk rd_heredoc 'a' "$(cat <<E
a
E
)"
chk rd_heredoc_expand 'v=1' "$(v=1; cat <<E
v=$v
E
)"
chk rd_heredoc_quoted 'v=$v' "$(v=1; cat <<'E'
v=$v
E
)"
chk rd_heredoc_dash 'x' "$(cat <<-E
	x
	E
)"
chk rd_here_string 'x' "$(cat <<<x)"
chk rd_rdwr 'data' "$(echo data > $D; cat <> $D; rm -f $D)"
chk rd_noclobber '1' "$(echo a > $D; (set -C; echo b > $D) 2>/dev/null; echo $?; rm -f $D)"
chk rd_compound_out 'a' "$( { echo a; } > $D; cat $D; rm -f $D)"
chk rd_loop_out 'a b' "$(for i in a b; do echo $i; done > $D; tr '\n' ' ' < $D | sed 's/ $//'; rm -f $D)"
chk rd_order '0' "$(echo 2 1>&2 2>/dev/null | wc -l | tr -d ' ')"
