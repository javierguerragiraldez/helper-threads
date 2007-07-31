/*
 * Helper Threads Toolkit
 * (c) 2006 Javier Guerra G.
 * $Id: nb_tcp.c,v 1.6 2007-07-31 23:53:34 jguerra Exp $
 */


#include <stdlib.h>
#define __USE_POSIX	1
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netdb.h>

#include "lua.h"
#include "lauxlib.h"

#include "helper.h"

#ifndef MIN
#define MIN(a,b)	((a)<(b) ? (a) : (b))
#endif

/*************************
 * mem pipe, a char FIFO or stream
 ************************/
typedef struct pipe_t {
	char *data;
	char *head;
	char *tail;
	size_t bufsize;
} pipe_t;

typedef void (*pipe_filler) (pipe_t *p, void *udata);

static void pipe_init (pipe_t *p, size_t bufsize) {
	p->head = p->tail = p->data = NULL;
	p->bufsize = 0;
	
	if (bufsize > 0) {
		p->data = malloc (bufsize);
		if (p->data) {
			p->bufsize = bufsize;
			p->head = p->tail = p->data;
		}
	}
}

static void pipe_free (pipe_t *p) {
	if (p->data)
		free (p->data);
	p->head = p->tail = p->data = NULL;
	p->bufsize = 0;
}

static size_t pipe_dataleft (pipe_t *p) {
	return p->tail - p->head;
}

static size_t pipe_spaceleft (pipe_t *p) {
	return p->bufsize - (p->tail - p->data);
}

static void pipe_makespace (pipe_t *p, size_t l) {
	size_t newbufsize = pipe_dataleft (p) + l;
	size_t len = p->tail - p->head;
	
	if (p->data + p->bufsize >= p->tail + l)		/* enough space, do nothing */
		return;
	
	if (p->bufsize >= newbufsize) {			/* enough space if old data is discarded */
		memmove (p->data, p->head, len);
		
	} else {								/* allocate new buffer, discard old data */
		char *new_data = malloc (newbufsize);
		if (new_data == NULL)
			return;					/* error, return untouched */
		memcpy (new_data, p->head, len);
		p->data = new_data;
		p->bufsize = newbufsize;
	}
	
	p->head = p->data;						/* in any case, there's no old data anymore */
	p->tail = p->head + len;
	return;
}

static int pipe_getchar (pipe_t *p, pipe_filler f, void *udata) {
	if (p->head >= p->tail)
		f (p, udata);
	if (p->head >= p->tail)
		return EOF;
	return *p->head++;
}

static size_t pipe_push (pipe_t *p, const char *d, size_t l) {
	if (pipe_spaceleft (p) < l)
		pipe_makespace (p, l);
	
	l = MIN (l, pipe_spaceleft (p));
	
	memcpy (p->tail, d, l);
	p->tail += l;
	
	return l;
}

/***********************************/
static const char ServerPortType[] = "__ServerPortType__";
static const char TCPStreamType[] = "__TCPStreamType__";

typedef struct tcpstream_t {
	int fd;
	struct sockaddr_in myaddr, remaddr;
	pipe_t r;
} tcpstream_t;

static tcpstream_t *check_tcpstream (lua_State *L, int index) {
	tcpstream_t *tcps = (tcpstream_t *)luaL_checkudata (L, index, TCPStreamType);
	luaL_argcheck (L, tcps, index, "TCP stream expected");
	return tcps;
}

typedef struct serverport_t {
	int fd;
	struct sockaddr_in myaddr;
} serverport_t;

static serverport_t *check_serverport (lua_State *L, int index) {
	serverport_t *sp = (serverport_t *)luaL_checkudata (L, index, ServerPortType);
	luaL_argcheck (L, sp, index, "server port expected");
	return sp;
}

/***********************************/
static int new_tcpstream (lua_State *L, tcpstream_t *in_tcps) {
	tcpstream_t *tcps = (tcpstream_t *)lua_newuserdata (L, sizeof (tcpstream_t));
	if (!tcps) {
		lua_pushnil (L);
		lua_pushliteral (L, "can't alloc userdata");
		return 2;
	}
	
	*tcps = *in_tcps;
	
	luaL_getmetatable (L, TCPStreamType);
	lua_setmetatable (L, -2);
	return 1;
}

/********************************
  nb_tcp.newserver (tcpport)
  new server port
 ********************************/
static int newserver (lua_State *L) {
	u_int16_t port = luaL_checkint (L, 1);
	serverport_t *sp = (serverport_t *)lua_newuserdata (L, sizeof (serverport_t));
	if (!sp) {
		lua_pushnil (L);
		lua_pushliteral (L, "can't alloc userdata");
		return 2;
	}
	
	sp->fd = socket (PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sp->fd <= 0) {
		free (sp);
		lua_pushnil (L);
		lua_pushstring (L, strerror (errno));
		return 2;
	}
	setsockopt (sp->fd, SOL_SOCKET, SO_REUSEADDR, NULL, 0);
	
	sp->myaddr.sin_family = AF_INET;
	sp->myaddr.sin_port = ntohs (port);
	sp->myaddr.sin_addr.s_addr = INADDR_ANY;
	
	if (bind (sp->fd, (struct sockaddr *)&sp->myaddr, sizeof (sp->myaddr)) <0 ) {
		close (sp->fd);
		free(sp);
		lua_pushnil (L);
		lua_pushstring (L, strerror (errno));
		return 2;
	}
	
	luaL_getmetatable (L, ServerPortType);
	lua_setmetatable (L, -2);
	return 1;
}

/*****************************************************
  nb_tcp.newclient (remaddr, remport [, localport])
 *****************************************************/
typedef struct newclient_udata  {
	char *hostname;
	u_int16_t remport, localport;
	tcpstream_t new;
	int err;
} newclient_udata;

/* #define STACKBUFSIZE 512 */

static int resolve (const char *name, struct in_addr *addr) {
	struct addrinfo hints, *res;
	int err;
	
	hints.ai_flags = 0;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_addrlen = sizeof (*addr);
	
	err = getaddrinfo (name, NULL, &hints, &res);
	if (!err)
		memcpy (addr, res->ai_addr, sizeof (*addr));
	
	freeaddrinfo (res);
	return err;
	
/*	char buf [STACKBUFSIZE];
	struct hostent he, *result;
	int err;
	
	if (gethostbyname2_r (name, AF_INET, &he, buf, STACKBUFSIZE, &result, &err))
		return err;
	
	memcpy (addr, result, sizeof (*addr));
	return err;*/
}

static int newclient_prepare (lua_State *L, void **udata) {
	size_t namelen;
	const char *hostname = luaL_checklstring (L, 1, &namelen);
	u_int16_t remport = luaL_checkint (L, 2);
	u_int16_t localport = luaL_optint (L, 3, 0);
	newclient_udata *ud = (newclient_udata *)malloc (sizeof (newclient_udata));
	if (!ud)
		luaL_error (L, "can't alloc userdata");
	
	*udata = ud;
	ud->hostname = malloc (namelen);
	if (!ud->hostname) {
		free (ud);
		luaL_error (L, "can't copy server name");
	}
	
	memcpy (ud->hostname, hostname, namelen);
	ud->remport = remport;
	ud->localport = localport;
	ud->err = 0;
	
	return 0;
}

static int newclient_work (void *udata) {
	newclient_udata *ud = (newclient_udata *)udata;
	
	ud->new.remaddr.sin_family = AF_INET;
	ud->new.remaddr.sin_port = ud->remport;
	if ((ud->err = resolve (ud->hostname, &ud->new.remaddr.sin_addr)))
		return 0;
	
	ud->new.fd = socket (PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (ud->new.fd < 0) {
		ud->err = errno;
		return 0;
	}
	
	if (ud->localport) {
		ud->new.myaddr.sin_family = AF_INET;
		ud->new.myaddr.sin_port = ud->localport;
		ud->new.myaddr.sin_addr.s_addr = INADDR_ANY;
		
		if (bind (ud->new.fd, (struct sockaddr *)&ud->new.myaddr, sizeof (ud->new.myaddr)) <0 ) {
			close (ud->new.fd);
			ud->err = errno;
			return 0;
		}
	}
	
	if (connect (ud->new.fd, (struct sockaddr *)&ud->new.remaddr.sin_addr, sizeof (ud->new.remaddr)) < 0) {
		close (ud->new.fd);
		ud->err = errno;
		return 0;
	}
	
	return 0;
}

static int newclient_finish (lua_State *L, void *udata) {
	int r;
	newclient_udata *ud = (newclient_udata *)udata;
	
	if (ud->err) {
		lua_pushnil (L);
		lua_pushstring (L, strerror (err));
		if(ud->hostname)
			free(ud->hostname);
		free(ud);

		return 2;
	}
	
	r = new_tcpstream (L, &ud->new);
	free (ud->hostname);
	free (ud);
	return r;
}

static const task_ops newclient_ops = {
	newclient_prepare,
	newclient_work,
	newclient_finish
};

/***************************************
  server:accept ()
 ***************************************/
typedef struct serv_accept_udata {
	serverport_t sp;
	tcpstream_t new;
	int err;
} serv_accept_udata;

static int serv_accept_prepare (lua_State *L, void **udata) {
	serverport_t *sp = check_serverport (L, 1);
	serv_accept_udata *ud = (serv_accept_udata *)malloc (sizeof (serv_accept_udata));
	if (!ud) {
		lua_pushnil (L);
		lua_pushliteral (L, "can't alloc userdata");
		return 2;
	}
	*udata = ud;
	
	ud->sp = *sp;
	ud->err = 0;
	memset (&ud->new, 0, sizeof (ud->new));
	pipe_init (&ud->new.r, 0);
	
	return 0;
}

static int serv_accept_work (void *udata) {
	serv_accept_udata *ud = (serv_accept_udata *)udata;
	socklen_t addrlen = sizeof (ud->new.remaddr);
	
	ud->err = 0;
	
	if (listen (ud->sp.fd, 10) < 0 ) {
		ud->err = errno;
		return 0;
	}
	
	ud->new.fd = accept (ud->sp.fd, (struct sockaddr *)&ud->new.remaddr, &addrlen);
	if (ud->new.fd < 0) {
		ud->err = errno;
		return 0;
	}
	ud->new.myaddr = ud->sp.myaddr;
	return 0;
}

static int serv_accept_finish (lua_State *L, void *udata) {
	int r;
	serv_accept_udata *ud = (serv_accept_udata *)udata;
	
	if (ud->err) {
		lua_pushnil (L);
		lua_pushstring (L, strerror (ud->err));
		free (ud);
		return 2;
	}
	
	r = new_tcpstream (L, &ud->new);
	free (ud);
	return r;
}

static const task_ops serv_accept_ops = {
	serv_accept_prepare,
	serv_accept_work,
	serv_accept_finish
};

/*****************************************
 tcpstream:write (data)
 *****************************************/
typedef struct tcpwrite_udata {
	int fd;
	pipe_t p;
	int err;
} tcpwrite_udata;

static int tcpwrite_prepare (lua_State *L, void **udata) {
	tcpstream_t *tcps = check_tcpstream (L, 1);
	size_t datalen;
	const char *data = luaL_checklstring (L, 2, &datalen);
	
	tcpwrite_udata *ud = (tcpwrite_udata *)malloc (sizeof (tcpwrite_udata));
	if (!ud) 
		luaL_error (L, "can't alloc userdata");
	
	*udata = ud;
	
	ud->fd = tcps->fd;
	ud->err = 0;
	pipe_init (&ud->p, datalen);
	if (!ud->p.data) {
		free (ud);
		luaL_error (L, "can't alloc buffer");
	}
	
	pipe_push (&ud->p, data, datalen);
	return 0;
}

static int tcpwrite_work (void *udata) {
	tcpwrite_udata *ud = (tcpwrite_udata *)udata;
	
	while (pipe_dataleft (&ud->p)) {
		size_t done = write (ud->fd, ud->p.head, pipe_dataleft (&ud->p));
		if (done < 0) {
			ud->err = errno;
			return 0;
		}
		ud->p.head += done;
	}
	
	return 0;
}

static int tcpwrite_finish (lua_State *L, void *udata) {
	tcpwrite_udata *ud = (tcpwrite_udata *)udata;
	
	if (ud->err) {
		lua_pushnil (L);
		lua_pushstring (L, strerror (ud->err));
		pipe_free (&ud->p);
		free (ud);
		return 2;
	}
	
	lua_pushboolean (L, 1);
	return 1;
}

static const task_ops tcpwrite_ops = {
	tcpwrite_prepare,
	tcpwrite_work,
	tcpwrite_finish
};

/*******************************
  tcpstream:read ([format])
 *******************************/
typedef struct tcpread_udata {
	tcpstream_t *str;
	enum {
		RK_NULL,
		RK_LINE,
		RK_ATMOST
	} kind;
	int size;
	int err;
} tcpread_udata;

static int tcpread_prepare (lua_State *L, void **udata) {
	int n;
	tcpstream_t *tcps = check_tcpstream (L, 1);
	tcpread_udata *ud = (tcpread_udata *)malloc (sizeof (tcpread_udata));
	if (!ud) 
		luaL_error (L, "can't alloc userdata");
	*udata = ud;
	
	ud->str = tcps;
	ud->kind = RK_NULL;
	ud->size = 0;
	ud->err = 0;
	
	if (lua_isnoneornil (L, 2))
		ud->kind = RK_LINE;
	
	else {
		n = lua_tonumber (L, 2);
		
		if (n) {
			ud->size = n;
			ud->kind = RK_ATMOST;
			
		} else {
			const char *str = lua_tostring (L, 2);
			if (str[0] == '*') {
				switch (str[1]) {
					case 'l':
						ud->kind = RK_LINE;
						break;
				}
			}
		}
	}
	return 0;
}

static int tcpread_work (void *udata) {
	tcpread_udata *ud = (tcpread_udata *)udata;
	pipe_t *p = &ud->str->r;
	size_t toread;
	
	switch (ud->kind) {
		case RK_NULL:
			return 0;
			break;
			
		case RK_LINE:
			{
				char *cp;
				ssize_t r = 0;
				
				while (1) {
					if (pipe_spaceleft (p) ==0)
						pipe_makespace (p, 1024);
					cp = p->head;
				
					while (*cp != '\r' && *cp != '\n' && cp < p->tail)
						cp++;
					if (cp < p->tail) {
						ud->size = cp-p->head;
						return 0;
					}
					
					r = read (ud->str->fd, p->tail, pipe_spaceleft (p));
					if (r <= 0) {
						ud->err = errno;
						return 0;
					}
					p->tail += r;
				}
			}
			break;
			
		case RK_ATMOST:
			toread = ud->size - pipe_dataleft (p);
			pipe_makespace (p, toread);
			if (pipe_spaceleft (p) < toread) {
				ud->err = ENOMEM;
				return 0;
			}
			while (toread > 0) {
				ssize_t r = read (ud->str->fd, p->tail, toread);
				if (r < 0) {
					ud->err = errno;
					return 0;
				}
				p->tail += r;
				toread -= r;
			}
			break;
	}
	return 0;
}

static int tcpread_finish (lua_State *L, void *udata) {
	tcpread_udata *ud = (tcpread_udata *)udata;
	pipe_t *p = &ud->str->r;
	
	if (ud->err) {
		lua_pushnil (L);
		lua_pushstring (L, strerror (ud->err));
		free (ud);
		return 2;
	}
	
	if (pipe_dataleft (p) <= 0) {
		lua_pushnil (L);
		lua_pushliteral (L, "closed");
		return 2;
	}
	
	switch (ud->kind) {
		case RK_NULL:
			lua_pushnil (L);
			break;
			
		case RK_LINE:
			lua_pushlstring (L, p->head, ud->size);
			p->head += ud->size;
			if (*p->head == '\n' && p->head < p->tail)
				p->head++;
			else {
				if (*p->head == '\r' && p->head < p->tail)
					p->head++;
				if (*p->head == '\n' && p->head < p->tail)
					p->head++;
			}
			break;
			
		case RK_ATMOST:
			lua_pushlstring (L, p->head, pipe_dataleft (p));
			p->head = p->tail;
			break;
	}
	
	free (ud);
	return 1;
}

static const task_ops tcpread_ops = {
	tcpread_prepare,
	tcpread_work,
	tcpread_finish
};

/**********************************
 tcpstream:close ()
 **********************************/
static int tcpclose (lua_State *L) {
	tcpstream_t *tcps = check_tcpstream (L, 1);
	
	if (tcps->fd) {
		close (tcps->fd);
		tcps->fd = 0;
	}
	pipe_free (&tcps->r);
	
	return 0;
}

/***********************************
  server:close ()
 ***********************************/
static int serverclose (lua_State *L) {
	serverport_t *sp = check_serverport (L, 1);
	if (sp->fd) {
		close (sp->fd);
		sp->fd = 0;
	}
	
	return 0;
}

/********************************************
  initialization
 ********************************************/

/* tcpstream methods and tasks */
static const struct luaL_reg tcp_meths [] = {
	{"close", tcpclose},
	{"__gc", tcpclose},
	{NULL, NULL}
};

static const task_reg tcp_tasks [] = {
	{"write", &tcpwrite_ops},
	{"read", &tcpread_ops},
	{NULL}
};

/* serverport methods and tasks */
static const struct luaL_reg server_meths [] = {
	{"close", serverclose},
	{"__gc", serverclose},
	{NULL, NULL}
};

static const task_reg server_tasks [] = {
	{"accept", &serv_accept_ops},
	{NULL}
};

/* nb_tcp library functions and tasks */
static const struct luaL_reg nb_tcp_funcs [] = {
	{"newserver", newserver},
	{NULL, NULL}
};

static const task_reg nb_tcp_tasks [] = {
	{"newclient", &newclient_ops},
	{NULL}
};

int luaopen_nb_tcp (lua_State *L);
int luaopen_nb_tcp (lua_State *L) {
	helper_init (L);
	
	luaL_newmetatable(L, TCPStreamType);
	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -2);
	lua_rawset(L, -3);
	luaL_openlib (L, NULL, tcp_meths, 0);
	tasklib (L, NULL, tcp_tasks);
	
	luaL_newmetatable(L, ServerPortType);
	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -2);
	lua_rawset(L, -3);
	luaL_openlib (L, NULL, server_meths, 0);
	tasklib (L, NULL, server_tasks);
	
	luaL_openlib (L, "nb_tcp", nb_tcp_funcs, 0);
	tasklib (L, NULL, nb_tcp_tasks);
	
	return 1;
}
