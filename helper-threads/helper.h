/*
 * Helper Threads Toolkit
 * (c) 2006 Javier Guerra G.
 * $Id: helper.h,v 1.7 2007-07-31 23:53:33 jguerra Exp $
 */

typedef struct task_ops {
	int (*prepare) (lua_State *L, void **udata);
	int (*work) (void *udata);
	int (*update) (lua_State *L, void *udata);
} task_ops;

typedef struct task_reg {
	const char *name;
	const task_ops *ops;
} task_reg;


typedef void (*add_helperfunc_t) (lua_State *L, const task_ops *ops);
typedef void (*tasklib_t) (lua_State *L, const char *libname, const task_reg *l);
typedef void (*signal_task_t) (int );

add_helperfunc_t add_helperfunc;
tasklib_t tasklib;
signal_task_t signal_task;



#define helper_init(L)													\
	{																	\
		lua_getglobal (L, "helper");									\
		lua_getfield (L, -1, "_API");										\
		lua_getfield (L, -1, "add_helperfunc");								\
		add_helperfunc = (add_helperfunc_t) lua_touserdata (L, -1);		\
		lua_pop (L, 1);													\
		lua_getfield (L, -1, "tasklib");									\
		tasklib = (tasklib_t) lua_touserdata (L, -1);					\
		lua_pop (L, 1);													\
		lua_getfield (L, -1, "signal_task");								\
		signal_task = (signal_task_t) lua_touserdata (L, -1);			\
		lua_pop (L, 3);													\
	}
