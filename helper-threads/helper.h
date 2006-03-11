/*
 * Helper Threads Toolkit
 * (c) 2006 Javier Guerra G.
 * $Id: helper.h,v 1.2 2006-03-11 00:28:40 jguerra Exp $
 */

typedef struct task_ops {
	int (*prepare) (lua_State *L, void **udata);
	int (*work) (void *udata);
	int (*finish) (lua_State *L, void *udata);
} task_ops;


typedef void (*add_helperfunc_t) (lua_State *, task_ops *);
add_helperfunc_t add_helperfunc;

#define lua_getfield(L,s)	\
		(lua_pushstring(L, s), lua_gettable(L, -2))

#define helper_init(L)									\
	{													\
		lua_getglobal (L, "helper");					\
		lua_getfield (L, "_API");						\
		lua_getfield (L, "add_helperfunc");				\
		add_helperfunc = (add_helperfunc_t) lua_touserdata (L, -1);		\
		lua_pop (L, 3);									\
	}
