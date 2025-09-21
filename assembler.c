/*
Transpiles assembly like code to binary for BattelASM. Made by Gregorcnik.
===================================================================
Compile normally (cc -o assembler assembler.c for instance). 
The program outputs the transpiled c code to stdout and errors to stderr, so you can do 
./assembler example.asm > example.c
For other options type ./assembler -help

This project is licensed under the following terms:
You are free to use, modify, and redistribute this software, provided that:
 - Credit is given to Gregorcnik as the original author.
 - Any redistributed or derivative work is released under the same license.
No warranty is provided. Use at your own risk.
*/

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define inside(low, mid, high) ((low) <= (mid) && (mid) <= (high))
typedef uint16_t fint;

#ifdef _WIN32
#define strncasecmp _strnicmp
#define strcasecmp _stricmp
#endif

#if defined(_WIN32) && !defined(HAVE_GETLINE)
#include <BaseTsd.h> // for SSIZE_T on MSVC
typedef SSIZE_T ssize_t;

ssize_t getline(char **lineptr, size_t *n, FILE *stream) {
	if (!lineptr || !n || !stream) {
		errno = EINVAL;
		return -1;
	}

	if (*lineptr == NULL || *n == 0) {
		*n = 128;
		*lineptr = (char *)malloc(*n);
		if (*lineptr == NULL) {
			errno = ENOMEM;
			return -1;
		}
	}

	size_t pos = 0;
	int c = 0;

	while ((c = fgetc(stream)) != EOF) {
		if (pos + 1 >= *n) { // need room for c and '\0'
			size_t new_size = (*n > SIZE_MAX / 2) ? SIZE_MAX : (*n * 2);
			if (new_size <= *n) { // overflow or stuck
				errno = EOVERFLOW;
				return -1;
			}
			char *new_ptr = (char *)realloc(*lineptr, new_size);
			if (new_ptr == NULL) {
				errno = ENOMEM;
				return -1;
			}
			*lineptr = new_ptr;
			*n = new_size;
		}

		(*lineptr)[pos++] = (char)c;
		if (c == '\n')
			break;
	}

	if (pos == 0 && c == EOF) {
		return -1; // EOF with no data read
	}

	(*lineptr)[pos] = '\0';
	return (ssize_t)pos;
}
#endif

enum {
	SP = 30,
	PC = 31,
};

enum {
	OP_LDI = 0x0,
	OP_MV = 0x20,
	OP_ADD,
	OP_SUB,
	OP_NOT,
	OP_AND,
	OP_OR,
	OP_XOR,
	OP_SHL,
	OP_SHR,
	OP_JMP,
	OP_JZ,
	OP_JNZ,
	OP_JN,
	OP_JP,
	OP_LD,
	OP_ST,
	OP_PUSH,
	OP_POP,
	OP_ADDI,
	OP_SUBI,
	OP_SHLI,
	OP_SHRI,
	OP_FLAG = 0x3f
};

typedef struct {
	bool comments, var_table, decimal_instr, vars;
} Options;

static char ERROR_TEXT[256];

int parseNum(char *s, int *ret);
int parseConst(char *s, size_t program_size, size_t instruction_num, int *ret);
void writeBin(FILE *fout, fint n);
int getRegister(char *symbol, fint *ret, bool use_vars);
int getOperation(char *symbol, fint *ret);
int compileLine(char *line, size_t program_size, size_t instruction_num, fint *ret, bool use_vars);
int compileFile(FILE *fin, Options *opts);
void countInstructions(FILE *fin, size_t *ret);

int main(int argc, char *argv[]) {
	srand(time(0));

	int argi = 1;
	Options opts = {.comments = true, .var_table = false, .decimal_instr = false, .vars = true};

	for (; argi < argc; argi++) {
		char *p = argv[argi];
		if (p[0] != '-')
			break;
		if (strcmp(p, "-nocomments") == 0)
			opts.comments = false;
		else if (strcmp(p, "-vartable") == 0)
			opts.var_table = true;
		else if (strcmp(p, "-novars") == 0)
			opts.vars = false;
		else if (strcmp(p, "-decimal") == 0)
			opts.decimal_instr = true;
		else if (strcmp(p, "-obfuscate") == 0) {
			opts.comments = false;
			opts.decimal_instr = true;
		} else if (strcmp(p, "-help") == 0)
			goto usage;
		else {
			fprintf(stderr, "Unknown parameter '%s'\n", p);
			goto usage;
		}
	}

	if (!opts.vars && opts.var_table) {
		fprintf(stderr, "-novars and -vartable aren't compatible.\n");
		goto usage;
	}

	if (argi >= argc) {
		fprintf(stderr, "Input file not specified.\n");
		goto usage;
	}
	if (argi < argc - 1) {
		fprintf(stderr, "Parameters after input file (%s) are prohibited\n", argv[argi]);
		goto usage;
	}

	FILE *fin = fopen(argv[argi], "r");
	if (!fin) {
		perror("fopen");
		return 1;
	}

	int rc = compileFile(fin, &opts);
	fclose(fin);
	return rc != 1;

usage:
	fprintf(stderr, "Usage: %s -help -nocomments [-vartable -novars] -decimal -obfuscate <input.asm>\n", argv[0]);
	return 1;
}

char variables[32][255];

void init_variables() {
	strcpy(variables[30], "sp");
	strcpy(variables[31], "pc");
}

void countInstructions(FILE *fin, size_t *ret) {
	*ret = -1; // because of the first line

	char *line = NULL;
	size_t cap = 0;
	ssize_t len;

	while ((len = getline(&line, &cap, fin)) > 0) {
		if (line[0] == '#') {
			if (strncasecmp(line, "#starts", 7) == 0) {
				int param;
				sscanf(line, "%*s %d", &param);
				*ret = param;
			} else if (strncasecmp(line, "#repeat", 7) == 0) {
				int param1, param2;
				sscanf(line, "%*s %d %d", &param1, &param2);
				*ret += param1 * (param2 - 1);
			}
		} else {
			size_t i = 0;
			for (; i < len && isspace((unsigned char)line[i]); i++)
				;
			if (i < len && line[i] != ';')
				(*ret)++;
		}
	}

	free(line);
}

int compileFile(FILE *fin, Options *opts) {
	if (!fin)
		return 0;

	init_variables();

	char *line = NULL;
	size_t cap = 0;
	size_t linenum = 1;
	size_t instruction_num = 0;
	ssize_t line_length;

	fint repeat_arr[1024];
	int repeat_what = 0, repeat_times = 0, repeat_already = 0;

	size_t program_size;
	countInstructions(fin, &program_size);
	errno = 0;
	rewind(fin);
	if (errno != 0) {
		perror("rewind");
		return 0;
	}

	int ok = 1;

	char name[255];
	int offset;
	if (fscanf(fin, "%s %d ", name, &offset) == 2) {
		printf("static uint16_t %s_mem[] = {\n", name);
	} else {
		fprintf(stderr, "Header line is missing (first line in the file must be 'name offset'. eg. example 10)\n");
		return 1;
	}

	if (offset == -1) {
		offset = rand() % ((1 << 10) - program_size);
	}

	while ((line_length = getline(&line, &cap, fin)) != -1) {			
		linenum++;
		
		// Add newline of not there
		{
			if (line[line_length - 1] != '\n') {
				if (cap <= (size_t)line_length + 1) {
					cap = (size_t)line_length + 2;
					char *tmp = realloc(line, cap);
					if (!tmp) {
						perror("realloc");
						ok = false;
						goto cleanup;
					}
					line = tmp;
				}
				line[line_length] = '\n';
				line[line_length + 1] = '\0';
			}
		}

		if (line[0] == '#') { // A directive

			if (strncasecmp(line, "#starts", 7) == 0) {
				int param;
				sscanf(line, "%*s %d", &param);
				if (param < instruction_num) {
					fprintf(stderr, "Error on line %zu: #starts directive wants to go back (current instruction: %zu, wanted instruction: %d)\n", linenum, instruction_num, param);
					ok = 0;
					goto cleanup;
				}
				for (; instruction_num < param; instruction_num++) {
					putchar('\t');
					if (opts->decimal_instr)
						printf("%d", 0b1111110000000000);
					else
						writeBin(stdout, 0b1111110000000000);
					printf(",\n");
				}
			} else if (strncasecmp(line, "#free", 5) == 0) {
				char param[255];
				sscanf(line, "%*s %254s", param);
				int i;
				for (i = 1; i < 30; i++) {
					if (strcmp(variables[i], param) == 0) {
						variables[i][0] = '\0';
						break;
					}
				}
				if (i == 30) {
					fprintf(stderr, "Error on line %zu: trying to free the variable %s which isn't in use\n", linenum, param);
					ok = 0;
					goto cleanup;
				}
			} else if (strncasecmp(line, "#repeat", 7) == 0) {
				int param1, param2;
				if (sscanf(line, "%*s %d %d", &param1, &param2) != 2) {
					fprintf(stderr, "Error on line %zu: #repeat needs 2 parameters - what and how many times\n", linenum);
					ok = 0;
					goto cleanup;
				}
				if (repeat_what != 0) {
					fprintf(stderr, "Error on line %zu: nesting #repeat-s isn't supported\n", linenum);
					ok = 0;
					goto cleanup;
				}
				repeat_what = param1;
				repeat_times = param2 - 1;
				repeat_already = 0;
			}

		} else { // An instruction

			fint l;
			int status = compileLine(line, program_size, instruction_num, &l, opts->vars);
			if (!status) {
				fprintf(stderr, "Error on line %zu: %s\n", linenum, ERROR_TEXT);
				ok = 0;
				goto cleanup;
			} else if (status == 1) {
				putchar('\t');
				if (opts->decimal_instr)
					printf("%d", l);
				else
					writeBin(stdout, l);
				if (opts->comments)
					printf(", // %s", line);
				else
					printf(",\n");

				instruction_num++;

				if (repeat_what != repeat_already) {
					repeat_arr[repeat_already] = l;
					repeat_already++;
				}
				if (repeat_what == repeat_already) {
					while (repeat_times--) {
						for (int i = 0; i < repeat_what; i++) {
							putchar('\t');
							if (opts->decimal_instr)
								printf("%d", repeat_arr[i]);
							else
								writeBin(stdout, repeat_arr[i]);
							if (opts->comments)
								printf(", // repeat %d\n", repeat_times + 1);
							else
								printf(",\n");
							instruction_num++;
						}
					}
					repeat_what = 0;
					repeat_already = 0;
				}

			}
		}
	}

	assert(program_size == instruction_num); // if false, instruction counting function probably doesn't work

	printf("};\n"
		   "static uint16_t %s_size = %zu;\n"
		   "static uint16_t %s_offset = %d;\n",
		   name, program_size, name, offset);

	if (opts->var_table) {
		putchar('\n');
		for (int i = 1; i < 30; i++)
			if (variables[i][0] != '\0')
				printf("// %s: r%d\n", variables[i], i);
	}

cleanup:
	if (line)
		free(line);
	return ok;
}

// Returns 2 on empty lines
int compileLine(char *line, size_t program_size, size_t instruction_num, fint *ret, bool use_vars) {
	*ret = 0;

	char *lline = strdup(line);
	if (!lline) {
		snprintf(ERROR_TEXT, 255, "Out of memory");
		return 0;
	}

	const char *delims = " ,\t\r\n";
	char *token = strtok(lline, delims);
	if (!token || token[0] == ';') {
		free(lline);
		return 2;
	}

	fint opcode;
	if (!getOperation(token, &opcode)) {
		free(lline);
		return 0;
	}

	// Encode opcode in the top 6 bits
	*ret = (opcode & 0x3F) << 10;

	int ind = 0; // operand index
	int ok = 1;

	while ((token = strtok(NULL, delims)) != NULL) {
		if (token[0] == ';')
			break;

		switch (opcode) {
			case OP_FLAG:
				snprintf(ERROR_TEXT, 255, "Too many parameters (0 expected)");
				ok = 0;
				goto cleanup;

			case OP_LDI:
				if (ind >= 1) {
					snprintf(ERROR_TEXT, 255, "Too many parameters (1 expected)");
					ok = 0;
					goto cleanup;
				}
				break;

			case OP_NOT:
			case OP_JMP:
			case OP_PUSH:
			case OP_POP:
				if (ind >= 1) {
					snprintf(ERROR_TEXT, 255, "Too many parameters (1 expected)");
					ok = 0;
					goto cleanup;
				}
				break;

			default:
				if (ind >= 2) {
					snprintf(ERROR_TEXT, 255, "Too many parameters (2 expected)");
					ok = 0;
					goto cleanup;
				}
				break;
		}

		switch (opcode) {
			case OP_LDI: {
				int val;
				if (!parseNum(token, &val) && !parseConst(token, program_size, instruction_num, &val)) {
					ok = 0;
					goto cleanup;
				}
				if (val < 0 || val >= (1 << 16)) {
					snprintf(ERROR_TEXT, 255, "Number not in range [0, 2^16): '%s' -> %d", token, val);
					ok = 0;
					goto cleanup;
				}
				*ret |= (fint)val;
				ind++;
				break;
			}

			case OP_ADDI:
			case OP_SUBI:
			case OP_SHLI:
			case OP_SHRI:
				if (ind == 1) {
					int val;
					if (!parseNum(token, &val) && !parseConst(token, program_size, instruction_num, &val)) {
						ok = 0;
						goto cleanup;
					}
					if (val < 0 || val >= (1 << 6)) {
						snprintf(ERROR_TEXT, 255, "Number not in range [0, 2^6): '%s' -> %d", token, val);
						ok = 0;
						goto cleanup;
					}
					*ret |= (fint)val << (1 - ind) * 5;
					ind++;
					break;
				}
				/* fall through */

			default: {
				fint reg;
				if (!getRegister(token, &reg, use_vars)) {
					ok = 0;
					goto cleanup;
				}
				*ret |= reg << (1 - ind) * 5;
				ind++;
			}
		}
	}

	// Final arity checks (too few parameters)
	switch (opcode) {
		case OP_FLAG:
			if (ind != 0) {
				ok = 0; // already reported above
			}
			break;

		case OP_LDI:
		case OP_NOT:
		case OP_JMP:
		case OP_PUSH:
		case OP_POP:
			if (ind != 1) {
				snprintf(ERROR_TEXT, 255, "Too few parameters (1 expected)");
				ok = 0;
			}
			break;

		default:
			if (ind != 2) {
				snprintf(ERROR_TEXT, 255, "Too few parameters (2 expected)");
				ok = 0;
			}
			break;
	}

cleanup:
	free(lline);
	return ok;
}

int getOperation(char *symbol, fint *ret) {
	if (strcasecmp(symbol, "LDI") == 0) {
		*ret = OP_LDI;
	} else if (strcasecmp(symbol, "MV") == 0) {
		*ret = OP_MV;
	} else if (strcasecmp(symbol, "ADD") == 0) {
		*ret = OP_ADD;
	} else if (strcasecmp(symbol, "SUB") == 0) {
		*ret = OP_SUB;
	} else if (strcasecmp(symbol, "NOT") == 0) {
		*ret = OP_NOT;
	} else if (strcasecmp(symbol, "AND") == 0) {
		*ret = OP_AND;
	} else if (strcasecmp(symbol, "OR") == 0) {
		*ret = OP_OR;
	} else if (strcasecmp(symbol, "XOR") == 0) {
		*ret = OP_XOR;
	} else if (strcasecmp(symbol, "SHL") == 0) {
		*ret = OP_SHL;
	} else if (strcasecmp(symbol, "SHR") == 0) {
		*ret = OP_SHR;
	} else if (strcasecmp(symbol, "JMP") == 0) {
		*ret = OP_JMP;
	} else if (strcasecmp(symbol, "JZ") == 0) {
		*ret = OP_JZ;
	} else if (strcasecmp(symbol, "JNZ") == 0) {
		*ret = OP_JNZ;
	} else if (strcasecmp(symbol, "JN") == 0) {
		*ret = OP_JN;
	} else if (strcasecmp(symbol, "JP") == 0) {
		*ret = OP_JP;
	} else if (strcasecmp(symbol, "LD") == 0) {
		*ret = OP_LD;
	} else if (strcasecmp(symbol, "ST") == 0) {
		*ret = OP_ST;
	} else if (strcasecmp(symbol, "PUSH") == 0) {
		*ret = OP_PUSH;
	} else if (strcasecmp(symbol, "POP") == 0) {
		*ret = OP_POP;
	} else if (strcasecmp(symbol, "ADDI") == 0) {
		*ret = OP_ADDI;
	} else if (strcasecmp(symbol, "SUBI") == 0) {
		*ret = OP_SUBI;
	} else if (strcasecmp(symbol, "SHLI") == 0) {
		*ret = OP_SHLI;
	} else if (strcasecmp(symbol, "SHRI") == 0) {
		*ret = OP_SHRI;
	} else if (strcasecmp(symbol, "FLAG") == 0) {
		*ret = OP_FLAG;
	} else {
		snprintf(ERROR_TEXT, 255, "Unknown instruction: '%s'", symbol);
		return 0;
	}

	return 1;
}

int getRegister(char *symbol, fint *ret, bool use_vars) {
	int n = strlen(symbol);
	if (!n)
		return 0;

	if ((symbol[0] == 'r' || symbol[0] == 'R') && n >= 2 && n <= 3) {
		int num = 0;

		if (inside(0, symbol[1] - '0', 9))
			num = symbol[1] - '0';
		else
			goto special_name;

		if (n == 3) {
			if (inside(0, symbol[2] - '0', 9))
				num = num * 10 + symbol[2] - '0';
			else
				goto special_name;
		}

		if (num < 32) {
			*ret = num;
			return 1;
		} else {
			snprintf(ERROR_TEXT, 255, "Unknown register: '%s'", symbol);
			return 0;
		}
	}

special_name:;
	if (use_vars && (isdigit(symbol[0]) || symbol[0] == '#')) {
		snprintf(ERROR_TEXT, 255, "Invalid variable name (starts with a digit or #): '%s'", symbol);
		return 0;
	}
	int empty = -1;
	for (int i = 1; i < 32; i++) {
		if (strcasecmp(variables[i], symbol) == 0) {
			*ret = i;
			return 1;
		}
		if (variables[i][0] == 0 && empty == -1)
			empty = i;
	}
	if (empty == -1) {
		snprintf(ERROR_TEXT, 255, "Too many variables (maybe #free some?): '%s'", symbol);
		return 0;
	}

	if (use_vars) {
		strncpy(variables[empty], symbol, 254);
		*ret = empty;
	} else {
		snprintf(ERROR_TEXT, 255, "Invalid register (you have variables turned off): '%s'", symbol);
		return 0;
	}

	return 1;
}

void writeBin(FILE *fout, fint n) {
	fprintf(fout, "0b");
	fint mask = (fint)1u << 15;
	do {
		fputc((n & mask) ? '1' : '0', fout);
		mask >>= 1;
	} while (mask);
}

int parseConst(char *s, size_t program_size, size_t instruction_num, int *ret) {
	char const_name[255];
	int change = 0, multiplier = 1;
	if (s[0] != '#')
		return 0;
	sscanf(s, "#%254[^:]:%d:%d", const_name, &change, &multiplier);
	if (strcasecmp(const_name, "size") == 0) {
		*ret = program_size * multiplier + change;
	} else if (strcasecmp(const_name, "before") == 0) {
		*ret = instruction_num * multiplier + change;
	} else if (strcasecmp(const_name, "after") == 0) {
		*ret = (program_size - instruction_num - 1) * multiplier + change;
	} else {
		snprintf(ERROR_TEXT, 255, "Unknown compile-time constant '%s'", const_name);
		return 0;
	}
	return 1;
}

int parseNum(char *s, int *ret) {
	char *endptr;

	errno = 0;

	if (s[0] == '0' && s[1] == 'b') {
		// Binary
		long val = 0;
		for (int i = 2; s[i]; i++) {
			if (s[i] == '0' || s[i] == '1') {
				val = val * 2 + (s[i] - '0');
			} else if (s[i] != '.') {
				snprintf(ERROR_TEXT, sizeof(ERROR_TEXT), "Invalid binary number: %s", s);
				return 0;
			}
		}
		*ret = val;
		return 1;
	} else if (s[0] == '0' && s[1] == 'x') {
		// Hexadecimal
		long val = strtol(s + 2, &endptr, 16);
		if (*endptr != '\0' || errno != 0) {
			snprintf(ERROR_TEXT, sizeof(ERROR_TEXT), "Invalid hexadecimal number: %s", s);
			return 0;
		}
		*ret = val;
		return 1;
	} else {
		// Decimal
		long val = strtol(s, &endptr, 10);
		if (*endptr != '\0' || errno != 0) {
			snprintf(ERROR_TEXT, sizeof(ERROR_TEXT), "Invalid decimal number: %s", s);
			return 0;
		}
		*ret = val;
		return 1;
	}
}
