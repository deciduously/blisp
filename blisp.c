#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "mpc.h"


/* Faking readline on Windows platforms */
#ifdef _WIN32
#include <string.h>

static char buffer[2048];

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

/* MACROS */

#define LASSERT(args, cond, err) \
  if (!(cond)) { lval_del(args); return lval_err(err); }

#define LASSERT_ARG_NUM(args, num) \
  LASSERT(args, args->count <= num, "Function passed too many args!");

#define LASSERT_EMPTY_LIST(args) \
  LASSERT(args, args->cell[0]->count != 0, "Function called on empty list")

#define LASSERT_TYPE(args, expected) \
  LASSERT(args, args->cell[0]->type == expected, "Function called with incorrect type")

/* TYPES */

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

/* lval variants */
enum { LVAL_NUM, LVAL_ERR, LVAL_SYM, LVAL_SEXPR, LVAL_QEXPR };

/* error variants */
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

/* sexpr */
lval* lval_sexpr(void) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_SEXPR;
  v->count = 0;
  v->cell = NULL;
  return v;
}

/* qexpr */
lval* lval_qexpr(void) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_QEXPR;
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

    case LVAL_QEXPR:
    case LVAL_SEXPR:
      for (int i = 0; i < v->count; i++) {
        /* Free the body recursively */
        lval_del(v->cell[i]);
      }

      /* as well as memory allocated to contain the pointers */
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
/* NOTE - this is NOT a cons cell like a Lisp usually uses */
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

  /* if root (>) or sexpr or qexpr then first create empty list */
  lval* x = NULL;
  if (strcmp(t->tag, ">") == 0) { x = lval_sexpr(); }
  if (strstr(t->tag, "sexpr")) { x = lval_sexpr(); }
  if (strstr(t->tag, "qexpr")) { x = lval_qexpr(); }

  /* Fill the list with any valid expression therein */
  for (int i = 0; i < t->children_num; i++) {
    /* First, skip the junk */
    if (strcmp(t->children[i]->contents, "(") == 0) { continue; }
    if (strcmp(t->children[i]->contents, ")") == 0) { continue; }
    if (strcmp(t->children[i]->contents, "{") == 0) { continue; }
    if (strcmp(t->children[i]->contents, "}") == 0) { continue; }
    if (strcmp(t->children[i]->tag, "regex") == 0) { continue; }

    x = lval_add(x, lval_read(t->children[i]));
  }

  return x;
}

/* EVAL */

lval* lval_eval(lval* v);

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

/* BUILTINS */

lval* builtin_head(lval* a) {
  /* Check for error conditions */
  LASSERT_ARG_NUM(a, 1);
  LASSERT_TYPE(a, LVAL_QEXPR);
  LASSERT_EMPTY_LIST(a);

  /* If no error, take the first arg */
  lval* v = lval_take(a, 0);
  /* Delete all elements that are not the head */
  while (v->count > 1) { lval_del(lval_pop(v, 1)); }
  return v;
}

lval* builtin_tail(lval* a) {
  /* Check for error conditions */
  LASSERT_ARG_NUM(a, 1);
  LASSERT_TYPE(a, LVAL_QEXPR);
  LASSERT_EMPTY_LIST(a);

  /* If no error, take the first arg */
  lval* v = lval_take(a, 0);
  /* Delete the first element and return */
  lval_del(lval_pop(v, 0));
  return v;
}

/* Simply convert the given Sexpr to a Qexpr */
/* Not unlike 'quote' */
lval* builtin_list(lval* a) {
  a->type = LVAL_QEXPR;
  return a;
}

lval* builtin_eval(lval* a) {
  LASSERT_ARG_NUM(a, 1);
  LASSERT_TYPE(a, LVAL_QEXPR);

  lval* x = lval_take(a, 0);
  x->type = LVAL_SEXPR;
  return lval_eval(x);
}

lval* lval_join(lval* x, lval* y) {
  /* add each cell in y to x */
  while (y->count) {
    x = lval_add(x, lval_pop(y, 0));
  }

  /* We've drained y and added it all to x, so cleanup */
  lval_del(y);
  return x;
}

lval* builtin_join(lval* a) {
  for (int i = 0; i < a->count; i++) {
    LASSERT(a, a->cell[i]->type == LVAL_QEXPR, "Function 'join' passed incorrect type");
  }

  lval* x = lval_pop(a, 0);

  while (a->count) {
    x = lval_join(x, lval_pop(a, 0));
  }

  lval_del(a);
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

lval* builtin(lval* a, char* func) {
  if (strcmp("list", func) == 0) { return builtin_list(a); }
  if (strcmp("head", func) == 0) { return builtin_head(a); }
  if (strcmp("tail", func) == 0) { return builtin_tail(a); }
  if (strcmp("join", func) == 0) { return builtin_join(a); }
  if (strcmp("eval", func) == 0) { return builtin_eval(a); }
  if (strstr("+-/*%^maxminaddsubmuldiv", func)) { return builtin_op(a, func); }
  lval_del(a);
  return lval_err("Unknown Function!");
}

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
  lval* result = builtin(v, f->sym);
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
    case LVAL_QEXPR: lval_expr_print(v, '{', '}'); break;
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
    mpc_parser_t* Qexpr    = mpc_new("qexpr");
    mpc_parser_t* Expr     = mpc_new("expr");
    mpc_parser_t* Blisp    = mpc_new("blisp");

    /* Define them with the following Language */
    mpca_lang(MPCA_LANG_DEFAULT,
    "                                                                            \
        number   : /-?[0-9]+/ ;                                                 \
        symbol   : '+' | '-' | '*' | '/' | '^' | '%'                             \
                 | \"add\" | \"sub\" | \"mul\" | \"div\" | \"min\" | \"max\"     \
                 | \"list\" | \"head\" | \"tail\" | \"join\" | \"eval\"     ;    \
        sexpr    : '(' <expr>* ')' ;                                             \
        qexpr    : '{' <expr>* '}' ;                                             \
        expr     : <number> | <symbol> | <sexpr> | <qexpr> ;                     \
        blisp    : /^/ <expr>* /$/ ;                                             \
    ",
        Number, Symbol, Sexpr, Qexpr, Expr, Blisp);

    puts("Blisp 0.0.1");
    puts("Press Ctrl+c to exit\n");

    while (1) {
        char* input = readline("blisp> ");
        add_history(input);

        /* Attempt to Parse the user Input */
        mpc_result_t r;
        if (mpc_parse("<stdin>", input, Blisp, &r)) {
            /* On success, eval and print */
            lval* result = lval_eval(lval_read(r.output));
            lval_println(result);
            lval_del(result);
            /*mpc_ast_print(r.output);*/
            mpc_ast_delete(r.output);
        } else {
            /* Otherwise print the error */
            mpc_err_print(r.error);
            mpc_err_delete(r.error);
        }
        free(input);
        }
    /* Undefine and Delete our Parsers */
    mpc_cleanup(6, Number, Symbol, Sexpr, Qexpr, Expr, Blisp);
    return 0;
}