#include "ekg2-config.h"

#include <ekg/debug.h>
#include <ekg/dynstuff.h>
#include <ekg/plugins.h>
#include <ekg/scripts.h>

#undef __
#undef _

#include <ruby.h>

static int ruby_initialize();
static int ruby_finalize_wrapper();
static int ruby_load(script_t *scr);
static int ruby_unload(script_t *scr);
static int ruby_bind_free(script_t *scr, void *data, /* niby to jest ale kiedys nie bedzie.. nie uzywac */ int type, void *private, ...);
static int ruby_query(script_t *scr, script_query_t *scr_que, void *args[]);
static int ruby_commands(script_t *scr, script_command_t *comm, char **params);
static int ruby_timers(script_t *scr, script_timer_t *time, int type);
static int ruby_variable_changed(script_t *scr, script_var_t *scr_var, char *what);
static int ruby_watches(script_t *scr, script_watch_t *scr_wat, int type, int fd, int watch);

PLUGIN_DEFINE(ruby, PLUGIN_SCRIPTING, NULL);

scriptlang_t ruby_lang = { /* SCRIPT_DEFINE(ruby, ".rb"); */
	name: "ruby",
	plugin: &ruby_plugin,
	ext: ".rb",
	init: ruby_initialize,
	deinit: ruby_finalize_wrapper,
	script_load: ruby_load,
	script_unload: ruby_unload,
	script_free_bind: ruby_bind_free,
	script_handler_query : ruby_query,
	script_handler_command: ruby_commands,
	script_handler_timer : ruby_timers,
	script_handler_var : ruby_variable_changed,
	script_handler_watch : ruby_watches
};

static script_t *last_scr = NULL;

static VALUE ekg2_ruby_module;
static VALUE ekg2_ruby_script;

static int ruby_finalize_wrapper() {
	ruby_finalize();
	return 0;
}

#define RUBY_EKG_INTERNAL_ERROR rb_eArgError /* XXX */

static script_t *ruby_find_script(VALUE self) {
	SCRIPT_FINDER (((scr->lang == &ruby_lang)) && script_private_get(scr) == (void *) self);
}

static VALUE ekg2_scripts_initialize(VALUE self) {
	if (!last_scr) {
		rb_raise(RUBY_EKG_INTERNAL_ERROR, "@ initialize internal error");
		return Qnil;	/* ??? */
	}

	last_scr->private = (void *) self;
	last_scr = NULL;

	return ekg2_ruby_script;
}

static VALUE ruby_format_add(int argc, VALUE *argv, VALUE self) {
	int replace = 1;

	if (argc != 2 && argc != 3) rb_raise(rb_eArgError, "format_add() accepts 2 or 3 params, but %d given", argc);

	Check_Type(argv[0], T_STRING);
	Check_Type(argv[1], T_STRING);

	if (argc == 3) {
		Check_Type(argv[2], T_FIXNUM);
		replace = FIX2INT(argv[2]);
	}
	
	format_add(RSTRING(argv[0])->ptr, RSTRING(argv[1])->ptr, replace);

	return Qnil;
}

static VALUE ruby_print(int argc, VALUE *argv, VALUE self) {
	script_t *scr = ruby_find_script(self);

	if (!scr) {
		rb_raise(RUBY_EKG_INTERNAL_ERROR, "@ handler_bind internal error");
		return Qnil;
	}
	if (argc == 0 || argc > 10) rb_raise(rb_eArgError, "print() accepts 1-10 params, but %d given", argc);

	Check_Type(argv[0], T_STRING);

	if (argc == 1)
		print("script_generic", "ruby", scr->name, RSTRING(argv[0])->ptr);
	else {
		char *args[9] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };
		int i;

		for (i = 1; i < argc; i++) {
			Check_Type(argv[i], T_STRING);
			args[i-1] = RSTRING(argv[i])->ptr;
		}

		print(RSTRING(argv[0])->ptr, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7], args[8], args[9]);
	}

	return Qnil;
}

static VALUE ruby_command_bind(int argc, VALUE *argv, VALUE self) {
	script_t *scr = ruby_find_script(self);

	if (!scr) {
		rb_raise(RUBY_EKG_INTERNAL_ERROR, "@ command_bind internal error");
		return Qnil;
	}
	if (argc != 2 && argc != 3) rb_raise(rb_eArgError, "command_bind() accepts 2 or 3 params, but %d given", argc);

	Check_Type(argv[0], T_STRING);
	Check_Type(argv[1], T_STRING);

	script_command_bind(&ruby_lang, scr, RSTRING(argv[0])->ptr, xstrdup(RSTRING(argv[1])->ptr));	/* XXX, memleak */

	return Qnil;
}

static VALUE ruby_timer_bind(int argc, VALUE *argv, VALUE self) {
	script_t *scr = ruby_find_script(self);

	if (!scr) {
		rb_raise(RUBY_EKG_INTERNAL_ERROR, "@ handler_bind internal error");
		return Qnil;
	}
	if (argc != 2) rb_raise(rb_eArgError, "timer_bind() accepts 2 paramss, but %d given", argc);

	Check_Type(argv[0], T_FIXNUM);		/* XXX, T_BIGNUM || T_FLOAT ? */
	Check_Type(argv[1], T_STRING);

	script_timer_bind(&ruby_lang, scr, FIX2INT(argv[0]), xstrdup(RSTRING(argv[1])->ptr));	/* XXX, memleak */

	return Qnil;
}

static VALUE ruby_handler_bind(int argc, VALUE *argv, VALUE self) {
	script_t *scr = ruby_find_script(self);
	char *query_name = NULL;

	if (!scr) {
		rb_raise(RUBY_EKG_INTERNAL_ERROR, "@ handler_bind internal error");
		return Qnil;
	}
	if (argc != 2) rb_raise(rb_eArgError, "handler_bind() accepts 2 params, but %d given", argc);

#if 0
	if (TYPE(obj) == T_STRING)	query_name = RSTRING(argv[0])->ptr;
	else if (TYPE(obj) == T_FIXNUM); /* XXX ? */
	else error.
#endif
	Check_Type(argv[0], T_STRING);
	Check_Type(argv[1], T_STRING);

	query_name = RSTRING(argv[0])->ptr;

	script_query_bind(&ruby_lang, scr, query_name, xstrdup(RSTRING(argv[1])->ptr));	/* XXX, memleak */

	return Qnil;
}

static int ruby_initialize() {
	ruby_init();
	ruby_init_loadpath();
	ruby_script("ekg2");

/* create ekg2 class */
	ekg2_ruby_module = rb_define_module("Ekg2");
	ekg2_ruby_script = rb_define_class_under(ekg2_ruby_module, "Script", rb_cObject);

	rb_define_method(ekg2_ruby_script, "command_bind", ruby_command_bind, -1);
	rb_define_method(ekg2_ruby_script, "handler_bind", ruby_handler_bind, -1);
	rb_define_method(ekg2_ruby_script, "timer_bind", ruby_timer_bind, -1);
	rb_define_method(ekg2_ruby_script, "format_add", ruby_format_add, -1);
	rb_define_method(ekg2_ruby_script, "print", ruby_print, -1);
	rb_define_method(ekg2_ruby_script, "initialize", ekg2_scripts_initialize, 0);

	return 0;
}

/* XXX, do sprawdzenia */
static char *ruby_geterror(const char *what) {
	string_t err = string_init(what);
	VALUE exception_instance;
	VALUE message;
	VALUE class;
	ID id;

/* where? */
	string_append_format(err, "%s:%d", ruby_sourcefile, ruby_sourceline);

	if ((id = rb_frame_last_func()))
		string_append_format(err, " @%s()", rb_id2name(id));
	string_append_c(err, '\n');

	exception_instance = rb_gv_get("$!");
/* class */
	class = rb_class_path(CLASS_OF(exception_instance));
	string_append_format(err, _("Class: %s\n"), RSTRING(class)->ptr);
/* message */
	message = rb_obj_as_string(exception_instance);
	string_append_format(err, _("Message: %s\n"), RSTRING(message)->ptr);

/* backtrace */
	if (!NIL_P(ruby_errinfo)) {
		VALUE ary = rb_funcall(ruby_errinfo, rb_intern("backtrace"), 0);
		int c;

		string_append(err, "Backtrace:\n");

		for (c=0; c<RARRAY(ary)->len; c++)
			string_append_format(err, "from %s\n", RSTRING(RARRAY(ary)->ptr[c])->ptr);
	}

	return string_free(err, 0);
}

static VALUE ruby_init_wrapper(VALUE arg) {
	VALUE klass = rb_const_get(ekg2_ruby_script, rb_intern((const char *) arg));
	VALUE self = rb_funcall2(klass, rb_intern("new"), 0, NULL);

	return self;
}

static VALUE ruby_load_wrapper(VALUE arg) {
	rb_require((const char *) arg);
	return Qnil;
}

static int ruby_load(script_t *scr) {
	int error = 0;

	if (last_scr) {
		debug_error("last_scr != NULL\n");
		return -1;
	}

	last_scr = scr;
	
	rb_protect(ruby_load_wrapper, (VALUE) scr->path, &error);
	
	if (error) {
		char *err = ruby_geterror("ruby_load() ");
		print("script_error", err);
		xfree(err);
		last_scr = NULL;
		return -1;
	}

	rb_protect(ruby_init_wrapper, (VALUE) scr->name, &error);

	if (error) {
		char *err = ruby_geterror("ruby_init() ");
		print("script_error", err);
		xfree(err);
		last_scr = NULL;
		return -1;
	}

	return 1;
}

static int ruby_unload(script_t *scr) {

	return 0;
}

static int ruby_bind_free(script_t *scr, void *data, /* niby to jest ale kiedys nie bedzie.. nie uzywac */ int type, void *private, ...) {

	return 0;
}

typedef struct {
	VALUE class;
	char *func;
	int argc;
	VALUE *argv;
} ruby_helper_t;

static VALUE ruby_funcall_wrapper(VALUE arg) {
	ruby_helper_t *ruby_helper = (ruby_helper_t *) arg;
	ID id = rb_intern(ruby_helper->func);

	return rb_funcall2(ruby_helper->class, id, ruby_helper->argc, ruby_helper->argv);
}

static VALUE ruby_funcall(ruby_helper_t *ruby_helper) {
	int error = 0;
	VALUE result = rb_protect(ruby_funcall_wrapper, (VALUE) ruby_helper, &error);

	if (error) {
		char *err = ruby_geterror("ruby_funcall() ");
		print("script_error", err);
		xfree(err);

		return result;
	}

	return result;
}

static int ruby_query(script_t *scr, script_query_t *scr_que, void *args[]) {
	ruby_helper_t ruby_query;
	VALUE *argv;

	if (!scr_que->argc) argv = NULL;
	else {
		int i;

		argv = ALLOCA_N(VALUE, scr_que->argc);

		/* XXX argv == NULL ? */

		for (i=0; i < scr_que->argc; i++) {
			switch ( scr_que->argv_type[i] ) {
				case (QUERY_ARG_INT):   /* int */
					argv[i] = INT2FIX( *(int  *) args[i] );	/* XXX ? */
					break;
				case (QUERY_ARG_CHARP):  /* char * */
					argv[i] = rb_str_new2(*(char **) args[i]);
					break;

#if 0
				case (QUERY_ARG_CHARPP): {/* char ** */
					 char *tmp = array_join((char **) args[i], " ");
					 if (xstrlen(tmp)) 
					 perlarg = new_pv(tmp);
					 xfree(tmp);
					 break;
				 }
				case (QUERY_ARG_WINDOW): /* window_t */
					 perlarg = ekg2_bless(BLESS_WINDOW, 0, (*(window_t **) args[i]));
					 break;
				case (QUERY_ARG_FSTRING): /* fstring_t */
					 perlarg = ekg2_bless(BLESS_FSTRING, 0, (*(fstring_t **) args[i]));
					 break;
#endif
				default:
					 debug("[NIMP] %s %d %d\n", __(query_name(scr_que->self->id)), i, scr_que->argv_type[i]);
					 argv[i] = Qnil;
			}
#if 0
			if (change)   perlargs[i] = (perlarg = newRV_noinc(perlarg));
			XPUSHs(sv_2mortal(perlarg));
#endif
		}
	}

	ruby_query.class = (VALUE) scr->private;
	ruby_query.func = scr_que->private;
	ruby_query.argc = scr_que->argc;
	ruby_query.argv = argv;

	ruby_funcall(&ruby_query); 

	return 0;
}

static int ruby_commands(script_t *scr, script_command_t *comm, char **params) {
	ruby_helper_t ruby_command;
	VALUE *argv;
	int argc = array_count(params);
	int i;

	argv = ALLOCA_N(VALUE, argc);

	/* XXX argv == NULL ? */

	for (i=0; i < argc; i++)
		argv[i] = rb_str_new2(params[0]);

	ruby_command.class = (VALUE) scr->private;
	ruby_command.func = comm->private;
	ruby_command.argc = argc;
	ruby_command.argv = argv;

	ruby_funcall(&ruby_command);

	return 0;
}

/* IF WATCH_READ_LINE int type == char *line */
static int ruby_watches(script_t *scr, script_watch_t *scr_wat, int type, int fd, int watch) {

	return 0;
}

static int ruby_variable_changed(script_t *scr, script_var_t *scr_var, char *what) {

	return 0;
}

static int ruby_timers(script_t *scr, script_timer_t *time, int type) {
	ruby_helper_t ruby_timer;

	ruby_timer.class = (VALUE) scr->private;
	ruby_timer.func = time->private;
	ruby_timer.argc = 0;
	ruby_timer.argv = NULL;

	ruby_funcall(&ruby_timer);
	return 0;
}

EXPORT int ruby_plugin_init(int prio) {
	plugin_register(&ruby_plugin, prio);
	scriptlang_register(&ruby_lang);
	return 0;
}

static int ruby_plugin_destroy() {
	scriptlang_unregister(&ruby_lang);
	plugin_unregister(&ruby_plugin);
	return 0;
}