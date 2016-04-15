#pragma once
#ifndef _INC_ARC
#define _INC_ARC

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

#ifdef READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#ifdef _MSC_VER
#define strdup _strdup
#endif

namespace arc {
	const char VERSION[] = "0.10.4";

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
		std::shared_ptr<void> p; // reference-counted pointer

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
			return *std::static_pointer_cast<T>(p);
		}

		friend bool operator ==(const atom a, const atom b);
	};

	struct cons {
		struct atom car, cdr;
		cons(atom car, atom cdr) : car(car), cdr(cdr) {}
	};

	typedef std::unordered_map<atom, atom> table;

	/* forward declarations */
	error apply(atom fn, atom args, atom *result);
	int listp(atom expr);
	char *slurp_fp(FILE *fp);
	char *slurp(const char *path);
	error eval_expr(atom expr, atom env, atom *result);
	error macex(atom expr, atom *result);
	std::string to_string(atom a, int write);
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
	bool iso(atom a, atom b);
	atom make_table();
	void repl();
	/* end forward */

	atom & car(atom & a);
	atom & cdr(atom & a);
	bool no(atom & a);
	bool sym_is(const atom & a, const atom & b);

	/* symbols for faster execution */
	extern atom sym_t, sym_quote, sym_assign, sym_fn, sym_if, sym_mac, sym_apply, sym_while, sym_cons, sym_sym, sym_string, sym_num, sym_table;
}

namespace std {
	template<>
	struct hash<arc::atom> {
		size_t operator ()(arc::atom a) const {
			size_t r = 1;
			switch (a.type) {
			case arc::T_NIL:
				return 0;
			case arc::T_CONS:
				while (!no(a)) {
					r *= 31;
					if (a.type == arc::T_CONS) {
						r += hash<arc::atom>()(car(a));
						a = cdr(a);
					}
					else {
						r += hash<arc::atom>()(a);
						break;
					}
				}
				return r;
			case arc::T_SYM:
				return hash<char *>()(a.value.symbol);
			case arc::T_STRING: {
				return hash<string>()(a.as<string>());
			}
			case arc::T_NUM: {
				return hash<double>()(a.value.number);
			}
			case arc::T_CLOSURE:
				return hash<arc::atom>()(cdr(a));
			case arc::T_MACRO:
				return hash<arc::atom>()(cdr(a));
			case arc::T_BUILTIN:
			case arc::T_INPUT:
			case arc::T_OUTPUT:
				return hash<void *>()(a.p.get());
			default:
				return 0;
			}
		}
	};
}
#endif
