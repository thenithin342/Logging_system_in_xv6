#!/usr/bin/env python3
"""Run the actual freestanding printint bodies with sanitizer-checked output sinks."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="xv6-printint-") as temp:
    for name, call, expected in (("console.c", "printint(INT_MIN, 10, 1)", "ffffffff"),
                                 ("printf.c", "printint(1, INT_MIN, 10, 1)", "FFFFFFFF")):
        source = (ROOT/"xv6-public"/name).read_text()
        start = source.index("static void\nprintint(")
        # These xv6 functions close at column zero; retain the exact body.
        body = source[start:source.index("\n}", start)+2]
        prefix = r"""
#include <assert.h>
#include <limits.h>
#include <string.h>
typedef unsigned int uint;
static char output[64];
static unsigned pos;
static void sink(int c) { assert(pos+1 < sizeof(output)); output[pos++]=c; output[pos]=0; }
#define consputc(c) sink(c)
#define putc(fd,c) ((void)(fd), sink(c))
"""
        second = "printint(-1, 16, 0)" if name == "console.c" else "printint(1, -1, 16, 0)"
        code = prefix+body+'\nint main(void) { '+call+'; assert(strcmp(output,"-2147483648")==0); pos=0; '+second+'; assert(strcmp(output,"'+expected+'")==0); return 0; }'
        if name == "printf.c":
            code = code.replace("return 0; }", 'pos=0; printint(1,-1,10,0); assert(strcmp(output,"4294967295")==0); return 0; }')
        file = Path(temp)/name
        binary = Path(temp)/(name+".test")
        file.write_text(code)
        subprocess.run(["gcc", "-std=gnu11", "-O2", "-g", "-Wall", "-Wextra", "-Werror",
                        "-fno-builtin", "-fno-pie", "-no-pie", "-fsanitize=address,undefined",
                        "-fno-sanitize-recover=all", str(file), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
        print(f"PASS {name}: INT_MIN decimal and unsigned hex", flush=True)
