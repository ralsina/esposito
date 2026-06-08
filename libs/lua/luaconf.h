#ifndef luaconf_h
#define luaconf_h

#include <limits.h>
#include <stddef.h>

#define LUAI_IS32INT	((UINT_MAX >> 30) >= 3)

#define LUA_INT_INT		1
#define LUA_INT_LONG		2
#define LUA_INT_LONGLONG	3

#define LUA_FLOAT_FLOAT		1
#define LUA_FLOAT_DOUBLE	2
#define LUA_FLOAT_LONGDOUBLE	3

#define LUA_INT_DEFAULT		LUA_INT_LONGLONG
#define LUA_FLOAT_DEFAULT	LUA_FLOAT_DOUBLE

#define LUA_32BITS	1

#if LUA_32BITS
#if LUAI_IS32INT
#define LUA_INT_TYPE	LUA_INT_INT
#else
#define LUA_INT_TYPE	LUA_INT_LONG
#endif
#define LUA_FLOAT_TYPE	LUA_FLOAT_FLOAT
#else
#define LUA_INT_TYPE	LUA_INT_DEFAULT
#define LUA_FLOAT_TYPE	LUA_FLOAT_DEFAULT
#endif

#define LUA_PATH_SEP            ";"
#define LUA_PATH_MARK           "?"
#define LUA_EXEC_DIR            "!"

#define LUA_VDIR	LUA_VERSION_MAJOR "." LUA_VERSION_MINOR
#define LUA_ROOT	"/sdcard/"
#define LUA_LDIR	LUA_ROOT "lua/" LUA_VDIR "/"
#define LUA_CDIR	LUA_ROOT "lua/" LUA_VDIR "/"

#define LUA_PATH_DEFAULT  \
  LUA_LDIR"?.lua;"  LUA_LDIR"?/init.lua;" \
  "./?.lua;" "./?/init.lua"

#define LUA_CPATH_DEFAULT  \
  LUA_CDIR"?.so;" "./?.so"

#if !defined(LUA_DIRSEP)
#define LUA_DIRSEP	"/"
#endif

#define LUA_IGMARK		"-"

#define LUA_API		extern
#define LUALIB_API	LUA_API
#define LUAMOD_API	LUA_API

#if defined(__GNUC__) && ((__GNUC__*100 + __GNUC_MINOR__) >= 302) && \
    defined(__ELF__)
#define LUAI_FUNC	__attribute__((visibility("internal"))) extern
#else
#define LUAI_FUNC	extern
#endif

#define LUAI_DDEC(dec)	LUAI_FUNC dec
#define LUAI_DDEF	/* empty */

#if LUA_FLOAT_TYPE == LUA_FLOAT_FLOAT

#define LUA_NUMBER	float
#define l_floatatt(n)		(FLT_##n)
#define LUAI_UACNUMBER	double
#define LUA_NUMBER_FRMLEN	""
#define LUA_NUMBER_FMT		"%.7g"
#define l_mathop(op)		op##f
#define lua_str2number(s,p)	strtof((s), (p))

#elif LUA_FLOAT_TYPE == LUA_FLOAT_LONGDOUBLE

#define LUA_NUMBER	long double
#define l_floatatt(n)		(LDBL_##n)
#define LUAI_UACNUMBER	long double
#define LUA_NUMBER_FRMLEN	"L"
#define LUA_NUMBER_FMT		"%.19Lg"
#define l_mathop(op)		op##l
#define lua_str2number(s,p)	strtold((s), (p))

#elif LUA_FLOAT_TYPE == LUA_FLOAT_DOUBLE

#define LUA_NUMBER	double
#define l_floatatt(n)		(DBL_##n)
#define LUAI_UACNUMBER	double
#define LUA_NUMBER_FRMLEN	""
#define LUA_NUMBER_FMT		"%.14g"
#define l_mathop(op)		op
#define lua_str2number(s,p)	strtod((s), (p))

#else
#error "numeric float type not defined"
#endif

#define l_floor(x)		(l_mathop(floor)(x))

#define lua_number2str(s,sz,n)  \
	l_sprintf((s), sz, LUA_NUMBER_FMT, (LUAI_UACNUMBER)(n))

#define lua_numbertointeger(n,p) \
  ((n) >= (LUA_NUMBER)(LUA_MININTEGER) && \
   (n) < -(LUA_NUMBER)(LUA_MININTEGER) && \
      (*(p) = (LUA_INTEGER)(n), 1))

#define LUA_INTEGER_FMT		"%" LUA_INTEGER_FRMLEN "d"
#define LUAI_UACINT		LUA_INTEGER

#define lua_integer2str(s,sz,n)  \
	l_sprintf((s), sz, LUA_INTEGER_FMT, (LUAI_UACINT)(n))

#define LUA_UNSIGNED		unsigned LUAI_UACINT

#if LUA_INT_TYPE == LUA_INT_INT

#define LUA_INTEGER		int
#define LUA_INTEGER_FRMLEN	""
#define LUA_MAXINTEGER		INT_MAX
#define LUA_MININTEGER		INT_MIN
#define LUA_MAXUNSIGNED		UINT_MAX

#elif LUA_INT_TYPE == LUA_INT_LONG

#define LUA_INTEGER		long
#define LUA_INTEGER_FRMLEN	"l"
#define LUA_MAXINTEGER		LONG_MAX
#define LUA_MININTEGER		LONG_MIN
#define LUA_MAXUNSIGNED		ULONG_MAX

#elif LUA_INT_TYPE == LUA_INT_LONGLONG

#if defined(LLONG_MAX)
#define LUA_INTEGER		long long
#define LUA_INTEGER_FRMLEN	"ll"
#define LUA_MAXINTEGER		LLONG_MAX
#define LUA_MININTEGER		LLONG_MIN
#define LUA_MAXUNSIGNED		ULLONG_MAX
#elif defined(LUA_USE_WINDOWS)
#define LUA_INTEGER		__int64
#define LUA_INTEGER_FRMLEN	"I64"
#define LUA_MAXINTEGER		_I64_MAX
#define LUA_MININTEGER		_I64_MIN
#define LUA_MAXUNSIGNED		_UI64_MAX
#else
#error "Compiler does not support 'long long'."
#endif

#else
#error "numeric integer type not defined"
#endif

#if !defined(LUA_USE_C89)
#define l_sprintf(s,sz,f,i)	snprintf(s,sz,f,i)
#else
#define l_sprintf(s,sz,f,i)	((void)(sz), sprintf(s,f,i))
#endif

#if !defined(LUA_USE_C89)
#define lua_strx2number(s,p)		lua_str2number(s,p)
#endif

#define lua_pointer2str(buff,sz,p)	l_sprintf(buff,sz,"%p",p)

#if !defined(LUA_USE_C89)
#define lua_number2strx(L,b,sz,f,n)  \
	((void)L, l_sprintf(b,sz,f,(LUAI_UACNUMBER)(n)))
#endif

#if defined(LUA_USE_C89) || (defined(HUGE_VAL) && !defined(HUGE_VALF))
#undef l_mathop
#undef lua_str2number
#define l_mathop(op)		(lua_Number)op
#define lua_str2number(s,p)	((lua_Number)strtod((s), (p)))
#endif

#define LUA_KCONTEXT	ptrdiff_t

#if !defined(LUA_USE_C89) && defined(__STDC_VERSION__) && \
    __STDC_VERSION__ >= 199901L
#include <stdint.h>
#if defined(INTPTR_MAX)
#undef LUA_KCONTEXT
#define LUA_KCONTEXT	intptr_t
#endif
#endif

#if !defined(lua_getlocaledecpoint)
#define lua_getlocaledecpoint()		('.')
#endif

#if !defined(luai_likely)
#if defined(__GNUC__) && !defined(LUA_NOBUILTIN)
#define luai_likely(x)		(__builtin_expect(((x) != 0), 1))
#define luai_unlikely(x)	(__builtin_expect(((x) != 0), 0))
#else
#define luai_likely(x)		(x)
#define luai_unlikely(x)	(x)
#endif
#endif

#if defined(LUA_CORE) || defined(LUA_LIB)
#define l_likely(x)	luai_likely(x)
#define l_unlikely(x)	luai_unlikely(x)
#endif

#if LUAI_IS32INT
#define LUAI_MAXSTACK		100000
#else
#define LUAI_MAXSTACK		15000
#endif

#define LUA_EXTRASPACE		(sizeof(void *))
#define LUA_IDSIZE	60
#define LUAL_BUFFERSIZE   ((int)(16 * sizeof(void*) * sizeof(lua_Number)))
#define LUAI_MAXALIGN  lua_Number n; double u; void *s; lua_Integer i; long l

/*
** Lua output for Esposito. Uses printf (serial) by default.
** Redirected to terminal display in the REPL app.
*/
#if !defined(lua_writestring)
#define lua_writestring(s,l)     printf("%.*s", (int)(l), (s))
#endif

#if !defined(lua_writeline)
#define lua_writeline()          printf("\n")
#endif

#if !defined(lua_writestringerror)
#define lua_writestringerror(s,p) printf((s), (p))
#endif

#endif
