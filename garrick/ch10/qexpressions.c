#include <stdio.h>
#include <stdlib.h>
#include <editline/readline.h>
#include <editline/history.h>
#include "mpc.h"
#define LASSERT(args, cond, err) if (!(cond)) { lval_del(args); return lval_err(err); }

/* enumeration of possible lval types */
enum { LVAL_ERR, LVAL_NUM, LVAL_SYM, LVAL_SEXPR, LVAL_QEXPR };

/* enumeration of possible error types */
/* enum { LERR_DIV_ZERO, LERR_BAD_OP, LERR_BAD_NUM }; */

/* type, type baby */
typedef struct lval {
  int type;

  long num;

  char* err;
  char* sym;

  /*Count & pointer to list of 'lval' */
  int count;
  struct lval** cell;


} lval;

/* forward dec. */
void lval_print(lval* v);
lval* lval_pop(lval* v, int i);
lval* lval_take(lval* v, int i);
lval* lval_eval(lval* v);
lval* builtin(lval* a, char* func);
lval* builtin_op(lval* a, char* op);
lval* lval_join(lval* x, lval* y);

/* pointer to new number type lval */
lval* lval_num(long x) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_NUM;
  v->num = x;
  return v;
}

/* pointer to new error type lval */
lval* lval_err(char* m) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_ERR;
  v->err = malloc(strlen(m) + 1);
  strcpy(v->err, m);
  return v;
}

/* pointer to new symbol type lval */
lval* lval_sym(char* s) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_SYM;
  v->sym  = malloc(strlen(s) + 1);
  strcpy(v->sym, s);
  return v;
}

/* pointer to new sexpr type lval */
lval* lval_sexpr(void) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_SEXPR;
  v->count = 0;
  v->cell = NULL;
  return v;
}

/* pointer to new Qexpr type lval */
lval* lval_qexpr(void) {
  lval* v = malloc(sizeof(lval));
  v->type = LVAL_QEXPR;
  v->count = 0;
  v->cell = NULL;
  return v;
}

/* delete an lvla* */
void lval_del(lval* v) {

  switch (v->type) {

  /* Do nothing special for number type */
  case LVAL_NUM: break;

  /* For Err or Sym free the string data */
  case LVAL_ERR: free(v->err); break;
  case LVAL_SYM: free(v->sym); break;
  
  /* If Qexpr or Sexpr then delete all elements inside */
  case LVAL_QEXPR:
  case LVAL_SEXPR:
    for (int i = 0; i < v->count; i++) {
      lval_del(v->cell[i]);    
    }
    /* Also free the memory allocated to contain the pointers */
    free(v->cell);
  break;  
  }
  /*  Don't forget to free the memory allocated for the lval struct itself! */
  free(v);
}

lval* lval_add(lval* v, lval* x) {
  v->count++;
  v->cell = realloc(v->cell, sizeof(lval*) * v->count);
  v->cell[v->count-1] = x;
  return v;
}

lval* lval_read_num(mpc_ast_t* t) {
  errno = 0;
  long x = strtol(t->contents, NULL, 10);
  return errno != ERANGE ? lval_num(x) : lval_err("invalid number");
}

lval* lval_read(mpc_ast_t* t) {
  /* If Symbol or Number return conversion to that type */
  if (strstr(t->tag, "number")) { return lval_read_num(t); }
  if (strstr(t->tag, "symbol")) { return lval_sym(t->contents); }

  /* If root (>) or sexpr then create empty list*/
  lval* x = NULL;
  if (strcmp(t->tag, ">") == 0) { x = lval_sexpr(); }
  if (strstr(t->tag, "sexpr"))  { x = lval_sexpr(); }
  if (strstr(t->tag, "qexpr"))  { x = lval_qexpr(); }

  /* Fill this list with any valid expression contained within */
  for (int i = 0; i < t->children_num; i++) {
    if (strcmp(t->children[i]->contents, "(") == 0) { continue; }
    if (strcmp(t->children[i]->contents, ")") == 0) { continue; }
    if (strcmp(t->children[i]->contents, "}") == 0) { continue; }
    if (strcmp(t->children[i]->contents, "{") == 0) { continue; }
    if (strcmp(t->children[i]->tag,  "regex") == 0) { continue; }
    x = lval_add(x, lval_read(t->children[i]));
  }

  return x;
}

void lval_expr_print(lval* v, char open, char close) {
  putchar(open);
  for (int i = 0; i < v->count; i++) {
    /* */
    lval_print(v->cell[i]);

    if (i != (v->count-1)) {
      putchar(' ');
    }
  }
  putchar(close);
}


/* Print an lval */
void lval_print(lval* v) {
  switch (v->type) {
    case LVAL_NUM: printf("%li", v->num); break;
    case LVAL_ERR: printf("Error: %s", v->err); break;
    case LVAL_SYM: printf("%s", v->sym); break;
    case LVAL_SEXPR: lval_expr_print(v, '(', ')'); break;
    case LVAL_QEXPR: lval_expr_print(v, '{', '}'); break;
  }
}



/* print and lval followed by a newline */
void lval_println(lval* v) { lval_print(v); putchar('\n'); }

lval* our_exponent(lval* base, lval* exp) {
  if(exp->num <= 1) {
    return base;
    }
  else {
    return lval_num(base->num * our_exponent(base, lval_num(exp->num - 1))->num);
  }
}

lval* min(lval* x, lval* y) {
  if(x->num <= y->num) {
    return x;
    } 
  return y;
}

lval* max(lval* x, lval* y) {
  if(x->num >= y->num) {
    return x;
    } 
  return y;
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
  if (v->count == 0) { return v;}

  /* Single expression */
  if (v->count == 1) { return lval_take(v, 0);}

  /* Ensure First element is Symbol */
  lval* f = lval_pop(v, 0);
  if (f->type != LVAL_SYM) {
    lval_del(f); lval_del(v);
    return lval_err("s-expression Does not start with symbol!");
  }


  /* Call builtin */
  lval* result = builtin(v, f->sym);
  lval_del(f);
  return result;
}

/* Let us eval */
lval* lval_eval(lval* v) {
  /* Evaluate Sexpressions */
  if (v->type == LVAL_SEXPR) { return lval_eval_sexpr(v); }
  /* All other lval types remain the same */
  return v;
}

lval* lval_pop(lval* v, int i) {
  /* Find the item at "i" */
  lval* x = v->cell[i];

  /* Shift the memory following the item at "i" over the top of it */
  memmove(&v->cell[i], &v->cell[i+1], sizeof(lval*) * (v->count-i-1));

  /* Decrease the count of items in the list */
  v->count--;

  /*Reallocate the memory used */
  v->cell = realloc(v->cell, sizeof(lval*) * v->count);
  return x;
}

lval* lval_take(lval* v, int i) {
  lval* x = lval_pop(v, i);
  lval_del(v);
  return x;
}

lval* builtin_head(lval* a) {
  LASSERT(a, (a->count == 1), "Function 'head' passed too many arguments!");
  LASSERT(a, (a->cell[0]->type == LVAL_QEXPR), "Function 'head' passed incorrect types!");
  LASSERT(a, a->cell[0]->count != 0, "Function 'head' passed {}!"); 

  /* Otherwise take the first argument */
  lval* v = lval_take(a, 0);
  /* Delete all elements that are not head and return */
  while (v->count > 1) { lval_del(lval_pop(v, 1)); }
  return v;
}

lval* builtin_tail(lval* a) {
  LASSERT(a, (a->count == 1), "Function 'tail' passed too many arguments!");
  LASSERT(a, (a->cell[0]->type == LVAL_QEXPR), "Function 'tail' passed incorrect types!");
  LASSERT(a, a->cell[0]->count != 0, "Function 'tail' passed {}!"); 

  /* Take the first argument */
  lval* v = lval_take(a, 0);

  /* Delete first element and return */
   lval_del(lval_pop(v, 0));
  return v;
}

lval* builtin_list(lval* a) {
  a->type = LVAL_QEXPR;
  return a;
}

lval* builtin_eval(lval* a) {
  LASSERT(a, (a->count == 1), "Function 'eval' passed too many arguments!");
  LASSERT(a, (a->cell[0]->type == LVAL_QEXPR), "Function 'eval' passed incorrect types!");

  lval* x = lval_take(a, 0);
  x->type = LVAL_SEXPR;
  return lval_eval(x);
}

lval* builtin_join(lval* a) {
  for (int i = 0; i < a->count; i++) {
    LASSERT(a, (a->cell[i]->type == LVAL_QEXPR), "Function 'join' passed incorrect types!");
  }

  lval* x = lval_pop(a, 0);

  while (a->count) {
    x = lval_join(x, lval_pop(a, 0));
  }

  lval_del(a);
  return x;
}

lval* lval_join(lval* x, lval* y) {
  
  /* For each cell in 'y' add it to 'x' */
  while (y->count) {
    x = lval_add(x, lval_pop(y, 0));
  }

  /* Delete the empty 'y' and return 'x' */
  lval_del(y);
  return x;
}

lval* builtin(lval* a, char* func) {
  if (strcmp("list", func) == 0) { return builtin_list(a); }
  if (strcmp("head", func) == 0) { return builtin_head(a); }
  if (strcmp("tail", func) == 0) { return builtin_tail(a); }
  if (strcmp("join", func) == 0) { return builtin_join(a); }
  if (strcmp("eval", func) == 0) { return builtin_eval(a); }
  if (strstr("+-/*", func)) { return builtin_op(a, func); }
  lval_del(a);
  return lval_err("Unknown Function!");
}

lval* builtin_op(lval* a, char* op) {
  /* Ensure all arguments are numbers */
  for (int i = 0; i < a->count; i++) {
    if(a->cell[i]->type != LVAL_NUM) {
      lval_del(a);
      return lval_err("Cannot operate on a non-number!");
    }
  }
  /* Pop the first element */
  lval* x = lval_pop(a, 0);

  /* If no argument and sub then perform unary negation */
  if ((strcmp(op, "-") == 0) && a->count == 0) { x->num = -x->num; }

  /* While there are still elements remaining */
  while (a->count > 0) {
    /* Pop the next element */
    lval* y = lval_pop(a, 0);

    /* perform the next operation */
    if (strcmp(op, "+") == 0) { x->num += y->num; }
    if (strcmp(op, "-") == 0) { x->num -= y->num; }
    if (strcmp(op, "*") == 0) { x->num *= y->num; }
    if (strcmp(op, "/") == 0) {
      if (y->num == 0) {
        lval_del(x);
        lval_del(y);
        x = lval_err("Division by Zero!");
        break;
      }
      x->num /= y->num;
    }
    /* Delete element no that we're finished with it */
    lval_del(y);
  }
  /* delete input expression and return result */
  lval_del(a);
  return x;
}

int main(int argc, char** argv) {

  /* Create some parsers */
  mpc_parser_t* Number    = mpc_new("number");
  mpc_parser_t* Symbol    = mpc_new("symbol");
  mpc_parser_t* Sexpr     = mpc_new("sexpr");
  mpc_parser_t* Qexpr     = mpc_new("qexpr");
  mpc_parser_t* Expr      = mpc_new("expr");
  mpc_parser_t* Lithp     = mpc_new("lithp");

  /* Define them with the following language */
  mpca_lang(MPCA_LANG_DEFAULT, 
    "\
    number : /-?[0-9]+/; \
    symbol : \"list\" | \"head\" | \"tail\" | \"join\" | \"eval\" | '+' | '-' | '*' | '/' | '%' | '^' | \"min\" | \"max\"; \
    sexpr  : '(' <expr>* ')'; \
    qexpr  : '{' <expr>* '}'; \
    expr   : <number> | <symbol> | <sexpr> | <qexpr> ; \
    lithp  : /^/ <expr>* /$/; \
    ",
  Number, Symbol, Sexpr, Qexpr, Expr, Lithp);

  /* Print version and exit */
  puts("Lithp Version 0.0.0.0.1");
  puts("Suitable for absolutely nothing!");
  puts("Press Ctrl-C to exit\n");

  /* never ending storrr-y! */
  while(1) {
      /* output our prompt and get input */ 
      char* input = readline("lithp> ");
      if (!input) break;
      /* add to our history */
      add_history(input);
      /* Attempt to parse the user input */
      mpc_result_t r;
      if (mpc_parse("<stdin>", input, Lithp, &r)) {
        /* Get a real result and print it */
        lval* x = lval_eval(lval_read(r.output));
        lval_println(x);
        lval_del(x);

        mpc_ast_delete(r.output);
      } else {
        /* Otherwise, print the error */
        mpc_err_print(r.error);
        mpc_err_delete(r.error);
      }
      /* free the malocs! */
      free(input);
  }
  /* Undefine and Delete our Parsers.  Or else... */
  mpc_cleanup(6, Number, Symbol, Sexpr, Qexpr, Expr, Lithp);
  return 0;
}

