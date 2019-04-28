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

struct lval;
struct lenv;
typedef struct lval lval;
typedef struct lenv lenv;

/* lval variants */
enum { LVAL_FUN, LVAL_NUM, LVAL_ERR, LVAL_SYM, LVAL_SEXPR, LVAL_QEXPR };

/* function pointer! */
typedef lval*(*lbuiltin)(lenv*, lval*);

typedef struct lval {
  int type;/* LVAL_**/ 
  /* if LVAL_NUM */
  long num;
  /* if LVAL_ERR */
  char* err;
  /* if LVAL_SYM */ 
  char* sym;
  /* if FUN */
  lbuiltin fun;
  /* if LVAL_SEXPR | LVAL_QEXPR */
  int count;
  struct lval** cell;
} lval;

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

/* function */
lval* lval_fun(lbuiltin func) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_FUN;
  v->fun = func;
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
    case LVAL_FUN:   printf("<function>"); break;
    case LVAL_NUM:   printf("%li", v->num); break;
    case LVAL_ERR:   printf("Error: %s", v->err); break;
    case LVAL_SYM:   printf("%s", v->sym); break;
    case LVAL_SEXPR: lval_expr_print(v, '(', ')'); break;
    case LVAL_QEXPR: lval_expr_print(v, '{', '}'); break;
  }
}

/* Print an "lval" followed by a newline */
void lval_println(lval* v) { lval_print(v); putchar('\n'); }

/* lval type Destructor */
/* no fancy Rust Drop semantics :( */
void lval_del(lval* v) {
  switch(v->type) {
    // Nothing malloc'd
    case LVAL_FUN:
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

/* copy an lval, for example in and out of the environment */
lval* lval_copy(lval* v) {
  /* build a new lval */
  lval* x = malloc(sizeof(lval));
  x->type = v-> type;

  switch (v->type) {
    /* functions and numbers can copy directly */
    case LVAL_FUN: x->fun = v->fun; break;
    case LVAL_NUM: x->num = v->num; break;

    /* strings need malloc and strcpy */
    case LVAL_ERR:
      x->err = malloc(strlen(v->err) + 1);
      strcpy(x->sym, v->sym); break;

    /* Copy lists by copying each sub-expression */
    case LVAL_QEXPR:
    case LVAL_SEXPR:
      x->count = v->count;
      x->cell = malloc(sizeof(lval*) * x->count);
      for (int i = 0; i < x->count; i++) {
        x->cell[i] = lval_copy(v->cell[i]);
      }
      break;
  }

  return x;
}

/* extract single element from sexpr at index i */
/* and shift the rest of the list backwards, returning the extracted lval */
lval* lval_pop(lval* v, int i) {
  lval* x = v->cell[i];

  memmove(&v->cell[i], &v->cell[i+1], sizeof(lval*) * (v->count-i-1));
  v->count--;
  v->cell = realloc(v->cell, sizeof(lval*) * v->count);
  return x;
}

/* wrapper around lval_pop that includes the destructor */
lval* lval_take(lval* v, int i) {
  lval* x = lval_pop(v, i);
  lval_del(v);
  return x;
}

/* ENVIRONMENT */

/* A sym corresponds to val at the same index */
struct lenv {
  int count;
  char** syms;
  lval** vals;
};

/* Constructor */
lenv* lenv_new(void) {
  lenv* e = malloc(sizeof(lenv));
  e->count = 0;
  e->syms = NULL;
  e->vals = NULL;
  return e;
}

/* Destructor */
void lenv_del(lenv* e) {
  for (int i = 0; i < e->count; i++) {
    free(e->syms[i]);
    lval_del(e->vals[i]);
  }
  free(e->syms);
  free(e->vals);
  free(e);
}

/* Getter */
lval* lenv_get(lenv* e, lval* k) {
  for (int i = 0; i < e->count; i++) {
    if (strcmp(e->syms[i], k->sym) == 0) {
      return lval_copy(e->vals[i]);
    }
  }
  return lval_err("unbound symbol!");
}

/* Setter */
void lenv_put(lenv* e, lval* k, lval* v) {
  /* first check if variable already exists */
  for (int i = 0; i < e->count; i++) {
    /* if found, delete and replace with new val */
    if (strcmp(e->syms[i], k->sym) == 0) {
      lval_del(e->vals[i]);
      e->vals[i] = lval_copy(v);
      return;
    }
  }

  /* otherwise allocate a new pair */
  e->count++;
  e->vals = realloc(e->vals, sizeof(lval*) * e->count);
  e->syms = realloc(e->syms, sizeof(char*) * e->count);

  /* copy key and value */
  e->vals[e->count-1] = lval_copy(v);
  e->syms[e->count-1] = malloc(strlen(k->sym) + 1);
  strcpy(e->syms[e->count-1], k->sym);
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
/* the book does this as a constantly resizing array */
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

lval* lval_eval(lenv* e, lval* a);

lval* lval_eval_sexpr(lenv* e, lval* v) {
  /* Evaluate children */
  for (int i = 0; i < v->count; i++) {
    v->cell[i] = lval_eval(e, v->cell[i]);
  }

  /* Error checking */
  for (int i = 0; i < v->count; i++) {
    if (v->cell[i]->type == LVAL_ERR) { return lval_take(v, i); }
  }

  /* Empty expression */
  if (v->count == 0) { return v; }

  /* Single expression */
  if (v->count == 1) { return lval_take(v, 0); }

  /* ensure first element is a function */
  lval* f = lval_pop(v, 0);
  if (f->type != LVAL_FUN) {
    lval_del(v); lval_del(f);
    return lval_err("first element is not a function!");
  }

  /* if it's a function, call it! */
  lval* result = f->fun(e, v);
  lval_del(f);
  return result;
}

lval* lval_eval(lenv* e, lval* v) {
  if (v->type == LVAL_SYM) {
    lval* x = lenv_get(e, v);
    lval_del(v);
    return x;
  }

  if (v->type == LVAL_SEXPR) { return lval_eval_sexpr(e, v); }
  /* otherwise there's nothing to do! */
  return v;
}

/* BUILTINS */

lval* builtin_head(lenv* e, lval* a) {
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

lval* builtin_tail(lenv* e, lval* a) {
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
lval* builtin_list(lenv* e, lval* a) {
  a->type = LVAL_QEXPR;
  return a;
}

lval* builtin_eval(lenv* e, lval* a) {
  LASSERT_ARG_NUM(a, 1);
  LASSERT_TYPE(a, LVAL_QEXPR);

  lval* x = lval_take(a, 0);
  x->type = LVAL_SEXPR;
  return lval_eval(e, x);
}

lval* lval_join(lenv* e, lval* x, lval* y) {
  /* add each cell in y to x */
  while (y->count) {
    x = lval_add(x, lval_pop(y, 0));
  }

  /* We've drained y and added it all to x, so cleanup */
  lval_del(y);
  return x;
}

lval* builtin_join(lenv* e, lval* a) {
  for (int i = 0; i < a->count; i++) {
    LASSERT_TYPE(a, LVAL_QEXPR);
  }

  lval* x = lval_pop(a, 0);

  while (a->count) {
    x = lval_join(e, x, lval_pop(a, 0));
  }

  lval_del(a);
  return x;
}

lval* builtin_len(lenv* e, lval* a) {
  /* This is not quite it */
  /* You need to ensure it's called on a Qexpr */
  LASSERT_ARG_NUM(a, 1);
  LASSERT_TYPE(a, LVAL_QEXPR);

  lval* v = lval_take(a, 0);
  int cnt = v->count;
  lval_del(v);
  return lval_num(cnt);
}

lval* builtin_init(lenv* e, lval* a) {
  LASSERT_ARG_NUM(a, 1);
  LASSERT_TYPE(a, LVAL_QEXPR);
  LASSERT_EMPTY_LIST(a);

  /* if no error, take the first arg */
  lval* v = lval_take(a, 0);

  // delete the last element of v
  lval_del(lval_pop(v, v->count-1));
  return v;
}

lval* builtin_cons(lenv* e, lval* a) {
  LASSERT_ARG_NUM(a, 2);
  /* TODO: assert types? */
  
  /* get first val and second qexpr */
  lval *v = lval_pop(a, 0);
  lval* q = lval_pop(a, 0);
  /* create a new qexpr */
  lval *ret = lval_qexpr();

  /* add the value and then join it with the list */
  lval_add(ret, v);
  lval_join(e, ret, q);

  lval_del(a);
  return ret;
}

lval* builtin_op(lenv* e, lval* a, char* op) {
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

lval* builtin_add(lenv* e, lval* a) {
  return builtin_op(e, a, "+");
}

lval* builtin_sub(lenv* e, lval* a) {
  return builtin_op(e, a, "-");
}

lval* builtin_mul(lenv* e, lval* a) {
  return builtin_op(e, a, "*");
}

lval* builtin_div(lenv* e, lval* a) {
  return builtin_op(e, a, "/");
}

lval* builtin_max(lenv* e, lval* a) {
  return builtin_op(e, a, "max");
}

lval* builtin_min(lenv* e, lval* a) {
  return builtin_op(e, a, "min");
}

lval* builtin_pow(lenv* e, lval* a) {
  return builtin_op(e, a, "^");
}

lval* builtin_mod(lenv* e, lval* a) {
  return builtin_op(e, a, "%");
}

/* Register builtins with environment */

void lenv_add_builtin(lenv* e, char* name, lbuiltin func) {
  lval* k = lval_sym(name);
  lval* v = lval_fun(func);
  lenv_put(e, k, v);
  lval_del(k); lval_del(v);
}

void lenv_add_builtins(lenv* e) {
  lenv_add_builtin(e, "list", builtin_list);
  lenv_add_builtin(e, "head", builtin_head);
  lenv_add_builtin(e, "tail", builtin_tail);
  lenv_add_builtin(e, "join", builtin_join);
  lenv_add_builtin(e, "len", builtin_len);
  lenv_add_builtin(e, "eval", builtin_eval);
  lenv_add_builtin(e, "init", builtin_init);
  lenv_add_builtin(e, "cons", builtin_cons);
  lenv_add_builtin(e, "+", builtin_add);
  lenv_add_builtin(e, "add", builtin_add);
  lenv_add_builtin(e, "-", builtin_sub);
  lenv_add_builtin(e, "sub", builtin_sub);
  lenv_add_builtin(e, "*", builtin_mul);
  lenv_add_builtin(e, "mul", builtin_mul);
  lenv_add_builtin(e, "/", builtin_div);
  lenv_add_builtin(e, "div", builtin_div);
  lenv_add_builtin(e, "^", builtin_pow);
  lenv_add_builtin(e, "pow", builtin_pow);
  lenv_add_builtin(e, "%", builtin_mod);
  lenv_add_builtin(e, "mod", builtin_mod);
}


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
        symbol   : /[a-zA-Z0-9_+\\-*\\/\\\\=<>!&]+/ ;                           \
        sexpr    : '(' <expr>* ')' ;                                             \
        qexpr    : '{' <expr>* '}' ;                                             \
        expr     : <number> | <symbol> | <sexpr> | <qexpr> ;                     \
        blisp    : /^/ <expr>* /$/ ;                                             \
    ",
        Number, Symbol, Sexpr, Qexpr, Expr, Blisp);

    puts("Blisp 0.0.1");
    puts("Press Ctrl+c to exit\n");

    /* create environment */

    lenv* e = lenv_new();
    lenv_add_builtins(e);

    while (1) {
        char* input = readline("blisp> ");
        add_history(input);

        /* Attempt to Parse the user Input */
        mpc_result_t r;
        if (mpc_parse("<stdin>", input, Blisp, &r)) {
            /* On success, eval and print */
            lval* result = lval_eval(e, lval_read(r.output));
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
    /* Cleanup environment */
    lenv_del(e);
    /* Undefine and Delete our Parsers */
    mpc_cleanup(6, Number, Symbol, Sexpr, Qexpr, Expr, Blisp);
    return 0;
}