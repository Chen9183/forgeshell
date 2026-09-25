#!/bin/sh
name="世界"; n=3
echo "你好, $name! ${name:-默认} ${#name} $(date +%s) $((n*2+1))"
for i in 1 2 3; do echo "第 $i 次"; done
while [ "$n" -gt 0 ]; do n=$((n-1)); done
if [ -f /etc/passwd ] && [ -n "$name" ]; then echo yes; else echo no; fi
case "$name" in
  世界|hello) echo 匹配1 ;;
  *) echo 其它 ;;&
esac
f() { local x=$1; return $((x+1)); }
f 41
printf '%s=%d\n' count 7 | cat > /tmp/out.txt
cat <<HD_EOF
here-doc 内容 $name
HD_EOF
cat <<'HD_RAW'
不展开 $name
HD_RAW
x=$(echo 嵌套 $(echo 命令替换))
echo "$@" "$*" $# ${name:1:2} ${name/世界/world} ${name^^}
