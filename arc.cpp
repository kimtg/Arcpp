#include "arc.h"
#include <ctype.h>

namespace arc {
	using namespace std;
	const char* error_string[] = { "", "Syntax error", "Symbol not bound", "Wrong number of arguments", "Wrong type", "File error", "" };
	const atom nil;
	shared_ptr<struct env> global_env = make_shared<struct env>(nullptr); /* the global environment */
	/* symbols for faster execution */
	atom sym_t, sym_quote, sym_quasiquote, sym_unquote, sym_unquote_splicing, sym_assign, sym_fn, sym_if, sym_mac, sym_apply, sym_cons, sym_sym, sym_string, sym_num, sym__, sym_o, sym_table, sym_int, sym_char, sym_do;
	atom err_expr; /* for error reporting */
	atom thrown;
	unordered_map<string, sym> id_of_sym;

	cons::cons(atom car, atom cdr) : car(car), cdr(cdr) {}
	env::env(shared_ptr<struct env> parent) : parent(parent) {}
	closure::closure(const shared_ptr<struct env>& env, atom args, atom body) : parent_env(env), args(args), body(body) {}

	atom vector_to_atom(const vector<atom>& a, int start) {
		atom r = nil;
		int i;
		for (i = a.size() - 1; i >= start; i--) {
			r = make_cons(a[i], r);
		}
		return r;
	}

	vector<atom> atom_to_vector(atom a) {
		vector<atom> r;
		for (; !no(a); a = cdr(a)) {
			r.push_back(car(a));
		}
		return r;
	}

	atom& car(const atom& a) {
		return get<shared_ptr<struct cons>>(a.val)->car;
	}

	atom& cdr(const atom& a) {
		return get<shared_ptr<struct cons>>(a.val)->cdr;
	}

	bool no(const atom& a) {
		return a.type == arc::T_NIL;
	}

	bool sym_is(const atom& a, const atom& b) {
		return get<sym>(a.val) == get<sym>(b.val);
	}

	bool operator ==(const atom& a, const atom& b) {
		return iso(a, b);
	}

	atom make_cons(const atom& car_val, const atom& cdr_val)
	{
		atom a;
		a.type = T_CONS;
		a.val = make_shared<cons>(car_val, cdr_val);
		return a;
	}

	atom make_number(double x)
	{
		atom a;
		a.type = T_NUM;
		a.val = x;
		return a;
	}

	atom make_sym(const string& s)
	{
		atom a;
		a.type = T_SYM;

		auto found = id_of_sym.find(s);
		if (found != id_of_sym.end()) {
			a.val = found->second;
			return a;
		}

		a.val = new string(s);
		id_of_sym[s] = get<sym>(a.val);
		return a;
	}

	atom make_builtin(builtin fn)
	{
		atom a;
		a.type = T_BUILTIN;
		a.val = fn;
		return a;
	}

	error make_closure(const shared_ptr<struct env>& env, atom args, atom body, atom* result)
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
		result->val = make_shared<struct closure>(env, args, body);

		return ERROR_OK;
	}

	atom make_string(const string& x)
	{
		atom a;
		a.type = T_STRING;
		a.val = make_shared<string>(x);
		return a;
	}

	atom make_input(FILE* fp) {
		atom a;
		a.type = T_INPUT;
		a.val = fp;
		return a;
	}

	atom make_input_pipe(FILE* fp) {
		atom a;
		a.type = T_INPUT_PIPE;
		a.val = fp;
		return a;
	}

	atom make_output(FILE* fp) {
		atom a;
		a.type = T_OUTPUT;
		a.val = fp;
		return a;
	}

	atom make_char(char c) {
		atom a;
		a.type = T_CHAR;
		a.val = c;
		return a;
	}

	void print_expr(const atom& a)
	{
		cout << to_string(a, 1);
	}

	void pr(const atom& a)
	{
		cout << to_string(a, 0);
	}

	error lex(const char* str, const char** start, const char** end)
	{
		const char* ws = " \t\r\n";
		const char* delim = "()[] \t\r\n;";
		const char* prefix = "()[]'`";
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
			while (1) {
				if (*str == 0) return ERROR_FILE; /* string not terminated */
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

	error parse_simple(const char* start, const char* end, atom* result)
	{
		char* p;

		/* Is it a number? */
		double val = strtod(start, &p);
		if (p == end) {
			*result = make_number(val);
			return ERROR_OK;
		}
		else if (start[0] == '"') { /* "string" */
			result->type = T_STRING;
			size_t length = end - start - 2;
			char* buf = (char*)malloc(length + 1);
			const char* ps = start + 1;
			char* pt = buf;
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
			buf = (char*)realloc(buf, pt - buf + 1);
			*result = make_string(buf);
			free(buf);
			return ERROR_OK;
		}
		else if (start[0] == '#') { /* #\char */
			char* buf = (char*)malloc(end - start + 1);
			memcpy(buf, start, end - start);
			buf[end - start] = 0;
			size_t length = end - start;
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
		char* buf = (char*)malloc(end - start + 1);
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
					if (err) {
						free(buf);
						return ERROR_SYNTAX;
					}
					err = parse_simple(buf + i + 1, buf + length, &a2);
					if (err) {
						free(buf);
						return ERROR_SYNTAX;
					}
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
					if (err) {
						free(buf);
						return ERROR_SYNTAX;
					}
					err = parse_simple(buf + i + 1, buf + length, &a2);
					if (err) {
						free(buf);
						return ERROR_SYNTAX;
					}
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
					if (err) {
						free(buf);
						return ERROR_SYNTAX;
					}
					err = parse_simple(buf + i + 1, buf + length, &a2);
					if (err) {
						free(buf);
						return ERROR_SYNTAX;
					}
					free(buf);
					*result = make_cons(make_sym("compose"), make_cons(a1, make_cons(a2, nil)));
					return ERROR_OK;
				}
			}
			if (length >= 2 && buf[0] == '~') { /* ~a => (complement a) */
				atom a1;
				error err = parse_simple(buf + 1, buf + length, &a1);
				free(buf);
				if (err) return ERROR_SYNTAX;
				*result = make_cons(make_sym("complement"), make_cons(a1, nil));
				return ERROR_OK;
			}
			*result = make_sym(buf);
		}

		free(buf);
		return ERROR_OK;
	}

	error read_list(const char* start, const char** end, atom* result)
	{
		atom p;

		*end = start;
		p = *result = nil;

		for (;;) {
			const char* token;
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
	error read_bracket(const char* start, const char** end, atom* result)
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
			const char* token;
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

	error read_expr(const char* input, const char** end, atom* result)
	{
		const char* token;
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
			*result = make_cons(sym_quote, make_cons(nil, nil));
			return read_expr(*end, end, &car(cdr(*result)));
		}
		else if (token[0] == '`') {
			*result = make_cons(sym_quasiquote, make_cons(nil, nil));
			return read_expr(*end, end, &car(cdr(*result)));
		}
		else if (token[0] == ',') {
			*result = make_cons(
				token[1] == '@' ? sym_unquote_splicing : sym_unquote,
				make_cons(nil, nil));
			return read_expr(*end, end, &car(cdr(*result)));
		}
		else
			return parse_simple(token, *end, result);
	}

#ifndef READLINE
	char* readline(const char* prompt) {
		return readline_fp(prompt, stdin);
	}
#endif /* READLINE */

	char* readline_fp(const char* prompt, FILE* fp) {
		size_t size = 80;
		/* The size is extended by the input with the value of the provisional */
		char* str;
		int ch;
		size_t len = 0;
		printf("%s", prompt);
		str = (char*)malloc(sizeof(char) * size); /* size is start size */
		if (!str) return NULL;
		while (EOF != (ch = fgetc(fp)) && ch != '\n') {
			str[len++] = ch;
			if (len == size) {
				char* p = (char*)realloc(str, sizeof(char) * (size *= 2));
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

		return (char*)realloc(str, sizeof(char) * len);
	}

	error env_get(shared_ptr<struct env> env, sym symbol, atom* result)
	{
		while (1) {
			auto& tbl = env->table;
			auto found = tbl.find(symbol);
			if (found != tbl.end()) {
				*result = found->second;
				return ERROR_OK;
			}
			env = env->parent;
			if (env == nullptr) {
				/*printf("%s: ", symbol.p.symbol);*/
				return ERROR_UNBOUND;
			}
		}
	}

	error env_assign(const shared_ptr<struct env>& env, sym symbol, const atom &value) {
		auto& tbl = env->table;
		tbl[symbol] = value;
		return ERROR_OK;
	}

	error env_assign_eq(shared_ptr<struct env> env, sym symbol, const atom &value) {
		while (1) {
			auto& tbl = env->table;
			auto found = tbl.find(symbol);
			if (found != tbl.end()) {
				found->second = value;
				return ERROR_OK;
			}
			auto& parent = env->parent;
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
				return ret + 1;
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

	error destructuring_bind(atom arg_name, atom val, int val_unspecified, const shared_ptr<struct env>& env) {
		switch (arg_name.type) {
		case T_SYM:
			return env_assign(env, get<sym>(arg_name.val), val);
		case T_CONS:
			if (is(car(arg_name), sym_o)) { /* (o ARG [DEFAULT]) */
				if (val_unspecified) { /* missing argument */
					if (!no(cdr(cdr(arg_name)))) {
						error err = eval_expr(car(cdr(cdr(arg_name))), env, &val);
						if (err) {
							return err;
						}
					}
				}
				return env_assign(env, get<sym>(car(cdr(arg_name)).val), val);
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
		case T_NIL:
			if (no(val))
				return ERROR_OK;
			else {
				return ERROR_ARGS;
			}
		default:
			return ERROR_ARGS;
		}
	}

	error env_bind(const shared_ptr<struct env>& env, atom arg_names, const vector<atom>& vargs) {
		size_t i = 0;
		while (!no(arg_names)) {
			if (arg_names.type == T_SYM) {
				env_assign(env, get<sym>(arg_names.val), vector_to_atom(vargs, i));
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

	error apply(const atom& fn, const vector<atom>& vargs, atom* result)
	{
		if (fn.type == T_BUILTIN)
			return get<builtin>(fn.val)(vargs, result);
		else if (fn.type == T_CLOSURE) {
			struct closure cls = *get<shared_ptr<struct closure>>(fn.val);
			shared_ptr<struct env> env = make_shared<struct env>(cls.parent_env);
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
			longjmp(*get<jmp_buf*>(fn.val), 1);
		}
		else if (fn.type == T_STRING) { /* implicit indexing for string */
			if (vargs.size() != 1) return ERROR_ARGS;
			long index = (long)(get<double>(vargs[0].val));
			*result = make_char(fn.asp<string>()[index]);
			return ERROR_OK;
		}
		else if (fn.type == T_CONS && listp(fn)) { /* implicit indexing for list */
			if (vargs.size() != 1) return ERROR_ARGS;
			long index = (long)(get<double>(vargs[0].val));
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
			auto& tbl = *get<shared_ptr<table>>(fn.val);
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
	error builtin_car(const vector<atom>& vargs, atom* result)
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

	error builtin_cdr(const vector<atom>& vargs, atom* result)
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

	error builtin_cons(const vector<atom>& vargs, atom* result)
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
	error builtin_add(const vector<atom>& vargs, atom* result)
	{
		if (vargs.size() == 0) {
			*result = make_number(0);
		}
		else {
			if (vargs[0].type == T_NUM) {
				double r = get<double>(vargs[0].val);
				size_t i;
				for (i = 1; i < vargs.size(); i++) {
					if (vargs[i].type != T_NUM) return ERROR_TYPE;
					r += get<double>(vargs[i].val);
				}
				*result = make_number(r);
			}
			else if (vargs[0].type == T_STRING) {
				string buf;
				size_t i;
				for (i = 0; i < vargs.size(); i++) {
					string s = to_string(vargs[i], 0);
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

	error builtin_subtract(const vector<atom>& vargs, atom* result)
	{
		if (vargs.size() == 0) { /* 0 argument */
			*result = make_number(0);
			return ERROR_OK;
		}
		if (vargs[0].type != T_NUM) return ERROR_TYPE;
		if (vargs.size() == 1) { /* 1 argument */
			*result = make_number(-get<double>(vargs[0].val));
			return ERROR_OK;
		}
		double r = get<double>(vargs[0].val);
		size_t i;
		for (i = 1; i < vargs.size(); i++) {
			if (vargs[i].type != T_NUM) return ERROR_TYPE;
			r -= get<double>(vargs[i].val);
		}
		*result = make_number(r);
		return ERROR_OK;
	}

	error builtin_multiply(const vector<atom>& vargs, atom* result)
	{
		double r = 1;
		size_t i;
		for (i = 0; i < vargs.size(); i++) {
			if (vargs[i].type != T_NUM) return ERROR_TYPE;
			r *= get<double>(vargs[i].val);
		}
		*result = make_number(r);
		return ERROR_OK;
	}

	error builtin_divide(const vector<atom>& vargs, atom* result)
	{
		if (vargs.size() == 0) { /* 0 argument */
			*result = make_number(1);
			return ERROR_OK;
		}
		if (vargs[0].type != T_NUM) return ERROR_TYPE;
		if (vargs.size() == 1) { /* 1 argument */
			*result = make_number(1.0 / get<double>(vargs[0].val));
			return ERROR_OK;
		}
		double r = get<double>(vargs[0].val);
		size_t i;
		for (i = 1; i < vargs.size(); i++) {
			if (vargs[i].type != T_NUM) return ERROR_TYPE;
			r /= get<double>(vargs[i].val);
		}
		*result = make_number(r);
		return ERROR_OK;
	}

	error builtin_less(const vector<atom>& vargs, atom* result)
	{
		if (vargs.size() <= 1) {
			*result = sym_t;
			return ERROR_OK;
		}
		size_t i;
		switch (vargs[0].type) {
		case T_NUM:
			for (i = 0; i < vargs.size() - 1; i++) {
				if (get<double>(vargs[i].val) >= get<double>(vargs[i + 1].val)) {
					*result = nil;
					return ERROR_OK;
				}
			}
			*result = sym_t;
			return ERROR_OK;
		case T_STRING:
			for (i = 0; i < vargs.size() - 1; i++) {
				if (vargs[i].asp<string>() >= vargs[i + 1].asp<string>()) {
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

	error builtin_greater(const vector<atom>& vargs, atom* result)
	{
		if (vargs.size() <= 1) {
			*result = sym_t;
			return ERROR_OK;
		}
		size_t i;
		switch (vargs[0].type) {
		case T_NUM:
			for (i = 0; i < vargs.size() - 1; i++) {
				if (get<double>(vargs[i].val) <= get<double>(vargs[i + 1].val)) {
					*result = nil;
					return ERROR_OK;
				}
			}
			*result = sym_t;
			return ERROR_OK;
		case T_STRING:
			for (i = 0; i < vargs.size() - 1; i++) {
				if (vargs[i].asp<string>() <= vargs[i + 1].asp<string>()) {
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

	error builtin_apply(const vector<atom>& vargs, atom* result)
	{
		atom fn;

		if (vargs.size() != 2)
			return ERROR_ARGS;

		fn = vargs[0];
		auto v = atom_to_vector(vargs[1]);
		return apply(fn, v, result);
	}

	bool is(const atom& a, const atom& b) {
		if (a.type == b.type) {
			switch (a.type) {
			case T_NIL:
				return true;
			case T_CONS:
				return get<shared_ptr<struct cons>>(a.val) == get<shared_ptr<struct cons>>(b.val); // compare pointers
			case T_CLOSURE:
			case T_MACRO:
				return get<shared_ptr<struct closure>>(a.val) == get<shared_ptr<struct closure>>(b.val); // compare pointers
			case T_BUILTIN:
				return get<builtin>(a.val) == get<builtin>(b.val);
			case T_SYM:
				return get<sym>(a.val) == get<sym>(b.val);
			case T_NUM:
				return get<double>(a.val) == get<double>(b.val);
			case T_STRING:
				return a.asp<string>() == b.asp<string>();
			case T_CHAR:
				return get<char>(a.val) == get<char>(b.val);
			case T_TABLE:
				return get<shared_ptr<table>>(a.val) == get<shared_ptr<table>>(b.val); // compare pointers
			case T_INPUT:
			case T_INPUT_PIPE:
			case T_OUTPUT:
				return get<FILE*>(a.val) == get<FILE*>(b.val);
			case T_CONTINUATION:
				return get<jmp_buf*>(a.val) == get<jmp_buf*>(b.val);
			}
		}
		return false;
	}

	bool iso(const atom& a, const atom& b) {
		if (a.type == b.type) {
			switch (a.type) {
			case T_CONS:
				return iso(car(a), car(b)) && iso(cdr(a), cdr(b));
			default:
				return is(a, b);
			}
		}
		return 0;
	}

	error builtin_is(const vector<atom>& vargs, atom* result)
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

	error builtin_scar(const vector<atom>& vargs, atom* result) {
		if (vargs.size() != 2) return ERROR_ARGS;
		atom place = vargs[0], value;
		if (place.type != T_CONS) return ERROR_TYPE;
		value = vargs[1];
		car(place) = value;
		*result = value;
		return ERROR_OK;
	}

	error builtin_scdr(const vector<atom>& vargs, atom* result) {
		if (vargs.size() != 2) return ERROR_ARGS;
		atom place = vargs[0], value;
		if (place.type != T_CONS) return ERROR_TYPE;
		value = vargs[1];
		cdr(place) = value;
		*result = value;
		return ERROR_OK;
	}

	error builtin_mod(const vector<atom>& vargs, atom* result) {
		if (vargs.size() != 2) return ERROR_ARGS;
		atom dividend = vargs[0];
		atom divisor = vargs[1];
		double r = fmod(get<double>(dividend.val), get<double>(divisor.val));
		if (get<double>(dividend.val) * get<double>(divisor.val) < 0 && r != 0) r += get<double>(divisor.val);
		*result = make_number(r);
		return ERROR_OK;
	}

	error builtin_type(const vector<atom>& vargs, atom* result) {
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
		case T_INPUT: *result = make_sym("input"); break;
		case T_INPUT_PIPE: *result = make_sym("input-pipe"); break;
		case T_OUTPUT: *result = make_sym("output"); break;
		default: *result = nil; break; /* impossible */
		}
		return ERROR_OK;
	}

	/* string-sref obj value index */
	error builtin_string_sref(const vector<atom>& vargs, atom* result) {
		atom index, obj, value;
		if (vargs.size() != 3) return ERROR_ARGS;
		obj = vargs[0];
		if (obj.type != T_STRING) return ERROR_TYPE;
		value = vargs[1];
		index = vargs[2];
		obj.asp<string>()[(long)get<double>(index.val)] = get<char>(value.val);
		*result = make_char(get<char>(value.val));
		return ERROR_OK;
	}

	/* disp [arg [output-port]] */
	error builtin_disp(const vector<atom>& vargs, atom* result) {
		long l = vargs.size();
		FILE* fp;
		switch (l) {
		case 0:
			*result = nil;
			return ERROR_OK;
		case 1:
			fp = stdout;
			break;
		case 2:
			fp = get<FILE*>(vargs[1].val);
			break;
		default:
			return ERROR_ARGS;
		}
		fprintf(fp, "%s", to_string(vargs[0], 0).c_str());
		*result = nil;
		return ERROR_OK;
	}

	error builtin_writeb(const vector<atom>& vargs, atom* result) {
		long l = vargs.size();
		FILE* fp;
		switch (l) {
		case 0: return ERROR_ARGS;
		case 1:
			fp = stdout;
			break;
		case 2:
			fp = get<FILE*>(vargs[1].val);
			break;
		default: return ERROR_ARGS;
		}
		fputc((int)get<double>(vargs[0].val), fp);
		*result = nil;
		return ERROR_OK;
	}

	error builtin_expt(const vector<atom>& vargs, atom* result) {
		atom a, b;
		if (vargs.size() != 2) return ERROR_ARGS;
		a = vargs[0];
		b = vargs[1];
		*result = make_number(pow(get<double>(a.val), get<double>(b.val)));
		return ERROR_OK;
	}

	error builtin_log(const vector<atom>& vargs, atom* result) {
		atom a;
		if (vargs.size() != 1) return ERROR_ARGS;
		a = vargs[0];
		*result = make_number(log(get<double>(a.val)));
		return ERROR_OK;
	}

	error builtin_sqrt(const vector<atom>& vargs, atom* result) {
		atom a;
		if (vargs.size() != 1) return ERROR_ARGS;
		a = vargs[0];
		*result = make_number(sqrt(get<double>(a.val)));
		return ERROR_OK;
	}

	error builtin_readline(const vector<atom>& vargs, atom* result) {
		long l = vargs.size();
		char* str;
		if (l == 0) {
			str = readline("");
		}
		else if (l == 1) {
			if (vargs[0].type != T_INPUT && vargs[0].type != T_INPUT_PIPE) return ERROR_TYPE;
			str = readline_fp("", get<FILE*>(vargs[0].val));
		}
		else {
			return ERROR_ARGS;
		}
		if (str == NULL) *result = nil; else *result = make_string(str);
		return ERROR_OK;
	}

	error builtin_quit(const vector<atom>& vargs, atom* result) {
		if (vargs.size() != 0) return ERROR_ARGS;
		exit(0);
	}

	double rand_double() {
		return (double)rand() / ((double)RAND_MAX + 1.0);
	}

	error builtin_rand(const vector<atom>& vargs, atom* result) {
		long alen = vargs.size();
		if (alen == 0) *result = make_number(rand_double());
		else if (alen == 1) *result = make_number(floor(rand_double() * get<double>(vargs[0].val)));
		else return ERROR_ARGS;
		return ERROR_OK;
	}

	error read_fp(FILE* fp, atom* result) {
		char* s = readline_fp("", fp);
		if (s == NULL) return ERROR_FILE;
		const char* buf = s;
		error err = read_expr(buf, &buf, result);

		/* bring back remaining expressions so that "(read) (read)" works */
		if (buf) {
			if (*buf) ungetc('\n', fp);
			const char* b0 = buf;
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
	error builtin_read(const vector<atom>& vargs, atom* result) {
		size_t alen = vargs.size();
		error err;
		if (alen == 0) {
			err = read_fp(stdin, result);
		}
		else if (alen <= 2) {
			atom src = vargs[0];
			if (src.type == T_STRING) {
				const char* s = src.asp<string>().c_str();
				const char* buf = s;
				err = read_expr(buf, &buf, result);
			}
			else if (src.type == T_INPUT || src.type == T_INPUT_PIPE) {
				err = read_fp(get<FILE*>(src.val), result);
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

	error builtin_macex(const vector<atom>& vargs, atom* result) {
		long alen = vargs.size();
		if (alen == 1) {
			error err = macex(vargs[0], result);
			return err;
		}
		else return ERROR_ARGS;
		return ERROR_OK;
	}

	error builtin_string(const vector<atom>& vargs, atom* result) {
		string s;
		size_t i;
		for (i = 0; i < vargs.size(); i++) {
			s += to_string(vargs[i], 0);
		}
		*result = make_string(s);
		return ERROR_OK;
	}

	error builtin_sym(const vector<atom>& vargs, atom* result) {
		long alen = vargs.size();
		if (alen == 1) {
			*result = make_sym(to_string(vargs[0], 0));
			return ERROR_OK;
		}
		else return ERROR_ARGS;
	}

	error builtin_system(const vector<atom>& vargs, atom* result) {
		long alen = vargs.size();
		if (alen == 1) {
			atom a = vargs[0];
			if (a.type != T_STRING) return ERROR_TYPE;
			*result = make_number(system(vargs[0].asp<string>().c_str()));
			return ERROR_OK;
		}
		else return ERROR_ARGS;
	}

	error builtin_eval(const vector<atom>& vargs, atom* result) {
		if (vargs.size() == 1) return macex_eval(vargs[0], result);
		else return ERROR_ARGS;
	}

	error builtin_load(const vector<atom>& vargs, atom* result) {
		if (vargs.size() == 1) {
			atom a = vargs[0];
			if (a.type != T_STRING) return ERROR_TYPE;
			*result = nil;
			return arc_load_file(a.asp<string>().c_str());
		}
		else return ERROR_ARGS;
	}

	error builtin_int(const vector<atom>& vargs, atom* result) {
		if (vargs.size() == 1) {
			atom a = vargs[0];
			switch (a.type) {
			case T_STRING:
				*result = make_number(atol(a.asp<string>().c_str()));
				break;
			case T_SYM:
				*result = make_number(atol(get<sym>(a.val)->c_str()));
				break;
			case T_NUM:
				*result = make_number((long)(get<double>(a.val)));
				break;
			case T_CHAR:
				*result = make_number(get<char>(a.val));
				break;
			default:
				return ERROR_TYPE;
			}
			return ERROR_OK;
		}
		else return ERROR_ARGS;
	}

	error builtin_trunc(const vector<atom>& vargs, atom* result) {
		if (vargs.size() == 1) {
			atom a = vargs[0];
			if (a.type != T_NUM) return ERROR_TYPE;
			*result = make_number(trunc(get<double>(a.val)));
			return ERROR_OK;
		}
		else return ERROR_ARGS;
	}

	error builtin_sin(const vector<atom>& vargs, atom* result) {
		if (vargs.size() == 1) {
			atom a = vargs[0];
			if (a.type != T_NUM) return ERROR_TYPE;
			*result = make_number(sin(get<double>(a.val)));
			return ERROR_OK;
		}
		else return ERROR_ARGS;
	}

	error builtin_cos(const vector<atom>& vargs, atom* result) {
		if (vargs.size() == 1) {
			atom a = vargs[0];
			if (a.type != T_NUM) return ERROR_TYPE;
			*result = make_number(cos(get<double>(a.val)));
			return ERROR_OK;
		}
		else return ERROR_ARGS;
	}

	error builtin_tan(const vector<atom>& vargs, atom* result) {
		if (vargs.size() == 1) {
			atom a = vargs[0];
			if (a.type != T_NUM) return ERROR_TYPE;
			*result = make_number(tan(get<double>(a.val)));
			return ERROR_OK;
		}
		else return ERROR_ARGS;
	}

	error builtin_bound(const vector<atom>& vargs, atom* result) {
		if (vargs.size() == 1) {
			atom a = vargs[0];
			if (a.type != T_SYM) return ERROR_TYPE;
			error err = env_get(global_env, get<sym>(a.val), result);
			*result = (err ? nil : sym_t);
			return ERROR_OK;
		}
		else return ERROR_ARGS;
	}

	error builtin_infile(const vector<atom>& vargs, atom* result) {
		if (vargs.size() == 1) {
			atom a = vargs[0];
			if (a.type != T_STRING) return ERROR_TYPE;
			FILE* fp = fopen(a.asp<string>().c_str(), "r");
			*result = make_input(fp);
			return ERROR_OK;
		}
		else return ERROR_ARGS;
	}

	error builtin_outfile(const vector<atom>& vargs, atom* result) {
		if (vargs.size() == 1) {
			atom a = vargs[0];
			if (a.type != T_STRING) return ERROR_TYPE;
			FILE* fp = fopen(a.asp<string>().c_str(), "w");
			*result = make_output(fp);
			return ERROR_OK;
		}
		else return ERROR_ARGS;
	}

	/* close port ... */
	error builtin_close(const vector<atom>& vargs, atom* result) {
		if (vargs.size() >= 1) {
			for (atom a : vargs) {
				if (a.type != T_INPUT && a.type != T_INPUT_PIPE && a.type != T_OUTPUT) return ERROR_TYPE;
				if (a.type == T_INPUT_PIPE)
					pclose(get<FILE*>(a.val));
				else
					fclose(get<FILE*>(a.val));
			}
			*result = nil;
			return ERROR_OK;
		}
		else return ERROR_ARGS;
	}

	error builtin_readb(const vector<atom>& vargs, atom* result) {
		long l = vargs.size();
		FILE* fp;
		switch (l) {
		case 0:
			fp = stdin;
			break;
		case 1:
			fp = get<FILE*>(vargs[0].val);
			break;
		default:
			return ERROR_ARGS;
		}
		*result = make_number(fgetc(fp));
		return ERROR_OK;
	}

	/* sread input-port eof */
	error builtin_sread(const vector<atom>& vargs, atom* result) {
		if (vargs.size() != 2) return ERROR_ARGS;
		FILE* fp = get<FILE*>(vargs[0].val);
		atom eof = vargs[1];
		error err;
		if (feof(fp)) {
			*result = eof;
			return ERROR_OK;
		}
		char* s = slurp_fp(fp);
		const char* p = s;
		err = read_expr(p, &p, result);
		return err;
	}

	/* write [arg [output-port]] */
	error builtin_write(const vector<atom>& vargs, atom* result) {
		long l = vargs.size();
		FILE* fp;
		switch (l) {
		case 0:
			*result = nil;
			return ERROR_OK;
		case 1:
			fp = stdout;
			break;
		case 2:
			fp = get<FILE*>(vargs[1].val);
			break;
		default:
			return ERROR_ARGS;
		}
		atom a = vargs[0];
		if (a.type == T_STRING) fputc('"', fp);
		string s = to_string(a, 1);
		fprintf(fp, "%s", s.c_str());
		if (a.type == T_STRING) fputc('"', fp);
		*result = nil;
		return ERROR_OK;
	}

	/* newstring length [char] */
	error builtin_newstring(const vector<atom>& vargs, atom* result) {
		long arg_len = vargs.size();
		long length = (long)get<double>(vargs[0].val);
		char c = 0;
		char* s;
		switch (arg_len) {
		case 1: break;
		case 2:
			c = get<char>(vargs[1].val);
			break;
		default:
			return ERROR_ARGS;
		}
		s = (char*)malloc((length + 1) * sizeof(char));
		int i;
		for (i = 0; i < length; i++)
			s[i] = c;
		s[length] = 0; /* end of string */
		*result = make_string(s);
		return ERROR_OK;
	}

	error builtin_table(const vector<atom>& vargs, atom* result) {
		long arg_len = vargs.size();
		if (arg_len != 0) return ERROR_ARGS;
		*result = make_table();
		return ERROR_OK;
	}

	/* maptable proc table */
	error builtin_maptable(const vector<atom>& vargs, atom* result) {
		long arg_len = vargs.size();
		if (arg_len != 2) return ERROR_ARGS;
		const atom& proc = vargs[0];
		const atom& tbl = vargs[1];
		if (tbl.type != T_TABLE) return ERROR_TYPE;
		auto& table1 = tbl.asp<table>();
		vector<atom> v;
		for (auto& p : table1) {
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
	error builtin_table_sref(const vector<atom>& vargs, atom* result) {
		atom index, obj, value;
		if (vargs.size() != 3) return ERROR_ARGS;
		obj = vargs[0];
		if (obj.type != T_TABLE) return ERROR_TYPE;
		value = vargs[1];
		index = vargs[2];
		obj.asp<table>()[index] = value;
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
	error builtin_coerce(const vector<atom>& vargs, atom* result) {
		atom obj, type;
		if (vargs.size() != 2) return ERROR_ARGS;
		obj = vargs[0];
		type = vargs[1];
		switch (obj.type) {
		case T_CHAR:
			if (is(type, sym_int) || is(type, sym_num)) *result = make_number(get<char>(obj.val));
			else if (is(type, sym_string)) {
				char buf[2];
				buf[0] = get<char>(obj.val);
				buf[1] = '\0';
				*result = make_string(buf);
			}
			else if (is(type, sym_sym)) {
				char buf[2];
				buf[0] = get<char>(obj.val);
				buf[1] = '\0';
				*result = make_sym(buf);
			}
			else if (is(type, sym_char))
				*result = obj;
			else
				return ERROR_TYPE;
			break;
		case T_NUM:
			if (is(type, sym_int)) *result = make_number(floor(get<double>(obj.val)));
			else if (is(type, sym_char)) *result = make_char((char)get<double>(obj.val));
			else if (is(type, sym_string)) {
				*result = make_string(to_string(obj, 0));
			}
			else if (is(type, sym_num))
				*result = obj;
			else
				return ERROR_TYPE;
			break;
		case T_STRING:
			if (is(type, sym_sym)) *result = make_sym(obj.asp<string>().c_str());
			else if (is(type, sym_cons)) {
				*result = nil;
				int i;
				for (i = strlen(obj.asp<string>().c_str()) - 1; i >= 0; i--) {
					*result = make_cons(make_char(obj.asp<string>().c_str()[i]), *result);
				}
			}
			else if (is(type, sym_num)) *result = make_number(atof(obj.asp<string>().c_str()));
			else if (is(type, sym_int)) *result = make_number(atoi(obj.asp<string>().c_str()));
			else if (is(type, sym_string))
				*result = obj;
			else
				return ERROR_TYPE;
			break;
		case T_CONS:
			if (is(type, sym_string)) {
				string s;
				atom p;
				for (p = obj; !no(p); p = cdr(p)) {
					atom x;
					vector<atom> v; /* (car(p) string) */
					v.push_back(car(p));
					v.push_back(sym_string);
					error err = builtin_coerce(v, &x);
					if (err) return err;
					s += x.asp<string>();
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
				*result = make_string(*get<sym>(obj.val));
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

	error builtin_flushout(const vector<atom>& vargs, atom* result) {
		if (vargs.size() != 0) return ERROR_ARGS;
		fflush(stdout);
		*result = sym_t;
		return ERROR_OK;
	}

	error builtin_err(const vector<atom>& vargs, atom* result) {
		if (vargs.size() == 0) return ERROR_ARGS;
		err_expr = nil;
		size_t i;
		for (i = 0; i < vargs.size(); i++) {
			cout << to_string(vargs[i], 0) << '\n';
		}
		return ERROR_USER;
	}

	error builtin_len(const vector<atom>& vargs, atom* result) {
		if (vargs.size() != 1) return ERROR_ARGS;
		atom a = vargs[0];
		if (a.type == T_STRING) {
			*result = make_number(strlen(a.asp<string>().c_str()));
		}
		else if (a.type == T_TABLE) {
			*result = make_number(a.asp<table>().size());
		}
		else {
			*result = make_number(len(a));
		}
		return ERROR_OK;
	}

	atom make_continuation(jmp_buf* jb) {
		atom a;
		a.type = T_CONTINUATION;
		a.val = jb;
		return a;
	}

	error builtin_ccc(const vector<atom>& vargs, atom* result) {
		if (vargs.size() != 1) return ERROR_ARGS;
		atom a = vargs[0];
		if (a.type != T_BUILTIN && a.type != T_CLOSURE) return ERROR_TYPE;
		jmp_buf jb;
		int val = setjmp(jb);
		if (val) {
			*result = thrown;
			return ERROR_OK;
		}
		vector<atom> args{ make_continuation(&jb) };
		return apply(a, args, result);
	}

	// mvfile source destination
	// Moves the specified file.
	error builtin_mvfile(const vector<atom>& vargs, atom* result) {
		if (vargs.size() != 2) return ERROR_ARGS;
		atom a = vargs[0];
		atom b = vargs[1];
		if (a.type != T_STRING || b.type != T_STRING) return ERROR_TYPE;
		*result = nil;
		int r = rename(a.asp<string>().c_str(), b.asp<string>().c_str());
		if (r != 0) {
			return ERROR_FILE;
		}
		return ERROR_OK;
	}

	// rmfile path
	// Removes the specified file.
	error builtin_rmfile(const vector<atom>& vargs, atom* result) {
		if (vargs.size() != 1) return ERROR_ARGS;
		atom a = vargs[0];
		if (a.type != T_STRING) return ERROR_TYPE;
		*result = nil;
		int r = remove(a.asp<string>().c_str());
		if (r != 0) {
			return ERROR_FILE;
		}
		return ERROR_OK;
	}

	// dir path
	// Returns the directory contents as a list.
	error builtin_dir(const vector<atom>& vargs, atom* result) {
		if (vargs.size() != 1) return ERROR_ARGS;
		atom a = vargs[0];
		if (a.type != T_STRING) return ERROR_TYPE;
		const string& path = a.asp<string>();
		if (path.length() == 0) return ERROR_FILE;
		*result = nil;
		for (auto& p : filesystem::directory_iterator(path)) {
			*result = make_cons(make_string(p.path().string()), *result);
		}
		return ERROR_OK;
	}

	// dir-exists path
	// Tests if a directory exists.
	error builtin_dir_exists(const vector<atom>& vargs, atom* result) {
		if (vargs.size() != 1) return ERROR_ARGS;
		atom a = vargs[0];
		if (a.type != T_STRING) return ERROR_TYPE;
		const string& path = a.asp<string>();
		if (path.length() == 0) return ERROR_FILE;

		*result = nil;
		if (filesystem::exists(path) && filesystem::is_directory(path)) {
			*result = sym_t;
		}
		return ERROR_OK;
	}

	// file-exists path
	// Tests if a file exists.
	error builtin_file_exists(const vector<atom>& vargs, atom* result) {
		if (vargs.size() != 1) return ERROR_ARGS;
		atom a = vargs[0];
		if (a.type != T_STRING) return ERROR_TYPE;
		const string& path = a.asp<string>();
		if (path.length() == 0) return ERROR_FILE;

		*result = nil;
		if (filesystem::exists(path) && filesystem::is_regular_file(path)) {
			*result = sym_t;
		}
		return ERROR_OK;
	}

	// ensure-dir path
	// Creates the specified directory, if it doesn't exist.
	error builtin_ensure_dir(const vector<atom>& vargs, atom* result) {
		if (vargs.size() != 1) return ERROR_ARGS;
		atom a = vargs[0];
		if (a.type != T_STRING) return ERROR_TYPE;
		const string& path = a.asp<string>();
		if (path.length() == 0) return ERROR_FILE;

		*result = nil;
		filesystem::create_directories(path);
		return ERROR_OK;
	}

	/* pipe-from command
	 * Executes command in the underlying OS. Then opens an input-port to the results.
	 */
	error builtin_pipe_from(const vector<atom>& vargs, atom* result) {
		if (vargs.size() != 1) return ERROR_ARGS;
		atom a = vargs[0];
		if (a.type != T_STRING) return ERROR_TYPE;
		FILE* fp = popen(vargs[0].asp<string>().c_str(), "r");
		if (fp == nullptr) return ERROR_FILE;
		*result = make_input_pipe(fp);
		return ERROR_OK;
	}

	/* end builtin */

	string to_string(atom a, int write) {
		string s;
		switch (a.type) {
		case T_NIL:
			s = "nil";
			break;
		case T_CONS: {
			if (listp(a) && len(a) == 2) {
				if (is(car(a), sym_quote)) {
					s = "'" + to_string(car(cdr(a)), write);
					break;
				}
				else if (is(car(a), sym_quasiquote)) {
					s = "`" + to_string(car(cdr(a)), write);
					break;
				}
				else if (is(car(a), sym_unquote)) {
					s = "," + to_string(car(cdr(a)), write);
					break;
				}
				else if (is(car(a), sym_unquote_splicing)) {
					s = ",@" + to_string(car(cdr(a)), write);
					break;
				}
			}
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
		}
		case T_SYM:
			s = *get<sym>(a.val);
			break;
		case T_STRING:
			if (write) s += "\"";
			s += a.asp<string>();
			if (write) s += "\"";
			break;
		case T_NUM:
		{
			stringstream ss;
			ss << setprecision(16) << get<double>(a.val);
			s = ss.str();
			break;
		}
		case T_BUILTIN:
		{
			stringstream ss;
			ss << "#<builtin:" << (void*)get<builtin>(a.val) << ">";
			s = ss.str();
			break;
		}
		case T_CLOSURE:
		{
			atom a2 = make_cons(sym_fn, make_cons(a.asp<struct closure>().args, a.asp<struct closure>().body));
			s = "#<closure>" + to_string(a2, 1);
			break;
		}
		case T_MACRO:
			s = "#<macro:" + to_string(a.asp<struct closure>().args, write) +
				" " + to_string(a.asp<struct closure>().body, write) + ">";
			break;
		case T_INPUT:
			s = "#<input>";
			break;
		case T_INPUT_PIPE:
			s = "#<input-pipe>";
			break;
		case T_OUTPUT:
			s = "#<output>";
			break;
		case T_TABLE: {
			s += "#<table:(";
			for (auto& p : a.asp<table>()) {
				s += "(" + to_string(p.first, write) + " . " + to_string(p.second, write) + ")";
			}
			s += ")>";
			break; }
		case T_CHAR:
			if (write) {
				s += "#\\";
				switch (get<char>(a.val)) {
				case '\0': s += "nul"; break;
				case '\r': s += "return"; break;
				case '\n': s += "newline"; break;
				case '\t': s += "tab"; break;
				case ' ': s += "space"; break;
				default:
					s += get<char>(a.val);
				}
			}
			else {
				s[0] = get<char>(a.val);
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
		a.val = make_shared<table>();
		return a;
	}

	char* slurp_fp(FILE* fp) {
		char* buf;
		size_t len;

		fseek(fp, 0, SEEK_END);
		len = ftell(fp);
		if (len < 0) return NULL;
		fseek(fp, 0, SEEK_SET);

		buf = (char*)malloc(len + 1);
		if (!buf)
			return NULL;

		if (fread(buf, 1, len, fp) != len) return NULL;
		buf[len] = 0;

		return buf;
	}

	char* slurp(const char* path)
	{
		FILE* fp = fopen(path, "rb");
		if (!fp) {
			/* printf("Reading %s failed.\n", path); */
			return NULL;
		}
		char* r = slurp_fp(fp);
		fclose(fp);
		return r;
	}

	/* compile-time macro */
	error macex(atom expr, atom* result) {
		error err = ERROR_OK;

		err_expr = expr;

		if (expr.type != T_CONS || !listp(expr)) {
			*result = expr;
			return ERROR_OK;
		}
		else {
			atom op = car(expr);

			/* Handle quote */
			if (op.type == T_SYM && get<sym>(op.val) == get<sym>(sym_quote.val)) {
				*result = expr;
				return ERROR_OK;
			}

			atom args = cdr(expr);

			/* Is it a macro? */
			if (op.type == T_SYM && !env_get(global_env, get<sym>(op.val), result) && result->type == T_MACRO) {
				/* Evaluate operator */
				op = *result;

				op.type = T_CLOSURE;

				atom result2;
				vector<atom> vargs = atom_to_vector(args);
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

	error macex_eval(atom expr, atom* result) {
		atom expr2;
		error err = macex(expr, &expr2);
		if (err) return err;
		/*printf("expanded: ");
		print_expr(expr2);
		puts("");*/
		return eval_expr(expr2, global_env, result);
	}

	error load_string(const char* text) {
		error err = ERROR_OK;
		const char* p = text;
		atom expr;
		while (*p) {
			if (isspace(*p)) {
				p++;
				continue;
			}
			/* comment */
			if (*p == ';') {
				p += strcspn(p, "\n");
				continue;
			}
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
		}
		//puts("");
		return err;
	}

	error arc_load_file(const char* path)
	{
		char* text;
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

	error eval_expr(atom expr, shared_ptr<struct env> env, atom* result)
	{
		error err;
	start_eval:

		if (expr.type == T_SYM) {
			err = env_get(env, get<sym>(expr.val), result);
			if (err) err_expr = expr;
			return err;
		}
		else if (expr.type != T_CONS) {
			*result = expr;
			return ERROR_OK;
		}
		else if (!listp(expr)) {
			err_expr = expr;
			return ERROR_SYNTAX;
		}
		else {
			const atom& op = car(expr);
			atom args = cdr(expr);

			if (op.type == T_SYM) {
				/* Handle special forms */
				if (sym_is(op, sym_if)) {
					while (!no(args)) {
						if (no(cdr(args))) { /* else */
							/* tail call optimization of else part */
							expr = car(args);
							goto start_eval;
						}
						err = eval_expr(car(args), env, result);
						if (err) {
							err_expr = expr;
							return err;
						}
						if (!no(*result)) { /* then */
							/* tail call optimization of err = eval_expr(car(cdr(args)), env, result); */
							expr = car(cdr(args));
							goto start_eval;
						}
						args = cdr(cdr(args));
					}
					return ERROR_OK;
				}
				else if (sym_is(op, sym_assign)) {
					atom sym1;
					if (no(args) || no(cdr(args))) {
						err_expr = expr;
						return ERROR_ARGS;
					}

					sym1 = car(args);
					if (sym1.type == T_SYM) {
						err = eval_expr(car(cdr(args)), env, result);
						if (err) {
							return err;
						}
						err = env_assign_eq(env, get<sym>(sym1.val), *result);
						if (err) err_expr = expr;
						return err;
					}
					else {
						err_expr = expr;
						return ERROR_TYPE;
					}
				}
				else if (sym_is(op, sym_quote)) {
					if (no(args) || !no(cdr(args))) {
						err_expr = expr;
						return ERROR_ARGS;
					}

					*result = car(args);
					return ERROR_OK;
				}
				else if (sym_is(op, sym_fn)) {
					if (no(args)) {
						err_expr = expr;
						return ERROR_ARGS;
					}
					err = make_closure(env, car(args), cdr(args), result);
					if (err) err_expr = expr;
					return err;
				}
				else if (sym_is(op, sym_do)) {
					/* Evaluate the body */
					*result = nil;
					while (!no(args)) {
						if (no(cdr(args))) {
							/* tail call */
							expr = car(args);
							goto start_eval;
						}
						error err = eval_expr(car(args), env, result);
						if (err) {
							err_expr = expr;
							return err;
						}
						args = cdr(args);
					}
					return ERROR_OK;
				}
				else if (sym_is(op, sym_mac)) { /* (mac name (arg ...) body) */
					atom name, macro;

					if (no(args) || no(cdr(args)) || no(cdr(cdr(args)))) {
						err_expr = expr;
						return ERROR_ARGS;
					}

					name = car(args);
					if (name.type != T_SYM) {
						err_expr = expr;
						return ERROR_TYPE;
					}

					err = make_closure(env, car(cdr(args)), cdr(cdr(args)), &macro);
					if (!err) {
						macro.type = T_MACRO;
						*result = name;
						err = env_assign(env, get<sym>(name.val), macro);
						if (err) err_expr = expr;
						return err;
					}
					else {
						err_expr = expr;
						return err;
					}
				}
			}

			/* Evaluate operator */
			atom fn;
			err = eval_expr(op, env, &fn);
			if (err) {
				err_expr = expr;
				return err;
			}

			/* Evaulate arguments */
			vector<atom> vargs;
			atom* p = &args;
			while (!no(*p)) {
				atom r;
				err = eval_expr(car(*p), env, &r);
				if (err) {
					err_expr = expr;
					return err;
				}
				vargs.push_back(r);
				p = &cdr(*p);
			}

			/* tail call optimization of err = apply(fn, args, result); */
			if (fn.type == T_CLOSURE) {
				struct closure cls = fn.asp<struct closure>();
				env = make_shared<struct env>(cls.parent_env);
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
						err_expr = expr;
						return err;
					}
					body = cdr(body);
				}
				return ERROR_OK;
			}
			else {
				err = apply(fn, vargs, result);
				if (err) err_expr = expr;
			}
			return err;
		}
	}

	void bind_global(const string& name, const atom &a) {
		env_assign(global_env, get<sym>(make_sym(name).val), a);
	}

	void arc_init() {
#ifdef READLINE
		rl_bind_key('\t', rl_insert); /* prevent tab completion */
#endif
		srand((unsigned int)time(0));

		/* Set up the initial environment */
		sym_t = make_sym("t");
		sym_quote = make_sym("quote");
		sym_quasiquote = make_sym("quasiquote");
		sym_unquote = make_sym("unquote");
		sym_unquote_splicing = make_sym("unquote-splicing");
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
		sym_do = make_sym("do");
		
		env_assign(global_env, get<sym>(sym_t.val), sym_t);
		bind_global("nil", nil);
		bind_global("car", make_builtin(builtin_car));
		bind_global("cdr", make_builtin(builtin_cdr));
		bind_global("cons", make_builtin(builtin_cons));
		bind_global("+", make_builtin(builtin_add));
		bind_global("-", make_builtin(builtin_subtract));
		bind_global("*", make_builtin(builtin_multiply));
		bind_global("/", make_builtin(builtin_divide));
		bind_global("<", make_builtin(builtin_less));
		bind_global(">", make_builtin(builtin_greater));
		bind_global("apply", make_builtin(builtin_apply));
		bind_global("is", make_builtin(builtin_is));
		bind_global("scar", make_builtin(builtin_scar));
		bind_global("scdr", make_builtin(builtin_scdr));
		bind_global("mod", make_builtin(builtin_mod));
		bind_global("type", make_builtin(builtin_type));
		bind_global("string-sref", make_builtin(builtin_string_sref));
		bind_global("writeb", make_builtin(builtin_writeb));
		bind_global("expt", make_builtin(builtin_expt));
		bind_global("log", make_builtin(builtin_log));
		bind_global("sqrt", make_builtin(builtin_sqrt));
		bind_global("readline", make_builtin(builtin_readline));
		bind_global("quit", make_builtin(builtin_quit));
		bind_global("rand", make_builtin(builtin_rand));
		bind_global("read", make_builtin(builtin_read));
		bind_global("macex", make_builtin(builtin_macex));
		bind_global("string", make_builtin(builtin_string));
		bind_global("sym", make_builtin(builtin_sym));
		bind_global("system", make_builtin(builtin_system));
		bind_global("eval", make_builtin(builtin_eval));
		bind_global("load", make_builtin(builtin_load));
		bind_global("int", make_builtin(builtin_int));
		bind_global("trunc", make_builtin(builtin_trunc));
		bind_global("sin", make_builtin(builtin_sin));
		bind_global("cos", make_builtin(builtin_cos));
		bind_global("tan", make_builtin(builtin_tan));
		bind_global("bound", make_builtin(builtin_bound));
		bind_global("infile", make_builtin(builtin_infile));
		bind_global("outfile", make_builtin(builtin_outfile));
		bind_global("close", make_builtin(builtin_close));
		bind_global("stdin", make_input(stdin));
		bind_global("stdout", make_output(stdout));
		bind_global("stderr", make_output(stderr));
		bind_global("disp", make_builtin(builtin_disp));
		bind_global("readb", make_builtin(builtin_readb));
		bind_global("sread", make_builtin(builtin_sread));
		bind_global("write", make_builtin(builtin_write));
		bind_global("newstring", make_builtin(builtin_newstring));
		bind_global("table", make_builtin(builtin_table));
		bind_global("maptable", make_builtin(builtin_maptable));
		bind_global("table-sref", make_builtin(builtin_table_sref));
		bind_global("coerce", make_builtin(builtin_coerce));
		bind_global("flushout", make_builtin(builtin_flushout));
		bind_global("err", make_builtin(builtin_err));
		bind_global("len", make_builtin(builtin_len));
		bind_global("ccc", make_builtin(builtin_ccc));
		bind_global("mvfile", make_builtin(builtin_mvfile));
		bind_global("rmfile", make_builtin(builtin_rmfile));
		bind_global("dir", make_builtin(builtin_dir));
		bind_global("pipe-from", make_builtin(builtin_pipe_from));
		bind_global("dir-exists", make_builtin(builtin_dir_exists));
		bind_global("file-exists", make_builtin(builtin_file_exists));
		bind_global("ensure-dir", make_builtin(builtin_ensure_dir));

#include "library.h"

		error err = load_string(stdlib);
		if (err) {
			print_error(err);
		}
	}

	void print_error(error e) {
		if (e != ERROR_USER) {
			printf("%s : ", error_string[e]);
			print_expr(err_expr);
			puts("");
		}
	}

	void repl() {
		char* temp;
		string input;

		while ((temp = readline("> ")) != NULL) {
			input = temp;
			free(temp);
		read_start:

			const char* p = input.c_str();
			error err;
			atom result;

			atom expr;
			err = read_expr(p, &p, &expr);
			if (err == ERROR_FILE) { /* read more lines */
				char* line = readline("  ");
				if (!line) break;
				input += string("\n") + line;
				free(line);
				goto read_start;
			}
#ifdef READLINE
			add_history(input.c_str());
#endif

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
		}
	}
}
