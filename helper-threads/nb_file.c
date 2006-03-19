/*
 * Helper Threads Toolkit
 * (c) 2006 Javier Guerra G.
 * $Id: nb_file.c,v 1.3 2006-03-19 03:04:41 jguerra Exp $
 */
 
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "lua.h"
#include "lauxlib.h"


#include "helper.h"

/**********************************
** buffer handling
***********************************/
typedef struct buffer_t {
	unsigned char *data;
	unsigned char *end;
	size_t bufsize;
} buffer_t;

static void buffer_init (buffer_t *b) {
	b->data = NULL;
	b->end = NULL;
	b->bufsize = 0;
}

static void buffer_free (buffer_t *b) {
	if (b->data != NULL)
		free (b->data);
	b->data = NULL;
	b->end = NULL;
	b->bufsize = 0;	
}

static size_t buffer_len (buffer_t *b) {
	return b->data ? b->end-b->data : 0;
}

static unsigned char *buffer_data (buffer_t *b) {
	return b->data;
}

static size_t buffer_resize (buffer_t *b, size_t size) {
	size_t len = buffer_len (b);
	
	if (b->bufsize < size) {
		b->data = realloc (b->data, size);
		b->end = b->data + len;
		b->bufsize = size;
	}
	
	return b->bufsize;
}

static void buffer_add (buffer_t *b, const char *data, size_t len) {
	buffer_resize (b, buffer_len (b) + len);
	
	memcpy (b->end, data, len);
	b->end += len;
}

static void buffer_fread (buffer_t *b, FILE *f, size_t len) {
	size_t readlen = 0;
	
	buffer_resize (b, buffer_len (b) + len);
	
	readlen = fread (b->end, 1, len, f);
	if (readlen >= 0)
		b->end += readlen;
}

/***********************************
** copied from liolib.c
***********************************/

#define FILEHANDLE		"FILE*"
static FILE **topfile (lua_State *L, int findex) {
  FILE **f = (FILE **)luaL_checkudata(L, findex, FILEHANDLE);
  if (f == NULL) luaL_argerror(L, findex, "bad file");
  return f;
}


static int io_type (lua_State *L) {
	FILE **f = (FILE **)luaL_checkudata(L, 1, FILEHANDLE);
	if (f == NULL) lua_pushnil(L);
	else if (*f == NULL)
		lua_pushliteral(L, "closed file");
	else
		lua_pushliteral(L, "file");
	return 1;
}


static FILE *tofile (lua_State *L, int findex) {
	FILE **f = topfile(L, findex);
	if (*f == NULL)
		luaL_error(L, "attempt to use a closed file");
	return *f;
}

/******************************************
 **  READ
 ******************************************/

typedef enum read_kind_t {
	RK_NULL,
	RK_FILL,
	RK_ATMOST,
	RK_LINE,
	RK_ALL
} read_kind_t;

typedef struct read_udata {
	FILE *f;
	long offset;
	size_t size;
	read_kind_t kind;
	buffer_t b;
} read_udata;

static int read_prepare (lua_State *L, void **udata) {
	int n = 0;
	read_udata *ud = (read_udata *)malloc (sizeof (read_udata));
	if (!ud)
		luaL_error (L, "can't allocate read udata");
	*udata = ud;
	buffer_init (&ud->b);
	
	ud->f = tofile (L, 1);
	ud->offset = ftell (ud->f);
	
	n = lua_tonumber (L, 2);
	
	if (n) {
		
		ud->size = n;
		ud->kind = RK_FILL;
		buffer_resize (&ud->b, n);
		
	} else {
		const char *str = lua_tostring (L, 2);
		luaL_error (L, "'%s' not implemented yet", str);
	}
	
	return 0;
}

static int read_work (void *udata) {
	read_udata *ud = (read_udata *)udata;
	switch (ud->kind) {
		
		case RK_FILL:
			while (buffer_len (&ud->b) < ud->size) {
				fseek (ud->f, ud->offset + buffer_len (&ud->b), SEEK_SET);
				buffer_fread (&ud->b, ud->f, ud->size - buffer_len (&ud->b));
			}
			break;
			
	}
	
	return 0;
}

static int read_update (lua_State *L, void *udata) {
	read_udata *ud = (read_udata *)udata;
	
	lua_pushlstring (L, (char *)ud->b.data, buffer_len (&ud->b));
	
	buffer_free (&ud->b);
	free (ud);
	
	return 1;
}

static const task_ops read_ops = {
	read_prepare,
	read_work,
	read_update
};


/******************************************
 **  WRITE
 ******************************************/

typedef struct write_udata {
	FILE *f;
	buffer_t b;
	int ferror;
} write_udata;

static int write_prepare (lua_State *L, void **udata) {
	write_udata *ud = NULL;
	FILE *f = tofile (L, 1);
	luaL_checktype (L, 2, LUA_TSTRING);
	
	ud = (write_udata *)malloc (sizeof (write_udata));
	if (!ud)
		luaL_error (L, "can't allocate write udata");
	*udata = ud;
	buffer_init (&ud->b);
	
	ud->f = f;
	buffer_add (&ud->b, lua_tostring (L, 2), lua_strlen (L, 2));
	ud->ferror = 0;
	
	return 0;
}

static int write_work (void *udata) {
	write_udata *ud = (write_udata *)udata;
	unsigned char *p = buffer_data (&ud->b);
	size_t len = buffer_len (&ud->b);
	
	if (fwrite (p, 1, len, ud->f) != len)
		ud->ferror = ferror (ud->f);
	
	return 0;
}

static int write_update (lua_State *L, void *udata) {
	write_udata *ud = (write_udata *)udata;
	int ret;
	
	if (ud->ferror) {
		lua_pushnil(L);
		lua_pushfstring(L, "%s", strerror(ud->ferror));
		ret = 2;
		
	} else {
		lua_pushboolean(L, 1);
		ret = 1;
	}
	buffer_free (&ud->b);
	free (ud);
	
	return ret;
}

static const task_ops write_ops = {
	write_prepare,
	write_work,
	write_update
};

/***************************************
 **  Initialization
 ***************************************/

static const task_reg nb_file_reg [] = {
	{"read", &read_ops},
	{"write", &write_ops},
	{NULL}
};

int luaopen_nb_file (lua_State *L);
int luaopen_nb_file (lua_State *L) {
	helper_init (L);
	tasklib (L, "nb_file", nb_file_reg);
	
	return 1;
}
