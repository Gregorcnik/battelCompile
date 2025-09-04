/*
Transpiles assembly like code to binary. And yeah, I know compile isn't the right word, but it is easier to type.
===================================================================
Compile normally (cc compile.c for instance). 
The program outputs the transpiled c code to stdout and errors to stderr, so you can do 
./compile example.asm > example.c
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
	OP_JN,
	OP_JNZ,
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

static char ERROR_TEXT[256];

int parseNum(char *s, int *ret);
int parseConst(char *s, size_t program_size, size_t instruction_num, int *ret);
void writeBin(FILE *fout, fint n);
int getRegister(char *symbol, fint *ret);
int getOperation(char *symbol, fint *ret);
int compileLine(char *line, size_t program_size, size_t instruction_num, fint *ret);
int compileFile(FILE *fin, bool comments);
void countInstructions(FILE *fin, size_t *ret);

int main(int argc, char *argv[]) {
	int argi = 1;
	bool comments = true;

	for (; argi < argc; argi++) {
		char *p = argv[argi];
		if (p[0] != '-')
			break;
		if (strcmp(p, "-nocomments") == 0)
			comments = false;
		else if (strcmp(p, "-help") == 0)
			goto help;
		else {
			fprintf(stderr, "Unknown parameter '%s'\n", p);
			goto help;
		}
	}

	if (argi >= argc) {
		fprintf(stderr, "Input file not specified.\n");
		goto help;
	}
	if (argi < argc - 1) {
		fprintf(stderr, "Parameters after input file (%s) are prohibited\n", argv[argi]);
		goto help;
	}

	FILE *fin = fopen(argv[argi], "r");
	if (!fin) {
		perror("fopen");
		return 1;
	}

	int rc = compileFile(fin, comments);
	fclose(fin);
	return rc != 1;

help:
	fprintf(stderr, "Usage: %s -help -nocomments <input.asm>\n", argv[0]);
	return 1;
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

int compileFile(FILE *fin, bool comments) {
	if (!fin)
		return 0;

	char *line = NULL;
	size_t cap = 0;
	size_t linenum = 1;
	size_t instruction_num = 0;

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
	if (fscanf(fin, "%s %d", name, &offset) == 2) {
		printf("static uint16_t %s_mem[] = {\n", name);
	} else {
		return 1;
	}

	while (getline(&line, &cap, fin) != -1) {
		if (line[0] == '#') {
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
					writeBin(stdout, 0b1111110000000000);
					printf(",\n");
				}
			}
		} else {
			fint l;
			int status = compileLine(line, program_size, instruction_num, &l);
			if (!status) {
				fprintf(stderr, "Error on line %zu: %s\n", linenum, ERROR_TEXT);
				ok = 0;
				goto cleanup;
			} else if (status == 1) {
				putchar('\t');
				writeBin(stdout, l);
				if (comments)
					printf(", // %s", line);
				else
					printf(",\n");
				instruction_num++;
			}
		}

		linenum++;
	}

	assert(program_size == instruction_num); // if false, instruction counting function probably doesn't work

	printf("\n};\n"
		   "static uint16_t %s_size = %zu;\n"
		   "static uint16_t %s_offset = %d;\n",
		   name, program_size, name, offset);

cleanup:
	if (line)
		free(line);
	return ok;
}

// Returns 2 on empty lines
int compileLine(char *line, size_t program_size, size_t instruction_num, fint *ret) {
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
				/* fall through */

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
				if (!parseConst(token, program_size, instruction_num, &val) && !parseNum(token, &val)) {
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
					if (!parseConst(token, program_size, instruction_num, &val) && !parseNum(token, &val)) {
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
				if (!getRegister(token, &reg)) {
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

int getRegister(char *symbol, fint *ret) {
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

		if (num <= 32) {
			*ret = num;
			return 1;
		} else {
			snprintf(ERROR_TEXT, 255, "Unknown register: '%s'", symbol);
			return 0;
		}
	}

special_name:
	if (strcasecmp(symbol, "sp") == 0) {
		*ret = SP;
		return 1;
	} else if (strcasecmp(symbol, "pc") == 0) {
		*ret = PC;
		return 1;
	}

	snprintf(ERROR_TEXT, 255, "Unknown register: '%s'", symbol);
	return 0;
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

	if (s[0] == 'b') {
		// Binary
		long val = 0;
		for (int i = 1; s[i]; i++) {
			if (s[i] == '0' || s[i] == '1') {
				val = val * 2 + (s[i] - '0');
			} else if (s[i] != '.') {
				snprintf(ERROR_TEXT, sizeof(ERROR_TEXT), "Invalid binary number: %s", s);
				return 0;
			}
		}
		*ret = val;
		return 1;
	} else if (s[0] == 'x') {
		// Hexadecimal
		long val = strtol(s + 1, &endptr, 16);
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
