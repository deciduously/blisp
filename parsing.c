#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "mpc.h"

#ifdef _WIN32
#include <string.h>

/* Fake readline function */
char* readline(char* prompt) {
  fputs(prompt, stdout);
  fgets(buffer, 2048, stdin);
  char* cpy = malloc(strlen(buffer)+1);
  strcpy(cpy, buffer);
  cpy[strlen(cpy)-1] = '\0';
  return cpy;
}

/* Fake add_history function */
void add_history(char* unused) {}

/* Otherwise include the readline headers */
#else
#include <readline/readline.h>
#include <readline/history.h>
#endif

typedef struct {
  int type;
  long num;
  int err;
} lval;

enum { LVAL_NUM, LVAL_ERR };

enum { LERR_DIV_ZERO, LERR_BAD_OP, LERR_BAD_NUM };

/* Create a new number type lval */
lval lval_num(long x) {
  lval v;
  v.type = LVAL_NUM;
  v.num = x;
  return v;
}

/* Create a new error type lval */
lval lval_err(int x) {
  lval v;
  v.type = LVAL_ERR;
  v.err = x;
  return v;
}

/* Use operator string to see which operation to perform */
long eval_op(long x, char* op, long y) {
  if (strcmp(op, "+") == 0 || strcmp(op, "add") == 0) { return x + y; }
  if (strcmp(op, "-") == 0 || strcmp(op, "sub") == 0) { return x - y; }
  if (strcmp(op, "*") == 0 || strcmp(op, "mul") == 0) { return x * y; }
  if (strcmp(op, "/") == 0 || strcmp(op, "div") == 0) { return x / y; }
  if (strcmp(op, "^") == 0) { return pow((double)x, (double)y); }
  if (strcmp(op, "%") == 0) { return x % y; }
  if (strcmp(op, "max") == 0) { if (x <= y) { return y; } else { return x; }}
  if (strcmp(op, "min") == 0) { if (x <= y) { return x; } else { return y; }}
  return 0;
}

long eval(mpc_ast_t* t) {

  /* If tagged as number return it directly. */
  if (strstr(t->tag, "number")) {
    return atoi(t->contents);
  }

  /* The operator is always second child. */
  char* op = t->children[1]->contents;

  /* We store the third child in `x` */
  long x = eval(t->children[2]);

  // negate case - unary operator
  if (strcmp(op, "-") == 0 && strcmp(t->children[3]->tag, "expr") != 0) {
      return -x;
  }

  /* Iterate the remaining children and combining. */
  int i = 3;
  while (strstr(t->children[i]->tag, "expr")) {
    x = eval_op(x, op, eval(t->children[i]));
    i++;
  }

  return x;
}

int main(int argc, char** argv) {
    /* Create Some Parsers */
    mpc_parser_t* Number   = mpc_new("number");
    mpc_parser_t* Operator = mpc_new("operator");
    mpc_parser_t* Expr     = mpc_new("expr");
    mpc_parser_t* Blisp    = mpc_new("blisp");

    /* Define them with the following Language */
    mpca_lang(MPCA_LANG_DEFAULT,
    "                                                                                                               \
        number   : /-?[0-9.]+/ ;                                                                                    \
        operator : '+' | '-' | '*' | '/' | '^' | '%' | \"add\" | \"sub\" | \"mul\" | \"div\" | \"min\" | \"max\" ;  \
        expr     : <number> | '(' <operator> <expr>+ ')' ;                                                          \
        blisp    : /^/ <operator> <expr>+ /$/ ;                                                                     \
    ",
    Number, Operator, Expr, Blisp);

    puts("Blisp 0.0.1");
    puts("Press Ctrl+c to exit\n");

    while (1) {
        char* input = readline("blisp> ");
        add_history(input);

        /* Attempt to Parse the user Input */
    mpc_result_t r;
        if (mpc_parse("<stdin>", input, Blisp, &r)) {
            /* On Success Print the AST */
            long result = eval(r.output);
            printf("%li\n", result);
            /*mpc_ast_print(r.output);*/
            mpc_ast_delete(r.output);
        } else {
            /* Otherwise Print the Error */
            mpc_err_print(r.error);
            mpc_err_delete(r.error);
        }
        free(input);
        }
    /* Undefine and Delete our Parsers */
    mpc_cleanup(4, Number, Operator, Expr, Blisp);
    return 0;
}