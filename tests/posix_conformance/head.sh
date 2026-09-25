#!/bin/sh
# POSIX 符合性自检（XCU 2.x 分节）。每条 chk 用「期望 vs 实得」判定，不依赖 bash。
PASS=0; FAIL=0
chk() { if [ "$2" = "$3" ]; then PASS=$((PASS+1)); else FAIL=$((FAIL+1)); printf 'FAIL %-24s 期望[%s] 实得[%s]\n' "$1" "$2" "$3"; fi; }
