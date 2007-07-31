/*
 * Helper Threads Toolkit
 * (c) 2006 Javier Guerra G.
 * $Id: nb_file.c,v 1.6 2007-07-31 23:53:34 jguerra Exp $
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
static const size_t BUF_GLOBSIZE = 1024;
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
		/* NOTE: Case where realloc cannot allocate enough memory isn't handled */
		b->data = realloc (b->data, size);
		b->end = b->data + len;
		b->bufsize = size;
	}
	
	return b->bufsize;
}

static void buffer_addchar (buffer_t *b, const char c) {
	if (buffer_len (b) >= b->bufsize)
		buffer_resize (b, buffer_len (b) + BUF_GLOBSIZE);
	
	*b->end++ = c;
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
	RK_ATMOST,
	RK_LINE,
	RK_ALL
} read_kind_t;

typedef struct read_udata {
	FILE *f;
	size_t size;
	read_kind_t kind;
	buffer_t b;
	int ferror;
	int feof;
} read_udata;

static int read_prepare (lua_State *L, void **udata) {
	int n = 0;
	read_udata *ud = (read_udata *)malloc (sizeof (read_udata));
	if (!ud)
		luaL_error (L, "can't allocate read udata");
	*udata = ud;
	ud->size = 0;
	ud->kind = RK_NULL;
	buffer_init (&ud->b);
	ud->ferror = 0;
	ud->feof = 0;
	
	ud->f = tofile (L, 1);
	
	if (lua_isnoneornil (L, 2))
		ud->kind = RK_LINE;
	
	else {
		n = lua_tonumber (L, 2);
		
		if (n) {
			ud->size = n;
			ud->kind = RK_ATMOST;
			buffer_resize (&ud->b, n);
			
		} else {
			const char *str = lua_tostring (L, 2);
			if (str[0] == '*') {
				switch (str[1]) {
					case 'l':
						ud->kind = RK_LINE;
						break;
					case 'a':
						ud->kind = RK_ALL;
						break;
				}
			}
		}
	}
	
	if (ud->kind == RK_NULL) {
		lua_pushnil (L);
		lua_pushliteral (L, "format not implemented");
		return 2;
	}
	
	return 0;
}

static int read_work (void *udata) {
	read_udata *ud = (read_udata *)udata;
	int c;
	switch (ud->kind) {
		
		case RK_ATMOST:
			buffer_fread (&ud->b, ud->f, ud->size);
			ud->ferror = ferror (ud->f);
			ud->feof = feof (ud->f);
			break;
			
		case RK_LINE:
			while ((c=fgetc (ud->f)) != '\n' && c != '\r' && c != EOF)
				buffer_addchar (&ud->b, c);
			
			ud->ferror = ferror (ud->f);
			ud->feof = feof (ud->f);
			break;
			
		case RK_ALL:
			while (!feof (ud->f))
				buffer_fread (&ud->b, ud->f, 8192);
			ud->feof = feof (ud->f);
			ud->ferror = ferror (ud->f);
			break;
		default:
			break;
	}
	
	return 0;
}

static int read_update (lua_State *L, void *udata) {
	read_udata *ud = (read_udata *)udata;
	int ret = 1;
	
	if (ud->ferror != 0) {
		lua_pushnil (L);
		lua_pushstring (L, strerror (ud->ferror));
		ret = 2;
	
	} else if (buffer_len (&ud->b) > 0) {
		lua_pushlstring (L, (char *)ud->b.data, buffer_len (&ud->b));
	
	} else if (ud->feof && buffer_len (&ud->b) == 0) {
		lua_pushnil (L);
	}
	
	buffer_free (&ud->b);
	free (ud);
	return ret;
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
