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

char* load_file(const char* path)
{
	char* buf = NULL;
	FILE* file = fopen(path, "r");
	
	if (file) {
		if (fseek(file, 0L, SEEK_END) == 0) {
			long len = ftell(file);
			if (len == -1) return NULL;
			
			buf = malloc(len + 1);

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

void save_file(const char* path, const char* str)
{
	FILE* file = fopen(path, "w");
	
	if (!file)
		fatal_error("Could not open output file.\n");
	
	fputs(str, file);
	fclose(file);
}

#define IS_BF_COMMAND(c)         \
	(c == '<' || c == '>'    \
	|| c == '-' || c == '+') \
	|| c == ',' || c == '.'  \
	|| c == '[' || c == ']'

char* remove_comments(char* src)
{
	char* buf = malloc(strlen(src) + 1);
	int buf_index = 0;

	char* c = src;
	while (*c) {
		if (IS_BF_COMMAND(*c))
			buf[buf_index++] = *c;
		c++;
	}

	buf[buf_index] = 0;
	free(src);

	return buf;
}

/* ANALYSIS ================================================================ */


/* COMPILATION ============================================================= */
typedef struct {
	enum {
		INSTR_ADD   , INSTR_SUB,
		INSTR_SUBPTR, INSTR_ADDPTR,
		INSTR_PUTCH , INSTR_GETCH,
		INSTR_CJUMP , INSTR_JUMP,
		INSTR_CLEAR , INSTR_MUL,
		INSTR_END
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
	for (int i = 0; i < strlen(bf_commands); i++)
		if (bf_commands[i] == command)
			return i;

	return -1;
}

#define MAX_STACK_DEPTH 4096
int stack[MAX_STACK_DEPTH], sp, ip;
char* tok;

#define IS_CONTRACTABLE(c)       \
	(c == '<' || c == '>'    \
	|| c == '-' || c == '+')

void contract()
{
	int type = command_type(*tok), data = 0, temp = type;

	while (temp == type && *tok) {
		data++, tok++;
		temp = command_type(*tok);
	}

	program[ip++] = make_instruction(type, data, -1);
}

void compile(char* src)
{
	/* in a worst case scenario, none of our optimizations apply,
	 * and we need an entire Instruction to represent each token */
	program = malloc(sizeof(Instruction) * strlen(src));

	tok = src;
	while (*tok) {
		int type = command_type(*tok);
		
		if (*tok == '[') {
			if (0) {
				continue;
			} else if (!strncmp(tok, "[-]", 3)) {
				program[ip++] = make_instruction(INSTR_CLEAR, -1, -1);
				tok += 3;
			} else {
				stack[sp++] = ip;
				program[ip++] = make_instruction(type, -1, -1);
				tok++;
			}
		} else if (type >= 4) { /* handles , . [ and ] (which are not contractable) */
			if (*tok == ']') {
				if (sp == 0) fatal_error("Unmatched ].\n");
				
				program[ip] = make_instruction(type, stack[--sp], -1);
				program[stack[sp]].data = ip++;
			} else
				program[ip++] = make_instruction(type, -1, -1);
			
			tok++;
		} else /* token is of type + - < or > */
			contract();
	}

	program[ip].type = INSTR_END;

	if (sp)
		fatal_error("Unmatched [.\n");
}

/* EXECUTION =============================================================== */
#define MAX_MEMORY 65536
char memory[MAX_MEMORY];
unsigned short ptr;

void execute()
{
	ip = 0;

	while (1) {
		switch (program[ip].type) {
			case INSTR_ADD   : memory[ptr] += program[ip].data; break;
			case INSTR_SUB   : memory[ptr] -= program[ip].data; break;
			case INSTR_SUBPTR: ptr -= program[ip].data;         break;
			case INSTR_ADDPTR: ptr += program[ip].data;         break;
			case INSTR_PUTCH : putchar(memory[ptr]);            break;
			case INSTR_GETCH : memory[ptr] = getchar();         break;
			case INSTR_CJUMP : if (!memory[ptr]) ip = program[ip].data; break;
			case INSTR_JUMP  : ip = program[ip].data - 1;       break;
			case INSTR_CLEAR : memory[ptr] = 0;                 break;
			case INSTR_MUL   : break;
			case INSTR_END   : return; break;
		}

		ip++;
	}
}

int main(int argc, char** argv)
{
	char* output_path = NULL, *input_path = NULL;
	for (int i = 1; i < argc; i++) {
		if (!strncmp(argv[i], "-o", 2)) {
			if (strlen(argv[i]) == 2)
				fatal_error("Provided output path is too short.\n");
			else
				output_path = &argv[i][2];
		} else {
			if (input_path)
				fatal_error("Usage: bfi INPUT_FILE -oOUTPUT_FILE\n");

			input_path = argv[i];
		}
	}

	if (!input_path) /* we never recieved an input path */
		fatal_error("Usage: bfi INPUT_FILE -oOUTPUT_FILE\n");

	char* src = load_file(input_path);
	src = remove_comments(src);

	compile(src);
	
#ifdef DEBUG
	puts("instruction listing:\n");
	for (int i = 0; i < ip; i++)
		printf("%d: [%5d][%5d][%5d]\n", i, program[i].type, program[i].data, program[i].offset);
#endif

	clock_t begin = clock();
	execute();
	clock_t end = clock();
	double time_spent = (double)(end - begin) / CLOCKS_PER_SEC;
	printf("\nProgram took %f seconds.\n", time_spent);

	return EXIT_SUCCESS;
}