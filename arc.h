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
#include <filesystem>
#include <variant>

#ifdef READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#ifdef _MSC_VER
#define strdup _strdup
#define popen _popen
#define pclose _pclose
#endif

namespace arc {
	constexpr auto VERSION = "0.31";

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
		T_INPUT_PIPE,
		T_OUTPUT,
		T_TABLE,
		T_CHAR,
		T_CONTINUATION
	};

	typedef enum {
		ERROR_OK = 0, ERROR_SYNTAX, ERROR_UNBOUND, ERROR_ARGS, ERROR_TYPE, ERROR_FILE, ERROR_USER
	} error;

	typedef struct atom atom;
	typedef error(*builtin)(const std::vector<atom> &vargs, atom *result);
	typedef std::unordered_map<atom, atom> table;
	typedef std::string* sym;
	typedef std::unordered_map<sym, atom> env_table;
	
	struct atom {
		enum type type = T_NIL;
		std::variant<
			std::monostate,
			std::shared_ptr<struct cons>,
			sym,
			double,
			builtin,
			std::shared_ptr<struct closure>,
			std::shared_ptr<std::string>,
			FILE *,
			std::shared_ptr<table>,
			char,
			jmp_buf *> val;

		template <typename T>
		T& asp() const { return *std::get<std::shared_ptr<T>>(val); }

		friend bool operator ==(const atom &a, const atom &b);
	};

	struct cons {
		struct atom car, cdr;
		cons(atom car, atom cdr);
	};

	struct env {
		std::shared_ptr<struct env> parent;
		env_table table;
		env(std::shared_ptr<struct env> parent);
	};

	struct closure {
		std::shared_ptr<struct env> parent_env;
		atom args;
		atom body;
		closure(const std::shared_ptr<struct env> &env, atom args, atom body);
	};

	/* forward declarations */
	error apply(const atom &fn, const std::vector<atom> &args, atom *result);
	int listp(atom expr);
	char *slurp_fp(FILE *fp);
	char *slurp(const char *path);
	error eval_expr(atom expr, std::shared_ptr<struct env> env, atom *result);
	error macex(atom expr, atom *result);
	std::string to_string(atom a, int write);
	error macex_eval(atom expr, atom *result);
	error arc_load_file(const char *path);
	void arc_init();
#ifndef READLINE
	char *readline(const char *prompt);
#endif
	char *readline_fp(const char *prompt, FILE *fp);
	error read_expr(const char *input, const char **end, atom *result);
	void print_expr(const atom &a);
	void print_error(error e);
	bool is(const atom &a, const atom &b);
	bool iso(const atom& a, const atom& b);
	atom make_table();
	void repl();
	atom make_cons(const atom &car_val, const atom &cdr_val);
	atom & car(const atom & a);
	atom & cdr(const atom & a);
	bool no(const atom & a);
	bool sym_is(const atom & a, const atom & b);
	/* end forward */
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
				return hash<arc::sym>()(std::get<arc::sym>(a.val));
			case arc::T_STRING: {
				return hash<string>()(a.asp<string>());
			}
			case arc::T_NUM: {
				return hash<double>()(std::get<double>(a.val));
			}
			case arc::T_CLOSURE:
				return hash<arc::atom>()(cdr(a));
			case arc::T_MACRO:
				return hash<arc::atom>()(cdr(a));
			case arc::T_BUILTIN:
				return hash<arc::builtin>()(std::get<arc::builtin>(a.val));
			case arc::T_INPUT:
			case arc::T_INPUT_PIPE:
			case arc::T_OUTPUT:
				return hash<void *>()(std::get<FILE *>(a.val));
			default:
				return 0;
			}
		}
	};
}
#endif
