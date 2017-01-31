#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* UTILITIES =============================================================== */
void fatal_error(const char* msg)
{
	if (msg)
		puts(msg);

	exit(EXIT_FAILURE);
}

void* bfi_malloc(size_t bytes)
{
	void *res = malloc(bytes);
	
	if (!res)
		fatal_error("out of memory.");

	return res;
}

void* bfi_realloc(void *block, size_t bytes)
{
	block = realloc(block, bytes);
	
	if (!block)
		fatal_error("out of memory.");

	return block;
}

char* load_file(const char* path)
{
	char* buf = NULL;
	FILE* file = fopen(path, "r");
	
	if (file) {
		if (fseek(file, 0L, SEEK_END) == 0) {
			long len = ftell(file);
			if (len == -1) return NULL;
			
			buf = bfi_malloc(len + 1);

			if (fseek(file, 0L, SEEK_SET) != 0)
				return NULL;

			size_t new_len = fread(buf, 1, len, file);
			if (ferror(file) != 0) {
				fatal_error("Input file could not be read.\n");
			} else {
				buf[new_len++] = '\0';
			}
		}
		
		fclose(file);
	}
	
	return buf;
}

#define IS_BF_COMMAND(c)         \
	(c == '<' || c == '>'    \
	|| c == '-' || c == '+') \
	|| c == ',' || c == '.'  \
	|| c == '[' || c == ']'

void sanitize(char* str)
{
#define SANITIZER_IS_BF_COMMAND(c) (   \
        c == '<' || c == '>'     \
        || c == '+' || c == '-'  \
        || c == '[' || c == ']'  \
        || c == ',' || c == '.')

#define SANITIZER_ADD(a, c) \
        if (a >= 0) { \
                for (int counter = 0; counter < abs(a); counter++) { \
                        *c++ = '+'; \
                } \
        } else { \
    	        for (int counter = 0; counter < abs(a); counter++) { \
                        *c++ = '-'; \
                } \
        }

#define SANITIZER_MOVE_PTR(a, c) \
    if (a >= 0) { \
        for (int counter = 0; counter < abs(a); counter++) { \
            *c++ = '>'; \
        } \
    } else { \
    	for (int counter = 0; counter < abs(a); counter++) { \
            *c++ = '<'; \
        } \
    }

#define SANITIZER_IS_CONTRACTABLE(c) (     \
        c == '<' || c == '>'     \
        || c == '+' || c == '-')

	char* buf = malloc(strlen(str) + 1);

	size_t starting_len;
	char* i, *out;

	starting_len = strlen(str);
	i = str, out = buf;

	while (*i != '\0') {
		if (SANITIZER_IS_CONTRACTABLE(*i)) {
			if (*i == '+' || *i == '-') {
				int sum = 0;
				while (*i == '+' || *i == '-') {
					if (*i == '+') sum++;
					else sum--;
					i++;
				}
				SANITIZER_ADD(sum, out)
			} else {
				int sum = 0;
				while (*i == '>' || *i == '<') {
					if (*i == '>') sum++;
					else sum--;
					i++;
				}
				SANITIZER_MOVE_PTR(sum, out)
			}
		} else if (!strncmp(i, "][", 2)) {
			i += 2;
			int depth = 1;
			while (*i != '\0' && depth) {
				if (*i == '[') depth++;
				else if (*i == ']') depth--;
				i++;
			}
			i--;
		} else if (SANITIZER_IS_BF_COMMAND(*i)) {
			*out++ = *i++;
		} else {
			i++;
		}
	}
	*out = '\0';
	strcpy(str, buf);

	if (strlen(str) < starting_len) {
		sanitize(str);
	}
}

/* COMPILATION ============================================================= */
#ifdef DEBUG
char instr_strings[12][20] = {
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

typedef struct {
	enum {
		INSTR_ADD   , INSTR_SUB,
		INSTR_SUBPTR, INSTR_ADDPTR,
		INSTR_PUTCH , INSTR_GETCH,
		INSTR_CJUMP , INSTR_JUMP,
		INSTR_CLEAR , INSTR_MUL,
		INSTR_END   , INSTR_DCLEAR /* dangerous clear*/
	} type;
	int data, offset;
} Instruction;
Instruction* program;

Instruction make_instruction(int type, int data, int offset)
{
	Instruction res;
	
	res.type = type;
	res.data = data;
	res.offset = offset;

	return res;
}

char* bf_commands = "+-<>.,[]";
int command_type(char command)
{
	for (int i = 0; i < (int)strlen(bf_commands); i++)
		if (bf_commands[i] == command)
			return i;

	return -1;
}

#define MAX_STACK_DEPTH 4096
int stack[MAX_STACK_DEPTH], sp, ip;
char* tok;

/* takes a pointer to the beginning and end of a piece of text
 * and turns it into an independant NUL-terminated string */
char* form_string(char* begin, char* end)
{
	char* str = bfi_malloc(end - begin + 15);
	strncpy(str, begin, end - begin + 1);
	str[end - begin + 1] = 0;

	return str;
}

int balanced_loop(const char* loop)
{
	int offset = 0;
	for (int i = 0; i < (int)strlen(loop); i++) {
		if (loop[i] == '<')      offset--;
		else if (loop[i] == '>') offset++;
	}

	return !offset;
}

typedef struct {
	int offset, data;
} LoopCellInformation;

typedef struct {
	LoopCellInformation* loop;
	int num;
} LoopInfo;

int find_cell_with_offset(LoopCellInformation* cells, int num_cells, int offset)
{
	for (int i = 0; i < num_cells; i++)
		if (cells[i].offset == offset)
			return i;

	return -1;
}

int emit_multiplication_loops(LoopInfo* loop_info)
{
	int index_of_main_cell = find_cell_with_offset(loop_info->loop, loop_info->num, 0);
	if (loop_info->loop[index_of_main_cell].data != -1)
		return 0;

	for (int i = 0; i < loop_info->num; i++) {
		if (i != index_of_main_cell) {
			program[ip++] = make_instruction(
			INSTR_MUL,
			loop_info->loop[i].data,
			loop_info->loop[i].offset);
		}
	}

	program[ip++] = make_instruction(INSTR_CLEAR, -1, -1);
	
	free(loop_info->loop);
	free(loop_info);

	return 1;
}

LoopInfo* analyze_loop(const char* loop)
{
	LoopCellInformation* cells = bfi_malloc(strlen(loop) * sizeof(LoopCellInformation));
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
	printf("loop: %s\nloop anaylsis:\n", loop);
	for (int j = 0; j < num_cells; j++) {
		printf("\toffset: [%4d] data: [%4d]\n", cells[j].offset, cells[j].data);
	}
#endif

	LoopInfo* res = bfi_malloc(sizeof(LoopInfo));
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
		if (*end == '\0' || command_type(*end) >= 4)
			return 0;
		end++;
	}

	/* if we've reached this point then the loop does not contain
	 * any embedded loops or commands other than + - < and > */
	
	char* loop = form_string(begin, end);
	if (!balanced_loop(loop)) {
		free(loop);
		return 0;
	}

	/* if we've reached this point then the loop is balanced. */
	LoopInfo* loop_info = analyze_loop(loop);

#ifdef DEBUG
	printf("\tloop passed as multiplication loop? ");
#endif

	if (emit_multiplication_loops(loop_info)) {
#ifdef DEBUG
		printf("yes\n");
#endif
		tok = end + 1;
		free(loop);
		return 1;
	} else {
#ifdef DEBUG
		printf("no\n");
#endif
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

			program[ip++] = make_instruction(INSTR_ADD, data, offset);

			while (*tok == '<' || *tok == '>') {
				if (*tok == '<') offset--;
				else offset++; tok++;
			}
		} else if (*tok == ',') {
			while (*tok == ',')
				data++, tok++;

			program[ip++] = make_instruction(INSTR_GETCH, data, offset);

			while (*tok == '<' || *tok == '>') {
				if (*tok == '<') offset--;
				else offset++; tok++;
			}
		} else if (*tok == '.') {
			while (*tok == '.')
				data++, tok++;

			program[ip++] = make_instruction(INSTR_PUTCH, data, offset);

			while (*tok == '<' || *tok == '>') {
				if (*tok == '<') offset--;
				else offset++; tok++;
			}
		}
	}

	if (offset)
		program[ip++] = make_instruction(INSTR_ADDPTR, offset, -1);
}

int is_clearloop()
{
	if (!strncmp(tok, "[-]", 3) || !strncmp(tok, "[+]", 3)) {
		program[ip++] = make_instruction(INSTR_CLEAR, -1, -1);
		tok += 3;
		return 1;
	}

	int data = 0;
	char* c = tok + 1;
	while (*c != ']') {
		if (*c != '+' && *c != '-')
			return 0;

		data++;
		c++;
	}

	program[ip++] = make_instruction(INSTR_DCLEAR, data, -1);
	tok = c + 1;

	return 1;
}

void compile(char* src)
{
	/* in a worst case scenario, none of our optimizations apply,
	 * and we need an entire Instruction to represent each token */
	program = bfi_malloc(sizeof(Instruction) * strlen(src));

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
				program[ip++] = make_instruction(type, -1, -1);
				tok++;
			}
		} else if (type >= 6) { /* handles [ and ] (which are not contractable) */
			if (*tok == ']') {
				if (sp == 0)
					fatal_error("Unmatched ].\n");
				
				program[ip] = make_instruction(type, stack[--sp], -1);
				program[stack[sp]].data = ip++;
			} else
				program[ip++] = make_instruction(type, -1, -1);
			
			tok++;
		} else /* token is of type + - , . < or > */
			contract();
	}

	program[ip++].type = INSTR_END;
	program = bfi_realloc(program, sizeof(Instruction) * ip);

	if (sp)
		fatal_error("Unmatched [.\n");
}

/* EXECUTION =============================================================== */
#define MAX_MEMORY 65536
char memory[MAX_MEMORY];
unsigned short ptr; /* free memory wrapping */

/* borrowed from https://github.com/rdebath/Brainfuck/blob/master/bf2any/bf2run.c */
#if defined(__GNUC__) && ((__GNUC__>4) || (__GNUC__==4 && __GNUC_MINOR__>=4))
/* Tell GNU C to think really hard about this function! */
__attribute__((optimize(3),hot,aligned(64)))
#endif
void execute()
{
	ip = 0;

	while (1) {
		switch (program[ip].type) {
			case INSTR_ADD:
				memory[(unsigned short)(ptr + program[ip].offset)] += program[ip].data; break;
			case INSTR_SUB:
				memory[(unsigned short)(ptr + program[ip].offset)] -= program[ip].data; break;
			case INSTR_SUBPTR:
				ptr -= program[ip].data; break;
			case INSTR_ADDPTR:
				ptr += program[ip].data; break;
			case INSTR_PUTCH:
				for (int i = 0; i < program[ip].data; i++)
					putchar(memory[(unsigned short)(ptr + program[ip].offset)]); break;
			case INSTR_GETCH: {
				char c = getchar();
				if (c == EOF) {
					memory[(unsigned short)(ptr + program[ip].offset)] = 0;
				} else {
					memory[(unsigned short)(ptr + program[ip].offset)] = c;
				}
			} break;
			case INSTR_CJUMP:
				if (!memory[ptr])
					ip = program[ip].data; break;
			case INSTR_JUMP:
				ip = program[ip].data - 1; break;
			case INSTR_CLEAR:
				memory[ptr] = 0; break;
			case INSTR_MUL:
				memory[(unsigned short)(ptr + program[ip].offset)] += memory[ptr] * program[ip].data; break;
			case INSTR_END:
				return;
			case INSTR_DCLEAR:
				if (program[ip].data % 3 == 0) memory[ptr] = 0;
				else if ((program[ip].data / 2 % 2 == 0) && (memory[ptr] / 2 % 2 == 0)) memory[ptr] = 0;
				else { printf("\nbfi: program has entered an infinite loop.\n"); return; }
				break;
		}

		ip++;
	}
}

int main(int argc, char** argv)
{
	char *input_path = NULL;
	int print_time = 0;
	
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-t"))
			print_time = 1;
		else {
			if (input_path)
				fatal_error("Usage: bfi INPUT_FILE -oOUTPUT_FILE\n");

			input_path = argv[i];
		}
	}

	if (!input_path) /* we never recieved an input path */
		fatal_error("Usage: bfi INPUT_FILE -oOUTPUT_FILE\n");

	char* src = load_file(input_path);

	if (!src)
		fatal_error("could not load file.\n");

	sanitize(src);

	compile(src);
	free(src);
	
#ifdef DEBUG
	printf("instruction listing:\n");
	for (int i = 0; i < ip; i++)
		printf("%d: [%s][%5d][%5d]\n", i, instr_strings[program[i].type], program[i].offset, program[i].data);
	printf("program output:\n");
#endif

	clock_t begin = clock();
	execute();
	free(program);
	clock_t end = clock();
	double time_spent = (double)(end - begin) / CLOCKS_PER_SEC;
	if (print_time)
		printf("\nProgram used %f seconds of processor time.\n", time_spent);

	return EXIT_SUCCESS;
}
