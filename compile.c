/*
Transpiles assembly like code to binary. And yeah, I know compile isn't the right word, but it is easier to type.
===================================================================
Compile normally (cc compile.c for instance). 
The program outputs the transpiled c code to stdout and errors to stderr, so you can do 
./compile example.asm > example.c
*/

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define inside(low, mid, high) ((low) <= (mid) && (mid) <= (high))
typedef uint16_t fint;

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

void writeBin(FILE *fout, fint n);
int parseNum(char *s, int *ret);
int getInstruction(char *symbol, fint *ret);
int getRegister(char *symbol, fint *ret);
int compileLine(char *line, fint *ret);
int compileFile(FILE *fin);

int main(int argc, char *argv[]) {
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <input.asm>\n", argv[0]);
		return 1;
	}

	FILE *fin = fopen(argv[1], "r");
	if (!fin) {
		perror("fopen");
		return 1;
	}

	int rc = compileFile(fin);
	fclose(fin);
	return rc != 1;
}

int compileFile(FILE *fin) {
	if (!fin)
		return 0;

	char *line = NULL;
	size_t cap = 0;
	size_t linenum = 1;
	size_t instructionnum = 0;

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
				if (param < instructionnum) {
					fprintf(stderr, "Error on line %zu: #starts directive wants to go back (current instruction: %zu, wanted instruction: %d)\n", linenum, instructionnum, param);
					ok = 0;
					goto cleanup;
				}
				for (; instructionnum < param; instructionnum++) {
					putchar('\t');
					writeBin(stdout, 0);
					printf(",\n");
				}
			}
		} else {
			fint l;
			int status = compileLine(line, &l);
			if (!status) {
				fprintf(stderr, "Error on line %zu: %s\n", linenum, ERROR_TEXT);
				ok = 0;
				goto cleanup;
			} else if (status == 1) {
				putchar('\t');
				writeBin(stdout, l);
				printf(", // %s", line);
				instructionnum++;
			}
		}

		linenum++;
	}

	printf("\n};\n"
		   "static uint16_t %s_size = %zu;\n"
		   "static uint16_t %s_offset = %d;\n",
		   name, instructionnum, name, offset);

cleanup:
	if (line)
		free(line);
	return ok;
}

// Returns 2 on empty lines
int compileLine(char *line, fint *ret) {
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
	if (!getInstruction(token, &opcode)) {
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
			case OP_SUB:
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
				if (!parseNum(token, &val)) {
					ok = 0;
					goto cleanup;
				}
				if (val < 0 || val >= (1 << 16)) {
					snprintf(ERROR_TEXT, 255, "Number not in range [0, 2^16): '%s'", token);
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
					if (!parseNum(token, &val)) {
						ok = 0;
						goto cleanup;
					}
					if (val < 0 || val >= (1 << 6)) {
						snprintf(ERROR_TEXT, 255, "Number not in range [0, 2^6): '%s'", token);
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
		case OP_SUB:
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

int parseNum(char *s, int *ret) {
	char *endptr;

	errno = 0;

	if (s[0] == 'b') {
		// Binary
		long val = 0;
		for (int i = 1; s[i]; i++) {
			if (s[i] == '0' || s[i] == '1') {
				val = val * 2 + (s[i] - '0');
			} else {
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

void writeBin(FILE *fout, fint n) {
	fprintf(fout, "0b");
	fint mask = (fint)1u << 15;
	do {
		fputc((n & mask) ? '1' : '0', fout);
		mask >>= 1;
	} while (mask);
}

int getInstruction(char *symbol, fint *ret) {
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
