/*
 * Helper Threads Toolkit
 * (c) 2006 Javier Guerra G.
 * $Id: timer.c,v 1.4 2007-07-31 23:53:34 jguerra Exp $
 */

#include <sys/time.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"
#include "lauxlib.h"

#include "helper.h"

/*
 * one shot timer
 */
 
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
	
	td->ret = select (0, &fd_a, &fd_b, &fd_c, &td->tv);
	
	return 0;
}

static int timer_finish (lua_State *L, void *udata) {
	timer_udata *td = (timer_udata *)udata;
	int ret = td->ret;
	free(td);
	
	if (ret < 0)
		luaL_error (L, strerror (ret));
	
	return 0;
}

static const task_ops timer_ops = {
	timer_prepare,
	timer_work,
	timer_finish
};


/*
 * repeated ticks timer
 */

typedef struct ticks_udata {
	lua_Number t;
	/*	struct timeval tv;*/
	int ret, end;
} ticks_udata;

static int ticks_prepare (lua_State *L, void **udata) {
	lua_Number t = luaL_checknumber (L, 1);
	ticks_udata *td = (ticks_udata *)malloc (sizeof (ticks_udata));
	if (!td)
		luaL_error (L, "can't alloc udata");
	
	td->t = t;
	td->end = 0;
	
	*udata = td;
	
	return 0;
}

static int ticks_work (void *udata) {
	ticks_udata *td = (ticks_udata *)udata;
	fd_set fd_a, fd_b, fd_c;
	struct timeval tv;
	
	FD_ZERO (&fd_a);
	FD_ZERO (&fd_b);
	FD_ZERO (&fd_c);
	
	while (!td->end) {
	
		tv.tv_sec = (int) td->t;
		tv.tv_usec = (td->t - tv.tv_sec) * 1000000;
		
		td->ret = select (0, &fd_a, &fd_b, &fd_c, &tv);
		signal_task (0);
	}
	
	return 0;
}

static int ticks_update (lua_State *L, void *udata) {
	ticks_udata *td = (ticks_udata *)udata;
	
	if (td->end) {
		int ret = td->ret;
		free (td);
		if (ret < 0)
			luaL_error (L, strerror (ret));
	} else {
		if (lua_isnumber (L, 1)) {
			td->t = lua_tonumber(L, 1);
			if (td->t < 0)
				td->end = 1;
		}
	}
	
	return 0;
}

static const task_ops ticks_ops = {
	ticks_prepare,
	ticks_work,
	ticks_update
};

static const task_reg timer_reg[] = {
	{"timer", &timer_ops},
	{"ticks", &ticks_ops},
	{NULL}
};

int luaopen_timer (lua_State *L);
int luaopen_timer (lua_State *L) {

	helper_init (L);

	tasklib (L, "timer", timer_reg);
	
	return 1;
}
