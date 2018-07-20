#include "arc.h"
#include <ctype.h>

namespace arc {
	const char *error_string[] = { "", "Syntax error", "Symbol not bound", "Wrong number of arguments", "Wrong type", "File error", "" };
	const atom nil;
	std::shared_ptr<struct env> global_env = std::make_shared<struct env>(nullptr); /* the global environment */
	/* symbols for faster execution */
	atom sym_t, sym_quote, sym_assign, sym_fn, sym_if, sym_mac, sym_apply, sym_cons, sym_sym, sym_string, sym_num, sym__, sym_o, sym_table, sym_int, sym_char;
	atom cur_expr;
	atom thrown;
	std::unordered_map<std::string, std::string *> id_of_sym;

	cons::cons(atom car, atom cdr) : car(car), cdr(cdr) {}
	env::env(std::shared_ptr<struct env> parent) : parent(parent) {}
	closure::closure(std::shared_ptr<struct env> env, atom args, atom body) : env(env), args(args), body(body) {}

	atom vector_to_atom(std::vector<atom> &a, int start) {
		atom r = nil;
		int i;
		for (i = a.size() - 1; i >= start; i--) {
			r = make_cons(a[i], r);
		}
		return r;
	}

	std::vector<atom> atom_to_vector(atom a) {
		std::vector<atom> r;
		for (; !no(a); a = cdr(a)) {
			r.push_back(car(a));
		}
		return r;
	}

	atom & car(atom & a) {
		return a.as<arc::cons>().car;
	}

	atom & cdr(atom & a) {
		return a.as<arc::cons>().cdr;
	}

	bool no(atom & a) {
		return a.type == arc::T_NIL;
	}

	bool sym_is(atom & a, atom & b) {
		return a.as<std::string *>() == b.as<std::string *>();
	}

	bool operator ==(const atom a, const atom b) {
		return iso(a, b);
	}

	atom make_cons(atom car_val, atom cdr_val)
	{
		atom a;
		a.type = T_CONS;
		a.p = std::make_shared<cons>(car_val, cdr_val);
		return a;
	}

	atom make_number(double x)
	{
		atom a;
		a.type = T_NUM;
		a.p = std::make_shared<double>(x);
		return a;
	}

	atom make_sym(std::string s)
	{
		atom a;
		a.type = T_SYM;

		auto found = id_of_sym.find(s);
		if (found != id_of_sym.end()) {
			a.p = std::make_shared<std::string *>(found->second);
			return a;
		}

		a.p = std::make_shared<std::string *>(new std::string(s));
		id_of_sym[s] = a.as<std::string *>();
		return a;
	}

	atom make_builtin(builtin fn)
	{
		atom a;
		a.type = T_BUILTIN;
		a.p = std::make_shared<builtin>(fn);
		return a;
	}

	error make_closure(std::shared_ptr<struct env> env, atom args, atom body, atom *result)
	{
		atom p;

		if (!listp(body))
			return ERROR_SYNTAX;

		/* Check argument names are all symbols or conses */
		p = args;
		while (!no(p)) {
			if (p.type == T_SYM)
				break;
			else if (p.type != T_CONS || (car(p).type != T_SYM && car(p).type != T_CONS))
				return ERROR_TYPE;
			p = cdr(p);
		}

		result->type = T_CLOSURE;
		result->p = std::make_shared<struct closure>(env, args, body);

		return ERROR_OK;
	}

	atom make_string(const std::string x)
	{
		atom a;
		a.type = T_STRING;
		a.p = std::make_shared<std::string>(x);
		return a;
	}

	atom make_input(FILE *fp) {
		atom a;
		a.type = T_INPUT;
		a.p = std::make_shared<FILE *>(fp);
		return a;
	}

	atom make_output(FILE *fp) {
		atom a;
		a.type = T_OUTPUT;
		a.p = std::make_shared<FILE *>(fp);
		return a;
	}

	atom make_char(char c) {
		atom a;
		a.type = T_CHAR;
		a.p = std::make_shared<char>(c);
		return a;
	}

	void print_expr(atom a)
	{
		std::cout << to_string(a, 1);
	}

	void pr(atom a)
	{
		std::cout << to_string(a, 0);
	}

	error lex(const char *str, const char **start, const char **end)
	{
		const char *ws = " \t\r\n";
		const char *delim = "()[] \t\r\n;";
		const char *prefix = "()[]'`";
	start:
		str += strspn(str, ws);

		if (str[0] == '\0') {
			*start = *end = NULL;
			return ERROR_FILE;
		}

		*start = str;

		if (strchr(prefix, str[0]) != NULL)
			*end = str + 1;
		else if (str[0] == ',')
			*end = str + (str[1] == '@' ? 2 : 1);
		else if (str[0] == '"') {
			str++;
			while (*str != 0) {
				if (*str == '\\') str++;
				else if (*str == '"') {
					break;
				}
				str++;
			}
			*end = str + 1;
		}
		else if (str[0] == ';') { /* end-of-line comment */
			str += strcspn(str, "\n");
			goto start;
		}
		else
			*end = str + strcspn(str, delim);

		return ERROR_OK;
	}

	error parse_simple(const char *start, const char *end, atom *result)
	{
		char *buf, *p;

		/* Is it a number? */
		double val = strtod(start, &p);
		if (p == end) {
			*result = make_number(val);
			return ERROR_OK;
		}
		else if (start[0] == '"') { /* "string" */
			result->type = T_STRING;
			size_t length = end - start - 2;
			char *buf = (char*)malloc(length + 1);
			const char *ps = start + 1;
			char *pt = buf;
			while (ps < end - 1) {
				if (*ps == '\\') {
					char c_next = *(ps + 1);
					switch (c_next) {
					case 'r':
						*pt = '\r';
						break;
					case 'n':
						*pt = '\n';
						break;
					case 't':
						*pt = '\t';
						break;
					default:
						*pt = c_next;
					}
					ps++;
				}
				else {
					*pt = *ps;
				}
				ps++;
				pt++;
			}
			*pt = 0;
			buf = (char *)realloc(buf, pt - buf + 1);
			*result = make_string(buf);
			free(buf);
			return ERROR_OK;
		}
		else if (start[0] == '#') { /* #\char */
			buf = (char *)malloc(end - start + 1);
			memcpy(buf, start, end - start);
			buf[end - start] = 0;
			size_t length = strlen(buf);
			if (length == 3 && buf[1] == '\\') { /* plain character e.g. #\a */
				*result = make_char(buf[2]);
				free(buf);
				return ERROR_OK;
			}
			else {
				char c;
				if (strcmp(buf, "#\\nul") == 0)
					c = '\0';
				else if (strcmp(buf, "#\\return") == 0)
					c = '\r';
				else if (strcmp(buf, "#\\newline") == 0)
					c = '\n';
				else if (strcmp(buf, "#\\tab") == 0)
					c = '\t';
				else if (strcmp(buf, "#\\space") == 0)
					c = ' ';
				else {
					free(buf);
					return ERROR_SYNTAX;
				}
				free(buf);
				*result = make_char(c);
				return ERROR_OK;
			}
		}

		/* NIL or symbol */
		buf = (char *)malloc(end - start + 1);
		memcpy(buf, start, end - start);
		buf[end - start] = 0;

		if (strcmp(buf, "nil") == 0)
			*result = nil;
		else if (strcmp(buf, ".") == 0)
			*result = make_sym(buf);
		else {
			atom a1, a2;
			long length = end - start, i;
			for (i = length - 1; i >= 0; i--) { /* left-associative */
				if (buf[i] == '.') { /* a.b => (a b) */
					if (i == 0 || i == length - 1) {
						free(buf);
						return ERROR_SYNTAX;
					}
					error err;
					err = parse_simple(buf, buf + i, &a1);
					if (err) return ERROR_SYNTAX;
					err = parse_simple(buf + i + 1, buf + length, &a2);
					if (err) return ERROR_SYNTAX;
					free(buf);
					*result = make_cons(a1, make_cons(a2, nil));
					return ERROR_OK;
				}
				else if (buf[i] == '!') { /* a!b => (a 'b) */
					if (i == 0 || i == length - 1) {
						free(buf);
						return ERROR_SYNTAX;
					}
					error err;
					err = parse_simple(buf, buf + i, &a1);
					if (err) return ERROR_SYNTAX;
					err = parse_simple(buf + i + 1, buf + length, &a2);
					if (err) return ERROR_SYNTAX;
					free(buf);
					*result = make_cons(a1, make_cons(make_cons(sym_quote, make_cons(a2, nil)), nil));
					return ERROR_OK;
				}
				else if (buf[i] == ':') { /* a:b => (compose a b) */
					if (i == 0 || i == length - 1) {
						free(buf);
						return ERROR_SYNTAX;
					}
					error err;
					err = parse_simple(buf, buf + i, &a1);
					if (err) return ERROR_SYNTAX;
					err = parse_simple(buf + i + 1, buf + length, &a2);
					if (err) return ERROR_SYNTAX;
					free(buf);
					*result = make_cons(make_sym("compose"), make_cons(a1, make_cons(a2, nil)));
					return ERROR_OK;
				}
			}
			if (length >= 2 && buf[0] == '~') { /* ~a => (complement a) */
				atom a1;
				error err = parse_simple(buf + 1, buf + length, &a1);
				if (err) return ERROR_SYNTAX;
				*result = make_cons(make_sym("complement"), make_cons(a1, nil));
				return ERROR_OK;
			}
			*result = make_sym(buf);
		}

		free(buf);

		return ERROR_OK;
	}

	error read_list(const char *start, const char **end, atom *result)
	{
		atom p;

		*end = start;
		p = *result = nil;

		for (;;) {
			const char *token;
			atom item;
			error err;

			err = lex(*end, &token, end);
			if (err)
				return err;

			if (token[0] == ')') {
				return ERROR_OK;
			}

			if (!no(p) && token[0] == '.' && *end - token == 1) {
				/* Improper list */
				if (no(p)) return ERROR_SYNTAX;

				err = read_expr(*end, end, &item);
				if (err) return err;

				cdr(p) = item;

				/* Read the closing ')' */
				err = lex(*end, &token, end);
				if (!err && token[0] != ')') {
					err = ERROR_SYNTAX;
				}
				return err;
			}

			err = read_expr(token, end, &item);
			if (err)
				return err;

			if (no(p)) {
				/* First item */
				*result = make_cons(item, nil);
				p = *result;
			}
			else {
				cdr(p) = make_cons(item, nil);
				p = cdr(p);
			}
		}
	}

	/* [...] => (fn (_) (...)) */
	error read_bracket(const char *start, const char **end, atom *result)
	{
		atom p;

		*end = start;
		p = *result = nil;

		/* First item */
		*result = make_cons(sym_fn, nil);
		p = *result;

		cdr(p) = make_cons(make_cons(sym__, nil), nil);
		p = cdr(p);

		atom body = nil;

		for (;;) {
			const char *token;
			atom item;
			error err;

			err = lex(*end, &token, end);
			if (err) return err;
			if (token[0] == ']') {
				return ERROR_OK;
			}

			err = read_expr(token, end, &item);
			if (err) return err;

			if (no(body)) {
				body = make_cons(item, nil);
				cdr(p) = make_cons(body, nil);
				p = body;
			}
			else {
				cdr(p) = make_cons(item, nil);
				p = cdr(p);
			}
		}
	}

	error read_expr(const char *input, const char **end, atom *result)
	{
		const char *token;
		error err;

		err = lex(input, &token, end);
		if (err)
			return err;

		if (token[0] == '(') {
			return read_list(*end, end, result);
		}
		else if (token[0] == ')')
			return ERROR_SYNTAX;
		else if (token[0] == '[') {
			return read_bracket(*end, end, result);
		}
		else if (token[0] == ']')
			return ERROR_SYNTAX;
		else if (token[0] == '\'') {
			*result = make_cons(make_sym("quote"), make_cons(nil, nil));
			return read_expr(*end, end, &car(cdr(*result)));
		}
		else if (token[0] == '`') {
			*result = make_cons(make_sym("quasiquote"), make_cons(nil, nil));
			return read_expr(*end, end, &car(cdr(*result)));
		}
		else if (token[0] == ',') {
			*result = make_cons(make_sym(
				token[1] == '@' ? "unquote-splicing" : "unquote"),
				make_cons(nil, nil));
			return read_expr(*end, end, &car(cdr(*result)));
		}
		else
			return parse_simple(token, *end, result);
	}

#ifndef READLINE
	char *readline(const char *prompt) {
		return readline_fp(prompt, stdin);
	}
#endif /* READLINE */

	char *readline_fp(const char *prompt, FILE *fp) {
		size_t size = 80;
		/* The size is extended by the input with the value of the provisional */
		char *str;
		int ch;
		size_t len = 0;
		printf("%s", prompt);
		str = (char *) malloc(sizeof(char)* size); /* size is start size */
		if (!str) return NULL;
		while (EOF != (ch = fgetc(fp)) && ch != '\n') {
			str[len++] = ch;
			if (len == size) {
				char *p = (char *) realloc(str, sizeof(char)*(size *= 2));
				if (!p) {
					free(str);
					return NULL;
				}
				str = p;
			}
		}
		if (ch == EOF && len == 0) {
			free(str);
			return NULL;
		}
		str[len++] = '\0';

		return (char *) realloc(str, sizeof(char)*len);
	}

	error env_get(std::shared_ptr<struct env> &env, std::string *symbol, atom *result)
	{
		while (1) {
			auto &tbl = env->table;
			auto found = tbl.find(symbol);
			if (found != tbl.end()) {
				*result = found->second;
				return ERROR_OK;
			}
			auto &parent = env->parent;
			if (parent == nullptr) {
				/*printf("%s: ", symbol.p.symbol);*/
				return ERROR_UNBOUND;
			}
			env = parent;
		}
	}

	error env_assign(std::shared_ptr<struct env> &env, std::string *symbol, atom value) {
		auto &tbl = env->table;
		tbl[symbol] = value;
		return ERROR_OK;
	}

	error env_assign_eq(std::shared_ptr<struct env> &env, std::string *symbol, atom value) {
		while (1) {
			auto &tbl = env->table;
			auto found = tbl.find(symbol);
			if (found != tbl.end()) {
				found->second = value;
				return ERROR_OK;
			}
			auto &parent = env->parent;
			if (parent == nullptr) {
				return env_assign(env, symbol, value);
			}
			env = parent;
		}
	}

	int listp(atom expr)
	{
		atom p = expr;
		while (!no(p)) {
			if (p.type != T_CONS)
				return 0;
			p = cdr(p);
		}
		return 1;
	}

	size_t len(atom xs) {
		atom p = xs;
		size_t ret = 0;
		while (!no(p)) {
			if (p.type != T_CONS)
				return 0;
			p = cdr(p);
			ret++;
		}
		return ret;
	}

	atom copy_list(atom list)
	{
		atom a, p;

		if (no(list))
			return nil;

		a = make_cons(car(list), nil);
		p = a;
		list = cdr(list);

		while (!no(list)) {
			cdr(p) = make_cons(car(list), nil);
			p = cdr(p);
			list = cdr(list);
			if (list.type != T_CONS) { /* improper list */
				p = list;
				break;
			}
		}

		return a;
	}

	error destructuring_bind(atom arg_name, atom val, int val_unspecified, std::shared_ptr<struct env> &env) {
		if (no(arg_name)) {
			if (no(val))
				return ERROR_OK;
			else {
				return ERROR_ARGS;
			}
		}
		else if (arg_name.type == T_SYM) {
			return env_assign(env, arg_name.as<std::string *>(), val);
		}
		else if (arg_name.type == T_CONS) {
			if (is(car(arg_name), sym_o)) { /* (o ARG [DEFAULT]) */
				if (val_unspecified) { /* missing argument */
					if (!no(cdr(cdr(arg_name)))) {
						error err = eval_expr(car(cdr(cdr(arg_name))), env, &val);
						if (err) {
							return err;
						}
					}
				}
				return env_assign(env, car(cdr(arg_name)).as<std::string *>(), val);
			}
			else {
				if (val.type != T_CONS) {
					return ERROR_ARGS;
				}
				error err = destructuring_bind(car(arg_name), car(val), 0, env);
				if (err) {
					return err;
				}
				return destructuring_bind(cdr(arg_name), cdr(val), no(cdr(val)), env);
			}
		}
		else {
			return ERROR_ARGS;
		}
	}

	error env_bind(std::shared_ptr<struct env> &env, atom arg_names, std::vector<atom> &vargs) {
		size_t i = 0;
		while (!no(arg_names)) {
			if (arg_names.type == T_SYM) {
				env_assign(env, arg_names.as<std::string *>(), vector_to_atom(vargs, i));
				i = vargs.size();
				break;
			}
			atom arg_name = car(arg_names);
			atom val;
			int val_unspecified = 0;
			if (i < vargs.size()) {
				val = vargs[i];
			}
			else {
				val_unspecified = 1;
			}
			error err = destructuring_bind(arg_name, val, val_unspecified, env);
			if (err) {
				return err;
			}
			arg_names = cdr(arg_names);
			i++;
		}
		if (i < vargs.size())
			return ERROR_ARGS;
		return ERROR_OK;
	}

	error apply(atom fn, std::vector<atom> &vargs, atom *result)
	{
		if (fn.type == T_BUILTIN)
			return fn.as<builtin>()(vargs, result);
		else if (fn.type == T_CLOSURE) {
			struct closure cls = fn.as<struct closure>();
			std::shared_ptr<struct env> env = std::make_shared<struct env>(cls.env);
			atom arg_names = cls.args;
			atom body = cls.body;

			/* Bind the arguments */
			env_bind(env, arg_names, vargs);

			/* Evaluate the body */
			*result = nil;
			while (!no(body)) {
				error err = eval_expr(car(body), env, result);
				if (err)
					return err;
				body = cdr(body);
			}

			return ERROR_OK;
		}
		else if (fn.type == T_CONTINUATION) {
			if (vargs.size() != 1) return ERROR_ARGS;
			thrown = vargs[0];
			longjmp(*fn.as<jmp_buf *>(), 1);
		}
		else if (fn.type == T_STRING) { /* implicit indexing for string */
			if (vargs.size() != 1) return ERROR_ARGS;
			long index = (long)(vargs[0]).as<double>();
			*result = make_char(fn.as<std::string>()[index]);
			return ERROR_OK;
		}
		else if (fn.type == T_CONS && listp(fn)) { /* implicit indexing for list */
			if (vargs.size() != 1) return ERROR_ARGS;
			long index = (long)(vargs[0]).as<double>();
			atom a = fn;
			long i;
			for (i = 0; i < index; i++) {
				a = cdr(a);
				if (no(a)) {
					*result = nil;
					return ERROR_OK;
				}
			}
			*result = car(a);
			return ERROR_OK;
		}
		else if (fn.type == T_TABLE) { /* implicit indexing for table */
			long len1 = vargs.size();
			if (len1 != 1 && len1 != 2) return ERROR_ARGS;
			atom key = vargs[0];
			auto &tbl = fn.as<table>();
			auto found = tbl.find(key);
			if (found != tbl.end()) {
				*result = found->second;
			}
			else {
				if (len1 == 2) /* default value is specified */
					*result = vargs[1];
				else
					*result = nil;
			}
			return ERROR_OK;
		}
		else {
			return ERROR_TYPE;
		}
	}

	/* start builtin */
	error builtin_car(std::vector<atom> &vargs, atom *result)
	{
		if (vargs.size() != 1)
			return ERROR_ARGS;

		atom a = vargs[0];
		if (no(a))
			*result = nil;
		else if (a.type != T_CONS)
			return ERROR_TYPE;
		else
			*result = car(a);

		return ERROR_OK;
	}

	error builtin_cdr(std::vector<atom> &vargs, atom *result)
	{
		if (vargs.size() != 1)
			return ERROR_ARGS;

		atom a = vargs[0];
		if (no(a))
			*result = nil;
		else if (a.type != T_CONS)
			return ERROR_TYPE;
		else
			*result = cdr(a);

		return ERROR_OK;
	}

	error builtin_cons(std::vector<atom> &vargs, atom *result)
	{
		if (vargs.size() != 2)
			return ERROR_ARGS;

		*result = make_cons(vargs[0], vargs[1]);

		return ERROR_OK;
	}

	/* appends two lists */
	atom append(atom a, atom b) {
		atom a1 = copy_list(a),
			b1 = copy_list(b);
		atom p = a1;
		if (no(p)) return b1;
		while (1) {
			if (no(cdr(p))) {
				cdr(p) = b1;
				return a1;
			}
			p = cdr(p);
		}
		return nil;
	}

	/*
+ args
Addition. This operator also performs string and list concatenation.
	*/
	error builtin_add(std::vector<atom> &vargs, atom *result)
	{
		if (vargs.size() == 0) {
			*result = make_number(0);
		}
		else {
			if (vargs[0].type == T_NUM) {
				double r = vargs[0].as<double>();
				size_t i;
				for (i = 1; i < vargs.size(); i++) {
					if (vargs[i].type != T_NUM) return ERROR_TYPE;
					r += vargs[i].as<double>();
				}
				*result = make_number(r);
			}
			else if (vargs[0].type == T_STRING) {
				std::string buf;
				size_t i;
				for (i = 0; i < vargs.size(); i++) {
					std::string s = to_string(vargs[i], 0);
					buf += s;
				}
				*result = make_string(buf);
			}
			else if (vargs[0].type == T_CONS || vargs[0].type == T_NIL) {
				atom acc = nil;
				size_t i;
				for (i = 0; i < vargs.size(); i++) {
					acc = append(acc, vargs[i]);
				}
				*result = acc;
			}
		}
		return ERROR_OK;
	}

	error builtin_subtract(std::vector<atom> &vargs, atom *result)
	{
		if (vargs.size() == 0) { /* 0 argument */
			*result = make_number(0);
			return ERROR_OK;
		}
		if (vargs[0].type != T_NUM) return ERROR_TYPE;
		if (vargs.size() == 1) { /* 1 argument */
			*result = make_number(-vargs[0].as<double>());
			return ERROR_OK;
		}
		double r = vargs[0].as<double>();
		size_t i;
		for (i = 1; i < vargs.size(); i++) {
			if (vargs[i].type != T_NUM) return ERROR_TYPE;
			r -= vargs[i].as<double>();
		}
		*result = make_number(r);
		return ERROR_OK;
	}

	error builtin_multiply(std::vector<atom> &vargs, atom *result)
	{
		double r = 1;
		size_t i;
		for (i = 0; i < vargs.size(); i++) {
			if (vargs[i].type != T_NUM) return ERROR_TYPE;
			r *= vargs[i].as<double>();
		}
		*result = make_number(r);
		return ERROR_OK;
	}

	error builtin_divide(std::vector<atom> &vargs, atom *result)
	{
		if (vargs.size() == 0) { /* 0 argument */
			*result = make_number(1);
			return ERROR_OK;
		}
		if (vargs[0].type != T_NUM) return ERROR_TYPE;
		if (vargs.size() == 1) { /* 1 argument */
			*result = make_number(1.0 / vargs[0].as<double>());
			return ERROR_OK;
		}
		double r = vargs[0].as<double>();
		size_t i;
		for (i = 1; i < vargs.size(); i++) {
			if (vargs[i].type != T_NUM) return ERROR_TYPE;
			r /= vargs[i].as<double>();
		}
		*result = make_number(r);
		return ERROR_OK;
	}

	error builtin_less(std::vector<atom> &vargs, atom *result)
	{
		if (vargs.size() <= 1) {
			*result = sym_t;
			return ERROR_OK;
		}
		size_t i;
		switch (vargs[0].type) {
		case T_NUM:
			for (i = 0; i < vargs.size() - 1; i++) {
				if (vargs[i].as<double>() >= vargs[i + 1].as<double>()) {
					*result = nil;
					return ERROR_OK;
				}
			}
			*result = sym_t;
			return ERROR_OK;
		case T_STRING:
			for (i = 0; i < vargs.size() - 1; i++) {
				if (vargs[i].as<std::string>() >= vargs[i + 1].as<std::string>()) {
					*result = nil;
					return ERROR_OK;
				}
			}
			*result = sym_t;
			return ERROR_OK;
		default:
			return ERROR_TYPE;
		}
	}

	error builtin_greater(std::vector<atom> &vargs, atom *result)
	{
		if (vargs.size() <= 1) {
			*result = sym_t;
			return ERROR_OK;
		}
		size_t i;
		switch (vargs[0].type) {
		case T_NUM:
			for (i = 0; i < vargs.size() - 1; i++) {
				if (vargs[i].as<double>() <= vargs[i + 1].as<double>()) {
					*result = nil;
					return ERROR_OK;
				}
			}
			*result = sym_t;
			return ERROR_OK;
		case T_STRING:
			for (i = 0; i < vargs.size() - 1; i++) {
				if (vargs[i].as<std::string>() <= vargs[i + 1].as<std::string>()) {
					*result = nil;
					return ERROR_OK;
				}
			}
			*result = sym_t;
			return ERROR_OK;
		default:
			return ERROR_TYPE;
		}
	}

	error builtin_apply(std::vector<atom> &vargs, atom *result)
	{
		atom fn;

		if (vargs.size() != 2)
			return ERROR_ARGS;

		fn = vargs[0];
		auto v = atom_to_vector(vargs[1]);
		return apply(fn, v, result);
	}

	bool is(atom a, atom b) {
		if (a.type == b.type) {
			switch (a.type) {
			case T_NIL:
				return true;
			case T_CONS:
			case T_CLOSURE:
			case T_MACRO:
				return a.p == b.p; // compare pointers
			case T_BUILTIN:
				return a.as<builtin>() == b.as<builtin>();
			case T_SYM:
				return (a.as<std::string *>()) == (b.as<std::string *>());
			case T_NUM:
				return (a.as<double>()) == (b.as<double>());
			case T_STRING:
				return a.as<std::string>() == b.as<std::string>();
			case T_CHAR:
				return a.as<char>() == b.as<char>();
			case T_TABLE:
				return a.as<table>() == b.as<table>();
			case T_INPUT:
			case T_OUTPUT:
				return a.as<FILE *>() == b.as<FILE *>();
			case T_CONTINUATION:
				return a.as<jmp_buf *>() == b.as<jmp_buf *>();
			}
		}
		return false;
	}

	bool iso(atom a, atom b) {
		if (a.type == b.type) {
			switch (a.type) {
			case T_CONS:
				return iso(a.as<cons>().car, b.as<cons>().car) && iso(a.as<cons>().cdr, b.as<cons>().cdr);
			default:
				return is(a, b);
			}
		}
		return 0;
	}
	
	error builtin_is(std::vector<atom> &vargs, atom *result)
	{
		atom a, b;
		if (vargs.size() <= 1) {
			*result = sym_t;
			return ERROR_OK;
		}
		size_t i;
		for (i = 0; i < vargs.size() - 1; i++) {
			a = vargs[i];
			b = vargs[i + 1];
			if (!is(a, b)) {
				*result = nil;
				return ERROR_OK;
			}
		}
		*result = sym_t;
		return ERROR_OK;
	}

	error builtin_scar(std::vector<atom> &vargs, atom *result) {
		if (vargs.size() != 2) return ERROR_ARGS;
		atom place = vargs[0], value;
		if (place.type != T_CONS) return ERROR_TYPE;
		value = vargs[1];
		place.as<cons>().car = value;
		*result = value;
		return ERROR_OK;
	}

	error builtin_scdr(std::vector<atom> &vargs, atom *result) {
		if (vargs.size() != 2) return ERROR_ARGS;
		atom place = vargs[0], value;
		if (place.type != T_CONS) return ERROR_TYPE;
		value = vargs[1];
		place.as<cons>().cdr = value;
		*result = value;
		return ERROR_OK;
	}

	error builtin_mod(std::vector<atom> &vargs, atom *result) {
		if (vargs.size() != 2) return ERROR_ARGS;
		atom dividend = vargs[0];
		atom divisor = vargs[1];
		double r = fmod(dividend.as<double>(), divisor.as<double>());
		if (dividend.as<double>() * divisor.as<double>() < 0 && r != 0) r += divisor.as<double>();
		*result = make_number(r);
		return ERROR_OK;
	}

	error builtin_type(std::vector<atom> &vargs, atom *result) {
		if (vargs.size() != 1) return ERROR_ARGS;
		atom x = vargs[0];
		switch (x.type) {
		case T_CONS: *result = sym_cons; break;
		case T_SYM:
		case T_NIL: *result = sym_sym; break;
		case T_BUILTIN:
		case T_CLOSURE:
		case T_CONTINUATION:
			*result = sym_fn; break;
		case T_STRING: *result = sym_string; break;
		case T_NUM: *result = sym_num; break;
		case T_MACRO: *result = sym_mac; break;
		case T_TABLE: *result = sym_table; break;
		case T_CHAR: *result = sym_char; break;
		default: *result = nil; break; /* impossible */
		}
		return ERROR_OK;
	}

	/* string-sref obj value index */
	error builtin_string_sref(std::vector<atom> &vargs, atom *result) {
		atom index, obj, value;
		if (vargs.size() != 3) return ERROR_ARGS;
		obj = vargs[0];
		if (obj.type != T_STRING) return ERROR_TYPE;
		value = vargs[1];
		index = vargs[2];
		obj.as<std::string>()[(long)index.as<double>()] = value.as<char>();
		*result = make_char(value.as<char>());
		return ERROR_OK;
	}

	/* disp [arg [output-port]] */
	error builtin_disp(std::vector<atom> &vargs, atom *result) {
		long l = vargs.size();
		FILE *fp;
		switch (l) {
		case 0:
			*result = nil;
			return ERROR_OK;
		case 1:
			fp = stdout;
			break;
		case 2:
			fp = vargs[1].as<FILE *>();
			break;
		default:
			return ERROR_ARGS;
		}
		fprintf(fp, "%s", to_string(vargs[0], 0).c_str());
		*result = nil;
		return ERROR_OK;
	}

	error builtin_writeb(std::vector<atom> &vargs, atom *result) {
		long l = vargs.size();
		FILE *fp;
		switch (l) {
		case 0: return ERROR_ARGS;
		case 1:
			fp = stdout;
			break;
		case 2:
			fp = vargs[1].as<FILE *>();
			break;
		default: return ERROR_ARGS;
		}
		fputc((int)vargs[0].as<double>(), fp);
		*result = nil;
		return ERROR_OK;
	}

	error builtin_expt(std::vector<atom> &vargs, atom *result) {
		atom a, b;
		if (vargs.size() != 2) return ERROR_ARGS;
		a = vargs[0];
		b = vargs[1];
		*result = make_number(pow(a.as<double>(), b.as<double>()));
		return ERROR_OK;
	}

	error builtin_log(std::vector<atom> &vargs, atom *result) {
		atom a;
		if (vargs.size() != 1) return ERROR_ARGS;
		a = vargs[0];
		*result = make_number(log(a.as<double>()));
		return ERROR_OK;
	}

	error builtin_sqrt(std::vector<atom> &vargs, atom *result) {
		atom a;
		if (vargs.size() != 1) return ERROR_ARGS;
		a = vargs[0];
		*result = make_number(sqrt(a.as<double>()));
		return ERROR_OK;
	}

	error builtin_readline(std::vector<atom> &vargs, atom *result) {
		long l = vargs.size();
		char *str;
		if (l == 0) {
			str = readline("");
		}
		else if (l == 1) {
			str = readline_fp("", vargs[0].as<FILE *>());
		}
		else {
			return ERROR_ARGS;
		}
		if (str == NULL) *result = nil; else *result = make_string(str);
		return ERROR_OK;
	}

	error builtin_quit(std::vector<atom> &vargs, atom *result) {
		if (vargs.size() != 0) return ERROR_ARGS;
		exit(0);
	}

	double rand_double() {
		return (double)rand() / ((double)RAND_MAX + 1.0);
	}

	error builtin_rand(std::vector<atom> &vargs, atom *result) {
		long alen = vargs.size();
		if (alen == 0) *result = make_number(rand_double());
		else if (alen == 1) *result = make_number(floor(rand_double() * vargs[0].as<double>()));
		else return ERROR_ARGS;
		return ERROR_OK;
	}

	error read_fp(FILE *fp, atom *result) {
		char *s = readline_fp("", fp);
		if (s == NULL) return ERROR_FILE;
		const char *buf = s;
		error err = read_expr(buf, &buf, result);

		/* bring back remaining expressions so that "(read) (read)" works */
		if (buf) {
			if (*buf) ungetc('\n', fp);
			const char *b0 = buf;
			for (; *buf; buf++) {
			}
			for (buf--; buf >= b0; buf--) {
				ungetc(*buf, fp);
			}
		}
		free(s);
		return err;
	}

	/* read [input-source [eof]]
	   Reads a S-expression from the input-source, which can be either a string or an input-port. If the end of file is reached, nil is returned or the specified eof value. */
	error builtin_read(std::vector<atom> &vargs, atom *result) {
		size_t alen = vargs.size();
		error err;
		if (alen == 0) {
			err = read_fp(stdin, result);
		}
		else if (alen <= 2) {
			atom src = vargs[0];
			if (src.type == T_STRING) {
				const char *s = src.as<std::string>().c_str();
				const char *buf = s;
				err = read_expr(buf, &buf, result);
			}
			else if (src.type == T_INPUT) {
				err = read_fp(src.as<FILE *>(), result);
			}
			else {
				return ERROR_TYPE;
			}
		}
		else {
			return ERROR_ARGS;
		}

		if (err == ERROR_FILE) {
			atom eof = nil; /* default value when EOF */
			if (alen == 2) { /* specified return value when EOF */
				eof = vargs[1];
			}

			*result = eof;
			return ERROR_OK;
		}
		else {
			return err;
		}
	}

	error builtin_macex(std::vector<atom> &vargs, atom *result) {
		long alen = vargs.size();
		if (alen == 1) {
			error err = macex(vargs[0], result);
			return err;
		}
		else return ERROR_ARGS;
		return ERROR_OK;
	}

	error builtin_string(std::vector<atom> &vargs, atom *result) {
		std::string s;
		size_t i;
		for (i = 0; i < vargs.size(); i++) {
			s += to_string(vargs[i], 0);
		}
		*result = make_string(s);
		return ERROR_OK;
	}

	error builtin_sym(std::vector<atom> &vargs, atom *result) {
		long alen = vargs.size();
		if (alen == 1) {
			*result = make_sym(to_string(vargs[0], 0));
			return ERROR_OK;
		}
		else return ERROR_ARGS;
	}

	error builtin_system(std::vector<atom> &vargs, atom *result) {
		long alen = vargs.size();
		if (alen == 1) {
			atom a = vargs[0];
			if (a.type != T_STRING) return ERROR_TYPE;
			*result = make_number(system(vargs[0].as<std::string>().c_str()));
			return ERROR_OK;
		}
		else return ERROR_ARGS;
	}

	error builtin_eval(std::vector<atom> &vargs, atom *result) {
		if (vargs.size() == 1) return macex_eval(vargs[0], result);
		else return ERROR_ARGS;
	}

	error builtin_load(std::vector<atom> &vargs, atom *result) {
		if (vargs.size() == 1) {
			atom a = vargs[0];
			if (a.type != T_STRING) return ERROR_TYPE;
			*result = nil;
			return arc_load_file(a.as<std::string>().c_str());
		}
		else return ERROR_ARGS;
	}

	error builtin_int(std::vector<atom> &vargs, atom *result) {
		if (vargs.size() == 1) {
			atom a = vargs[0];
			switch (a.type) {
			case T_STRING:
				*result = make_number(round(atof(a.as<std::string>().c_str())));
				break;
			case T_SYM:
				*result = make_number(round(atof(a.as<std::string *>()->c_str())));
				break;
			case T_NUM:
				*result = make_number(round(a.as<double>()));
				break;
			case T_CHAR:
				*result = make_number(a.as<char>());
				break;
			default:
				return ERROR_TYPE;
			}
			return ERROR_OK;
		}
		else return ERROR_ARGS;
	}

	error builtin_trunc(std::vector<atom> &vargs, atom *result) {
		if (vargs.size() == 1) {
			atom a = vargs[0];
			if (a.type != T_NUM) return ERROR_TYPE;
			*result = make_number(trunc(a.as<double>()));
			return ERROR_OK;
		}
		else return ERROR_ARGS;
	}

	error builtin_sin(std::vector<atom> &vargs, atom *result) {
		if (vargs.size() == 1) {
			atom a = vargs[0];
			if (a.type != T_NUM) return ERROR_TYPE;
			*result = make_number(sin(a.as<double>()));
			return ERROR_OK;
		}
		else return ERROR_ARGS;
	}

	error builtin_cos(std::vector<atom> &vargs, atom *result) {
		if (vargs.size() == 1) {
			atom a = vargs[0];
			if (a.type != T_NUM) return ERROR_TYPE;
			*result = make_number(cos(a.as<double>()));
			return ERROR_OK;
		}
		else return ERROR_ARGS;
	}

	error builtin_tan(std::vector<atom> &vargs, atom *result) {
		if (vargs.size() == 1) {
			atom a = vargs[0];
			if (a.type != T_NUM) return ERROR_TYPE;
			*result = make_number(tan(a.as<double>()));
			return ERROR_OK;
		}
		else return ERROR_ARGS;
	}

	error builtin_bound(std::vector<atom> &vargs, atom *result) {
		if (vargs.size() == 1) {
			atom a = vargs[0];
			if (a.type != T_SYM) return ERROR_TYPE;
			error err = env_get(global_env, a.as<std::string *>(), result);
			*result = (err ? nil : sym_t);
			return ERROR_OK;
		}
		else return ERROR_ARGS;
	}

	error builtin_infile(std::vector<atom> &vargs, atom *result) {
		if (vargs.size() == 1) {
			atom a = vargs[0];
			if (a.type != T_STRING) return ERROR_TYPE;
			FILE *fp = fopen(a.as<std::string>().c_str(), "r");
			*result = make_input(fp);
			return ERROR_OK;
		}
		else return ERROR_ARGS;
	}

	error builtin_outfile(std::vector<atom> &vargs, atom *result) {
		if (vargs.size() == 1) {
			atom a = vargs[0];
			if (a.type != T_STRING) return ERROR_TYPE;
			FILE *fp = fopen(a.as<std::string>().c_str(), "w");
			*result = make_output(fp);
			return ERROR_OK;
		}
		else return ERROR_ARGS;
	}

	error builtin_close(std::vector<atom> &vargs, atom *result) {
		if (vargs.size() == 1) {
			atom a = vargs[0];
			if (a.type != T_INPUT && a.type != T_OUTPUT) return ERROR_TYPE;
			fclose(a.as<FILE *>());
			*result = nil;
			return ERROR_OK;
		}
		else return ERROR_ARGS;
	}

	error builtin_readb(std::vector<atom> &vargs, atom *result) {
		long l = vargs.size();
		FILE *fp;
		switch (l) {
		case 0:
			fp = stdin;
			break;
		case 1:
			fp = vargs[0].as<FILE *>();
			break;
		default:
			return ERROR_ARGS;
		}
		*result = make_number(fgetc(fp));
		return ERROR_OK;
	}

	/* sread input-port eof */
	error builtin_sread(std::vector<atom> &vargs, atom *result) {
		if (vargs.size() != 2) return ERROR_ARGS;
		FILE *fp = vargs[0].as<FILE *>();
		atom eof = vargs[1];
		error err;
		if (feof(fp)) {
			*result = eof;
			return ERROR_OK;
		}
		char *s = slurp_fp(fp);
		const char *p = s;
		err = read_expr(p, &p, result);
		return err;
	}

	/* write [arg [output-port]] */
	error builtin_write(std::vector<atom> &vargs, atom *result) {
		long l = vargs.size();
		FILE *fp;
		switch (l) {
		case 0:
			*result = nil;
			return ERROR_OK;
		case 1:
			fp = stdout;
			break;
		case 2:
			fp = vargs[1].as<FILE *>();
			break;
		default:
			return ERROR_ARGS;
		}
		atom a = vargs[0];
		if (a.type == T_STRING) fputc('"', fp);
		std::string s = to_string(a, 1);
		fprintf(fp, "%s", s.c_str());
		if (a.type == T_STRING) fputc('"', fp);
		*result = nil;
		return ERROR_OK;
	}

	/* newstring length [char] */
	error builtin_newstring(std::vector<atom> &vargs, atom *result) {
		long arg_len = vargs.size();
		long length = (long)vargs[0].as<double>();
		char c = 0;
		char *s;
		switch (arg_len) {
		case 1: break;
		case 2:
			c = vargs[1].as<char>();
			break;
		default:
			return ERROR_ARGS;
		}
		s = (char *)malloc((length + 1) * sizeof(char));
		int i;
		for (i = 0; i < length; i++)
			s[i] = c;
		s[length] = 0; /* end of string */
		*result = make_string(s);
		return ERROR_OK;
	}

	error builtin_table(std::vector<atom> &vargs, atom *result) {
		long arg_len = vargs.size();
		if (arg_len != 0) return ERROR_ARGS;
		*result = make_table();
		return ERROR_OK;
	}

	/* maptable proc table */
	error builtin_maptable(std::vector<atom> &vargs, atom *result) {
		long arg_len = vargs.size();
		if (arg_len != 2) return ERROR_ARGS;
		atom &proc = vargs[0];
		atom &tbl = vargs[1];
		if (tbl.type != T_TABLE) return ERROR_TYPE;
		auto &table1 = tbl.as<table>();
		std::vector<atom> v;
		for (auto &p : table1) {
			v.clear();
			v.push_back(p.first);
			v.push_back(p.second);
			error err = apply(proc, v, result);
			if (err) return err;
		}
		*result = tbl;
		return ERROR_OK;
	}

	/* table-sref obj value index */
	error builtin_table_sref(std::vector<atom> &vargs, atom *result) {
		atom index, obj, value;
		if (vargs.size() != 3) return ERROR_ARGS;
		obj = vargs[0];
		if (obj.type != T_TABLE) return ERROR_TYPE;
		value = vargs[1];
		index = vargs[2];
		obj.as<table>()[index] = value;
		*result = value;
		return ERROR_OK;
	}

	/* coerce obj type */
	/*
Coerces object to a new type.
A char can be coerced to int, num, string, or sym.
A number can be coerced to int, char, or string.
A string can be coerced to sym, cons (char list), num, or int.
A list of characters can be coerced to a string.
A symbol can be coerced to a string.
	*/
	error builtin_coerce(std::vector<atom> &vargs, atom *result) {
		atom obj, type;
		if (vargs.size() != 2) return ERROR_ARGS;
		obj = vargs[0];
		type = vargs[1];
		switch (obj.type) {
		case T_CHAR:
			if (is(type, sym_int) || is(type, sym_num)) *result = make_number(obj.as<char>());
			else if (is(type, sym_string)) {
				char *buf = (char *) malloc(2);
				buf[0] = obj.as<char>();
				buf[1] = '\0';
				*result = make_string(buf);
			}
			else if (is(type, sym_sym)) {
				char buf[2];
				buf[0] = obj.as<char>();
				buf[1] = '\0';
				*result = make_sym(buf);
			}
			else if (is(type, sym_char))
				*result = obj;
			else
				return ERROR_TYPE;
			break;
		case T_NUM:
			if (is(type, sym_int)) *result = make_number(floor(obj.as<double>()));
			else if (is(type, sym_char)) *result = make_char((char)obj.as<double>());
			else if (is(type, sym_string)) {
				*result = make_string(to_string(obj, 0));
			}
			else if (is(type, sym_num))
				*result = obj;
			else
				return ERROR_TYPE;
			break;
		case T_STRING:
			if (is(type, sym_sym)) *result = make_sym(obj.as<std::string>().c_str());
			else if (is(type, sym_cons)) {
				*result = nil;
				int i;
				for (i = strlen(obj.as<std::string>().c_str()) - 1; i >= 0; i--) {
					*result = make_cons(make_char(obj.as<std::string>().c_str()[i]), *result);
				}
			}
			else if (is(type, sym_num)) *result = make_number(atof(obj.as<std::string>().c_str()));
			else if (is(type, sym_int)) *result = make_number(atoi(obj.as<std::string>().c_str()));
			else if (is(type, sym_string))
				*result = obj;
			else
				return ERROR_TYPE;
			break;
		case T_CONS:
			if (is(type, sym_string)) {
				std::string s;
				atom p;
				for (p = obj; !no(p); p = cdr(p)) {
					s += car(p).as<char>();
				}
				*result = make_string(s);
			}
			else if (is(type, sym_cons))
				*result = obj;
			else
				return ERROR_TYPE;
			break;
		case T_SYM:
			if (is(type, sym_string)) {
				*result = make_string(*obj.as<std::string *>());
			}
			else if (is(type, sym_sym))
				*result = obj;
			else
				return ERROR_TYPE;
			break;
		default:
			*result = obj;
		}
		return ERROR_OK;
	}

	error builtin_flushout(std::vector<atom> &vargs, atom *result) {
		if (vargs.size() != 0) return ERROR_ARGS;
		fflush(stdout);
		*result = sym_t;
		return ERROR_OK;
	}

	error builtin_err(std::vector<atom> &vargs, atom *result) {
		if (vargs.size() == 0) return ERROR_ARGS;
		cur_expr = nil;
		size_t i;
		for (i = 0; i < vargs.size(); i++) {
			std::cout << to_string(vargs[i], 0) << '\n';
		}
		return ERROR_USER;
	}

	error builtin_len(std::vector<atom> &vargs, atom *result) {
		if (vargs.size() != 1) return ERROR_ARGS;
		atom a = vargs[0];
		if (a.type == T_CONS) {
			*result = make_number(len(a));
		}
		else if (a.type == T_STRING) {
			*result = make_number(strlen(a.as<std::string>().c_str()));
		}
		else if (a.type == T_TABLE) {
			*result = make_number(a.as<table>().size());
		}
		else {
			*result = make_number(0);
		}
		return ERROR_OK;
	}

	atom make_continuation(jmp_buf *jb) {
		atom a;
		a.type = T_CONTINUATION;
		a.as<jmp_buf *>() = jb;
		return a;
	}

	error builtin_ccc(std::vector<atom> &vargs, atom *result) {
		if (vargs.size() != 1) return ERROR_ARGS;
		atom a = vargs[0];
		if (a.type != T_BUILTIN && a.type != T_CLOSURE) return ERROR_TYPE;
		jmp_buf jb;
		int val = setjmp(jb);
		if (val) {
			*result = thrown;
			return ERROR_OK;
		}
		return apply(a, vargs, result);
	}
	
	/* end builtin */

	std::string to_string(atom a, int write) {
		std::string s;
		switch (a.type) {
		case T_NIL:
			s = "nil";
			break;
		case T_CONS:
			s = "(" + to_string(car(a), write);
			a = cdr(a);
			while (!no(a)) {
				if (a.type == T_CONS) {
					s += " " + to_string(car(a), write);
					a = cdr(a);
				}
				else {
					s += " . " + to_string(a, write);
					break;
				}
			}
			s += ")";
			break;
		case T_SYM:
			s = *a.as<std::string *>();
			break;
		case T_STRING:
			if (write) s += "\"";
			s += a.as<std::string>();
			if (write) s += "\"";
			break;
		case T_NUM:
		{
			std::stringstream ss;
			ss << std::setprecision(16) << a.as<double>();
			s = ss.str();
			break;
		}
		case T_BUILTIN:
		{
			std::stringstream ss;
			ss << "#<builtin:" << (void *)a.as<builtin>() << ">";
			s = ss.str();
			break;
		}
		case T_CLOSURE:
		{
			s = "#<closure>";
			break;
		}
		case T_MACRO:
			s = "#<macro:" + to_string(a.as<struct closure>().args, write) +
				" " + to_string(a.as<struct closure>().body, write) + ">";
			break;
		case T_INPUT:
			s = "#<input>";
			break;
		case T_OUTPUT:
			s = "#<output>";
			break;
		case T_TABLE: {
			s += "#<table:(";
			for (auto &p : a.as<table>()) {
				s += "(" + to_string(p.first, write) + " . " + to_string(p.second, write) + ")";
			}
			s += ")>";
			break; }
		case T_CHAR:
			if (write) {
				s += "#\\";
				switch (a.as<char>()) {
				case '\0': s += "nul"; break;
				case '\r': s += "return"; break;
				case '\n': s += "newline"; break;
				case '\t': s += "tab"; break;
				case ' ': s += "space"; break;
				default:
					s += a.as<char>();
				}
			}
			else {
				s[0] = a.as<char>();
				s[1] = '\0';
			}
			break;
		case T_CONTINUATION:
			s = "#<continuation>";
			break;
		default:
			s = "#<unknown type>";
			break;
		}
		return s;
	}

	atom make_table() {
		atom a;
		a.type = T_TABLE;
		a.p = std::shared_ptr<void>(std::make_shared<table>());
		return a;
	}

	char *slurp_fp(FILE *fp) {
		char *buf;
		long len;

		fseek(fp, 0, SEEK_END);
		len = ftell(fp);
		if (len < 0) return NULL;
		fseek(fp, 0, SEEK_SET);

		buf = (char *)malloc(len + 1);
		if (!buf)
			return NULL;

		fread(buf, 1, len, fp);
		buf[len] = 0;
		fclose(fp);

		return buf;
	}

	char *slurp(const char *path)
	{
		FILE *fp = fopen(path, "rb");
		if (!fp) {
			/* printf("Reading %s failed.\n", path); */
			return NULL;
		}
		return slurp_fp(fp);
	}

	/* compile-time macro */
	error macex(atom expr, atom *result) {
		error err = ERROR_OK;

		cur_expr = expr; /* for error reporting */

		if (expr.type != T_CONS || !listp(expr)) {
			*result = expr;
			return ERROR_OK;
		}
		else {
			atom op = car(expr);

			/* Handle quote */
			if (op.type == T_SYM && op.as<std::string *>() == sym_quote.as<std::string *>()) {
				*result = expr;
				return ERROR_OK;
			}

			atom args = cdr(expr);

			/* Is it a macro? */
			if (op.type == T_SYM && !env_get(global_env, op.as<std::string *>(), result) && result->type == T_MACRO) {
				/* Evaluate operator */
				op = *result;

				op.type = T_CLOSURE;

				atom result2;
				std::vector<atom> vargs = atom_to_vector(args);
				err = apply(op, vargs, &result2);
				if (err) {
					return err;
				}
				err = macex(result2, result); /* recursive */
				if (err) {
					return err;
				}
				return ERROR_OK;
			}
			else {
				/* macex elements */
				atom expr2 = copy_list(expr);
				atom h;
				for (h = expr2; !no(h); h = cdr(h)) {
					err = macex(car(h), &car(h));
					if (err) {
						return err;
					}
				}
				*result = expr2;
				return ERROR_OK;
			}
		}
	}

	error macex_eval(atom expr, atom *result) {
		atom expr2;
		error err = macex(expr, &expr2);
		if (err) return err;
		/*printf("expanded: ");
		print_expr(expr2);
		puts("");*/
		return eval_expr(expr2, global_env, result);
	}

	error load_string(const char *text) {
		error err = ERROR_OK;
		const char *p = text;
		atom expr;
		while (*p) {
			err = read_expr(p, &p, &expr);
			if (err) {
				break;
			}
			atom result;
			err = macex_eval(expr, &result);
			if (err) {
				break;
			}
			//else {
			//	print_expr(result);
			//	putchar(' ');
			//}
			while (*p && isspace(*p)) p++;
		}
		//puts("");
		return err;
	}
	
	error arc_load_file(const char *path)
	{
		char *text;
		error err = ERROR_OK;
		/* printf("Reading %s...\n", path); */
		text = slurp(path);
		if (text) {
			err = load_string(text);
			free(text);
			return err;
		}
		else {
			return ERROR_FILE;
		}
	}

	error eval_expr(atom expr, std::shared_ptr<struct env> env, atom *result)
	{
		error err;
	start_eval:

		cur_expr = expr; /* for error reporting */
		if (expr.type == T_SYM) {
			err = env_get(env, expr.as<std::string *>(), result);
			return err;
		}
		else if (expr.type != T_CONS) {
			*result = expr;
			return ERROR_OK;
		}
		else if (!listp(expr)) {
			return ERROR_SYNTAX;
		}
		else {
			atom op = car(expr);
			atom args = cdr(expr);

			if (op.type == T_SYM) {
				/* Handle special forms */
				if (sym_is(op, sym_if)) {
					atom *p = &args;
					while (!no(*p)) {
						atom cond;
						if (no(cdr(*p))) { /* else */
							/* tail call optimization of else part */
							expr = car(*p);
							goto start_eval;
						}
						err = eval_expr(car(*p), env, &cond);
						if (err) {
							return err;
						}
						if (!no(cond)) { /* then */
							/* tail call optimization of err = eval_expr(car(cdr(*p)), env, result); */
							expr = car(cdr(*p));
							goto start_eval;
						}
						p = &cdr(cdr(*p));
					}
					*result = nil;
					return ERROR_OK;
				}
				else if (sym_is(op, sym_assign)) {
					atom sym;
					if (no(args) || no(cdr(args))) {
						return ERROR_ARGS;
					}

					sym = car(args);
					if (sym.type == T_SYM) {
						atom val;
						err = eval_expr(car(cdr(args)), env, &val);
						if (err) {
							return err;
						}

						*result = val;
						err = env_assign_eq(env, sym.as<std::string *>(), val);
						return err;
					}
					else {
						return ERROR_TYPE;
					}
				}
				else if (sym_is(op, sym_quote)) {
					if (no(args) || !no(cdr(args))) {
						return ERROR_ARGS;
					}

					*result = car(args);
					return ERROR_OK;
				}
				else if (sym_is(op, sym_fn)) {
					if (no(args)) {
						return ERROR_ARGS;
					}
					err = make_closure(env, car(args), cdr(args), result);
					return err;
				}
				else if (sym_is(op, sym_mac)) { /* (mac name (arg ...) body) */
					atom name, macro;

					if (no(args) || no(cdr(args)) || no(cdr(cdr(args)))) {
						return ERROR_ARGS;
					}

					name = car(args);
					if (name.type != T_SYM) {
						return ERROR_TYPE;
					}

					err = make_closure(env, car(cdr(args)), cdr(cdr(args)), &macro);
					if (!err) {
						macro.type = T_MACRO;
						*result = name;
						err = env_assign(env, name.as<std::string *>(), macro);
						return err;
					}
					else {
						return err;
					}
				}
			}

			/* Evaluate operator */
			atom fn;
			err = eval_expr(op, env, &fn);
			if (err) {
				return err;
			}

			/* Evaulate arguments */
			std::vector<atom> vargs;
			atom *p = &args;
			while (!no(*p)) {
				atom r;
				err = eval_expr(car(*p), env, &r);
				if (err) {
					return err;
				}
				vargs.push_back(r);
				p = &cdr(*p);
			}

			/* tail call optimization of err = apply(fn, args, result); */
			if (fn.type == T_CLOSURE) {
				struct closure cls = fn.as<struct closure>();
				env = std::make_shared<struct env>(cls.env);
				atom arg_names = cls.args;
				atom body = cls.body;

				/* Bind the arguments */
				env_bind(env, arg_names, vargs);

				/* Evaluate the body */
				while (!no(body)) {
					if (no(cdr(body))) {
						/* tail call */
						expr = car(body);
						goto start_eval;
					}
					atom r;
					error err = eval_expr(car(body), env, &r);
					if (err) {
						return err;
					}
					body = cdr(body);
				}
				return ERROR_OK;
			}
			else {
				err = apply(fn, vargs, result);
			}
			return err;
		}
	}

	void arc_init() {
#ifdef READLINE
		rl_bind_key('\t', rl_insert); /* prevent tab completion */
#endif
		srand((unsigned int)time(0));		

		/* Set up the initial environment */
		sym_t = make_sym("t");
		sym_quote = make_sym("quote");
		sym_assign = make_sym("assign");
		sym_fn = make_sym("fn");
		sym_if = make_sym("if");
		sym_mac = make_sym("mac");
		sym_apply = make_sym("apply");
		sym_cons = make_sym("cons");
		sym_sym = make_sym("sym");
		sym_string = make_sym("string");
		sym_num = make_sym("num");
		sym__ = make_sym("_");
		sym_o = make_sym("o");
		sym_table = make_sym("table");
		sym_int = make_sym("int");
		sym_char = make_sym("char");

		env_assign(global_env, sym_t.as<std::string *>(), sym_t);
		env_assign(global_env, make_sym("nil").as<std::string *>(), nil);
		env_assign(global_env, make_sym("car").as<std::string *>(), make_builtin(builtin_car));
		env_assign(global_env, make_sym("cdr").as<std::string *>(), make_builtin(builtin_cdr));
		env_assign(global_env, make_sym("cons").as<std::string *>(), make_builtin(builtin_cons));
		env_assign(global_env, make_sym("+").as<std::string *>(), make_builtin(builtin_add));
		env_assign(global_env, make_sym("-").as<std::string *>(), make_builtin(builtin_subtract));
		env_assign(global_env, make_sym("*").as<std::string *>(), make_builtin(builtin_multiply));
		env_assign(global_env, make_sym("/").as<std::string *>(), make_builtin(builtin_divide));
		env_assign(global_env, make_sym("<").as<std::string *>(), make_builtin(builtin_less));
		env_assign(global_env, make_sym(">").as<std::string *>(), make_builtin(builtin_greater));
		env_assign(global_env, make_sym("apply").as<std::string *>(), make_builtin(builtin_apply));
		env_assign(global_env, make_sym("is").as<std::string *>(), make_builtin(builtin_is));
		env_assign(global_env, make_sym("scar").as<std::string *>(), make_builtin(builtin_scar));
		env_assign(global_env, make_sym("scdr").as<std::string *>(), make_builtin(builtin_scdr));
		env_assign(global_env, make_sym("mod").as<std::string *>(), make_builtin(builtin_mod));
		env_assign(global_env, make_sym("type").as<std::string *>(), make_builtin(builtin_type));
		env_assign(global_env, make_sym("string-sref").as<std::string *>(), make_builtin(builtin_string_sref));
		env_assign(global_env, make_sym("writeb").as<std::string *>(), make_builtin(builtin_writeb));
		env_assign(global_env, make_sym("expt").as<std::string *>(), make_builtin(builtin_expt));
		env_assign(global_env, make_sym("log").as<std::string *>(), make_builtin(builtin_log));
		env_assign(global_env, make_sym("sqrt").as<std::string *>(), make_builtin(builtin_sqrt));
		env_assign(global_env, make_sym("readline").as<std::string *>(), make_builtin(builtin_readline));
		env_assign(global_env, make_sym("quit").as<std::string *>(), make_builtin(builtin_quit));
		env_assign(global_env, make_sym("rand").as<std::string *>(), make_builtin(builtin_rand));
		env_assign(global_env, make_sym("read").as<std::string *>(), make_builtin(builtin_read));
		env_assign(global_env, make_sym("macex").as<std::string *>(), make_builtin(builtin_macex));
		env_assign(global_env, make_sym("string").as<std::string *>(), make_builtin(builtin_string));
		env_assign(global_env, make_sym("sym").as<std::string *>(), make_builtin(builtin_sym));
		env_assign(global_env, make_sym("system").as<std::string *>(), make_builtin(builtin_system));
		env_assign(global_env, make_sym("eval").as<std::string *>(), make_builtin(builtin_eval));
		env_assign(global_env, make_sym("load").as<std::string *>(), make_builtin(builtin_load));
		env_assign(global_env, make_sym("int").as<std::string *>(), make_builtin(builtin_int));
		env_assign(global_env, make_sym("trunc").as<std::string *>(), make_builtin(builtin_trunc));
		env_assign(global_env, make_sym("sin").as<std::string *>(), make_builtin(builtin_sin));
		env_assign(global_env, make_sym("cos").as<std::string *>(), make_builtin(builtin_cos));
		env_assign(global_env, make_sym("tan").as<std::string *>(), make_builtin(builtin_tan));
		env_assign(global_env, make_sym("bound").as<std::string *>(), make_builtin(builtin_bound));
		env_assign(global_env, make_sym("infile").as<std::string *>(), make_builtin(builtin_infile));
		env_assign(global_env, make_sym("outfile").as<std::string *>(), make_builtin(builtin_outfile));
		env_assign(global_env, make_sym("close").as<std::string *>(), make_builtin(builtin_close));
		env_assign(global_env, make_sym("stdin").as<std::string *>(), make_input(stdin));
		env_assign(global_env, make_sym("stdout").as<std::string *>(), make_output(stdout));
		env_assign(global_env, make_sym("stderr").as<std::string *>(), make_output(stderr));
		env_assign(global_env, make_sym("disp").as<std::string *>(), make_builtin(builtin_disp));
		env_assign(global_env, make_sym("readb").as<std::string *>(), make_builtin(builtin_readb));
		env_assign(global_env, make_sym("sread").as<std::string *>(), make_builtin(builtin_sread));
		env_assign(global_env, make_sym("write").as<std::string *>(), make_builtin(builtin_write));
		env_assign(global_env, make_sym("newstring").as<std::string *>(), make_builtin(builtin_newstring));
		env_assign(global_env, make_sym("table").as<std::string *>(), make_builtin(builtin_table));
		env_assign(global_env, make_sym("maptable").as<std::string *>(), make_builtin(builtin_maptable));
		env_assign(global_env, make_sym("table-sref").as<std::string *>(), make_builtin(builtin_table_sref));
		env_assign(global_env, make_sym("coerce").as<std::string *>(), make_builtin(builtin_coerce));
		env_assign(global_env, make_sym("flushout").as<std::string *>(), make_builtin(builtin_flushout));
		env_assign(global_env, make_sym("err").as<std::string *>(), make_builtin(builtin_err));
		env_assign(global_env, make_sym("len").as<std::string *>(), make_builtin(builtin_len));
		env_assign(global_env, make_sym("ccc").as<std::string *>(), make_builtin(builtin_ccc));

		const char *stdlib =
			#include "library.h"
			;
		error err = load_string(stdlib);
		if (err) {
			print_error(err);
		}
	}

	void print_error(error e) {
		if (e != ERROR_USER) {
			printf("%s : ", error_string[e]);
			print_expr(cur_expr);
			puts("");
		}
	}

	void repl() {
		char *temp;
		std::string input;

		while ((temp = readline("> ")) != NULL) {
			input = temp;
		read_start:
#ifdef READLINE
			if (temp && *temp)
				add_history(temp);
#endif

			const char *p = input.c_str();
			error err;
			atom result;

			atom expr;
			err = read_expr(p, &p, &expr);
			if (err == ERROR_FILE) { /* read more lines */
				char *line = readline("  ");
				if (!line) break;
				input += std::string("\n") + line;
				goto read_start;
			}

			if (!err) {
				while (1) {
					atom result;
					error err = macex_eval(expr, &result);
					if (err) {
						print_error(err);
						printf("error in expression:\n");
						print_expr(expr);
						putchar('\n');
						break;
					}
					else {
						print_expr(result);
						puts("");
					}
					err = read_expr(p, &p, &expr);
					if (err != ERROR_OK) {
						break;
					}
				}
			}
			else {
				print_error(err);
			}
			free(temp);
		}
	}
}
