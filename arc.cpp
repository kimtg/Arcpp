#include "arc.h"

namespace arc {
	const char *error_string[] = { "", "Syntax error", "Symbol not bound", "Wrong number of arguments", "Wrong type", "File error", "" };
	const atom nil = { T_NIL };
	atom env; /* the global environment */
	atom sym_t, sym_quote, sym_assign, sym_fn, sym_if, sym_mac, sym_apply, sym_while, sym_cons, sym_sym, sym_string, sym_num, sym__, sym_o, sym_table, sym_int, sym_char;
	atom cur_expr;
	int arc_reader_unclosed = 0;
	atom thrown;
	std::unordered_map<std::string, char *> id_of_sym;

	atom & car(atom & a) {
		return std::static_pointer_cast<arc::cons>(a.p)->car;
	}

	atom & cdr(atom & a) {
		return std::static_pointer_cast<arc::cons>(a.p)->cdr;
	}

	bool no(atom & a) {
		return a.type == arc::T_NIL;
	}

	bool sym_is(const atom & a, const atom & b) {
		return a.value.symbol == b.value.symbol;
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
		a.value.number = x;
		return a;
	}

	atom make_sym(std::string s)
	{
		atom a;
		a.type = T_SYM;

		auto found = id_of_sym.find(s);
		if (found != id_of_sym.end()) {
			a.value.symbol = found->second;
			return a;
		}

		a.value.symbol = (char*)strdup(s.c_str());
		id_of_sym[s] = a.value.symbol;
		return a;
	}

	atom make_builtin(builtin fn)
	{
		atom a;
		a.type = T_BUILTIN;
		a.value.bi = fn;
		return a;
	}

	error make_closure(atom env, atom args, atom body, atom *result)
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
			if (car(p).type == T_CONS && !is(car(car(p)), sym_o))
				return ERROR_SYNTAX;
			p = cdr(p);
		}

		*result = make_cons(env, make_cons(args, body));
		result->type = T_CLOSURE;

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
		a.value.fp = fp;
		return a;
	}

	atom make_output(FILE *fp) {
		atom a;
		a.type = T_OUTPUT;
		a.value.fp = fp;
		return a;
	}

	atom make_char(char c) {
		atom a;
		a.type = T_CHAR;
		a.value.ch = c;
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
			return ERROR_SYNTAX;
		}

		*start = str;

		if (strchr(prefix, str[0]) != NULL)
			*end = str + 1;
		else if (str[0] == ',')
			*end = str + (str[1] == '@' ? 2 : 1);
		else if (str[0] == '"') {
			arc_reader_unclosed++;
			str++;
			while (*str != 0) {
				if (*str == '\\') str++;
				else if (*str == '"') {
					arc_reader_unclosed--;
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
			result->type = T_NUM;
			result->value.number = val;
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
				arc_reader_unclosed--;
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
				arc_reader_unclosed--;
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
				arc_reader_unclosed--;
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
			arc_reader_unclosed++;
			return read_list(*end, end, result);
		}
		else if (token[0] == ')')
			return ERROR_SYNTAX;
		else if (token[0] == '[') {
			arc_reader_unclosed++;
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
		str = (char *)malloc(sizeof(char)* size); /* size is start size */
		if (!str) return NULL;
		while (EOF != (ch = fgetc(fp)) && ch != '\n') {
			str[len++] = ch;
			if (len == size) {
				str = (char *)realloc(str, sizeof(char)*(size *= 2));
				if (!str) return NULL;
			}
		}
		if (ch == EOF && len == 0) return NULL;
		str[len++] = '\0';

		return (char *)realloc(str, sizeof(char)*len);
	}

	atom env_create(atom parent)
	{
		return make_cons(parent, make_table());
	}

	error env_get(atom env, atom symbol, atom *result)
	{
		while (1) {
			atom parent = car(env);

			auto &tbl = cdr(env).as<table>();
			auto found = tbl.find(symbol);
			if (found != tbl.end()) {
				*result = found->second;
				return ERROR_OK;
			}
			if (no(parent)) {
				/*printf("%s: ", symbol.p.symbol);*/
				return ERROR_UNBOUND;
			}
			env = parent;
		}
	}

	error env_assign(atom env, atom symbol, atom value) {
		auto &tbl = cdr(env).as<table>();
		tbl[symbol] = value;
		return ERROR_OK;
	}

	error env_assign_eq(atom env, atom symbol, atom value) {
		while (1) {
			auto &tbl = cdr(env).as<table>();
			auto found = tbl.find(symbol);
			if (found != tbl.end()) {
				found->second = value;
				return ERROR_OK;
			}
			atom parent = car(env);
			if (no(parent)) {
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

	error apply(atom fn, atom args, atom *result)
	{
		atom env, arg_names, body;

		if (fn.type == T_BUILTIN)
			return fn.value.bi(args, result);
		else if (fn.type == T_CLOSURE) {
			env = env_create(car(fn));
			arg_names = car(cdr(fn));
			body = cdr(cdr(fn));

			/* Bind the arguments */
			while (!no(arg_names)) {
				if (arg_names.type == T_SYM) {
					env_assign(env, arg_names, args);
					args = nil;
					break;
				}
				atom arg_name = car(arg_names);
				if (arg_name.type == T_SYM) {
					if (no(args)) /* missing argument */
						return ERROR_ARGS;
					env_assign(env, arg_name, car(args));
					args = cdr(args);
				}
				else { /* (o ARG [DEFAULT]) */
					atom val;
					if (no(args)) { /* missing argument */
						if (no(cdr(cdr(arg_name))))
							val = nil;
						else {
							error err = eval_expr(car(cdr(cdr(arg_name))), env, &val);
							if (err) return err;
						}
					}
					else {
						val = car(args);
						args = cdr(args);
					}
					env_assign(env, car(cdr(arg_name)), val);
				}
				arg_names = cdr(arg_names);
			}
			if (!no(args))
				return ERROR_ARGS;

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
			if (len(args) != 1) return ERROR_ARGS;
			thrown = car(args);
			longjmp(*fn.value.jb, 1);
		}
		else if (fn.type == T_STRING) { /* implicit indexing for string */
			if (len(args) != 1) return ERROR_ARGS;
			long index = (long)car(args).value.number;
			*result = make_char(fn.as<std::string>()[index]);
			return ERROR_OK;
		}
		else if (fn.type == T_CONS && listp(fn)) { /* implicit indexing for list */
			if (len(args) != 1) return ERROR_ARGS;
			long index = (long)(car(args).value.number);
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
			long len1 = len(args);
			if (len1 != 1 && len1 != 2) return ERROR_ARGS;
			atom key = car(args);
			auto &tbl = fn.as<table>();
			auto found = tbl.find(key);
			if (found != tbl.end()) {
				*result = found->second;
			}
			else {
				if (len1 == 2) /* default value is specified */
					*result = car(cdr(args));
				else
					*result = nil;
			}
			return ERROR_OK;
		}
		else {
			return ERROR_TYPE;
		}
	}

	error builtin_car(atom args, atom *result)
	{
		if (no(args) || !no(cdr(args)))
			return ERROR_ARGS;

		if (no(car(args)))
			*result = nil;
		else if (car(args).type != T_CONS)
			return ERROR_TYPE;
		else
			*result = car(car(args));

		return ERROR_OK;
	}

	error builtin_cdr(atom args, atom *result)
	{
		if (no(args) || !no(cdr(args)))
			return ERROR_ARGS;

		if (no(car(args)))
			*result = nil;
		else if (car(args).type != T_CONS)
			return ERROR_TYPE;
		else
			*result = cdr(car(args));

		return ERROR_OK;
	}

	error builtin_cons(atom args, atom *result)
	{
		if (no(args) || no(cdr(args)) || !no(cdr(cdr(args))))
			return ERROR_ARGS;

		*result = make_cons(car(args), car(cdr(args)));

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
	error builtin_add(atom args, atom *result)
	{
		if (no(args)) {
			*result = make_number(0);
		}
		else {
			if (car(args).type == T_NUM) {
				double r = car(args).value.number;
				args = cdr(args);
				while (!no(args)) {
					if (args.type != T_CONS) return ERROR_ARGS;
					if (car(args).type != T_NUM) return ERROR_TYPE;
					r += (car(args).value.number);
					args = cdr(args);
				}
				*result = make_number(r);
			}
			else if (car(args).type == T_STRING) {
				std::string buf;
				while (!no(args)) {
					if (args.type != T_CONS) return ERROR_ARGS;
					std::string s = to_string(car(args), 0);
					buf += s;
					args = cdr(args);
				}
				*result = make_string(buf);
			}
			else if (car(args).type == T_CONS || car(args).type == T_NIL) {
				atom acc = nil;
				while (!no(args)) {
					if (args.type != T_CONS) return ERROR_ARGS;
					acc = append(acc, car(args));
					args = cdr(args);
				}
				*result = acc;
			}
		}
		return ERROR_OK;
	}

	error builtin_subtract(atom args, atom *result)
	{
		if (no(args)) { /* 0 argument */
			*result = make_number(0);
			return ERROR_OK;
		}
		if (no(cdr(args))) { /* 1 argument */
			if (car(args).type != T_NUM) return ERROR_TYPE;
			*result = make_number(-(car(args).value.number));
			return ERROR_OK;
		}
		if (car(args).type != T_NUM) return ERROR_TYPE;
		double r = (car(args).value.number);
		args = cdr(args);
		while (!no(args)) {
			if (args.type != T_CONS) return ERROR_ARGS;
			if (car(args).type != T_NUM) return ERROR_TYPE;
			r -= (car(args).value.number);
			args = cdr(args);
		}
		*result = make_number(r);
		return ERROR_OK;
	}

	error builtin_multiply(atom args, atom *result)
	{
		double r = 1;
		while (!no(args)) {
			if (args.type != T_CONS) return ERROR_ARGS;
			if (car(args).type != T_NUM) return ERROR_TYPE;
			r *= (car(args).value.number);
			args = cdr(args);
		}
		*result = make_number(r);
		return ERROR_OK;
	}

	error builtin_divide(atom args, atom *result)
	{
		if (no(args)) { /* 0 argument */
			*result = make_number(1);
			return ERROR_OK;
		}
		if (no(cdr(args))) { /* 1 argument */
			if (car(args).type != T_NUM) return ERROR_TYPE;
			*result = make_number(1.0 / (car(args).value.number));
			return ERROR_OK;
		}
		if (car(args).type != T_NUM) return ERROR_TYPE;
		double r = (car(args).value.number);
		args = cdr(args);
		while (!no(args)) {
			if (args.type != T_CONS) return ERROR_ARGS;
			if (car(args).type != T_NUM) return ERROR_TYPE;
			r /= (car(args).value.number);
			args = cdr(args);
		}
		*result = make_number(r);
		return ERROR_OK;
	}

	error builtin_less(atom args, atom *result)
	{
		if (no(args) || no(cdr(args))) {
			*result = sym_t;
			return ERROR_OK;
		}
		switch (car(args).type) {
		case T_NUM:
			while (!no(cdr(args))) {
				if ((car(args).value.number) >= (car(cdr(args)).value.number)) {
					*result = nil;
					return ERROR_OK;
				}
				args = cdr(args);
			}
			*result = sym_t;
			return ERROR_OK;
		case T_STRING:
			while (!no(cdr(args))) {
				if ((car(args).as<std::string>()) >= (car(cdr(args)).as<std::string>())) {
					*result = nil;
					return ERROR_OK;
				}
				args = cdr(args);
			}
			*result = sym_t;
			return ERROR_OK;
		default:
			return ERROR_TYPE;
		}
	}

	error builtin_greater(atom args, atom *result)
	{
		atom a, b;
		if (no(args) || no(cdr(args))) {
			*result = sym_t;
			return ERROR_OK;
		}
		switch (car(args).type) {
		case T_NUM:
			while (!no(cdr(args))) {
				a = car(args);
				b = car(cdr(args));
				if ((a.value.number) <= (b.value.number)) {
					*result = nil;
					return ERROR_OK;
				}
				args = cdr(args);
			}
			*result = sym_t;
			return ERROR_OK;
		case T_STRING:
			while (!no(cdr(args))) {
				a = car(args);
				b = car(cdr(args));
				if (a.as<std::string>() <= (b.as<std::string>())) {
					*result = nil;
					return ERROR_OK;
				}
				args = cdr(args);
			}
			*result = sym_t;
			return ERROR_OK;
		default:
			return ERROR_TYPE;
		}
	}

	error builtin_apply(atom args, atom *result)
	{
		atom fn;

		if (no(args) || no(cdr(args)) || !no(cdr(cdr(args))))
			return ERROR_ARGS;

		fn = car(args);
		args = car(cdr(args));

		if (!listp(args))
			return ERROR_SYNTAX;

		return apply(fn, args, result);
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
				return a.value.bi == b.value.bi;
			case T_SYM:
				return (a.value.symbol) == (b.value.symbol);
			case T_NUM:
				return (a.value.number) == (b.value.number);
			case T_STRING:
				return a.as<std::string>() == b.as<std::string>();
			case T_CHAR:
				return a.value.ch == b.value.ch;
			case T_TABLE:
				return a.as<table>() == b.as<table>();
			case T_INPUT:
			case T_OUTPUT:
				return a.value.fp == b.value.fp;
			case T_CONTINUATION:
				return a.value.jb == b.value.jb;
			}
		}
		return false;
	}

	bool iso(atom a, atom b) {
		if (a.type == b.type) {
			switch (a.type) {
			case T_CONS:
			case T_CLOSURE:
			case T_MACRO:
				return iso(a.as<cons>().car, b.as<cons>().car) && iso(a.as<cons>().cdr, b.as<cons>().cdr);
			default:
				return is(a, b);
			}
		}
		return 0;
	}

	error builtin_is(atom args, atom *result)
	{
		atom a, b;
		if (no(args) || no(cdr(args))) {
			*result = sym_t;
			return ERROR_OK;
		}
		while (!no(cdr(args))) {
			a = car(args);
			b = car(cdr(args));
			if (!is(a, b)) {
				*result = nil;
				return ERROR_OK;
			}
			args = cdr(args);
		}
		*result = sym_t;
		return ERROR_OK;
	}

	error builtin_scar(atom args, atom *result) {
		if (len(args) != 2) return ERROR_ARGS;
		atom place = car(args), value;
		if (place.type != T_CONS) return ERROR_TYPE;
		value = car(cdr(args));
		place.as<cons>().car = value;
		*result = value;
		return ERROR_OK;
	}

	error builtin_scdr(atom args, atom *result) {
		if (len(args) != 2) return ERROR_ARGS;
		atom place = car(args), value;
		if (place.type != T_CONS) return ERROR_TYPE;
		value = car(cdr(args));
		place.as<cons>().cdr = value;
		*result = value;
		return ERROR_OK;
	}

	error builtin_mod(atom args, atom *result) {
		if (len(args) != 2) return ERROR_ARGS;
		atom dividend = car(args);
		atom divisor = car(cdr(args));
		double r = fmod(dividend.value.number, divisor.value.number);
		if (dividend.value.number * divisor.value.number < 0 && r != 0) r += divisor.value.number;
		*result = make_number(r);
		return ERROR_OK;
	}

	error builtin_type(atom args, atom *result) {
		if (len(args) != 1) return ERROR_ARGS;
		atom x = car(args);
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
	error builtin_string_sref(atom args, atom *result) {
		atom index, obj, value;
		if (len(args) != 3) return ERROR_ARGS;
		index = car(cdr(cdr(args)));
		obj = car(args);
		if (obj.type != T_STRING) return ERROR_TYPE;
		value = car(cdr(args));
		obj.as<std::string>()[(long)index.value.number] = value.value.ch;
		*result = value;
		return ERROR_OK;
	}

	/* disp [arg [output-port]] */
	error builtin_disp(atom args, atom *result) {
		long l = len(args);
		FILE *fp;
		switch (l) {
		case 0:
			*result = nil;
			return ERROR_OK;
		case 1:
			fp = stdout;
			break;
		case 2:
			fp = car(cdr(args)).value.fp;
			break;
		default:
			return ERROR_ARGS;
		}
		fprintf(fp, "%s", to_string(car(args), 0).c_str());
		*result = nil;
		return ERROR_OK;
	}

	error builtin_writeb(atom args, atom *result) {
		long l = len(args);
		FILE *fp;
		switch (l) {
		case 0: return ERROR_ARGS;
		case 1:
			fp = stdout;
			break;
		case 2:
			fp = car(cdr(args)).value.fp;
			break;
		default: return ERROR_ARGS;
		}
		fputc((int)(car(args).value.number), fp);
		*result = nil;
		return ERROR_OK;
	}

	error builtin_expt(atom args, atom *result) {
		atom a, b;
		if (len(args) != 2) return ERROR_ARGS;
		a = car(args);
		b = car(cdr(args));
		*result = make_number(pow((a.value.number), (b.value.number)));
		return ERROR_OK;
	}

	error builtin_log(atom args, atom *result) {
		atom a;
		if (len(args) != 1) return ERROR_ARGS;
		a = car(args);
		*result = make_number(log((a.value.number)));
		return ERROR_OK;
	}

	error builtin_sqrt(atom args, atom *result) {
		atom a;
		if (len(args) != 1) return ERROR_ARGS;
		a = car(args);
		*result = make_number(sqrt((a.value.number)));
		return ERROR_OK;
	}

	error builtin_readline(atom args, atom *result) {
		long l = len(args);
		char *str;
		if (l == 0) {
			str = readline("");
		}
		else if (l == 1) {
			str = readline_fp("", car(args).value.fp);
		}
		else {
			return ERROR_ARGS;
		}
		if (str == NULL) *result = nil; else *result = make_string(str);
		return ERROR_OK;
	}

	error builtin_quit(atom args, atom *result) {
		if (len(args) != 0) return ERROR_ARGS;
		exit(0);
	}

	double rand_double() {
		return (double)rand() / ((double)RAND_MAX + 1.0);
	}

	error builtin_rand(atom args, atom *result) {
		long alen = len(args);
		if (alen == 0) *result = make_number(rand_double());
		else if (alen == 1) *result = make_number(floor(rand_double() * (car(args).value.number)));
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
	error builtin_read(atom args, atom *result) {
		size_t alen = len(args);
		error err;
		if (alen == 0) {
			err = read_fp(stdin, result);
		}
		else if (alen <= 2) {
			atom src = car(args);
			if (src.type == T_STRING) {
				const char *s = car(args).as<std::string>().c_str();
				const char *buf = s;
				err = read_expr(buf, &buf, result);
			}
			else if (src.type == T_INPUT) {
				err = read_fp(src.value.fp, result);
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
				eof = car(cdr(args));
			}

			*result = eof;
			return ERROR_OK;
		}
		else {
			return err;
		}
	}

	error builtin_macex(atom args, atom *result) {
		long alen = len(args);
		if (alen == 1) {
			error err = macex(car(args), result);
			return err;
		}
		else return ERROR_ARGS;
		return ERROR_OK;
	}

	error builtin_string(atom args, atom *result) {
		std::string s;
		while (!no(args)) {
			s += to_string(car(args), 0);
			args = cdr(args);
		}
		*result = make_string(s);
		return ERROR_OK;
	}

	error builtin_sym(atom args, atom *result) {
		long alen = len(args);
		if (alen == 1) {
			*result = make_sym(to_string(car(args), 0));
			return ERROR_OK;
		}
		else return ERROR_ARGS;
	}

	error builtin_system(atom args, atom *result) {
		long alen = len(args);
		if (alen == 1) {
			atom a = car(args);
			if (a.type != T_STRING) return ERROR_TYPE;
			*result = make_number(system(car(args).as<std::string>().c_str()));
			return ERROR_OK;
		}
		else return ERROR_ARGS;
	}

	error builtin_eval(atom args, atom *result) {
		if (len(args) == 1) return macex_eval(car(args), result);
		else return ERROR_ARGS;
	}

	error builtin_load(atom args, atom *result) {
		if (len(args) == 1) {
			atom a = car(args);
			if (a.type != T_STRING) return ERROR_TYPE;
			*result = nil;
			return arc_load_file(a.as<std::string>().c_str());
		}
		else return ERROR_ARGS;
	}

	error builtin_int(atom args, atom *result) {
		if (len(args) == 1) {
			atom a = car(args);
			switch (a.type) {
			case T_STRING:
				*result = make_number(round(atof(a.as<std::string>().c_str())));
				break;
			case T_SYM:
				*result = make_number(round(atof(a.value.symbol)));
				break;
			case T_NUM:
				*result = make_number(round((a.value.number)));
				break;
			case T_CHAR:
				*result = make_number(a.value.ch);
				break;
			default:
				return ERROR_TYPE;
			}
			return ERROR_OK;
		}
		else return ERROR_ARGS;
	}

	error builtin_trunc(atom args, atom *result) {
		if (len(args) == 1) {
			atom a = car(args);
			if (a.type != T_NUM) return ERROR_TYPE;
			*result = make_number(trunc((a.value.number)));
			return ERROR_OK;
		}
		else return ERROR_ARGS;
	}

	error builtin_sin(atom args, atom *result) {
		if (len(args) == 1) {
			atom a = car(args);
			if (a.type != T_NUM) return ERROR_TYPE;
			*result = make_number(sin((a.value.number)));
			return ERROR_OK;
		}
		else return ERROR_ARGS;
	}

	error builtin_cos(atom args, atom *result) {
		if (len(args) == 1) {
			atom a = car(args);
			if (a.type != T_NUM) return ERROR_TYPE;
			*result = make_number(cos((a.value.number)));
			return ERROR_OK;
		}
		else return ERROR_ARGS;
	}

	error builtin_tan(atom args, atom *result) {
		if (len(args) == 1) {
			atom a = car(args);
			if (a.type != T_NUM) return ERROR_TYPE;
			*result = make_number(tan(a.value.number));
			return ERROR_OK;
		}
		else return ERROR_ARGS;
	}

	error builtin_bound(atom args, atom *result) {
		if (len(args) == 1) {
			atom a = car(args);
			if (a.type != T_SYM) return ERROR_TYPE;
			error err = env_get(env, a, result);
			*result = (err ? nil : sym_t);
			return ERROR_OK;
		}
		else return ERROR_ARGS;
	}

	error builtin_infile(atom args, atom *result) {
		if (len(args) == 1) {
			atom a = car(args);
			if (a.type != T_STRING) return ERROR_TYPE;
			FILE *fp = fopen(a.as<std::string>().c_str(), "r");
			*result = make_input(fp);
			return ERROR_OK;
		}
		else return ERROR_ARGS;
	}

	error builtin_outfile(atom args, atom *result) {
		if (len(args) == 1) {
			atom a = car(args);
			if (a.type != T_STRING) return ERROR_TYPE;
			FILE *fp = fopen(a.as<std::string>().c_str(), "w");
			*result = make_output(fp);
			return ERROR_OK;
		}
		else return ERROR_ARGS;
	}

	error builtin_close(atom args, atom *result) {
		if (len(args) == 1) {
			atom a = car(args);
			if (a.type != T_INPUT && a.type != T_OUTPUT) return ERROR_TYPE;
			fclose(a.value.fp);
			*result = nil;
			return ERROR_OK;
		}
		else return ERROR_ARGS;
	}

	error builtin_readb(atom args, atom *result) {
		long l = len(args);
		FILE *fp;
		switch (l) {
		case 0:
			fp = stdin;
			break;
		case 1:
			fp = car(args).value.fp;
			break;
		default:
			return ERROR_ARGS;
		}
		*result = make_number(fgetc(fp));
		return ERROR_OK;
	}

	/* sread input-port eof */
	error builtin_sread(atom args, atom *result) {
		if (len(args) != 2) return ERROR_ARGS;
		FILE *fp = car(args).value.fp;
		atom eof = car(cdr(args));
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
	error builtin_write(atom args, atom *result) {
		long l = len(args);
		FILE *fp;
		switch (l) {
		case 0:
			*result = nil;
			return ERROR_OK;
		case 1:
			fp = stdout;
			break;
		case 2:
			fp = car(cdr(args)).value.fp;
			break;
		default:
			return ERROR_ARGS;
		}
		atom a = car(args);
		if (a.type == T_STRING) fputc('"', fp);
		std::cout << to_string(a, 1);
		if (a.type == T_STRING) fputc('"', fp);
		*result = nil;
		return ERROR_OK;
	}

	/* newstring length [char] */
	error builtin_newstring(atom args, atom *result) {
		long arg_len = len(args);
		long length = (long)car(args).value.number;
		char c = 0;
		char *s;
		switch (arg_len) {
		case 1: break;
		case 2:
			c = car(cdr(args)).value.ch;
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

	error builtin_table(atom args, atom *result) {
		long arg_len = len(args);
		if (arg_len != 0) return ERROR_ARGS;
		*result = make_table();
		return ERROR_OK;
	}

	/* maptable proc table */
	error builtin_maptable(atom args, atom *result) {
		long arg_len = len(args);
		if (arg_len != 2) return ERROR_ARGS;
		atom proc = car(args);
		atom tbl = car(cdr(args));
		if (proc.type != T_BUILTIN && proc.type != T_CLOSURE) return ERROR_TYPE;
		if (tbl.type != T_TABLE) return ERROR_TYPE;
		auto &table1 = tbl.as<table>();
		for (auto &p : table1) {
			error err = apply(proc, make_cons(p.first, make_cons(p.second, nil)), result);
			if (err) return err;
		}
		*result = tbl;
		return ERROR_OK;
	}

	/* table-sref obj value index */
	error builtin_table_sref(atom args, atom *result) {
		atom index, obj, value;
		if (len(args) != 3) return ERROR_ARGS;
		index = car(cdr(cdr(args)));
		obj = car(args);
		if (obj.type != T_TABLE) return ERROR_TYPE;
		value = car(cdr(args));
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
	error builtin_coerce(atom args, atom *result) {
		atom obj, type;
		if (len(args) != 2) return ERROR_ARGS;
		obj = car(args);
		type = car(cdr(args));
		switch (obj.type) {
		case T_CHAR:
			if (is(type, sym_int) || is(type, sym_num)) *result = make_number(obj.value.ch);
			else if (is(type, sym_string)) {
				char *buf = (char *)malloc(2);
				buf[0] = obj.value.ch;
				buf[1] = '\0';
				*result = make_string(buf);
			}
			else if (is(type, sym_sym)) {
				char buf[2];
				buf[0] = obj.value.ch;
				buf[1] = '\0';
				*result = make_sym(buf);
			}
			else if (is(type, sym_char))
				*result = obj;
			else
				return ERROR_TYPE;
			break;
		case T_NUM:
			if (is(type, sym_int)) *result = make_number(floor(obj.value.number));
			else if (is(type, sym_char)) *result = make_char((char)obj.value.number);
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
					s += car(p).value.ch;
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
				*result = make_string(obj.value.symbol);
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

	error builtin_flushout(atom args, atom *result) {
		if (len(args) != 0) return ERROR_ARGS;
		fflush(stdout);
		*result = sym_t;
		return ERROR_OK;
	}

	error builtin_err(atom args, atom *result) {
		if (len(args) == 0) return ERROR_ARGS;
		cur_expr = nil;
		atom p = args;
		for (; !no(p); p = cdr(p)) {
			std::cout << to_string(car(p), 0) << '\n';
		}
		return ERROR_USER;
	}

	error builtin_len(atom args, atom *result) {
		if (len(args) != 1) return ERROR_ARGS;
		atom a = car(args);
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
		a.value.jb = jb;
		return a;
	}

	error builtin_ccc(atom args, atom *result) {
		if (len(args) != 1) return ERROR_ARGS;
		atom a = car(args);
		if (a.type != T_BUILTIN && a.type != T_CLOSURE) return ERROR_TYPE;
		jmp_buf jb;
		int val = setjmp(jb);
		if (val) {
			*result = thrown;
			return ERROR_OK;
		}
		return apply(a, make_cons(make_continuation(&jb), nil), result);
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
			s = a.value.symbol;
			break;
		case T_STRING:
			if (write) s += "\"";
			s += a.as<std::string>();
			if (write) s += "\"";
			break;
		case T_NUM:
		{
			std::stringstream ss;
			ss << std::setprecision(16) << a.value.number;
			s = ss.str();
			break;
		}
		case T_BUILTIN:
		{
			std::stringstream ss;
			ss << "#<builtin:" << (void *)a.value.bi << ">";
			s = ss.str();
			break;
		}
		case T_CLOSURE:
		{
			atom a2 = make_cons(sym_fn, cdr(a));
			s = to_string(a2, write);
			break;
		}
		case T_MACRO:
			s = "#<macro:" + to_string(cdr(a), write) + ">";
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
				switch (a.value.ch) {
				case '\0': s += "nul"; break;
				case '\r': s += "return"; break;
				case '\n': s += "newline"; break;
				case '\t': s += "tab"; break;
				case ' ': s += "space"; break;
				default:
					s += a.value.ch;
				}
			}
			else {
				s[0] = a.value.ch;
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
		a.p = std::make_shared<table>();
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
			atom args = cdr(expr);

			if (op.type == T_SYM) {
				/* Handle special forms */

				if (sym_is(op, sym_quote)) {
					if (no(args) || !no(cdr(args))) {
						return ERROR_ARGS;
					}

					*result = expr;
					return ERROR_OK;
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
						*result = make_cons(sym_quote, make_cons(car(args), nil));
						err = env_assign(env, name, macro);
						return err;
					}
					else {
						return err;
					}
				}
			}

			/* Is it a macro? */
			if (op.type == T_SYM && !env_get(env, op, result) && result->type == T_MACRO) {
				/* Evaluate operator */
				op = *result;

				op.type = T_CLOSURE;
				atom result2;
				err = apply(op, args, &result2);
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
				/* preprocess elements */
				atom expr2 = copy_list(expr);
				atom p = expr2;
				while (!no(p)) {
					err = macex(car(p), &car(p));
					if (err) {
						return err;
					}
					p = cdr(p);
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
		return eval_expr(expr2, env, result);
	}

	error arc_load_file(const char *path)
	{
		char *text;
		error err = ERROR_OK;
		/* printf("Reading %s...\n", path); */
		text = slurp(path);
		if (text) {
			const char *p = text;
			atom expr;
			while (1) {
				if (read_expr(p, &p, &expr) != ERROR_OK) {
					break;
				}
				atom result;
				err = macex_eval(expr, &result);
				if (err) {
					print_error(err);
					printf("error in expression:\n\t");
					print_expr(expr);
					putchar('\n');
					break;
				}
				//else {
				//	print_expr(result);
				//	putchar(' ');
				//}
			}
			//puts("");
			free(text);
			return err;
		}
		else {
			return ERROR_FILE;
		}
	}

	error eval_expr(atom expr, atom env, atom *result)
	{
		error err;
	start_eval:

		cur_expr = expr; /* for error reporting */
		if (expr.type == T_SYM) {
			err = env_get(env, expr, result);
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
						err = env_assign_eq(env, sym, val);
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
						err = env_assign(env, name, macro);
						return err;
					}
					else {
						return err;
					}
				}
				else if (sym_is(op, sym_while)) {
					atom pred;
					if (no(args)) {
						return ERROR_ARGS;
					}
					pred = car(args);
					while (err = eval_expr(pred, env, result), !no(*result)) {
						if (err) {
							return err;
						}
						atom e = cdr(args);
						while (!no(e)) {
							err = eval_expr(car(e), env, result);
							if (err) {
								return err;
							}
							e = cdr(e);
						}
					}
					return ERROR_OK;
				}
			}

			/* Evaluate operator */
			err = eval_expr(op, env, &op);
			if (err) {
				return err;
			}

			/* Is it a macro? */
			if (op.type == T_MACRO) {
				atom expansion;
				op.type = T_CLOSURE;
				err = apply(op, args, &expansion);
				if (err) {
					return err;
				}
				err = eval_expr(expansion, env, result);
				return err;
			}

			/* Evaulate arguments */
			atom head = nil, tail;
			atom *p = &args;
			while (!no(*p)) {
				atom r;
				err = eval_expr(car(*p), env, &r);
				if (err) {
					return err;
				}
				if (no(head)) {
					head = make_cons(r, nil);
					tail = head;
				}
				else {
					cdr(tail) = make_cons(r, nil);
					tail = cdr(tail);
				}

				p = &cdr(*p);
			}
			args = head;

			if (op.type == T_CLOSURE) {
				/* tail call optimization of err = apply(op, args, result); */
				atom env2, arg_names, body;
				env2 = env_create(car(op));
				arg_names = car(cdr(op));
				body = cdr(cdr(op));

				/* Bind the arguments */
				while (!no(arg_names)) {
					if (arg_names.type == T_SYM) {
						env_assign(env2, arg_names, args);
						args = nil;
						break;
					}
					atom arg_name = car(arg_names);
					if (arg_name.type == T_SYM) {
						if (no(args)) {/* missing argument */
							return ERROR_ARGS;
						}
						env_assign(env2, arg_name, car(args));
						args = cdr(args);
					}
					else { /* (o ARG [DEFAULT]) */
						atom val;
						if (no(args)) { /* missing argument */
							if (no(cdr(cdr(arg_name))))
								val = nil;
							else {
								error err = eval_expr(car(cdr(cdr(arg_name))), env2, &val);
								if (err) {
									return err;
								}
							}
						}
						else {
							val = car(args);
							args = cdr(args);
						}
						env_assign(env2, car(cdr(arg_name)), val);
					}
					arg_names = cdr(arg_names);
				}
				if (!no(args)) {
					return ERROR_ARGS;
				}

				/* Evaluate the body */
				while (!no(body)) {
					if (no(cdr(body))) {
						/* tail call */
						expr = car(body);
						env = env2;
						goto start_eval;
					}
					error err = eval_expr(car(body), env2, result);
					if (err) {
						return err;
					}
					body = cdr(body);
				}
				return ERROR_OK;
			}
			else {
				err = apply(op, args, result);
			}
			return err;
		}
	}

	void arc_init(char *file_path) {
#ifdef READLINE
		rl_bind_key('\t', rl_insert); /* prevent tab completion */
#endif
		srand((unsigned int)time(0));
		env = env_create(nil);

		/* Set up the initial environment */
		sym_t = make_sym("t");
		sym_quote = make_sym("quote");
		sym_assign = make_sym("assign");
		sym_fn = make_sym("fn");
		sym_if = make_sym("if");
		sym_mac = make_sym("mac");
		sym_apply = make_sym("apply");
		sym_while = make_sym("while");
		sym_cons = make_sym("cons");
		sym_sym = make_sym("sym");
		sym_string = make_sym("string");
		sym_num = make_sym("num");
		sym__ = make_sym("_");
		sym_o = make_sym("o");
		sym_table = make_sym("table");
		sym_int = make_sym("int");
		sym_char = make_sym("char");

		env_assign(env, sym_t, sym_t);
		env_assign(env, make_sym("nil"), nil);
		env_assign(env, make_sym("car"), make_builtin(builtin_car));
		env_assign(env, make_sym("cdr"), make_builtin(builtin_cdr));
		env_assign(env, make_sym("cons"), make_builtin(builtin_cons));
		env_assign(env, make_sym("+"), make_builtin(builtin_add));
		env_assign(env, make_sym("-"), make_builtin(builtin_subtract));
		env_assign(env, make_sym("*"), make_builtin(builtin_multiply));
		env_assign(env, make_sym("/"), make_builtin(builtin_divide));
		env_assign(env, make_sym("<"), make_builtin(builtin_less));
		env_assign(env, make_sym(">"), make_builtin(builtin_greater));
		env_assign(env, make_sym("apply"), make_builtin(builtin_apply));
		env_assign(env, make_sym("is"), make_builtin(builtin_is));
		env_assign(env, make_sym("scar"), make_builtin(builtin_scar));
		env_assign(env, make_sym("scdr"), make_builtin(builtin_scdr));
		env_assign(env, make_sym("mod"), make_builtin(builtin_mod));
		env_assign(env, make_sym("type"), make_builtin(builtin_type));
		env_assign(env, make_sym("string-sref"), make_builtin(builtin_string_sref));
		env_assign(env, make_sym("writeb"), make_builtin(builtin_writeb));
		env_assign(env, make_sym("expt"), make_builtin(builtin_expt));
		env_assign(env, make_sym("log"), make_builtin(builtin_log));
		env_assign(env, make_sym("sqrt"), make_builtin(builtin_sqrt));
		env_assign(env, make_sym("readline"), make_builtin(builtin_readline));
		env_assign(env, make_sym("quit"), make_builtin(builtin_quit));
		env_assign(env, make_sym("rand"), make_builtin(builtin_rand));
		env_assign(env, make_sym("read"), make_builtin(builtin_read));
		env_assign(env, make_sym("macex"), make_builtin(builtin_macex));
		env_assign(env, make_sym("string"), make_builtin(builtin_string));
		env_assign(env, make_sym("sym"), make_builtin(builtin_sym));
		env_assign(env, make_sym("system"), make_builtin(builtin_system));
		env_assign(env, make_sym("eval"), make_builtin(builtin_eval));
		env_assign(env, make_sym("load"), make_builtin(builtin_load));
		env_assign(env, make_sym("int"), make_builtin(builtin_int));
		env_assign(env, make_sym("trunc"), make_builtin(builtin_trunc));
		env_assign(env, make_sym("sin"), make_builtin(builtin_sin));
		env_assign(env, make_sym("cos"), make_builtin(builtin_cos));
		env_assign(env, make_sym("tan"), make_builtin(builtin_tan));
		env_assign(env, make_sym("bound"), make_builtin(builtin_bound));
		env_assign(env, make_sym("infile"), make_builtin(builtin_infile));
		env_assign(env, make_sym("outfile"), make_builtin(builtin_outfile));
		env_assign(env, make_sym("close"), make_builtin(builtin_close));
		env_assign(env, make_sym("stdin"), make_input(stdin));
		env_assign(env, make_sym("stdout"), make_output(stdout));
		env_assign(env, make_sym("stderr"), make_output(stderr));
		env_assign(env, make_sym("disp"), make_builtin(builtin_disp));
		env_assign(env, make_sym("readb"), make_builtin(builtin_readb));
		env_assign(env, make_sym("sread"), make_builtin(builtin_sread));
		env_assign(env, make_sym("write"), make_builtin(builtin_write));
		env_assign(env, make_sym("newstring"), make_builtin(builtin_newstring));
		env_assign(env, make_sym("table"), make_builtin(builtin_table));
		env_assign(env, make_sym("maptable"), make_builtin(builtin_maptable));
		env_assign(env, make_sym("table-sref"), make_builtin(builtin_table_sref));
		env_assign(env, make_sym("coerce"), make_builtin(builtin_coerce));
		env_assign(env, make_sym("flushout"), make_builtin(builtin_flushout));
		env_assign(env, make_sym("err"), make_builtin(builtin_err));
		env_assign(env, make_sym("len"), make_builtin(builtin_len));
		env_assign(env, make_sym("ccc"), make_builtin(builtin_ccc));

		std::string dir_path = get_dir_path(file_path);
		std::string lib = dir_path + "library.arc";
		arc_load_file(lib.c_str());
	}

	char *get_dir_path(char *file_path) {
		size_t len = strlen(file_path);
		long i = len - 1;
		for (; i >= 0; i--) {
			char c = file_path[i];
			if (c == '\\' || c == '/') {
				break;
			}
		}
		size_t len2 = i + 1;
		char *r = (char *)malloc((len2 + 1) * sizeof(char));
		memcpy(r, file_path, len2);
		r[len2] = 0;
		return r;
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
			arc_reader_unclosed = 0;
#ifdef READLINE
			if (temp && *temp)
				add_history(temp);
#endif

			std::string buf = "(" + input + "\n)";
			const char *p = buf.c_str();
			error err;
			atom result;

			atom code_expr;
			err = read_expr(p, &p, &code_expr);
			if (arc_reader_unclosed > 0) { /* read more lines */
				char *line = readline("  ");
				if (!line) break;
				input += std::string("\n") + line;
				goto read_start;
			}

			if (!err) {
				while (!no(code_expr)) {
					err = macex_eval(car(code_expr), &result);
					if (err) {
						print_error(err);
						break;
					}
					else {
						print_expr(result);
						putchar('\n');
					}
					code_expr = cdr(code_expr);
				}
			}
			free(temp);
		}
	}
}
