#!/bin/sh
# 构建运行时模板：静态 musl、纯静态、代码段基址 0x100000
set -e
cd "$(dirname "$0")/.."
# rt_mem.c 必须单独编译：-fno-builtin 防止 GCC 把循环又优化成 memcpy 调用（自我递归）
musl-gcc -c -O2 -std=gnu99 -fno-builtin -fno-builtin-memcpy -fno-builtin-memset \
    -o build/rt_mem.o src/runtime/rt_mem.c

musl-gcc -static -no-pie -O2 -std=gnu99 \
    -Wno-unused-parameter -Werror=implicit-function-declaration \
    -I src/runtime -I src/common \
    -Wl,-Ttext=0x100000 \
    -o build/template.elf \
    src/runtime/rt_template_main.c \
    src/runtime/rt_main.c src/runtime/rt_core.c src/runtime/rt_exec.c \
    src/runtime/rt_builtin.c src/runtime/rt_protect.c \
    src/common/parse.c src/common/crypto.c build/rt_mem.o
echo "模板已生成: build/template.elf"
ls -l build/template.elf
