/*
 * Helper Threads Toolkit
 * (c) 2006 Javier Guerra G.
 * $Id: helper.c,v 1.11 2006-05-27 18:51:44 jguerra Exp $
 */

#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "lua.h"
#include "lauxlib.h"
#include "pthread.h"

#include "helper.h"

#ifndef NUMBER_TO_TIMESPEC
#define NUMBER_TO_TIMESPEC(f, ts)								\
	do {														\
		(ts)->tv_sec = (int) (f);								\
		(ts)->tv_nsec = ((f) - (ts)->tv_sec) * 1000000000;		\
	} while (0)
#endif

#ifndef TIMEVAL_TO_TIMESPEC
# define TIMEVAL_TO_TIMESPEC(tv, ts) {						\
	(ts)->tv_sec = (tv)->tv_sec;							\
	(ts)->tv_nsec = (tv)->tv_usec * 1000;					\
}
#endif

#ifndef timeradd
# define timeradd(a, b, result)								\
	do {													\
		(result)->tv_sec = (a)->tv_sec + (b)->tv_sec;		\
		(result)->tv_nsec = (a)->tv_nsec + (b)->tv_nsec;	\
		if ((result)->tv_nsec >= 1000000000) {				\
			++(result)->tv_sec;								\
			(result)->tv_nsec -= 1000000000;				\
		}													\
	} while (0)
#endif


static const char TaskType[] = "__HelperTaskType__";
static const char QueueType[] = "__HelperQueueType__";
static const char ThreadType[] = "__HelperThreadType__";

typedef enum {
	TSK_NULL,
	TSK_READY,
	TSK_WAITING,
	TSK_BUSY,
	TSK_PAUSED,
	TSK_DONE,
	TSK_FINISHED
} task_state;

typedef struct task_t {
	const char *type;
	struct task_t *next;
	task_state state;
	pthread_mutex_t lock;
	pthread_cond_t unpaused;
	task_ops *ops;
	void *udata;
} task_t;

typedef struct queue_t {
	task_t *head;
	task_t *tail;
	pthread_mutex_t lock;
	pthread_cond_t notempty;
} queue_t;

typedef struct thread_t {
	pthread_t pth;
	queue_t *in;
	queue_t *out;
	task_t *task;
	int signal;
} thread_t;

/********************************************
 * thread-safe FIFO queue functions
 *******************************************/

static void q_init (queue_t *q) {
	q->head = NULL;
	q->tail = NULL;
	pthread_mutex_init (&q->lock, NULL);
	pthread_cond_init (&q->notempty, NULL);
}

static void q_push (queue_t *q, task_t *t) {
	if (!q || !t)
		return;
	
	pthread_mutex_lock (&q->lock);
	
	t->next = NULL;
	
	if (q->tail)
		q->tail->next = t;
	q->tail = t;
	
	if (!q->head)
		q->head = t;
	
	pthread_cond_broadcast (&q->notempty);
	pthread_mutex_unlock (&q->lock);
}

static task_t *q_remove (queue_t *q, task_t *t) {
	task_t *p;
	if (!q || !t)
		return NULL;
	
	pthread_mutex_lock (&q->lock);
	
	if (q->head == t) {
		q->head = t->next;
		if (!q->head)
			q->tail = NULL;
		return t;
	}
	for (p = q->head; p; p = p->next) {
		if (p->next == t) {
			p->next = t->next;
			if (q->tail == t)
				q->tail = p;
			return t;
		}
	}
	
	return NULL;
	
	pthread_mutex_unlock (&q->lock);
}

static task_t *q_pop (queue_t *q) {
	task_t *t = NULL;
	
	if (!q)
		return NULL;
	
	pthread_mutex_lock (&q->lock);
	
	t = q->head;
	if (t) {
		q->head = t->next;
		t->next = NULL;
	}
	
	if (q->tail == t)
		q->tail = NULL;
	
	pthread_mutex_unlock (&q->lock);
	
	return t;
}

static task_t *q_wait (queue_t *q, const struct timespec *timeout) {
	int ret = 0;
	task_t *t = NULL;
	
	if (!q)
		return NULL;
	
	pthread_mutex_lock (&q->lock);
	while (q->head == NULL) {
		if (timeout)
			ret = pthread_cond_timedwait (&q->notempty, &q->lock, timeout);
		else
			ret = pthread_cond_wait (&q->notempty, &q->lock);
	}
	
	if (ret == 0) {
		t = q->head;
		if (t) {
			q->head = t->next;
			t->next = NULL;
		}
		
		if (q->tail == t)
			q->tail = NULL;
	}
	
	pthread_mutex_unlock (&q->lock);
	return t;
}

static void q_free (queue_t *q) {
	task_t *t = NULL;
	if (!q)
		return;
	
	pthread_mutex_lock (&q->lock);
	t = q->head;
	while (t) {
		task_t *nxt = t->next;
		free (t);
		t = nxt;
	}
	q->head = q->tail = NULL;
	pthread_mutex_unlock (&q->lock);
	pthread_cond_destroy (&q->notempty);
	pthread_mutex_destroy (&q->lock);
}

/******************************************
 * task handling funcions
 ******************************************/
static void tsk_setstate (task_t *t, task_state state) {
	pthread_mutex_lock (&t->lock);
	t->state = state;
	if (state != TSK_PAUSED)
		pthread_cond_broadcast (&t->unpaused);
	
	pthread_mutex_unlock (&t->lock);
}

/*******************************************
 *  userdata types functions
 *******************************************/

static task_t *check_task (lua_State *L, int index) {
	task_t *t = NULL;
	
	luaL_checktype (L, index, LUA_TLIGHTUSERDATA);
	t = lua_touserdata (L, index);
	
	if (!t || t->type != TaskType)
		luaL_typerror (L, index, "helper task");

	return t;
}

static task_t *is_task (lua_State *L, int index) {
	task_t *t = NULL;
	
	if (!lua_islightuserdata (L, index))
		return NULL;
	
	t = lua_touserdata (L, index);
	if (!t || t->type != TaskType)
		return NULL;;

	return t;
}

static queue_t *check_queue (lua_State *L, int index) {
	queue_t *q = (queue_t *)luaL_checkudata (L, index, QueueType);
	luaL_argcheck (L, q, index, "task queue expected");
	return q;
}

static thread_t *check_thread (lua_State *L, int index) {
	thread_t *thrd = (thread_t *)luaL_checkudata (L, index, ThreadType);
	luaL_argcheck (L, thrd, index, "bg thread expected");
	return thrd;
}

/**************************************************
 *  task lua functions
 **************************************************/

/*
 * helper.newtask ()
 */
static task_t *new_task (lua_State *L) {
	task_t *t = (task_t *)malloc (sizeof (task_t));
	if (!t)
		luaL_error (L, "can't alloc a new task");
	
	t->type = TaskType;
	t->next = NULL;
	t->state = TSK_NULL;
	pthread_mutex_init (&t->lock, NULL);
	pthread_cond_init (&t->unpaused, NULL);
	
	lua_pushlightuserdata (L, t);
	return t;
}


/*
 * helper.update (task)
 */
static int task_update (lua_State *L) {
	int ret = 0;
	task_state nxtstate;
	
	task_t *t = check_task (L, 1);
	lua_remove (L, 1);
	if (!t)
		return 0;
	
	pthread_mutex_lock (&t->lock);
	
	switch (t->state) {
		case TSK_READY:
			if (t->ops && t->ops->work)
				t->ops->work (t->udata);
			t->state = TSK_DONE;
			nxtstate = TSK_FINISHED;
			break;
		case TSK_BUSY:
		case TSK_PAUSED:
			nxtstate = TSK_BUSY;
			break;
		case TSK_DONE:
			nxtstate = TSK_FINISHED;
			break;
		default:
			luaL_error (L, "the task is in the wrong state");
			return 0;
	}
	
	if (t && t->ops && t->ops->update)
		ret = t->ops->update (L, t->udata);
	t->state = nxtstate;
	if (t->state != TSK_PAUSED)
		pthread_cond_broadcast (&t->unpaused);
	if (t->state == TSK_FINISHED)					/* can this be dangerous??? */
		free (t);
	
	pthread_mutex_unlock (&t->lock);
	return ret;
}

/*
 * helper.state (task)
 */
static int state (lua_State *L) {
	const char *s = NULL;
	task_t *t = is_task (L, 1);
	if (!t) {
		lua_pushnil (L);
		lua_pushliteral (L, "Not a valid task");
		return 2;
	}
	
	switch (t->state) {
		case TSK_NULL:
			s = "NULL";
			break;
		case TSK_READY:
			s = "Ready";
			break;
		case TSK_WAITING:
			s = "Waiting";
			break;
		case TSK_BUSY:
			s = "Busy";
			break;
		case TSK_PAUSED:
			s = "Paused";
			break;
		case TSK_DONE:
			s = "Done";
			break;
		case TSK_FINISHED:
			s = "Finished";
			break;
	}
	lua_pushstring (L, s);
	return 1;
}

/*
 * helper.newqueue ()
 */
static int new_queue (lua_State *L) {
	queue_t *q = (queue_t *)lua_newuserdata (L, sizeof (queue_t));
	q_init (q);
	
	luaL_getmetatable (L, QueueType);
	lua_setmetatable (L, -2);
	return 1;
}

/*
 * queue:addtask (tsk)
 */
static int queue_addtask (lua_State *L) {
	queue_t *q = check_queue (L, 1);
	task_t *t = check_task (L, 2);
	if (t && t->state != TSK_READY)
		luaL_error (L, "task not 'Ready'");
	tsk_setstate (t, TSK_WAITING);
	q_push (q, t);
	return 0;
}

/*
 * queue:remove (task)
 */
static int queue_removetask (lua_State *L) {
	queue_t *q = check_queue (L, 1);
	task_t *t = check_task (L, 2);
	
	if (q_remove (q, t)) {
		if (t->state == TSK_WAITING)
			tsk_setstate (t, TSK_READY);
		return 1;
	} else
		return 0;
}

/*
 * queue:peek ()
 */
static int queue_peek (lua_State *L) {
	queue_t *q = check_queue (L, 1);
	task_t *t = q->head;
	if (t) {
		lua_pushlightuserdata (L, t);
		return 1;
	} else
		return 0;
}

/*
 * queue:wait ([timeout])
 */
static int queue_wait (lua_State *L) {
	queue_t *q = check_queue (L, 1);
	task_t *t = NULL;
	
	if (lua_isnoneornil (L, 2)) {
		t = q_wait (q, NULL);
		lua_pushlightuserdata (L, t);
		
	} else {
		
		struct timeval tv;
		struct timespec ts, now;
		
		lua_Number timeout = lua_tonumber (L, 2);
		NUMBER_TO_TIMESPEC (timeout, &ts);
		
		gettimeofday (&tv, NULL);
		TIMEVAL_TO_TIMESPEC (&tv, &now);
		timeradd (&ts, &now, &ts);
		
		t = q_wait (q, &ts);
		if (t)
			lua_pushlightuserdata (L, t);
		else
			return 0;
	}

	return 1;
}

/*
 * queue:__gc()
 */
static int queue_gc (lua_State *L) {
	queue_t *q = check_queue (L, 1);
	q_free (q);
	return 0;
}

static pthread_key_t thread_key;

static void *thread_work (void *arg) {
	thread_t *thrd = (thread_t *)arg;
	if (!thrd || !thrd->in || !thrd->out)
		return NULL;
	
	pthread_setspecific (thread_key, arg);
	
	while (!thrd->signal) {
		task_t *t = q_wait (thrd->in, NULL);
		if (t) {
			thrd->task = t;
			t->state = TSK_BUSY;
			if (t->ops && t->ops->work)
				t->ops->work (t->udata);
			tsk_setstate (t, TSK_DONE);
		}
		q_push (thrd->out, t);
		thrd->task = NULL;
	}
	return NULL;
}

/*
 * helper.newthread (in_q, out_q)
 */
static int new_thread (lua_State *L) {
	int ret = 0;
	queue_t *in_q = check_queue (L, 1);
	queue_t *out_q = check_queue (L, 2);
	
	thread_t *thrd = (thread_t *)lua_newuserdata (L, sizeof (thread_t));
	thrd->in = in_q;
	thrd->out = out_q;
	thrd->task = NULL;
	thrd->signal = 0;
	ret = pthread_create (&thrd->pth, NULL, thread_work, thrd);
	if (ret)
		luaL_error (L, "error %d (\"%s\") creating helper thread", ret, strerror (ret));
	
	luaL_getmetatable (L, ThreadType);
	lua_setmetatable (L, -2);
	
	return 1;
}


/*
 * thread:currenttask ()
 */
static int currenttask (lua_State *L) {
	thread_t *thrd = check_thread (L, 1);
	
	if (thrd->task) {
		
		lua_pushlightuserdata (L, thrd->task);
		return 1;
		
	} else {
		
		lua_pushnil (L);
		return 1;
	}
}

/*
 * thread:__gc()
 */
static int thread_gc (lua_State *L) {
	int ret = 0;
	thread_t *thrd = check_thread (L, 1);
	
	thrd->signal = 1;
	ret = pthread_join (thrd->pth, NULL);
	if (ret)
		luaL_error (L, "error %d (\"%s\") joining helper thread", ret, strerror (ret));
	
	return 0;
}

static const struct luaL_reg queue_meths [] = {
	{"addtask", queue_addtask},
	{"remove", queue_removetask},
	{"peek", queue_peek},
	{"wait", queue_wait},
	{"__gc", queue_gc},
	{NULL, NULL}
};
static const struct luaL_reg thread_meths [] = {
	{"currenttask", currenttask },
	{"__gc", thread_gc},
	{NULL, NULL}
};
static const struct luaL_reg helper_funcs [] = {
	{"update", task_update},
	{"state", state},
	{"newqueue", new_queue},
	{"newthread", new_thread},
	{NULL, NULL}
};

/************************************************
 * C API
 ***********************************************/
static int task_init (lua_State *L) {
	int ret = 0;
	
	task_ops *ops = (task_ops *)lua_touserdata (L, lua_upvalueindex (1));
	task_t *t = new_task (L);
	t->ops = ops;
	if (ops && ops->prepare)
		ret = ops->prepare (L, &t->udata);
	tsk_setstate (t, TSK_READY);
	return ret+1;
}

static void add_helperfunc_st (lua_State *L, const task_ops *ops) {
	lua_pushlightuserdata (L, (void *)ops);
	lua_pushcclosure (L, task_init, 1);
}

static void tasklib_st (lua_State *L, const char *libname, const task_reg *l) {
	if (libname) {
		lua_pushstring (L, libname);
		lua_gettable (L, LUA_GLOBALSINDEX);  /* check whether lib already exists */
		if (lua_isnil (L, -1)) {  /* no? */
			lua_pop (L, 1);
			lua_newtable (L);  /* create it */
			lua_pushstring (L, libname);
			lua_pushvalue (L, -2);
			lua_settable (L, LUA_GLOBALSINDEX);  /* register it with given name */
		}
	}
	for (; l->name; l++) {
		lua_pushstring(L, l->name);
		add_helperfunc_st (L, l->ops);
		lua_settable (L, -3);
	}
}



static void signal_task_st (int pause) {
	thread_t *thrd = (thread_t *)pthread_getspecific (thread_key);
	task_t *t = thrd->task;
	
	pthread_mutex_lock (&t->lock);
	
	q_push (thrd->out, t);
	if (pause) {
		t->state = TSK_PAUSED;
		while (t->state == TSK_PAUSED)
			pthread_cond_wait (&t->unpaused, &t->lock);
	}
	
	pthread_mutex_unlock (&t->lock);
}

/********************************************
 * null task
 ********************************************/
static int null_prepare (lua_State *L, void **udata) {
	*udata = NULL;
	return 0;
}

static int null_work (void *udata) {
	return 0;
}

static int null_update (lua_State *L, void *udata) {
	return 0;
}

static task_ops null_task = {
	null_prepare,
	null_work,
	null_update
};

/**********************************
 * waiter task
 **********************************/
typedef struct waiter_udata {
	queue_t *q;
	task_t *t;
	struct timespec timeout;
} waiter_udata;

static int waiter_prepare (lua_State *L, void **udata) {
	queue_t *q = check_queue (L, 1);
	waiter_udata *ud = (waiter_udata *)malloc (sizeof (waiter_udata));
	if (!ud)
		luaL_error (L, "can't allocate userdata");
	*udata = ud;
	ud->q = q;
	ud->t = NULL;
	
	if (lua_isnoneornil (L, 2)) {
		ud->timeout.tv_sec = 0;
		ud->timeout.tv_nsec = 0;
		
	} else {
		
		struct timeval tv;
		struct timespec now;
		
		lua_Number timeout = lua_tonumber (L, 2);
		NUMBER_TO_TIMESPEC (timeout, &ud->timeout);
		
		gettimeofday (&tv, NULL);
		TIMEVAL_TO_TIMESPEC (&tv, &now);
		timeradd (&ud->timeout, &now, &ud->timeout);
	}

	return 0;
}

static int waiter_work (void *udata) {
	waiter_udata *ud = (waiter_udata *)udata;
	
	if (ud->timeout.tv_sec != 0 || ud->timeout.tv_nsec != 0)
		ud->t = q_wait (ud->q, NULL);
	else
		ud->t = q_wait (ud->q, &ud->timeout);
	
	return 0;
}

static int waiter_update (lua_State *L, void *udata) {
	waiter_udata *ud = (waiter_udata *)udata;
	
	if (ud->t != NULL)
		lua_pushlightuserdata (L, ud->t);
	else
		lua_pushnil (L);
	
	return 1;
}

static task_ops waiter_ops = {
	waiter_prepare,
	waiter_work,
	waiter_update
};

/*******************************************************
 * Initialization
 *******************************************************/

/*
** Assumes the table is on top of the stack.
*/
static void set_info (lua_State *L) {
	lua_pushliteral (L, "_COPYRIGHT");
	lua_pushliteral (L, "Copyright (C) 2006 Javier Guerra");
	lua_settable (L, -3);
	lua_pushliteral (L, "_DESCRIPTION");
	lua_pushliteral (L, "a helper threads middle layer");
	lua_settable (L, -3);
	lua_pushliteral (L, "_NAME");
	lua_pushliteral (L, "helper");
	lua_settable (L, -3);
	lua_pushliteral (L, "_VERSION");
	lua_pushliteral (L, "0.1");
	lua_settable (L, -3);
}

static void set_tasks (lua_State *L) {
	lua_pushliteral (L, "null");
	add_helperfunc_st (L, &null_task);
	lua_settable (L, -3);
	
	lua_pushliteral (L, "waiter");
	add_helperfunc_st (L, &waiter_ops);
	lua_settable (L, -3);
}

static void setCAPI (lua_State *L) {
	lua_pushliteral (L, "_API");
	lua_newtable (L);
	
	lua_pushliteral (L, "add_helperfunc");
	lua_pushlightuserdata (L, (void *)add_helperfunc_st);
	lua_settable (L, -3);
	
	lua_pushliteral (L, "tasklib");
	lua_pushlightuserdata (L, (void *)tasklib_st);
	lua_settable (L, -3);
	
	lua_pushliteral (L, "signal_task");
	lua_pushlightuserdata (L, (void *)signal_task_st);
	lua_settable (L, -3);
	
	lua_settable (L, -3);
}

int luaopen_helper (lua_State *L);
int luaopen_helper (lua_State *L)
{
	pthread_key_create (&thread_key, NULL);
	
	luaL_newmetatable(L, QueueType);
	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -2);
	lua_rawset(L, -3);
	luaL_openlib (L, NULL, queue_meths, 0);
	
	luaL_newmetatable(L, ThreadType);
	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -2);
	lua_rawset(L, -3);
	luaL_openlib (L, NULL, thread_meths, 0);
	
	luaL_openlib (L, "helper", helper_funcs, 0);
	set_tasks (L);
	set_info (L);
	setCAPI (L);

	return 1;
}

