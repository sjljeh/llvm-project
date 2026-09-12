#!/usr/bin/env bash
#
# Build and validate the llvm-mingw RISC-V branch from a pair of sibling
# checkouts.  The GitHub Actions workflow supplies those checkouts and the
# ACT/Sail tools; the same script can be run locally with the corresponding
# environment variables.

set -Eeuo pipefail

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
LLVM_MINGW_DIR=${LLVM_MINGW_DIR:-"$ROOT/../llvm-mingw"}
ACT_DIR=${ACT_DIR:-"$ROOT/../riscv-arch-test"}
WORK_DIR=${LLVM_MINGW_WORK_DIR:-"${RUNNER_TEMP:-${TMPDIR:-/tmp}}/llvm-mingw-riscv-worker"}
LLVM_BUILD=${LLVM_WORKER_BUILD_DIR:-"$WORK_DIR/llvm-build"}
PREFIX=${LLVM_MINGW_PREFIX:-"$WORK_DIR/toolchain"}
REPORT_DIR=${LLVM_MINGW_REPORT_DIR:-"$WORK_DIR/report"}
CORES=${CORES:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '%s' 4)}
ACT_JOBS=${ACT_JOBS:-0}

mkdir -p "$WORK_DIR" "$REPORT_DIR"

on_exit() {
    rc=$?
    if [ "$rc" -ne 0 ]; then
        printf 'worker failed with exit status %s\n' "$rc" > "$REPORT_DIR/failure.txt"
    fi
}
trap on_exit EXIT

die() {
    echo "error: $*" >&2
    exit 1
}

require_command() {
    command -v "$1" >/dev/null 2>&1 || die "$1 is required"
}

run_logged() {
    name=$1
    shift
    echo "== $name =="
    printf '$'
    printf ' %q' "$@"
    printf '\n'
    "$@" 2>&1 | tee "$REPORT_DIR/$name.log"
}

for dep in git cmake ninja tar sha256sum timeout; do
    require_command "$dep"
done

[ -d "$LLVM_MINGW_DIR" ] || die "llvm-mingw checkout not found: $LLVM_MINGW_DIR"
[ -d "$ACT_DIR" ] || die "riscv-arch-test checkout not found: $ACT_DIR"

source_branch=$(git -C "$ROOT" symbolic-ref --quiet --short HEAD || true)
recipe_branch=$(git -C "$LLVM_MINGW_DIR" symbolic-ref --quiet --short HEAD || true)
[ "$source_branch" = llvm-mingw-riscv ] ||
    die "LLVM checkout must be on llvm-mingw-riscv (found ${source_branch:-detached HEAD})"
[ "$recipe_branch" = llvm-mingw-riscv ] ||
    die "llvm-mingw checkout must be on llvm-mingw-riscv (found ${recipe_branch:-detached HEAD})"

source_commit=$(git -C "$ROOT" rev-parse HEAD)
recipe_commit=$(git -C "$LLVM_MINGW_DIR" rev-parse HEAD)
act_commit=$(git -C "$ACT_DIR" rev-parse HEAD)

echo "LLVM source:      $source_commit ($source_branch)"
echo "llvm-mingw:       $recipe_commit ($recipe_branch)"
echo "riscv-arch-test:  $act_commit"
echo "Worker directory: $WORK_DIR"
echo "Parallelism:      $CORES"

# Build the complete enabled LLVM/Clang/LLD graph with tests enabled.  This is
# intentionally a separate build tree from build-riscv-mingw.sh, whose recipe
# uses the source tree's llvm/build directory with tests disabled.
run_logged llvm-configure cmake -S "$ROOT/llvm" -B "$LLVM_BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DLLVM_ENABLE_ASSERTIONS=ON \
    -DLLVM_ENABLE_PROJECTS='clang;lld' \
    -DLLVM_TARGETS_TO_BUILD='ARM;AArch64;X86;NVPTX;RISCV' \
    -DLLVM_BUILD_TESTS=ON \
    -DLLVM_INCLUDE_TESTS=ON \
    -DLLVM_BUILD_EXAMPLES=OFF \
    -DLLVM_INCLUDE_EXAMPLES=OFF
run_logged llvm-build ninja -C "$LLVM_BUILD" -j "$CORES"
run_logged llvm-checks ninja -C "$LLVM_BUILD" -j "$CORES" check-all

# Build the exact llvm-mingw RISC-V recipe into a worker-only prefix and run
# its end-to-end COFF checks.  Keep the recipe's source build tree independent
# from the full-check build above.
export LLVM_CMAKEFLAGS='-DLLVM_BUILD_TESTS=OFF -DLLVM_INCLUDE_TESTS=OFF'
run_logged riscv-toolchain-build env LLVM_CMAKEFLAGS="$LLVM_CMAKEFLAGS" \
    "$LLVM_MINGW_DIR/build-riscv-mingw.sh" \
    --source="$ROOT" --prefix="$PREFIX" --cores="$CORES"
run_logged riscv-smoke "$LLVM_MINGW_DIR/test-riscv.sh" "$PREFIX"

printf '%s\n' 'int rv64gc_probe(int x) { return x + 1; }' > "$WORK_DIR/rv64gc.c"
run_logged rv64gc-compile "$PREFIX/bin/clang" -target riscv64-unknown-elf \
    -march=rv64gc -mabi=lp64d -ffreestanding -fno-builtin -nostdinc \
    -c "$WORK_DIR/rv64gc.c" -o "$WORK_DIR/rv64gc.o"
run_logged rv64gc-headers "$PREFIX/bin/llvm-readobj" --file-headers "$WORK_DIR/rv64gc.o"
grep -F 'Format: elf64-littleriscv' "$REPORT_DIR/rv64gc-headers.log" >/dev/null
grep -F 'EF_RISCV_FLOAT_ABI_DOUBLE' "$REPORT_DIR/rv64gc-headers.log" >/dev/null
grep -F 'EF_RISCV_RVC' "$REPORT_DIR/rv64gc-headers.log" >/dev/null

for dep in uv bundle; do
    require_command "$dep"
done
require_command sail_riscv_sim

ACT_CONFIG_ROOT="$WORK_DIR/act-config"
ACT_CONFIG_DIR="$ACT_CONFIG_ROOT/llvm-mingw-RVA23S64"
ACT_WORK="$WORK_DIR/act-work"
mkdir -p "$ACT_CONFIG_ROOT"
mkdir -p "$ACT_CONFIG_DIR"
cp -a "$ACT_DIR/config/sail/sail-RVA23S64"/. "$ACT_CONFIG_DIR"/
sed -i \
    -e "s#^name:.*#name: llvm-mingw-RVA23S64#" \
    -e "s#^compiler_exe:.*#compiler_exe: $PREFIX/bin/clang #" \
    -e "s#^objdump_exe:.*#objdump_exe: $PREFIX/bin/llvm-objdump #" \
    -e "s#^ref_model_exe:.*#ref_model_exe: $(command -v sail_riscv_sim) #" \
    "$ACT_CONFIG_DIR/test_config.yaml"

run_logged act-rva23s64 bash -c \
    "cd \"$ACT_DIR\" && uv run --project framework act \"$ACT_CONFIG_DIR/test_config.yaml\" --workdir \"$ACT_WORK\" --test-dir tests --jobs \"$ACT_JOBS\""

ACT_ELF_DIR="$ACT_WORK/llvm-mingw-RVA23S64/elfs"
[ -d "$ACT_ELF_DIR" ] || die "ACT did not produce an ELF directory"
elf_count=$(find "$ACT_ELF_DIR" -type f -name '*.elf' | wc -l)
objdump_count=$(find "$ACT_ELF_DIR" -type f -name '*.elf.objdump' | wc -l)
[ "$elf_count" -gt 0 ] || die 'ACT produced no final ELF images'
[ "$objdump_count" -eq "$elf_count" ] ||
    die "ACT produced $elf_count ELFs but $objdump_count objdump files"
act_build_tasks=$(sed -n 's/.*Build complete: \([0-9][0-9]*\) succeeded.*/\1/p' \
    "$REPORT_DIR/act-rva23s64.log" | tail -1)
[ -n "$act_build_tasks" ] || die 'ACT report did not contain a build-task count'

# Run every final self-checking ELF through the same Sail configuration used
# by ACT.  ACT validates generation and signature production; this pass checks
# the generated final images themselves and leaves failures in sail-output/.
SAIL_OUTPUT_DIR="$WORK_DIR/sail-output"
mkdir -p "$SAIL_OUTPUT_DIR"
export SAIL_CONFIG="$ACT_CONFIG_DIR/sail.json"
export SAIL_OUTPUT_DIR
if ! find "$ACT_ELF_DIR" -type f -name '*.elf' -print0 |
    xargs -0 -r -n1 -P "$CORES" bash -c '
        elf=$1
        id=$(printf "%s" "$elf" | sha256sum | cut -d" " -f1)
        output="$SAIL_OUTPUT_DIR/$id.log"
        if timeout 30s "$(command -v sail_riscv_sim)" --config "$SAIL_CONFIG" "$elf" >"$output" 2>&1 &&
            grep -q "RVCP-SUMMARY: TEST PASSED" "$output"; then
            printf "PASS %s\n" "$elf"
            exit 0
        fi
        printf "FAIL %s\n" "$elf"
        tail -20 "$output"
        exit 1
    ' bash > "$REPORT_DIR/sail-dut.log" 2>&1; then
    cat "$REPORT_DIR/sail-dut.log"
    exit 1
fi
sail_pass_count=$(grep -c '^PASS ' "$REPORT_DIR/sail-dut.log")
[ "$sail_pass_count" -eq "$elf_count" ] ||
    die "Sail passed $sail_pass_count of $elf_count final ELF images"

# Produce the same binary artifact shape as the manual release.  It is kept as
# a workflow artifact, while publishing a GitHub release remains a deliberate
# manual operation.
PACKAGE_NAME=llvm-mingw-riscv-worker
PACKAGE_ROOT="$WORK_DIR/package/$PACKAGE_NAME"
mkdir -p "$PACKAGE_ROOT"
cp -a "$PREFIX"/. "$PACKAGE_ROOT"/
cp "$LLVM_MINGW_DIR/LICENSE.txt" "$PACKAGE_ROOT/LICENSE.txt"
{
    printf 'LLVM-MinGW RISC-V worker artifact\n\n'
    printf 'llvm-project: %s\n' "$source_commit"
    printf 'llvm-mingw:   %s\n' "$recipe_commit"
    printf 'ACT4:         %s\n\n' "$act_commit"
    printf 'The worker ran the full LLVM check-all target, test-riscv.sh, rv64gc\n'
    printf 'header checks, ACT4 RVA23S64, and Sail on %s/%s final ELF images.\n\n' \
        "$sail_pass_count" "$elf_count"
    printf 'This is a development preview. RISC-V COFF relocations, PE linking,\n'
    printf 'and MinGW-w64 CRT/headers/libraries are not implemented.\n'
} > "$PACKAGE_ROOT/README.txt"
ARCHIVE="$WORK_DIR/$PACKAGE_NAME.tar.xz"
source_date_epoch=$(git -C "$ROOT" show -s --format=%ct HEAD)
tar --sort=name --mtime="@$source_date_epoch" --owner=0 --group=0 --numeric-owner \
    -cJf "$ARCHIVE" -C "$WORK_DIR/package" "$PACKAGE_NAME"
sha256sum "$ARCHIVE" > "$WORK_DIR/SHA256SUMS"

{
    printf '# LLVM-MinGW RISC-V worker report\n\n'
    printf -- '- source commit: `%s`\n' "$source_commit"
    printf -- '- recipe commit: `%s`\n' "$recipe_commit"
    printf -- '- ACT4 commit: `%s`\n' "$act_commit"
    printf -- '- LLVM build: `PASS` (full graph)\n'
    printf -- '- LLVM checks: `PASS` (`check-all`)\n'
    printf -- '- llvm-mingw smoke: `PASS` (riscv32/riscv64 COFF)\n'
    printf -- '- rv64gc headers: `PASS` (double ABI and RVC flags)\n'
    printf -- '- ACT4 build tasks: `%s succeeded`\n' "$act_build_tasks"
    printf -- '- final ELF images: `%s`\n' "$elf_count"
    printf -- '- final Sail executions: `%s/%s PASS`\n' "$sail_pass_count" "$elf_count"
    printf -- '- archive SHA256: `%s`\n' "$(cut -d' ' -f1 "$WORK_DIR/SHA256SUMS")"
    printf '\nThis validates compiler/code-generation and COFF emission. It is not a\n'
    printf 'hardware certification and does not validate PE linking or a Windows CRT.\n'
} > "$REPORT_DIR/summary.md"

cat "$REPORT_DIR/summary.md"
echo "Worker completed successfully. Report: $REPORT_DIR/summary.md"
