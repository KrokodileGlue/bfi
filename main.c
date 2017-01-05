#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdarg.h>

char* source;
char* token;
char* output_path;
int dump_to_file;

#define MAX_OUTPUT_SIZE 11000000
char output_buffer[MAX_OUTPUT_SIZE];
char* outbuf_ptr = output_buffer;

char* load_file(const char* path)
{
	char* buf = NULL;
	FILE* file = fopen(path, "r");
	
	if (file != NULL) {
		if (fseek(file, 0L, SEEK_END) == 0) {
			long len = ftell(file);
			if (len == -1) return NULL;
		
			buf = malloc(len + 1);

			if (fseek(file, 0L, SEEK_SET) != 0) return NULL;

			size_t newLen = fread(buf, 1, len, file);
			if (ferror(file) != 0) {
				fputs("Input file could not be read.", stderr);
				exit(EXIT_FAILURE);
			} else {
				buf[newLen++] = '\0';
			}
		}
		
		fclose(file);
	}
	
	return buf;
}

enum {
	INSTR_SUB,
	INSTR_ADD,
	INSTR_SUBPTR,
	INSTR_ADDPTR,
	INSTR_PRINT,
	INSTR_GETKEY,
	INSTR_CJUMP,
	INSTR_JUMP,
	INSTR_CLEAR,
	INSTR_MUL
};

#define NUM_KEYWORDS 8
const char keywords[NUM_KEYWORDS] = "-+<>.,[]"; /* this string is aligned with the enum above */
int get_instr_type(char a) {
	int i;
	for (i = 0; i < NUM_KEYWORDS; i++) /* finds the "type" as an index of the "keywords" string */
		if (a == keywords[i])
			return i;
	return -1;
}

struct instruction {
	int type;
	int data;
	int offset;
};

struct instruction* tape;
unsigned int ip = 0;

/*
 * used to get a rough estimate of how many instructions should
 * be allocated.
 */
int count_commands(const char* input) {
	int len = strlen(input);
	int res = 0;
	
	int i;
	for (i = 0; i < len; i++)
		if (get_instr_type(input[i]) >= 0) res++;
	
	return res;
}

/* removes all characters that aren't bf commands */
void remove_comments() {
	int num_commands = count_commands(source);
	int len = strlen(source);
	char* buf = malloc(num_commands + 1);
	
	int i, j = 0;
	for (i = 0; i < len; i++) {
		if (get_instr_type(source[i]) >= 0) {
			buf[j] = source[i];
			j++;
		}
	}
	buf[j] = '\0';
	
	free(source);
	source = buf;
}

#define MAX_STACK_DEPTH 2048
int stack[MAX_STACK_DEPTH];
int sp = 0;

/* The criteria for a loop to be considered a multiplication loop are:
 *  + it must have a balanced set of < and > commands
 *  + it must not contain a nested loop
 *  + it must not contain any commands other than + - < and >
 *  + the first or last command of a mulloop should be a - (but not both)
 */
int multiplication_loop() {
	char* begin = token;
	char* end = token + 1;
	while (*end != ']') {
		if (*end == '\0' || get_instr_type(*end) >= 4) return 0;
		end++;
	}
	/* end is now a pointer to the end of the theoretical
	 * mulloop loop */
	
	int num_lptr = 0, num_rptr = 0; /* the count of < and > */
	char* index;
	for (index = begin; index <= end; index++) {
		if (*index == '<') num_lptr++;
		else if (*index == '>') num_rptr++;
	}

	/* every multiplication loop must have a balanced set
	 * of < and > */
	if (num_lptr != num_rptr) return 0;
	
	if (!(begin[1] == '-') != !(end[-1] == '-')) {
		/* a multiplication loop has been found! */
		
		index = begin;
		int offset = 0;
		while (index <= end) {
			if (*index == '>') offset++;
			else if (*index == '<') offset--;
			int amount = 0;
			index++;
			while (*index == '+' || *index == '-') {
				if (*index == '+') amount++;
				else if (*index == '-') amount--;
				index++;
			}

			/* dont emit a multiplication command if there
			 * is no offset... a cell should never be
			 * multiplied by itself!!! */
			if (amount != 0 && offset != 0) {
				tape[ip].type = INSTR_MUL;
				tape[ip].data = amount;
				tape[ip].offset = offset;
				ip++;
			}
		}
	} else {
		return 0;
	}
	
	token = end + 1;
	tape[ip].type = INSTR_CLEAR;
	ip++;
	
	return 1;
}

void contract() {
	int data = 0;
	int type = get_instr_type(*token);
	int temp = type;

	/* + - < and > can be contracted */
	while (temp == type && *token != '\0') {
		data++;
		token++;
		temp = get_instr_type(*token);
	}
	
	tape[ip].type = type;
	tape[ip].data = data;
	ip++;
}

void compile() {
	tape = calloc(strlen(source), sizeof(struct instruction));
	
	while (*token != '\0') {
		int type = get_instr_type(*token);
		int data = 1;
		if (type == INSTR_CJUMP) {
			if (multiplication_loop()) {
				/* continue */
			} else if (!strncmp(token, "[-]", 3)) {
				tape[ip].type = INSTR_CLEAR;
				ip++;
				token += 3;
			} else {
				stack[sp++] = ip;
				tape[ip].type = type;
				tape[ip].data = data;
				ip++;
				token++;
			}
		} else if (type >= 4) { /* handles , . [ and ] (which are noncontractable) */
			if (type == INSTR_JUMP) {
				data = stack[--sp];
				tape[stack[sp]].data = ip;
			}
			tape[ip].type = type;
			tape[ip].data = data;
			ip++;
			token++;
		} else { /* token is of type + - < or > (which are contractable) */
			contract();
		}
	}
	
	tape[ip].type = -1;
}

#define MAX_MEMORY 65536
char memory[MAX_MEMORY];
unsigned short ptr = 0;

unsigned long long instr_counter = 0;
void execute() {
	ip = 0;
	
	while (tape[ip].type >= 0) {
		switch(tape[ip].type) {
			case INSTR_SUB: memory[ptr] -= tape[ip].data; break;
			case INSTR_ADD: memory[ptr] += tape[ip].data; break;
			case INSTR_SUBPTR: ptr -= tape[ip].data; break;
			case INSTR_ADDPTR: ptr += tape[ip].data; break;
			case INSTR_PRINT: if (dump_to_file) {*outbuf_ptr++ = memory[ptr];} else {putchar(memory[ptr]);} break;
			case INSTR_GETKEY: memory[ptr] = getchar(); break;
			case INSTR_CJUMP: if (!memory[ptr]) ip = tape[ip].data; break;
			case INSTR_JUMP: ip = tape[ip].data; ip--; break;
			case INSTR_CLEAR: memory[ptr] = 0; break;
			case INSTR_MUL: memory[ptr + tape[ip].offset] += memory[ptr] * tape[ip].data; break;
		}
		
		ip++;
		instr_counter++;
	}
}

int main(int argc, char** argv) {
	if (argc < 2) {
		printf("usage: appname [path_of_file_to_run]");
		exit(EXIT_FAILURE);
	}
	
	int i;
	for (i = 0; i < argc; i++) {
		if (!strncmp(argv[i], "-o", 2)) {
			output_path = &argv[i][2];
			dump_to_file = 1;
		}
	}
	
	source = load_file(argv[1]);
	if (!source) {
		printf("invalid file: %s\n", argv[1]);
		exit(EXIT_FAILURE);
	}
	
	remove_comments();
	token = source;
	compile();
	
	clock_t begin = clock();
	
	execute();
	
	clock_t end = clock();
	double time_spent = (double)(end - begin) / CLOCKS_PER_SEC;
	printf("\nProgram executed %llu instructions in %f seconds.\n", instr_counter, time_spent);
	
	if (dump_to_file) {
		printf("writing output to file %s...\n", output_path);
		
		FILE* file = fopen(output_path, "w");
		if (!file) {
			printf("error: could not open output file.\n");
			exit(EXIT_FAILURE);
		}
		
		fprintf(file, "%s", output_buffer);
		
		fclose(file);
	}
	
	printf("done.\n");
	
    return EXIT_SUCCESS;
}
