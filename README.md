# Assembler

A transpiler for [qyx22122/BattelASM](https://github.com/qyx22122/BattelASM). It creates ``.c`` files ready to include from ``main.c``.

## Syntax

The first line of the file must be a header line. The header line consists of the program name and program offset separated by a space. The program name must be a valid c variable name, and the offset must be within the range [0, 2^10 - program_size).

For opcodes see [BattelASM/arch.ods](https://github.com/qyx22122/BattelASM/blob/main/arch.ods).

### Comments

You can create comments with a semicolon (;). They can occupy a whole line or start somewhere in between, and they last to the end of the line. There are no block comments.

```asm
comments 0
;================================== (this whole line is a comment)
ldi 1 ;this is also a comment
```

### Code style and whitespaces

The assembler is quite permissive style-wise. Commas are considered whitespaces, and whitespaces are considered separators. Multiple whitespaces and leading whitespaces are ignored. Everything is case-insensitive.

### Numbers

Numbers can be written in decimal (e.g. ``123``), hexadecimal (e.g. ``0x7b``), and binary (e.g., `0b1111011`) systems. Dots are allowed when writing in binary and are ignored. Negative numbers aren't allowed, and too big numbers will raise errors.

### Variables

Although a bit controversial, the assembler supports variables. Variables are optional aliases for registers. The assembler will allocate the first free register for a new variable name and remember the mapping. Use ``#free <name>`` to release the register binding (the runtime value is not cleared).

Variables can contain everything but whitespaces, semicolons, and commas, and mustn't start with # or a digit. By convention, variables should be written in brackets, e.g., ``[start]``. They are case‑insensitive. It is possible to use both registers and variables in the same program, but it can quickly result in a mess, as variables can overwrite registers and vice versa, and it is strongly advised against.

You can turn variables off with the ``-novars`` flag.

Example:

```asm
variables 0
;==================================
mv [start], pc ;variables are by convention written inside square brackets...
addi '123=+..\/wtf, 1 ;...but you can make them as ugly as you desire (just stay in ascii)
shri [StArT], 1 ;they are - as everything else - case insensitive

#free [start] ;you can free them (but values stay so you should be super cautious. See "Directives & Constants")...
addi [bla], 1 ;so that other variables can use the freed registers
;because the values aren't freed, the [bla] now holds `pc / 2 + 1`
```

Which is equivalent to:

```asm
variables 0
;==================================
mv r1, pc
addi r2, 1
shri r1, 1
addi r1, 1
```

### Directives & Constants

There are also *directives* and *compile-time constants*. All of them start with #. Directives are in their own lines, while constants act like numbers.

- ``#starts param`` (param denotes the parameter): *Directive* to pad with flag instructions so before the next instruction, there are `param` instructions. This can be useful when developing the program, so that relative jumps need not be corrected when adding code.
- ``#free param``: *Directive* to mark the register, where the variable by the name of ``param`` is saved, free. In other words, it frees the register binding, but not its runtime value; later variables may reuse that register. The ``#size`` and co. do include it in their count.

- ``#size``: *Compile time constant* that expands to the number of instructions in the program.
- ``#before``: *Compile time constant* that expands to the number of instructions before the current instructions.
- ``#after``: *Compile time constant* that expands to the number of instructions after the current instructions.

Compile-time constants can be modified by adding a predetermined amount and multiplying them by a predetermined amount. So ``#constant:a:b`` means ``(#constant * b) + a``. You can omit adding and multiplying like this ``#constant`` and ``#constant:a``.

```asm
directives_and_constants 0
;==================================
ldi #size ;-> 9
#starts 5
ldi #after ;-> 3
ldi #before ;-> 6
ldi #before:-2 ;-> 5
ldi #before:+10:-1 ;-> 8*(-1) + 10 = 10 - 8 = 2
```

## Usage

Compile the assembler normally. For instance, with:

```bash
cc -O2 -Wall assembler.c -o assembler
```

To assemble a file, run the assembler on the file and pipe the output to the output file. This works as the errors are written to stderr. Note that the file will be overwritten even if the assembler fails.

```bash
./assembler example.asm > example.c
```

To avoid accidental overwrite on errors, you can use this instead:

```bash
./assembler example.asm > tmp.c && mv tmp.c example.c
```

There are some flags you can use:

- ``-vartable``: Append the final variable table to the output file as a comment.
- ``-nocomments``: Doesn’t copy source lines as comments to the output file.
- ``-decimal``: Emit instructions as decimal rather than binary.
- ``-obfuscate``: Equivalent to ``-nocomments -decimal``.

A very useful trick is to use a Makefile to assemble your bots with one command (change your assemble, bots, and main.c paths and set your own compiler of choice; put it into your main folder):

```Makefile
CC := cc
CFLAGS := -Wall -O3 -march=native

ASM := $(wildcard bots/*.asm)
GENC := $(ASM:.asm=.c)

.PHONY: all clean
all: main

assembler: assembler/assembler.c
    $(CC) $(CFLAGS) -o $@ $<

bots/%.c: bots/%.asm assembler
    @tmpfile=$$(mktemp) || exit 1; \
 echo "./assembler $< > $@"; \
 trap 'rm -f "$$tmpfile"' EXIT; \
 ./assembler -vartable $< > "$$tmpfile" && mv "$$tmpfile" $@

main: main.c $(GENC)
    $(CC) $(CFLAGS) -o $@ main.c

clean:
    $(RM) main assembler $(GENC)
```

## License

This project is licensed under the following terms:

You are free to use, modify, and redistribute this software, provided that:

1. Credit is given to Gregorcnik as the original author.
2. Any redistributed or derivative work is released under the same license.
3. No warranty is provided. Use at your own risk

