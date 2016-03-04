#pragma once
#ifndef _INC_ARC
#define _INC_ARC

#define VERSION "0.10"

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <math.h>
#include <time.h>
#include <setjmp.h>
#include <memory>
#include <vector>
#include <unordered_map>
#include <utility>
#include <iostream>
#include <sstream>
#include <iomanip>

using namespace std;

#ifdef READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#ifdef _MSC_VER
#define strdup _strdup
#endif

enum type {
	T_NIL, 
	T_CONS,
	T_SYM,
	T_NUM,
	T_BUILTIN,
	T_CLOSURE,
	T_MACRO,
	T_STRING,
	T_INPUT,
	T_OUTPUT,
	T_TABLE,
	T_CHAR,
	T_CONTINUATION
};

typedef enum {
  ERROR_OK = 0, ERROR_SYNTAX, ERROR_UNBOUND, ERROR_ARGS, ERROR_TYPE, ERROR_FILE, ERROR_USER
} error;

typedef struct atom atom;
typedef error(*builtin)(atom args, atom *result);

struct atom {
	enum type type;
	shared_ptr<void> p; // reference-counted pointer

	union { // primitive
		double number;
		char *symbol;
		builtin bi;
		FILE *fp;
		char ch;
		jmp_buf *jb;
	} value;

	template <typename T>
	T &as() {
		return *static_pointer_cast<T>(p);
	}
	
	friend bool operator ==(const atom a, const atom b);
};

struct cons {
	struct atom car, cdr;
	cons(atom car, atom cdr) : car(car), cdr(cdr) {}
};

typedef unordered_map<atom, atom> table;

/* forward declarations */
error apply(atom fn, atom args, atom *result);
int listp(atom expr);
char *slurp_fp(FILE *fp);
char *slurp(const char *path);
error eval_expr(atom expr, atom env, atom *result);
error macex(atom expr, atom *result);
string to_string(atom a, int write);
char *strcat_alloc(char **dst, char *src);
char *str_new();
error macex_eval(atom expr, atom *result);
error arc_load_file(const char *path);
char *get_dir_path(char *file_path);
void arc_init(char *file_path);
#ifndef READLINE
char *readline(const char *prompt);
#endif
char *readline_fp(const char *prompt, FILE *fp);
error read_expr(const char *input, const char **end, atom *result);
void print_expr(atom a);
void print_error(error e);
bool is(atom a, atom b);
atom make_table();
/* end forward */

#define car(a) (static_pointer_cast<cons>((a).p)->car)
#define cdr(a) (static_pointer_cast<cons>((a).p)->cdr)
#define no(a) ((a).type == T_NIL)

#define sym_is(a, b) ((a).value.symbol == (b).value.symbol)

extern hash<string> string_hash;
extern hash<double> double_hash;

namespace std {
	template<>
	struct hash<atom> {
		size_t operator ()(atom a) const {
			size_t r = 0;
			switch (a.type) {
			case T_NIL:
				return 0;
			case T_CONS:
				while (!no(a)) {
					r *= 31;
					if (a.type == T_CONS) {
						r += hash<atom>()(car(a));
						a = cdr(a);
					}
					else {
						r += hash<atom>()(a);
						break;
					}
				}
				return r;
			case T_SYM:
				return (size_t)a.value.symbol;
			case T_STRING: {
				return string_hash(a.as<string>());
			}
			case T_NUM: {
				return double_hash(a.as<double>());
			}
			case T_BUILTIN:
				return (size_t)a.p.get() / sizeof(a.p.get());
			case T_CLOSURE:
				return hash<atom>()(cdr(a));
			case T_MACRO:
				return hash<atom>()(cdr(a));
			case T_INPUT:
			case T_OUTPUT:
				return (size_t)a.p.get() / sizeof(a.p.get());
			default:
				return 0;
			}
		}
	};
}

/* symbols for faster execution */
extern atom sym_t, sym_quote, sym_assign, sym_fn, sym_if, sym_mac, sym_apply, sym_while, sym_cons, sym_sym, sym_string, sym_num, sym_table;
extern int arc_reader_unclosed;

#endif
