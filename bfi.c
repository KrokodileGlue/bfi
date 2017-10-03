#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

/* UTILITIES =============================================================== */
#ifdef DEBUG
#define DBG(...) fprintf(stdout, __VA_ARGS__);
#else
#define DBG(...)
#endif

#define USAGE "Usage: bfi INPUT_FILE -oOUTPUT_FILE\n"

void die(const char *msg)
{
	if (msg)
		printf("\nbfi: error: %s", msg);

	exit(EXIT_FAILURE);
}

char *load_file(const char *path)
{
	char *buf = NULL;
	FILE *file = fopen(path, "r");

	if (!file) die("input file could not be opened for reading");

	if (!fseek(file, 0L, SEEK_END)) {
		long len = ftell(file);
		if (len == -1) return NULL;

		buf = malloc(len + 1);

		if (fseek(file, 0L, SEEK_SET) != 0)
			return NULL;

		size_t new_len = fread(buf, 1, len, file);
		if (ferror(file) != 0)
			die("input file could not be read");

		buf[new_len++] = 0;
	}

	fclose(file);

	return buf;
}

#define IS_BF_COMMAND(c) (       \
           c == '<' || c == '>'  \
        || c == '+' || c == '-'  \
        || c == '[' || c == ']'  \
        || c == ',' || c == '.')

#define ADD(a,c)                             \
	for (int i = 0; i < abs(a); i++)     \
		*c++ = (a) >= 0 ? '+' : '-'; \

#define MOVPTR(a,c)                          \
	for (int i = 0; i < abs(a); i++)     \
		*c++ = (a) >= 0 ? '>' : '<'; \

void sanitize(char* str)
{
	if (!str) return;

	size_t starting_len = strlen(str);
	char* i = str, *out = str;

	while (*i) {
		if (*i == '+' || *i == '-' || *i == '<' || *i == '>') {
			if (*i == '+' || *i == '-') {
				int sum = 0;

				while (*i == '+' || *i == '-') {
					if (*i == '+') sum++;
					else sum--;

					i++;
				}

				ADD(sum, out)
			} else {
				int sum = 0;

				while (*i == '>' || *i == '<') {
					if (*i == '>') sum++;
					else sum--;

					i++;
				}

				MOVPTR(sum, out)
			}
		} else if (!strncmp(i, "][", 2)) {
			i += 2;
			int depth = 1;
			do {
				i++;
				if (*i == '[') depth++;
				else if (*i == ']') depth--;
			} while (depth && *i);
		}
		else if (IS_BF_COMMAND(*i)) *out++ = *i++;
		else i++;
	}

	*out = 0;
	if (strlen(str) < starting_len) sanitize(str);
}

/* COMPILATION ============================================================= */
#ifdef DEBUG
const char *instr_str[12] = {
	"ADD   ",
	"SUB   ",
	"SUBPTR",
	"ADDPTR",
	"PUTCH ",
	"GETCH ",
	"CJUMP ",
	"JUMP  ",
	"CLEAR ",
	"MUL   ",
	"END   ",
	"DCLEAR"
};
#endif

struct instr {
	enum {
		INSTR_ADD,
		INSTR_SUB,
		INSTR_SUBPTR,
		INSTR_ADDPTR,
		INSTR_PUTCH,
		INSTR_GETCH,
		INSTR_CJUMP,
		INSTR_JUMP,
		INSTR_CLEAR,
		INSTR_DCLEAR,
		INSTR_MUL,
		INSTR_END
	} type;

	int data, offset;
} *code;

struct instr make_instruction(int type, int data, int offset)
{
	struct instr res;

	res.type = type;
	res.data = data;
	res.offset = offset;

	return res;
}

char *bf_commands = "+-<>.,[]";
int command_type(char command)
{
	for (int i = 0; i < (int)strlen(bf_commands); i++)
		if (bf_commands[i] == command)
			return i;

	return -1;
}

#define MAX_STACK_DEPTH 4096
int stack[MAX_STACK_DEPTH], sp, ip;
char *tok;

/* takes a pointer to the beginning and end of a piece of text
 * and turns it into an independant NUL-terminated string */
char *form_string(char *begin, char *end)
{
	char *str = malloc(end - begin + 15);
	strncpy(str, begin, end - begin + 1);
	str[end - begin + 1] = 0;

	return str;
}

int balanced_loop(const char *loop)
{
	int offset = 0;
	for (int i = 0; i < (int)strlen(loop); i++) {
		if (loop[i] == '<')      offset--;
		else if (loop[i] == '>') offset++;
	}

	return !offset;
}

struct loop_cell_info {
	int offset, data;
};

struct loop_info {
	struct loop_cell_info *loop;
	int num;
};

int find_cell_with_offset(struct loop_cell_info *cells, int num_cells, int offset)
{
	for (int i = 0; i < num_cells; i++)
		if (cells[i].offset == offset)
			return i;

	return -1;
}

int emit_multiplication_loops(struct loop_info *loop_info)
{
	int index_of_main_cell = find_cell_with_offset(loop_info->loop, loop_info->num, 0);
	if (loop_info->loop[index_of_main_cell].data != -1)
		return 0;

	for (int i = 0; i < loop_info->num; i++) {
		if (i != index_of_main_cell) {
			code[ip++] = make_instruction(
			INSTR_MUL,
			loop_info->loop[i].data,
			loop_info->loop[i].offset);
		}
	}

	code[ip++] = make_instruction(INSTR_CLEAR, -1, -1);

	free(loop_info->loop);
	free(loop_info);

	return 1;
}

struct loop_info *analyze_loop(const char *loop)
{
	struct loop_cell_info *cells = malloc(strlen(loop) * sizeof *cells);
	int num_cells = 0, offset = 0, amount = 0;

	int i = 1;
	while (loop[i] != ']') {
		while (loop[i] == '<' || loop[i] == '>') {
			if (loop[i] == '<') offset--;
			else offset++;

			i++;
		}

		amount = 0;
		while (loop[i] == '+' || loop[i] == '-') {
			if (loop[i] == '+') amount++;
			else amount--;

			i++;
		}

		int cell_index = find_cell_with_offset(cells, num_cells, offset);
		if (cell_index != -1) {
			cells[cell_index].data += amount;
		} else {
			cells[num_cells].data = amount;
			cells[num_cells++].offset = offset;
		}
	}

#ifdef DEBUG
	DBG("loop: %s\n", loop);
	for (int j = 0; j < num_cells; j++) {
		DBG("\toffset: [%4d] data: [%4d]\n", cells[j].offset, cells[j].data);
	}
#endif

	struct loop_info *res = malloc(sizeof *res);
	res->loop = cells;
	res->num = num_cells;

	return res;
}

int muliplication_loop()
{
	char *begin = tok, *end = tok + 1;

	if (!strncmp(begin, "[-]", 3))
		return 0;

	while (*end != ']') {
		if (!*end || command_type(*end) >= 4)
			return 0;
		end++;
	}

	/*
	 * If we've reached this point then the loop does not contain
	 * any embedded loops or commands other than + - < and >.
	 */

	char *loop = form_string(begin, end);
	if (!balanced_loop(loop)) {
		free(loop);
		return 0;
	}

	/*
	 * If we've reached this point then the loop is balanced.
	 */
	struct loop_info *loop_info = analyze_loop(loop);

	DBG("\tmultiplication loop? ");

	if (emit_multiplication_loops(loop_info)) {
		DBG("yes\n");
		tok = end + 1;
		free(loop);
		return 1;
	} else {
		DBG("no\n");
		free(loop);
		return 0;
	}
}

#define IS_CONTRACTABLE(c)      \
	(c == '<' || c == '>'   \
	|| c == '-' || c == '+' \
	|| c == ',' || c == '.')

void contract()
{
	int data = 0, offset = 0;

	while (IS_CONTRACTABLE(*tok)) {
		data = 0;

		while (*tok == '<' || *tok == '>') {
			if (*tok == '<') offset--;
			else offset++; tok++;
		}

		if (*tok == '-' || *tok == '+') {
			while (*tok == '-' || *tok == '+') {
				if (*tok == '-') data--;
				else data++;

				tok++;
			}

			code[ip++] = make_instruction(INSTR_ADD, data, offset);

			while (*tok == '<' || *tok == '>') {
				if (*tok == '<') offset--;
				else offset++; tok++;
			}
		} else if (*tok == ',') {
			while (*tok == ',')
				data++, tok++;

			code[ip++] = make_instruction(INSTR_GETCH, data, offset);

			while (*tok == '<' || *tok == '>') {
				if (*tok == '<') offset--;
				else offset++; tok++;
			}
		} else if (*tok == '.') {
			while (*tok == '.')
				data++, tok++;

			code[ip++] = make_instruction(INSTR_PUTCH, data, offset);

			while (*tok == '<' || *tok == '>') {
				if (*tok == '<') offset--;
				else offset++; tok++;
			}
		}
	}

	if (offset)
		code[ip++] = make_instruction(INSTR_ADDPTR, offset, -1);
}

int is_clearloop()
{
	if (!strncmp(tok, "[-]", 3) || !strncmp(tok, "[+]", 3)) {
		code[ip++] = make_instruction(INSTR_CLEAR, -1, -1);
		tok += 3;
		return 1;
	}

	int data = 0;
	char *c = tok + 1;
	while (*c != ']') {
		if (*c != '+' && *c != '-')
			return 0;

		data++;
		c++;
	}

	code[ip++] = make_instruction(INSTR_DCLEAR, data, -1);
	tok = c + 1;

	return 1;
}

void compile(char *src)
{
	/*
	 * In a worst case scenario, none of our optimizations apply,
	 * and we need an entire instruction to represent each token.
	 */
	code = malloc(sizeof (*code) * strlen(src));

	tok = src;
	while (*tok) {
		int type = command_type(*tok);

		if (*tok == '[') {
			if (muliplication_loop()) {
				continue;
			} else if (is_clearloop()) {
				continue;
			} else {
				stack[sp++] = ip;
				code[ip++] = make_instruction(type, -1, -1);
				tok++;
			}
		} else if (type >= 6) { /* handles [ and ] (which are not contractable) */
			if (*tok == ']') {
				if (sp == 0)
					die("Unmatched ].\n");

				code[ip] = make_instruction(type, stack[--sp], -1);
				code[stack[sp]].data = ip++;
			} else
				code[ip++] = make_instruction(type, -1, -1);

			tok++;
		} else /* token is of type + - , . < or > */
			contract();
	}

	code[ip++].type = INSTR_END;
	code = realloc(code, sizeof (*code) * ip);

	if (sp) die("Unmatched [.\n");
}

/* EXECUTION =============================================================== */
uint8_t memory[1 << 16];
uint16_t ptr;

/* borrowed from https://github.com/rdebath/Brainfuck/blob/master/bf2any/bf2run.c */
#if defined(__GNUC__) && ((__GNUC__>4) || (__GNUC__==4 && __GNUC_MINOR__>=4))
/* Tell GNU C to think really hard about this function! */
__attribute__((optimize(3),hot,aligned(64)))
#endif
void execute()
{
	ip = 0;

loop:
	switch (code[ip].type) {
	case INSTR_ADD:
		memory[(uint16_t)(ptr + code[ip].offset)] += code[ip].data;
		break;

	case INSTR_SUB:
		memory[(uint16_t)(ptr + code[ip].offset)] -= code[ip].data;
		break;

	case INSTR_SUBPTR:
		ptr -= code[ip].data; break;

	case INSTR_ADDPTR:
		ptr += code[ip].data; break;

	case INSTR_PUTCH:
		for (int i = 0; i < code[ip].data; i++) {
			putchar(memory[(uint16_t)(ptr + code[ip].offset)]);
		}
		break;

	case INSTR_GETCH: {
		char c = getchar();
		if (c == EOF) {
			memory[(uint16_t)(ptr + code[ip].offset)] = 0;
		} else {
			memory[(uint16_t)(ptr + code[ip].offset)] = c;
		}
	} break;

	case INSTR_CJUMP:
		if (!memory[ptr]) {
			ip = code[ip].data;
		}
		break;

	case INSTR_JUMP:
		ip = code[ip].data - 1;
		break;

	case INSTR_CLEAR:
		memory[ptr] = 0;
		break;

	case INSTR_MUL:
		memory[(uint16_t)(ptr + code[ip].offset)] += memory[ptr] * code[ip].data;
		break;

	case INSTR_END:
		return;

	case INSTR_DCLEAR:
		if (code[ip].data % 3 == 0)
			memory[ptr] = 0;
		else if ((code[ip].data / 2 % 2 == 0)
		         && (memory[ptr] / 2 % 2 == 0))
			memory[ptr] = 0;
		else
			die("\nbfi: program has entered an infinite loop.\n");
		break;
	}

	ip++;
	goto loop;
}

int main(int argc, char **argv)
{
	char *input_path = NULL;

	for (int i = 1; i < argc; i++) {
		if (input_path)
			die(USAGE);
		else
			input_path = argv[i];
	}

	if (!input_path) die(USAGE);

	char *src = load_file(input_path);
	if (!src) die("could not load file.\n");

	sanitize(src);
	compile(src);
	free(src);

#ifdef DEBUG
	DBG("instruction listing:\n");
	for (int i = 0; i < ip; i++)
		DBG("%d: %s %5d, %5d\n", i, instr_str[code[i].type], code[i].offset, code[i].data);
	DBG("program output:\n");
#endif

	execute();
	free(code);

	return EXIT_SUCCESS;
}
