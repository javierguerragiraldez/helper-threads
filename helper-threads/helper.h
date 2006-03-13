/*
 * Helper Threads Toolkit
 * (c) 2006 Javier Guerra G.
 * $Id: helper.h,v 1.5 2006-03-13 22:08:08 jguerra Exp $
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
typedef void (*signal_task_t) (int pause);

add_helperfunc_t add_helperfunc;
tasklib_t tasklib;
signal_task_t signal_task;



#define lua_getfield(L,s)	\
		(lua_pushstring(L, s), lua_gettable(L, -2))

#define helper_init(L)													\
	{																	\
		lua_getglobal (L, "helper");									\
		lua_getfield (L, "_API");										\
		lua_getfield (L, "add_helperfunc");								\
		add_helperfunc = (add_helperfunc_t) lua_touserdata (L, -1);		\
		lua_pop (L, 1);													\
		lua_getfield (L, "tasklib");									\
		tasklib = (tasklib_t) lua_touserdata (L, -1);					\
		lua_pop (L, 1);													\
		lua_getfield (L, "signal_task");								\
		signal_task = (signal_task_t) lua_touserdata (L, -1);			\
		lua_pop (L, 3);													\
	}
