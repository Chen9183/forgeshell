#!/bin/sh
# 端到端：编译几个脚本并运行，检查输出与退出码
set -e
cd "$(dirname "$0")/.."
S2A=bin/s2a
fail=0
run_case() {
    name=$1; script=$2; expect=$3
    printf '%s\n' "$script" > /tmp/s2a_case.sh
    if ! $S2A /tmp/s2a_case.sh -o /tmp/s2a_case.elf >/tmp/s2a_cc.log 2>&1; then
        echo "✗ $name: 编译失败"; tail -3 /tmp/s2a_cc.log; fail=1; return
    fi
    got=$(/tmp/s2a_case.elf 2>&1) || true
    if [ "$got" = "$expect" ]; then echo "✓ $name"
    else echo "✗ $name: 期望 [$expect] 实得 [$got]"; fail=1; fi
}
run_case "echo"        'echo hello'                                   'hello'
run_case "变量"        'x=world; echo "hi $x"'                        'hi world'
run_case "算术"        'echo $((2+3*4))'                              '14'
run_case "if真"        'if true; then echo T; else echo F; fi'         'T'
run_case "if假"        'if false; then echo T; else echo F; fi'        'F'
run_case "for"         'for i in a b c; do echo -n $i; done'           'abc'
run_case "while"       'i=0; while [ $i -lt 3 ]; do echo -n $i; i=$((i+1)); done' '012'
run_case "函数"        'f() { echo "in $1"; }; f 42'                   'in 42'
run_case "管道重定向"  'echo abc | tr a-z A-Z'                         'ABC'
run_case "命令替换"    'x=$(echo nested); echo "[$x]"'                 '[nested]'
run_case "参数展开"    'x=hello; echo ${x:-d} ${#x} ${x/l/L}'           'hello 5 heLlo'
run_case "case"        'case ab in a*) echo A ;; *) echo B ;; esac'    'A'
run_case "退出码"      'exit 7'
[ $? -eq 0 ] || true
exit $fail
