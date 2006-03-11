#include <sys/time.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"

#include "helper.h"

typedef struct timer_udata {
	struct timeval tv;
	int ret;
} timer_udata;

static int timer_prepare (lua_State *L, void **udata) {
	lua_Number t = luaL_checknumber (L, 1);
	timer_udata *td = (timer_udata *)malloc (sizeof (timer_udata));
	if (!td)
		luaL_error (L, "can't alloc udata");
	
	td->tv.tv_sec = (int) t;
	td->tv.tv_usec = (t - td->tv.tv_sec) * 1000000;
	
	*udata = td;
	
	return 0;
}

static int timer_work (void *udata) {
	timer_udata *td = (timer_udata *)udata;
	fd_set fd_a, fd_b, fd_c;
	FD_ZERO (&fd_a);
	FD_ZERO (&fd_b);
	FD_ZERO (&fd_c);
	
	/* printf ("[before select]\n"); */
	td->ret = select (0, &fd_a, &fd_b, &fd_c, &td->tv);
	/* printf ("[after select]\n"); */
	
	return 0;
}

static int timer_finish (lua_State *L, void *udata) {
	timer_udata *td = (timer_udata *)udata;
	
	if (td->ret < 0)
		luaL_error (L, strerror (td->ret));
	
	free (td);
	
	return 0;
}

static task_ops timer_ops = {
	timer_prepare,
	timer_work,
	timer_finish
};

int luaopen_timer (lua_State *L);
int luaopen_timer (lua_State *L) {

	helper_init (L);
	add_helperfunc (L, &timer_ops);
	return 1;
}
