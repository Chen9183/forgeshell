# 铸壳工具链 ForgeShell (s2a) —— 构建
#
#   make            构建运行时模板 + 内嵌 blob + 编译器 bin/s2a
#   make test       跑自检（发射器对 as、前端解析、端到端编译运行）
#   make clean
#
# Author: @Chen9183 (github)

# 编译器本体也编译成**纯静态**（可直接拷到任意 aarch64 机器/安卓宿主运行，无 libc 依赖）
CC      := musl-gcc
MUSL    := musl-gcc
CFLAGS  := -std=gnu99 -Wall -Wextra -Wno-unused-parameter -Werror=implicit-function-declaration -O2 -Isrc -Isrc/common -Isrc/compiler -Isrc/runtime

RT_SRC  := src/runtime/rt_template_main.c src/runtime/rt_main.c src/runtime/rt_core.c \
           src/runtime/rt_exec.c src/runtime/rt_builtin.c src/runtime/rt_protect.c \
           src/common/parse.c src/common/crypto.c
CP_SRC  := src/common/parse.c src/common/crypto.c src/compiler/main.c src/compiler/elf.c src/compiler/emit_a64.c \
           src/compiler/image.c src/compiler/codegen.c src/compiler/protect.c \
           src/compiler/rand.c src/compiler/emu.c src/compiler/iso_bridge.c

all: bin/s2a bin/s2a-s

# 1) 运行时模板：纯静态 musl，带符号表，代码段基址 0x100000
build/template.elf: $(RT_SRC) $(wildcard src/common/*.h src/runtime/*.h) scripts/build_template.sh
	sh scripts/build_template.sh

# 2) 把模板作为二进制 blob 链进编译器（不加壳、不压缩，符号 _binary_build_template_elf_*）
build/template_blob.o: build/template.elf
	ld -r -b binary -o $@ $<

# 2b) 内嵌 qemu（upx -9 压过，宿主没装 qemu 时用它回退）
build/ash_blob.o: assets/ash.upx
	ld -r -b binary -o $@ $<

# help 文本：TXT（仓库里可读、可编辑）→ 编译时加密成 .enc → ld 嵌入密文
build/help-enc: tools/help-enc.c src/common/crypto.c
	@mkdir -p build
	$(MUSL) -static -O2 -std=gnu99 -Werror=implicit-function-declaration -Isrc -Isrc/common -o $@ tools/help-enc.c src/common/crypto.c

build/help-short.enc: assets/help-short.txt build/help-enc
	build/help-enc $< $@ short
build/help.enc: assets/help.txt build/help-enc
	build/help-enc $< $@ detail
build/help-full.enc: assets/help-full.txt build/help-enc
	build/help-enc $< $@ full
build/help-dev.enc: assets/help-dev.txt build/help-enc
	build/help-enc $< $@ dev

build/help_short_blob.o: build/help-short.enc
	ld -r -b binary -o $@ $<
build/help_blob.o: build/help.enc
	ld -r -b binary -o $@ $<
build/help_full_blob.o: build/help-full.enc
	ld -r -b binary -o $@ $<
build/help_dev_blob.o: build/help-dev.enc
	ld -r -b binary -o $@ $<

build/launcher.elf: src/launcher/launcher.c src/common/crypto.c $(wildcard src/common/*.h)
	$(MUSL) -static -O2 -std=gnu99 -Werror=implicit-function-declaration \
	    -Isrc -Isrc/common -o $@ src/launcher/launcher.c src/common/crypto.c

build/launcher_blob.o: build/launcher.elf
	ld -r -b binary -o $@ $<

build/qemu_blob.o: assets/qemu-aarch64-static.upx
	ld -r -b binary -o $@ $<

# 3) 编译器本体
bin/s2a: build/help-short.enc build/help.enc build/help-full.enc build/help-dev.enc $(CP_SRC) $(wildcard src/compiler/*.h src/common/*.h) build/template_blob.o build/qemu_blob.o build/ash_blob.o build/launcher_blob.o build/help_short_blob.o build/help_blob.o build/help_full_blob.o build/help_dev_blob.o
	@mkdir -p bin
	$(CC) -static $(CFLAGS) -o $@ $(CP_SRC) build/template_blob.o build/qemu_blob.o build/ash_blob.o build/launcher_blob.o build/help_short_blob.o build/help_blob.o build/help_full_blob.o build/help_dev_blob.o
	@echo "编译器已生成: bin/s2a"

# 第二个：strip 过的（分发用，体积更小；功能完全一样）
bin/s2a-s: bin/s2a
	@mkdir -p bin
	cp -f bin/s2a $@ && strip $@
	@printf '已生成 %s: ' "$@"; ls -l $@ | awk '{print $$5" 字节"}'

# 自检
tests/test_emit_bin: tests/test_emit.c src/compiler/emit_a64.c
	$(CC) -std=c99 -O2 -Isrc/compiler -o $@ tests/test_emit.c src/compiler/emit_a64.c tests/stub_code_va.c

tests/test_parse_bin: tests/test_parse.c src/common/parse.c
	$(CC) -std=gnu99 -O2 -Isrc/common -o $@ tests/test_parse.c src/common/parse.c

test: bin/s2a tests/test_emit_bin tests/test_parse_bin
	./tests/test_emit_bin
	./tests/test_parse_bin tests/cases/basic.sh | tail -1
	sh tests/run_e2e.sh

clean:
	rm -rf bin/s2a-s build/help-enc build/*.enc build/template.elf build/template_blob.o build/qemu_blob.o build/ash_blob.o build/launcher_blob.o build/help_short_blob.o build/help_blob.o build/help_full_blob.o build/help_dev_blob.o bin tests/test_emit_bin tests/test_parse_bin
