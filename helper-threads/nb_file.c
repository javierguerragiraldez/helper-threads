#include <stdlib.h>
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

static size_t buffer_len (buffer_t *b) {
	return b->end - b->data;
}

static size_t buffer_resize (buffer_t *b, size_t size) {
	size_t len = b->data ? b->end-b->data : 0;
	
	if (b->bufsize < size) {
		b->data = realloc (b->data, size);
		b->end = b->data + len;
		b->bufsize = size;
	}
	
	return b->bufsize;
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
		luaL_error (L, "not implemented yet");
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
	return 1;
}

static const task_ops read_ops = {
	read_prepare,
	read_work,
	read_update
};

static const task_reg nb_file_reg [] = {
	{"read", &read_ops},
	{NULL}
};

int luaopen_nb_file (lua_State *L);
int luaopen_nb_file (lua_State *L) {
	helper_init (L);
	tasklib (L, "nb_file", nb_file_reg);
	
	return 1;
}
