# SS 2025/2026 Toolchain Project

This repository contains a complete Level C systems-software course project: a one-input assembler, an architecture-independent linker, and an interpretive emulator for the specified 32-bit abstract computer.

The maintained solution is in `C:\dev\ssGithub\SS-Projekat\project\resenje`.

> **Naming note:** implementation file names intentionally use the Serbian spelling `asembler` (for example, `src\asembler.cpp`). The built executable is **`assembler`**, with two `s` characters, because that is the name used by the official defense scripts and the recorded project ruling.

## Contents

- [Repository layout](#repository-layout)
- [Architecture and end-to-end flow](#architecture-and-end-to-end-flow)
- [Build under WSL/Linux](#build-under-wsllinux)
- [Command-line tools](#command-line-tools)
- [Assembly language](#assembly-language)
- [Instruction encoding and literal pools](#instruction-encoding-and-literal-pools)
- [Textual object format](#textual-object-format)
- [Linker behavior](#linker-behavior)
- [Emulator behavior](#emulator-behavior)
- [Errors and output safety](#errors-and-output-safety)
- [Source-level API and implementation guide](#source-level-api-and-implementation-guide)
- [Official defense tests](#official-defense-tests)
- [Specification interpretations](#specification-interpretations)

## Repository layout

Paths in this section are Windows paths, matching the checkout.

| Path | Purpose |
|---|---|
| `C:\dev\ssGithub\SS-Projekat\project\resenje` | Maintained implementation and the directory from which the tools are built and tested. |
| `...\project\resenje\inc` | Public C++ declarations for the assembler, linker, object model, and emulator. |
| `...\project\resenje\src` | Hand-written C++ implementation, shared file-safety utilities, POSIX device adapter, and the three CLI entry points. |
| `...\project\resenje\misc` | Flex lexer and Bison parser inputs. Generated files belong in `build`, not in a submission archive. |
| `...\project\resenje\makefile` | C++17 build graph, flex/bison generation, dependency tracking, and cleanup. |
| `...\project\resenje\build` | Generated lexer/parser files, dependency files, and compiler object files. It is disposable build output. |
| `...\project\resenje\assembler`, `linker`, `emulator` | Locally built executables. They are build artifacts and are not part of the required submission contents. |
| `...\project\shared memory` | Append-only project record containing requirements analysis, design decisions, ownership history, implementation checkpoints, and validation results. |
| `...\01-ss-2025-2026-projekat-postavka-v1.0_organized.pdf` | Official project specification. |
| `...\v02-make.pdf` | Lecture material on GNU Make. |
| `...\v03a-konstrukcija-asemblera.pdf` | Lecture material on assembler construction, forward references, symbol tables, and literal pools. |
| `...\v03b-elf.pdf` | ELF concepts used as the model for sections, symbols, and RELA relocations. |
| `...\v04-emulatori.pdf` | Emulator design material. |
| `...\v05-makro-procesori.pdf` | Macro-processor lecture material; useful background, but macro expansion is not a project requirement and is not implemented. |
| `...\v06-linkeri.pdf` | Linker algorithms, symbol resolution, relocation, and section mapping. |
| `...\v02-make` | Progressive makefile examples accompanying the Make lecture. |
| `...\01-ss-2025-2026-projekat-odbrana-testovi\01-ss-2025-2026-projekat-odbrana-testovi` | Official Level A/B/C defense sources and launch scripts. The repeated directory name is intentional in the supplied archive. |
| `...\bee3\doc-review` and `...\bee4\pdf-text` | Historical working copies of text extracted from the PDFs for review/search. They are evidence and research artifacts, not runtime dependencies. |
| `...\bee1` | Historical independent review and adversarial reproduction fixtures. The currently modified implementation includes fixes for the confirmed findings recorded there. |
| `...\vodic-1.txt` | Informal project guide. The official PDF and supplied defense programs remain authoritative where historical notes conflict. |

The maintained solution directory contains `makefile`, `misc`, `inc`, and
`src`. Executables, `build`, generated flex/bison files, PDFs, defense tests,
and bee workspaces must not be included in the submitted solution archive.

## Architecture and end-to-end flow

```text
assembly source (.s)
    │
    ▼
Flex/Bison framing validation
    │
    ▼
semantic assembler ──► textual relocatable object (.o)
                              │
                 one or more │
                              ▼
                 merge → map → determine symbols → relocate
                              │
                    ┌─────────┴─────────┐
                    ▼                   ▼
          relocatable object (.o)   memory image (.hex)
                                            │
                                            ▼
                              strict loader → emulator
                                            │
                                            ▼
                               terminal output + halt report
```

The shared object model is deliberately small and architecture-independent. The assembler knows instruction encodings and creates sections, symbols, and `ABS32` relocations. The linker never decodes instructions: it concatenates bytes and applies metadata-driven 32-bit patches. The emulator only consumes the final addressed hex image.

### What happens from command line to program termination

The complete lifecycle is deliberately split into three processes. No tool
keeps hidden state for the next one; each stage writes a file that the next
stage validates and consumes.

1. **The assembler CLI validates its arguments.** It finds the one input and
   one output path, rejects aliases between them, reads the complete `.s` file,
   and calls `Assemble`. Output is written through a temporary file so a failed
   run cannot leave a partial or stale object.
2. **Flex and Bison validate physical source structure.** Flex converts text
   into broad tokens and Bison checks line framing: an optional label, at most
   one statement, then a newline. After the `.end` line Flex enters a discard
   state, so later bytes are not part of the program.
3. **The semantic assembler performs the real assembly pass.** It reads the
   original lines, creates/reopens sections, records symbols and `.equ`
   expressions, emits immediately encodable bytes, and records fixups for
   values that are not known yet. Branches and full-width operands use nearby
   literal pools; the assembler inserts pool islands before references can
   exceed their signed 12-bit reach.
4. **Assembly is finalized after all source lines are known.** Remaining pools
   are emitted, recursive `.equ` expressions are resolved, deferred
   displacements are patched, and unresolved global/external terms become
   relocations. The result is serialized as a strict textual object containing
   sections, symbols, and relocation tables.
5. **The linker reads every object before changing addresses.** Its object
   parser validates the complete file and rejects malformed order, indices,
   sizes, or payloads. Sections with the same name are concatenated in input
   order, and each original section receives a contribution offset into its
   merged section.
6. **The linker assigns addresses and resolves names.** Explicit `-place`
   requests are applied first; remaining sections are packed without overlap.
   Local symbols are interpreted within their input file, global definitions
   are collected once, imports are matched to those definitions, and each
   relocation writes `symbol value + addend` as a little-endian 32-bit word.
7. **The linker chooses one of two outputs.** In `-relocatable` mode it rewrites
   symbols and relocations into merged-section coordinates and emits another
   reusable object. In `-hex` mode it emits sorted addressed byte records that
   describe the emulator's initial memory.
8. **The emulator strictly loads the hex image.** It rejects malformed,
   duplicate, overlapping, or overflowing records and stores valid bytes in
   sparse memory. Missing addresses read as zero. The initial PC is
   `0x40000000`.
9. **Execution repeats fetch, decode, execute, then device polling.** Four
   instruction bytes are fetched in PDF order, the PC advances by four, and
   the decoded operation reads/writes registers, CSRs, memory, or the stack.
   `%r0` remains zero. Only after an instruction finishes are terminal and
   timer events collected, which keeps every instruction atomic.
10. **Interrupts transfer control through the program's handler.** Entry saves
    status and the already advanced return PC on the emulated stack, writes the
    cause CSR, adjusts the required mask bits, and jumps to `%handler`.
    `iret` restores PC and status in the matching reverse order. External
    events remain pending while masked.
11. **MMIO connects the abstract machine to the host.** Writes to `term_out`
    print the low byte, reads of `term_in` observe the newest terminal byte,
    and writes to `tim_cfg` restart the host timer. On a real POSIX terminal,
    the device layer temporarily enables raw, no-echo input and restores the
    original terminal state on normal exit or handled signals.
12. **`halt` ends the instruction loop.** The emulator restores host resources
    as device objects are destroyed and prints the final register report. Any
    parsing, linking, loading, or runtime validation error instead produces a
    diagnostic and a nonzero process exit.

### Why the implementation has two assembler passes

Flex/Bison and the C++ semantic assembler intentionally have different jobs.
The generated frontend cheaply rejects malformed lexical structure and enforces
physical-line boundaries. The semantic pass retains the original spelling and
line numbers needed for expressions, directives, helpful diagnostics, section
state, and delayed fixups. Keeping these responsibilities separate avoids
putting the complete assembler state machine into grammar actions while still
meeting the requirement to use Flex and Bison.

## Build under WSL/Linux

The binding target is Linux/amd64. From the repository root in WSL or another Debian/Ubuntu-like Linux environment:

```bash
sudo apt update
sudo apt install -y build-essential flex bison python3 gawk

make -C project/resenje clean
make -C project/resenje -j"$(nproc)"
```

Build one tool:

```bash
make -C project/resenje assembler
make -C project/resenje linker
make -C project/resenje emulator
```

Clean generated files and executables:

```bash
make -C project/resenje clean
```

The makefile uses `g++`, C++17, `-Wall -Wextra -O2`, generated dependency files, and build-local flex/bison outputs.

## Command-line tools

Examples below start at the repository root.

### Assembler

```text
assembler -o <output-file> <input-file>
assembler <input-file> -o <output-file>
```

```bash
cd project/resenje
./assembler -o program.o program.s
```

Exactly one source file is assembled. Input and output must not be the same file, including equivalent normalized paths, symlinks, or hard links detectable by the filesystem.

### Linker

```text
linker [options] <input-file>...

-o <output-file>                 required
-hex                             produce an addressed memory image
-relocatable                     produce another textual object file
-place=<section>@<address>       fix a merged section's base address
```

Exactly one of `-hex` and `-relocatable` is required.

```bash
cd project/resenje

./linker -hex \
  -place=text@0x40000000 \
  -o program.hex program.o

./linker -relocatable \
  -o combined.o first.o second.o
```

Addresses accept decimal or `0x`-prefixed hexadecimal notation.

### Emulator

```text
emulator <hex-image>
```

```bash
cd project/resenje
./emulator program.hex
```

### Complete example

```bash
cd project/resenje
./assembler -o e2e.o program.s
./linker -hex -place=text@0x40000000 -o e2e.hex e2e.o
./emulator e2e.hex
```

## Assembly language

### Lines, comments, names, and literals

- A physical line contains at most one instruction or directive.
- `#` begins a comment outside a quoted string.
- A label is `name:` at the start of a non-comment line, optionally after whitespace.
- A label may share a line with a statement or stand alone.
- A standalone label denotes the next source item. Automatically inserted literal-pool bytes are transparent to this rule.
- Names begin with a letter, `_`, or `.`, followed by letters, digits, `_`, or `.`.
- General registers are `%r0` through `%r15`; `%sp` aliases `%r14`, and `%pc` aliases `%r15`.
- CSRs are `%status`, `%handler`, and `%cause`.
- Numeric literals are decimal or `0x`/`0X` hexadecimal. A leading zero does not select octal.
- Unary `+`/`-`, binary `+`/`-`, and parentheses are supported in expressions.
- Values must remain within the accepted 32-bit range. A relocatable expression must reduce to either an absolute value or one section-relative term with coefficient `+1`.

### Directives

| Directive | Meaning |
|---|---|
| `.global a, b` | Mark symbols as globally visible. A local definition is exported; a symbol left undefined is emitted as `GLOB UND` for resolution by the linker. |
| `.extern a, b` | Declare imported global symbols. An extern cannot also be locally defined or declared global. |
| `.section name` | Select a section. Repeating a name reopens that section and appends to it. |
| `.word expr, ...` | Emit one little-endian 32-bit word per expression. Absolute expressions are written directly; relocatable expressions create `ABS32` records. |
| `.skip literal` | Emit the given non-negative number of zero bytes. |
| `.ascii "text"` | Emit bytes without a terminating NUL. Supported escapes are `\n`, `\r`, `\t`, `\\`, `\"`, and `\0`. |
| `.equ name, expr` | Define an assembly-time expression. Forward dependencies are supported; cycles, external dependencies, overflow, and non-relocatable results are rejected. Absolute exported values use the `ABS` section index. |
| `.end` | Finish assembly and discard all remaining source text, including otherwise invalid tokens. |

Statements that emit bytes require an active section. Labels also require an active section.

### Instructions

All source operands are written in source-first, destination-second order where two registers are present.

| Instruction | Architectural effect |
|---|---|
| `halt` | Stop execution. |
| `int` | Enter the interrupt handler with cause `4`. |
| `iret` | Restore status and return PC using the stack-consistent two-instruction expansion described below. |
| `call target` | Push the next PC and branch to `target`. |
| `ret` | Pop PC. |
| `jmp target` | Unconditional branch. |
| `beq %r1, %r2, target` | Branch when registers are equal. |
| `bne %r1, %r2, target` | Branch when registers differ. |
| `bgt %r1, %r2, target` | Branch when `%r1` is signed-greater than `%r2`. |
| `push %r1[, %r2[, %r3]]` | Push one to three registers left-to-right; the last listed register ends at the top of the stack. |
| `pop %r` | Load the register and postincrement SP by four. |
| `inc %r` | Increment the register by one without using a temporary register. |
| `xchg %src, %dst` | Atomically exchange two registers. |
| `add %src, %dst` | `%dst = %dst + %src`. |
| `sub %src, %dst` | `%dst = %dst - %src`. |
| `mul %src, %dst` | `%dst = %dst * %src`. |
| `div %src, %dst` | Signed `%dst = %dst / %src`; invalid division raises cause `1`. |
| `not %r` | Bitwise complement. |
| `and %src, %dst` | Bitwise AND. |
| `or %src, %dst` | Bitwise OR. |
| `xor %src, %dst` | Bitwise XOR. |
| `shl %src, %dst` | Logical left shift by the low five bits of `%src`. |
| `shr %src, %dst` | Logical right shift by the low five bits of `%src`. |
| `ld operand, %dst` | Load an immediate, register value, or memory word. |
| `st %src, operand` | Store to a register or memory word. Immediate destinations are invalid. |
| `csrrd %csr, %dst` | Copy a CSR into a GPR. |
| `csrwr %src, %csr` | Copy a GPR into a CSR. |

### `ld` and `st` addressing

| Form | `ld` meaning | `st` meaning |
|---|---|---|
| `$literal`, `$symbol`, `$expression` | Immediate value. Small signed-12-bit constants are encoded directly; other values use a pool. | Not supported. |
| `literal`, `symbol`, `expression` | Load `mem32[address]`. | Store to `mem32[address]`. |
| `%reg` | Copy register to destination. | Copy source to the named register. |
| `[%reg]` | Load from `mem32[reg]`. | Store to `mem32[reg]`. |
| `[%reg + expr]`, `[%reg - expr]` | Load from `mem32[reg + displacement]`. | Store to `mem32[reg + displacement]`. |
| `[%reg1 + %reg2]` | Load from `mem32[reg1 + reg2]`. | Store to `mem32[reg1 + reg2]`. |

Register-relative displacement expressions must be fully known during assembly and fit signed 12 bits (`-2048..2047`). In practice, a symbolic displacement must resolve through an absolute `.equ`; unresolved or section-relative symbols are rejected.

`call`, `jmp`, `beq`, `bne`, and `bgt` accept a literal or symbol/expression target. The assembler routes these through a nearby literal pool so the full 32-bit target remains linkable.

## Instruction encoding and literal pools

Every machine instruction is exactly four bytes. Logically, its 32 bits are:

```text
31          24 23      20 19      16 15      12 11                  0
+--------------+----------+----------+----------+---------------------+
| opcode | mod |   RegA   |   RegB   |   RegC   | signed Disp[11:0] |
+--------------+----------+----------+----------+---------------------+
```

The instruction bytes are emitted in the order shown by the specification:

```text
byte I:   opcode << 4 | modifier
byte II:  RegA << 4 | RegB
byte III: RegC << 4 | Disp[11:8]
byte IV:  Disp[7:0]
```

The emulator sign-extends bit 11 of `Disp`. Register arithmetic and effective addresses wrap modulo `2^32`.

### Opcode/modifier families

| Opcode | Valid modifiers | Operation |
|---|---|---|
| `0x0` | `0` | `halt` |
| `0x1` | `0` | software interrupt |
| `0x2` | `0`, `1` | direct or memory-indirect call |
| `0x3` | `0..3`, `8..11` | direct/indirect unconditional, equal, not-equal, signed-greater jumps |
| `0x4` | `0` | `xchg` |
| `0x5` | `0..3` | add, subtract, multiply, signed divide |
| `0x6` | `0..3` | not, and, or, xor |
| `0x7` | `0`, `1` | left and logical-right shift |
| `0x8` | `0`, `1`, `2` | direct store, preincrement store, memory-indirect store |
| `0x9` | `0..7` | CSR/GPR loads, immediate/address calculation, memory loads, postincrement loads |

Reserved opcode/modifier/field combinations cause an invalid-instruction interrupt rather than being interpreted loosely.

### Pseudo-instruction lowering

- `iret` emits two instructions: load `status` from `[sp + 4]`, then load `pc` from `[sp]` while advancing `sp` by `8`.
- `ret`, `push`, and `pop` use the architecture's postincrement/preincrement load/store forms.
- Absolute memory `ld` first loads the address through a pool and then dereferences it.
- Absolute memory `st` uses the memory-indirect store form through a pool.
- Calls and branches use indirect pool-based encodings.

### Literal pools

Literal pools hold wide constants and relocatable addresses that cannot live directly in the signed 12-bit displacement.

- Pools are maintained independently per section.
- Equal expressions in the same pending pool are deduplicated.
- References are PC-relative to the next instruction.
- Before the earliest reference would exceed `+2047`, the assembler emits an island: a direct jump over the pool followed by its four-byte entries.
- Large directives remain contiguous; they are not split internally.
- Later references start a new pool, so large sections can contain multiple islands.
- Source labels are queued until any required island has been emitted, ensuring a label still names the following source instruction/data rather than the synthetic jump or pool.
- Pool entries use ordinary `.word` resolution and can therefore carry `ABS32` relocations.

## Textual object format

The assembler and `linker -relocatable` use version 1 of a strict, textual, RELA-style format:

```text
#objfile 1
#section <name> <byte-count>
<two-digit hex bytes, normally eight per emitted line>
...
#symtab <symbol-count>
<num> <value> <type> <bind> <ndx> <name>
...
#rela <relocation-count>
<section-ndx> <offset> <type> <symbol-num> <addend>
...
```

Example:

```text
#objfile 1
#section text 8
00 00 00 00 00 00 00 00
#symtab 2
0 0 SCTN LOC 0 text
1 0 NOTYP GLOB UND external
#rela 1
0 4 ABS32 1 0
```

### Record fields

- `#objfile 1`: mandatory first non-comment record; no other version is accepted.
- `#section name byte-count`: sections occur first and define section indices by order. Names must be unique within one object. Exactly `byte-count` two-digit hexadecimal bytes follow.
- `#symtab count`: exactly one symbol table follows all sections.
- `num`: zero-based symbol number; entries must be consecutive.
- `value`: unsigned 32-bit decimal or `0x`-prefixed value. For ordinary defined symbols it is section-relative; for `ABS` it is the final absolute value.
- `type`: `SCTN` for section symbols or `NOTYP` for ordinary symbols.
- `bind`: `LOC` or `GLOB`.
- `ndx`: a numeric section index, `UND` for imports, or `ABS` for absolute symbols. An `UND` symbol must be `GLOB`.
- `#rela count`: exactly one relocation table follows the symbol table and is the final record group.
- `section-ndx`: section containing the four-byte patch site.
- `offset`: byte offset of that site inside the section; four bytes must fit.
- `type`: only `ABS32` exists.
- `symbol-num`: index into the already parsed symbol table.
- `addend`: signed 32-bit explicit addend.

The relocation formula is:

```text
content[P : P+4] := uint32(S + A), stored little-endian
```

`P` is the recorded patch offset in its target section, `S` is the symbol value in the current link mode, and `A` is the explicit signed addend interpreted modulo `2^32`.

Blank lines and lines whose first non-whitespace character is `;` are skipped by the parser. Inline `;` text is not an inline-comment facility. The parser rejects missing, duplicate, or out-of-order tables; sections after `#symtab`; unknown records/types/bindings; invalid indices; malformed numbers/bytes; oversized payloads; and relocation sites outside their section.

## Linker behavior

### Phases

1. **Ingest:** parse every input with the strict object reader.
2. **Merge:** concatenate same-named input sections in command-line order. Each original section becomes a contribution with an offset in the merged section.
3. **Map (`-hex`):** honor explicit placements, place all remaining merged sections consecutively after the highest explicit section end, and reject address overflow or overlap.
4. **Determine symbols:** calculate contribution-adjusted section-relative global values, preserve absolute symbols, and reject duplicate definitions.
5. **Resolve (`-hex`):** compute each referenced symbol and apply `ABS32` patches.
6. **Emit:** produce either an addressed hex image or a reusable merged object.

### `-hex`

- Explicitly placed sections use their requested base addresses.
- Unplaced sections retain first-appearance order and begin immediately after the highest placed section end; if nothing is placed, packing starts at address zero.
- Same-named sections have already been concatenated before placement.
- No implicit alignment or padding is inserted.
- A section may end exactly at `0x100000000`; no unplaced section can then receive a representable base address.
- Placement overlaps and sections extending beyond the 32-bit address space fail.
- An unknown `-place` section produces a warning and is ignored.
- Duplicate definitions and unresolved imports fail.
- Output records are sorted by address, use eight uppercase address digits, lowercase bytes, and at most eight bytes per line:

```text
40000000: 05 00 10 91 03 00 20 91
40000008: 00 20 11 50 00 00 00 00
```

Only bytes contributed by sections are emitted; gaps have no records.

### `-relocatable`

- `-place` options are accepted but completely ignored.
- Sections are merged from relative address zero.
- Duplicate global definitions fail.
- Unresolved global imports remain `GLOB UND` and may be resolved by a later link.
- Defined globals remain section-relative; exported absolute symbols remain `ABS`.
- Local-symbol relocations are rewritten against the merged section symbol, folding the original contribution offset and local symbol value into the addend.
- The output is accepted by the same parser and can be linked again without changing the final image.

## Emulator behavior

### CPU and memory

- Sixteen 32-bit GPRs: `r0..r15`.
- `r0` always reads as zero and ignores writes.
- `r14` is `sp`; the stack grows downward.
- `r15` is `pc`; instruction fetch advances it by four before execution.
- Three 32-bit CSRs: `status` (`0`), `handler` (`1`), and `cause` (`2`).
- Initial PC is `0x40000000`.
- Memory is a sparse byte map. Missing bytes read as zero.
- Words are four little-endian bytes. Unaligned accesses are supported.
- Runtime address arithmetic wraps on the 32-bit address bus.

The linked-hex loader requires:

- exactly eight hexadecimal address digits;
- at least one literal space immediately after `:`; later whitespace is accepted;
- one to eight two-digit hexadecimal bytes per non-empty line;
- no blank lines, duplicate/overlapping bytes, malformed tokens, or records crossing `0xFFFFFFFF`.

### Execution details

- Each instruction is atomic. External devices are polled only after it completes.
- Signed comparison is used by `bgt`.
- Division is signed. Division by zero and `INT32_MIN / -1` raise cause `1` without changing the destination.
- Shift counts use the low five bits; right shift is logical.
- Invalid or reserved encodings enter the interrupt handler with cause `1`.

### Interrupts

Cause values:

| Cause | Meaning |
|---:|---|
| `1` | Invalid instruction or invalid arithmetic operation |
| `2` | Timer |
| `3` | Terminal |
| `4` | Software interrupt |

On invalid-instruction, timer, or terminal interrupt entry:

1. push the old `status`;
2. push the already advanced return `pc`;
3. write `cause`;
4. set global external-mask bit 2 with `status |= 0x4`;
5. jump to `handler`.

The `int` instruction performs the PDF-defined software-interrupt sequence:
it saves status and the already advanced PC, writes cause `4`, clears timer-mask
bit 0 with `status &= ~0x1`, and jumps to `handler`.

Status bits are `Tr=bit 0` (timer mask), `Tl=bit 1` (terminal mask), and `I=bit 2` (global external mask), where `1` means masked.

Synchronous invalid-instruction and software interrupts ignore external masks. External priority is terminal before timer. Masked and lower-priority external requests remain pending and are serviced once eligible.

### MMIO and host devices

| Register | Address range | Behavior |
|---|---|---|
| `term_out` | `0xFFFFFF00..0xFFFFFF03` | A 32-bit store whose effective address is exactly `0xFFFFFF00` writes the low byte to stdout and flushes it. |
| `term_in` | `0xFFFFFF04..0xFFFFFF07` | The newest available keyboard byte is stored as a little-endian 32-bit value and raises a terminal request. |
| `tim_cfg` | `0xFFFFFF10..0xFFFFFF13` | A store at exactly `0xFFFFFF10` selects and restarts the timer period using the low three bits. |

Timer periods:

| Value | Period |
|---:|---:|
| `0` | 500 ms |
| `1` | 1000 ms |
| `2` | 1500 ms |
| `3` | 2000 ms |
| `4` | 5000 ms |
| `5` | 10 s |
| `6` | 30 s |
| `7` | 60 s |

The reset value is `0`. Writes to interior bytes update memory but do not independently trigger a terminal or timer action.

On POSIX, a real TTY is temporarily placed in noncanonical, no-echo mode. Input is unbuffered in the architectural sense: a later byte replaces an unread earlier byte. Polling drains a bounded batch and retains the newest byte. Non-TTY stdin is not treated as keyboard input. Terminal settings are restored during normal destruction, exception unwinding, and handled termination signals.

### Halt report

The emulator prints no implementation output while the program runs, apart from program-generated terminal characters. After `halt`, it prints:

```text
-----------------------------------------------------------------
Emulated processor executed halt instruction
Emulated processor state:
 r0=0x00000000    r1=0x00000000    r2=0x00000000    r3=0x00000000
 r4=0x00000000    r5=0x00000000    r6=0x00000000    r7=0x00000000
 r8=0x00000000    r9=0x00000000   r10=0x00000000   r11=0x00000000
r12=0x00000000   r13=0x00000000   r14=0x00000000   r15=0x00000000
```

Values reflect the final state and are printed as eight lowercase hexadecimal digits.

## Errors and output safety

All three tools print diagnostics to stderr and exit nonzero on failure.

- The assembler and linker reject input/output aliasing before deleting or replacing files.
- Assembler output is staged in `<output>.tmp` and moved into place only after complete assembly and serialization.
- Linker output is staged in `<output>.tmp` and moved into place only after the complete link and write.
- After a valid assembler CLI has identified distinct input/output paths, an assembly or write failure removes temporary and stale destination files.
- Linker failures remove temporary output and any stale destination identified by one unambiguous, non-aliased `-o`, including argument-validation failures where that cleanup is safe.
- Malformed input never becomes a partially accepted default object/image.
- The emulator does not begin execution unless the complete input image parses successfully.

## Source-level API and implementation guide

This section explains project-defined calls that are more substantial than ordinary C/C++ library operations.

### `inc\asembler.hpp` and assembler implementation

| API/function | Role |
|---|---|
| `AssemblerError` | Exception carrying source/CLI assembly diagnostics. |
| `Assemble(source, origin)` | Public assembly entry point. Runs flex/bison framing validation in the normal build, then semantic assembly, fixup resolution, pool flushing, and object construction. |
| `ValidateAssemblerSource(source, origin)` | Creates a reentrant flex scanner, supplies a newline-terminated memory buffer, invokes Bison, and converts scanner/parser failures into `AssemblerError`. |
| `Trim`, `IsName`, `SplitOperands` | Normalize source fragments, validate identifier spelling, and split comma lists while respecting strings, brackets, and parentheses. |
| `ExpressionParser::Parse` | Parses an entire linear expression and rejects trailing tokens. |
| `ExpressionParser::Skip` | Advances over expression whitespace. |
| `ParseSum`, `ParseUnary`, `ParsePrimary` | Recursive-descent precedence layers for binary `+/-`, repeated unary signs, literals, symbols, and parenthesized expressions. |
| `Assembler::Run` | Coordinates line parsing, `.equ` resolution, final pool flush, symbolic displacement resolution, and object creation. |
| `Error` | Throws a file-and-line-qualified `AssemblerError`. |
| `RequireSection` | Returns the active section or rejects a statement that cannot exist outside a section. |
| `GetSymbol` | Validates/interns a symbol while preserving first-seen order. |
| `Fits12` | Checks the architectural signed displacement range. |
| `EmitInstruction` | Encodes one four-byte instruction and, when enabled, first keeps literal-pool reach valid and binds pending source labels. |
| `EmitPoolInstruction` | Emits a placeholder PC-relative instruction, deduplicates its expression in the pending pool, and records the reference for patching. |
| `PatchDisplacement` | Writes a signed 12-bit displacement into bytes III and IV of an existing instruction. |
| `EnsurePoolReach` | Predicts whether upcoming bytes would push the earliest pool reference out of range and flushes before that happens. |
| `FlushPool` | Emits a jump over the island, reserves pool words, creates word fixups, patches all references, and clears the pending pool. |
| `Register`, `Csr`, `Literal` | Decode GPR aliases, decode CSR names, and recognize fully absolute literal expressions. |
| `DefineLabel`, `QueueLabel`, `BindPendingLabels` | Implement source-label definition and the transparency rule around automatically inserted pool islands. |
| `ParseLines` | Removes comments outside strings, recognizes an optional leading label, and dispatches one directive or instruction per line until `.end`. |
| `Directive` | Implements all eight directives, section reopening, declarations, data emission, escapes, and expression definitions. |
| `Instruction` | Validates mnemonic arity and selects the exact machine or pseudo-instruction encoding. |
| `MemoryOperand` | Parses bracketed register addressing and separates the base register from an optional signed expression. |
| `EmitRegisterMemory` | Encodes a known signed-12-bit displacement or records a deferred absolute-expression fixup. |
| `Load`, `Store` | Select addressing modes and any required multi-instruction/pool lowering. |
| `ResolveSymbol` | Recursively evaluates symbols and `.equ` definitions, detects cycles/external dependencies, and classifies absolute versus section-relative results. |
| `ResolveExpression` | Substitutes symbols into a linear expression and verifies that the result is absolute or singly relocatable. |
| `ResolveEquations` | Forces every defined `.equ` through recursive resolution; undefined globals remain imports for `BuildObject`. |
| `FlushAllPools` | Emits every section's final pending pool. |
| `ResolveDisplacements` | Resolves symbolic register displacements and patches them after all equations are known. |
| `BuildObject` | Moves final section bytes into `ObjectFile`, creates section/ordinary symbols, writes absolute words, and emits relocation records. |
| CLI `Usage`, `PathsAlias`, and `main` | Print assembler syntax, detect equivalent filesystem paths, validate argument order, read one source, and perform staged replacement/cleanup. |

Important internal records are `Linear` (constant plus symbolic coefficients), `SymbolState`, `WordFixup`, `DisplacementFixup`, `PoolEntry`, `PoolReference`, `PendingLabel`, and `SectionState`.

### `inc\object.hpp` and `src\object.cpp`

| API/function | Role |
|---|---|
| `Section`, `Symbol`, `Relocation`, `ObjectFile` | In-memory representation shared by assembler and linker. |
| `ObjectFile::FindSection` | Linear lookup by section name, returning `kSectionUndefined` when absent. |
| `ParseObject` | Strict stateful parser for object version 1 and all structural/range checks. |
| `ReadObjectFile` | Reads a complete file and delegates to `ParseObject`. |
| `WriteObject` | Deterministically serializes sections, symbols, and relocations. |
| `WriteObjectFile` | Writes the serialized object and checks open/flush failures. The assembler CLI stages output before replacing the destination. |
| `SectionIndexToString`, `BindToString`, `TypeToString`, `RelocationTypeToString` | Canonical textual spellings. |
| `LineReader::Next`, `AtEnd`, `Error` | Skip permitted blank/comment lines, track line numbers, and form precise parser errors. |
| `Split` | Tokenizes one object-format record on whitespace. |
| `ParseU32`, `ParseI32`, `ParseHexByte`, `ParseSectionIndex` | Strict numeric and index readers that reject overflow and trailing garbage. |

### `inc\linker.hpp` and linker implementation

| API/function | Role |
|---|---|
| `ParseArguments` | Parses modes, output, inputs, and placements; enforces exclusivity and protects input aliases. |
| `Linker::Ingest` | Reads every object in command-line order. |
| `MergeSections` | Creates one `MergedSection` per name and records each input `Contribution`. |
| `MapSections` | Applies explicit placement, packs unplaced sections with a 64-bit boundary cursor, and detects overflow/overlap. |
| `DetermineSymbols` | Builds the global definition map, adjusts values by contribution offsets, preserves `ABS`, and performs mode-dependent undefined checks. |
| `SymbolValueFor` | Computes the current-mode value of an absolute, global, or file-local relocation symbol. |
| `ResolveRelocations` | Finds each merged patch site and writes `S + A` as four little-endian bytes. |
| `EmitHex` | Sorts non-empty sections by base and emits addressed rows of up to eight bytes. |
| `BuildRelocatable` | Produces merged sections, rewritten section/global/import symbols, and merged-coordinate relocations. |
| `Hex32`, `AddOverflows` | Format 32-bit addresses and detect a section end beyond the address space. |
| `PathsAlias` | Detects lexically equivalent and filesystem-equivalent input/output paths. |
| `RemoveOutputAfterParseFailure` | Safely identifies and removes stale linker output after invalid arguments without deleting an aliased input. |
| `WriteTextFileAtomically` | Shared file utility that stages text, verifies completion, and commits the temporary file so partial output is never exposed. |
| CLI `PrintUsage` and `main` | Present linker syntax, run the mode-specific phase sequence, classify expected exceptions, and enforce cleanup. |

`Contribution` maps an original input section into a merged section. `ResolvedSymbol` records either an absolute value or a merged-section/value pair.

### `inc\emulator.hpp` and emulator implementation

| API/function | Role |
|---|---|
| `ParseHexImage`, `ReadHexImage` | Strictly parse/load the linker's addressed text format into sparse byte memory. |
| `Devices::Poll` | Returns terminal/timer events available after the current instruction. |
| `Devices::WriteTerminal` | Handles a `term_out` base write. |
| `Devices::ConfigureTimer` | Selects and restarts a timer period. |
| `CreateHostDevices` | Constructs the POSIX-backed terminal/timer implementation. |
| `Emulator(image, devices)` | Installs memory/devices, initializes PC, and initializes `tim_cfg` to zero through MMIO. |
| `Step` | Executes one instruction, reasserts `r0=0`, polls/services external interrupts, and reports whether execution continues. |
| `Run` | Calls `Step` until `halt`. |
| `StateReport` | Produces the required final register report. |
| `registers`, `csrs`, `InspectWord` | Read-only observation seams used by tests and embedders. |
| `ReadByte`, `ReadWord`, `WriteByte`, `WriteWord` | Sparse byte memory and little-endian word access; word writes also dispatch base-address MMIO actions. |
| `ReadRegister`, `WriteRegister` | Centralize hardwired-zero semantics. |
| `Push`, `Pop` | Implement downward-growing, four-byte stack operations. |
| `EnterInterrupt` | Handles invalid-instruction and external interrupts by saving status/PC, setting cause and the global mask, and vectoring through `handler`. |
| `EnterSoftwareInterrupt` | Implements the distinct PDF software-interrupt transition, including clearing timer-mask bit 0. |
| `PollAndServiceExternalInterrupts` | Updates `term_in`, latches requests, applies masks, and enforces terminal-before-timer priority. |
| `ExecuteOne` | Fetches, decodes, validates, and executes the complete ISA. |
| `InvalidInstruction` | Enters cause `1`. |
| `HexDigit`, `ParseAddress`, `ParseByte` | Validate and decode fixed-width hex-image fields. |
| `HostDevices` constructor/destructor | Configure a real POSIX TTY and signal restoration, initialize the timer deadline, and restore host state on teardown. |
| `HostDevices::Poll` | Advances the steady-clock timer and non-blockingly drains a bounded amount of TTY input, retaining the newest byte. |
| `HostDevices::WriteTerminal`, `ConfigureTimer`, `Period` | Flush terminal output, restart timer configuration, and map values `0..7` to periods. |
| `RestoreTerminalForSignal` | Restores saved terminal settings before re-raising selected POSIX signals. |

### CLI entry points and generated frontend

- `src\file_utils.cpp`: centralizes filesystem-alias checks, stale-output cleanup, temporary-file commits, and staged text writes used by both producer CLIs.
- `src\asembler_main.cpp`: validates the four-argument CLI, protects input paths, reads source, invokes `Assemble`, and replaces output only after staging succeeds.
- `src\linker_main.cpp`: prints usage, separates argument-validation cleanup from link execution, selects the correct phase sequence for each mode, and delegates staged writing to the shared file utility.
- `src\emulator.cpp`: contains the strict image loader and architecture-independent CPU/memory execution core.
- `src\emulator_devices.cpp`: contains the POSIX terminal, signal-restoration, and steady-clock timer adapter.
- `src\emulator_main.cpp`: loads one image, creates host devices, runs to halt, and prints the state report.
- `misc\asembler_lexer.l`: reentrant lexer for whitespace, `#` comments, newlines, directives, registers, CSRs, decimal/hex numbers, strings, words, and punctuation. It enters a discard state after `.end`; unknown characters before then become `INVALID`.
- `misc\asembler_parser.y`: pure parser that validates physical source framing: empty lines, optional leading labels, one statement per line, and legal token sequences. Detailed operand meaning remains in the semantic C++ core.
- `build\asembler_parser.*` and `build\asembler_lexer.*`: generated by Bison/Flex. They are intentionally not source-controlled submission inputs.

## Official defense tests

The separate official defense tree contains:

- **Level A:** six units exercising global/extern linkage, software interrupts, stack calls, arithmetic routines, cross-file data, two explicit placements, final linking, and emulation.
- **Level B:** terminal-driven counting and `.ascii` output.
- **Level C:** terminal/timer interaction, MMIO constants via `.equ`, and same-section difference expressions.

The tool paths at the top of each supplied `start.sh` must point to the three
built executables. The scripts use CRLF line endings in this checkout, so copy
or save them with LF endings before running them in the Linux defense VM.

## Specification interpretations

The implementation follows the official PDF specification. Historical decisions in `project\shared memory` remain useful design context, but the PDF and its supplied defense programs take precedence where they disagree:

- Target the complete Level C feature set.
- Build and defend on Linux/amd64 with `g++` and C++17.
- Use the executable name `assembler`, despite source files named `asembler` and the spelling in parts of the Serbian specification.
- Use flex and bison for source framing/lexical validation.
- Use one textual object format only, with RELA-style explicit addends.
- Resolve every `.equ` during assembly; represent exported absolute values with `ABS`.
- Emit an undefined `.global` as `GLOB UND`, matching the supplied Level B/C programs; `.extern` remains the explicit import directive.
- Reject a used symbol that is neither defined nor declared through `.global` or `.extern`.
- Emit reusable section-relative `-relocatable` output.
- Ignore `-place` in relocatable mode.
- Do not implement archives, dynamic linking, shared objects, PIC, GOT, PLT, or macro processing.
- Implement stack-consistent `iret`: interrupt entry pushes status then PC, and return restores PC then status through the assembler's two-instruction sequence.
- Emit and fetch instruction bytes in the PDF's displayed `I, II, III, IV` order.
- For `int`, follow the PDF pseudocode and clear status bit 0; invalid-instruction and external interrupts set global mask bit 2.
- Retain masked external requests; prioritize terminal over timer.
- Treat division edge cases and reserved encodings as invalid instructions.
- Support unaligned little-endian words and zero-filled sparse memory.
- Insert no implicit linker alignment.

These choices are deliberate specification-compatibility rules, not accidental omissions.
