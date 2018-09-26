#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "mpc.h"


/* Faking readline on Windows platforms */
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

/* Types */

typedef struct lval {
  int type;/* LVAL_**/ 
  /* if LVAL_NUM */
  long num;
  /* if LVAL_ERR */
  char* err;
  /* if LVAL_SYM */ 
  char* sym;
  /* if LVAL_SEXPR */
  int count;
  struct lval** cell; /* This is what you don't know how to do in safe Rust - it contains a reference to itself */
} lval;

enum { LVAL_NUM, LVAL_ERR, LVAL_SYM, LVAL_SEXPR };

enum { LERR_DIV_ZERO, LERR_BAD_OP, LERR_BAD_NUM };

/* Type Constructors */
/* Each constructor returns a pointer to a heap-allocated lval */

/* number */
lval* lval_num(long x) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_NUM;
  v->num = x;
  return v;
}

/* error */
lval* lval_err(char* message) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_ERR;
  v->err = malloc(strlen(message) + 1); /* because strlen excludes the null terminator but we (obviously) still need it */
  strcpy(v->err, message);
  return v;
}

/* symbol */
lval* lval_sym(char* s) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_SYM;
  v->sym = malloc(strlen(s) + 1);
  strcpy(v->sym, s);
  return v;
}

/* sexp */
lval* lval_sexpr(void) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_SEXPR;
  v->count = 0;
  v->cell = NULL;
  return v;
}

/* lval type Destructor */
/* no fancy Rust Drop semantics :( */
void lval_del(lval* v) {
  switch(v->type) {
    // Nothing malloc'd
    case LVAL_NUM: break;

    /* Free the char* if applicable */
    case LVAL_ERR: free(v->err); break;
    case LVAL_SYM: free(v->sym); break;

    case LVAL_SEXPR:
      for (int i = 0; i < v->count; i++) {
        /* Free the body recursively */
        lval_del(v->cell[i]);
      }

      free(v->cell);
    break;
  }
  /* don't forget the lval struct itself */
  free(v);
}

/* READ */

/* Error-catching wrapper around lval_num constructor */
lval* lval_read_num(mpc_ast_t* t) {
  errno = 0;
  long x = strtol(t->contents, NULL, 10);
  return errno != ERANGE
  ? lval_num(x)
  : lval_err("invalid number");
}

/* add an element to a sexp */
/* the book does this a constantly resizing array */
/* TODO come back and reimplement as a cons cell */
/* see if you can linked list it up */
lval* lval_add(lval* v, lval* x) {
  v->count++;
  v->cell = realloc(v->cell, sizeof(lval*) * v->count);
  v->cell[v->count-1] = x;
  return v;
}

lval* lval_read(mpc_ast_t* t) {
  /* Symbols and Numbers are straightforward */
  if (strstr(t->tag, "number")) { return lval_read_num(t); }
  if (strstr(t->tag, "symbol")) { return lval_sym(t->contents); }

  /* if root (>) or sexpr then first create empty list */
  lval* x = NULL;
  if (strcmp(t->tag, ">") == 0) { x = lval_sexpr(); }
  if (strcmp(t->tag, "sexpr")) { x = lval_sexpr(); }

  /* Fill the list with any valid expression therein */
  for (int i = 0; i < t->children_num; i++) {
    if (strcmp(t->children[i]->contents, "(") == 0 ||
        strcmp(t->children[i]->contents, ")") == 0 ||
        strcmp(t->children[i]->tag, "regex") == 0) {
      continue;
    }

    x = lval_add(x, lval_read(t->children[i]));
  }

  return x;
}



/* EVAL */

/* extract single element from sexpr at index i */
/* and shift the rest of the list backwards, returning the extracted lval */
lval* lval_pop(lval* v, int i) {
  lval* x = v->cell[i];

  memmove(&v->cell[i], &v->cell[i+1], sizeof(lval*) * (v->count-i-1));
  v->count--;
  v->cell = realloc(v->cell, sizeof(lval*) * v->count);
  return x;
}

/* wrapper arround lval_pop that includes the destructor */
lval* lval_take(lval* v, int i) {
  lval* x = lval_pop(v, i);
  lval_del(v);
  return x;
}

lval* builtin_op(lval* a, char* op) {
  /* Ensure all args are numbers */
  for (int i = 0; i < a->count; i++) {
    if (a->cell[i]->type != LVAL_NUM) {
      lval_del(a);
      return lval_err("Cannot operate on non-number!");
    }
  }

  lval* x = lval_pop(a, 0);

  /* If no arguments and subtraction, perform unary negation */
  if ((strcmp(op, "-") == 0 || strcmp(op, "sub") == 0) && a->count == 0) {
    x->num = -x->num;
  }

  //consume the children
  while (a->count > 0) {
    lval* y = lval_pop(a, 0);

    if (strcmp(op, "+") == 0 || strcmp(op, "add") == 0) { x->num += y->num; }
    if (strcmp(op, "-") == 0 || strcmp(op, "sub") == 0) { x->num -= y->num; }
    if (strcmp(op, "*") == 0 || strcmp(op, "mul") == 0) { x->num *= y->num; }
    if (strcmp(op, "/") == 0 || strcmp(op, "div") == 0) {
      if (y->num == 0) {
        lval_del(x); lval_del(y);
        x = lval_err("Division By Zero!"); break;
      }
      x->num /= y->num;
    }
    if (strcmp(op, "^") == 0) { pow((double)x->num, (double)y->num); }
    if (strcmp(op, "%") == 0) { x-> num = x->num % y->num; }
    if (strcmp(op, "max") == 0) { if (x->num <= y->num) { x->num = y->num; }}
    if (strcmp(op, "min") == 0) { if (x->num <= y->num) {} else { x->num = y->num; }}

    lval_del(y);
  }

  lval_del(a); return x;
}

lval* lval_eval(lval* v);

lval* lval_eval_sexpr(lval* v) {
  /* Evaluate children */
  for (int i = 0; i < v->count; i++) {
    v->cell[i] = lval_eval(v->cell[i]);
  }

  /* Error checking */
  for (int i = 0; i < v->count; i++) {
    if (v->cell[i]->type == LVAL_ERR) { return lval_take(v, i); }
  }

  /* Empty expression */
  if (v->count == 0) { return v; }

  /* Single expression */
  if (v->count == 1) { return lval_take(v, 0); }

  /* Ensure first element is a Symbol */
  lval* f = lval_pop(v, 0);
  if (f->type != LVAL_SYM) {
    lval_del(f); lval_del(v);
    return lval_err("S-expression does not start with symbol!");
  }

  /* Call with built-in operator */
  lval* result = builtin_op(v, f->sym);
  lval_del(f);
  return result;
}

lval* lval_eval(lval* v) {
  if (v->type == LVAL_SEXPR) { return lval_eval_sexpr(v); }
  /* otherwise there's nothing to do! */
  return v;
}

/* PRINT */

void lval_print(lval* v); 

void lval_expr_print(lval* v, char open, char close) {
  putchar(open);
  for (int i = 0; i < v->count; i++) {
    lval_print(v->cell[i]);
    if (i != (v->count-1)) {
      putchar(' ');
    }
  }

  putchar(close);
}

void lval_print(lval* v) {
  switch (v->type) {
    case LVAL_NUM:   printf("%li", v->num); break;
    case LVAL_ERR:   printf("Error: %s", v->err); break;
    case LVAL_SYM:   printf("%s", v->sym); break;
    case LVAL_SEXPR: lval_expr_print(v, '(', ')'); break;
  }
}

/* Print an "lval" followed by a newline */
void lval_println(lval* v) { lval_print(v); putchar('\n'); }

/* LOOP */

int main(int argc, char** argv) {
    /* Create Some Parsers */
    mpc_parser_t* Number   = mpc_new("number");
    mpc_parser_t* Symbol   = mpc_new("symbol");
    mpc_parser_t* Sexpr    = mpc_new("sexpr");
    mpc_parser_t* Expr     = mpc_new("expr");
    mpc_parser_t* Blisp    = mpc_new("blisp");

    /* Define them with the following Language */
    mpca_lang(MPCA_LANG_DEFAULT,
    "                                                                                                               \
        number   : /-?[0-9.]+/ ;                                                                                    \
        symbol   : '+' | '-' | '*' | '/' | '^' | '%' | \"add\" | \"sub\" | \"mul\" | \"div\" | \"min\" | \"max\" ;  \
        sexpr    : '(' <expr>* ')' ;                                                                                \
        expr     : <number> | <symbol> | <sexpr> ;                                                                  \
        blisp    : /^/ <expr>* /$/ ;                                                                       \
    ",
    Number, Symbol, Sexpr, Expr, Blisp);

    puts("Blisp 0.0.1");
    puts("Press Ctrl+c to exit\n");

    while (1) {
        char* input = readline("blisp> ");
        add_history(input);

        /* Attempt to Parse the user Input */
        mpc_result_t r;
        if (mpc_parse("<stdin>", input, Blisp, &r)) {
            /* On Success Print the AST */
            lval* result = lval_eval(lval_read(r.output));
            lval_println(result);
            lval_del(result);
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
    mpc_cleanup(5, Number, Symbol, Sexpr, Expr, Blisp);
    return 0;
}