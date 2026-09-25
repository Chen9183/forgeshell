#!/bin/sh
cd "$(dirname "$0")"
cat head.sh s_*.sh > all.sh
printf '\nprintf "PASS=%%s FAIL=%%s\\n" "$PASS" "$FAIL"\n' >> all.sh
