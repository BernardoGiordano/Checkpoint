#!/usr/bin/env bash
#
# Parse-check picoc scripts on the host, with the real interpreter.
#
# Scripts ship as romfs assets: nothing compiles them, so a stray paren only
# shows up as a run that dies on the console. This builds a host binary out of
# picoc's interpreter core plus Checkpoint's platform layer and stdlib, and runs
# PicocParse with RunIt = false over each file — the same parser the console
# uses, without executing a line.
#
# It catches what the parser catches: syntax, malformed declarations, bad
# preprocessor use. It cannot catch an unknown identifier or a wrong argument
# count, because picoc resolves those when the statement actually runs.
#
# Usage:
#   tools/scriptlint.sh                 # every bundled script, both targets
#   tools/scriptlint.sh path/to/x.c ...
#
# Needs a host gcc and the picoc submodule checked out.

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${TMPDIR:-/tmp}/checkpoint-scriptlint"
mkdir -p "$build"

if [ ! -f "$root/3rd-party/picoc/include/interpreter.h" ]; then
    echo "3rd-party/picoc is not checked out: git submodule update --init --recursive" >&2
    exit 2
fi

# Every native binding has the same picoc signature, so the whole stub file
# follows from the CKPT_BINDING() list in the API header — no hand-kept copy to
# drift out of date when a binding is added.
python3 - "$root" "$build" <<'PY'
import pathlib, re, sys
root, build = (pathlib.Path(p) for p in sys.argv[1:3])
hdr = (root / "common/script/checkpoint_api.h").read_text()
names = [n for n in re.findall(r"CKPT_BINDING\((\w+)\)", hdr) if n != "name"]
body = [
    '#include "checkpoint_api.h"',
    '#include "interpreter.h"',
    "#include <stdio.h>",
    "#include <stdlib.h>",
    "",
]
body += [
    "void %s(struct ParseState* P, struct Value* R, struct Value** A, int N)\n"
    "{ (void)P; (void)R; (void)A; (void)N; }" % n
    for n in names
]
body.append(
    "\n/* the console shim, the abort flag and the script heap: C++ / platform\n"
    "   code on device. The heap is the plain allocator here — nothing longjmps\n"
    "   out of a parse-only run, so there is nothing to reclaim. */\n"
    "void ckpt_console_write(const char* d, int n) { (void)d; (void)n; }\n"
    "FILE* ckpt_console_stdout(void) { return stdout; }\n"
    "int ckpt_script_abort_requested(void) { return 0; }\n"
    "void* ckpt_script_malloc(size_t n) { return malloc(n); }\n"
    "void* ckpt_script_calloc(size_t n, size_t s) { return calloc(n, s); }\n"
    "void* ckpt_script_realloc(void* p, size_t n) { return realloc(p, n); }\n"
    "void ckpt_script_free(void* p) { free(p); }\n"
)
(build / "stubs.c").write_text("\n".join(body) + "\n")
PY

cat > "$build/main.c" <<'EOF'
#include "picoc.h"
#include "interpreter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv)
{
    int bad = 0;
    for (int i = 1; i < argc; i++) {
        FILE* f = fopen(argv[i], "rb");
        if (f == NULL) {
            fprintf(stderr, "%s: cannot open\n", argv[i]);
            bad = 1;
            continue;
        }
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        char* src = malloc(n + 1);
        if (fread(src, 1, n, f) != (size_t)n) {
            fprintf(stderr, "%s: short read\n", argv[i]);
            fclose(f);
            free(src);
            bad = 1;
            continue;
        }
        src[n] = '\0';
        fclose(f);

        Picoc pc;
        PicocInitialize(&pc, 256 * 1024);
        /* a parse error longjmps back here with the exit value set */
        if (!PicocPlatformSetExitPoint(&pc)) {
            PicocParse(&pc, argv[i], src, strlen(src), 0 /* RunIt */, 0, 1, 0);
            printf("  OK    %s\n", argv[i]);
        }
        else {
            printf("  FAIL  %s\n", argv[i]);
            bad = 1;
        }
        PicocCleanup(&pc);
    }
    return bad;
}
EOF

# Only the interpreter core: -maxdepth 1 skips picoc's own cstdlib/, which
# Checkpoint replaces with the newlib-safe copy under common/script/cstdlib.
# debug.c is picoc's interactive debugger; the console build drops it too.
mapfile -t picoc < <(find "$root/3rd-party/picoc/source/interpreter" -maxdepth 1 -name '*.c' ! -name 'debug.c' | sort)

gcc -o "$build/scriptlint" -w \
    -I"$root/3rd-party/picoc/include" -I"$root/common/script" -I"$root/common/script/cstdlib" \
    -DUNIX_HOST -D_GNU_SOURCE=1 \
    "${picoc[@]}" \
    "$root/common/script/platform_checkpoint.c" \
    "$root"/common/script/cstdlib/*.c \
    "$root/common/script/library_checkpoint.c" \
    "$build/stubs.c" "$build/main.c" -lm

if [ "$#" -gt 0 ]; then
    files=("$@")
else
    mapfile -t files < <(find "$root/scripts" -name '*.c' | sort)
fi

echo "parsing ${#files[@]} script(s):"
"$build/scriptlint" "${files[@]}"
