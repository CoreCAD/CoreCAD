// SPDX-License-Identifier: LGPL-2.1-or-later

// clang-format off
/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison implementation for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2021 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output, and Bison version.  */
#define YYBISON 30802

/* Bison version string.  */
#define YYBISON_VERSION "3.8.2"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1




/* First part of user prologue.  */

#define YYSTYPE App::ExpressionParser::semantic_type

std::stack<FunctionExpression::Function> functions;                /**< Function identifier */

#define yyparse ExpressionParser_yyparse
#define yyerror ExpressionParser_yyerror


# ifndef YY_CAST
#  ifdef __cplusplus
#   define YY_CAST(Type, Val) static_cast<Type> (Val)
#   define YY_REINTERPRET_CAST(Type, Val) reinterpret_cast<Type> (Val)
#  else
#   define YY_CAST(Type, Val) ((Type) (Val))
#   define YY_REINTERPRET_CAST(Type, Val) ((Type) (Val))
#  endif
# endif
# ifndef YY_NULLPTR
#  if defined __cplusplus
#   if 201103L <= __cplusplus
#    define YY_NULLPTR nullptr
#   else
#    define YY_NULLPTR 0
#   endif
#  else
#   define YY_NULLPTR ((void*)0)
#  endif
# endif

#include "Expression.tab.h"
/* Symbol kind.  */
enum yysymbol_kind_t
{
  YYSYMBOL_YYEMPTY = -2,
  YYSYMBOL_YYEOF = 0,                      /* "end of file"  */
  YYSYMBOL_YYerror = 1,                    /* error  */
  YYSYMBOL_YYUNDEF = 2,                    /* "invalid token"  */
  YYSYMBOL_FUNC = 3,                       /* FUNC  */
  YYSYMBOL_ONE = 4,                        /* ONE  */
  YYSYMBOL_NUM = 5,                        /* NUM  */
  YYSYMBOL_IDENTIFIER = 6,                 /* IDENTIFIER  */
  YYSYMBOL_UNIT = 7,                       /* UNIT  */
  YYSYMBOL_USUNIT = 8,                     /* USUNIT  */
  YYSYMBOL_INTEGER = 9,                    /* INTEGER  */
  YYSYMBOL_CONSTANT = 10,                  /* CONSTANT  */
  YYSYMBOL_CELLADDRESS = 11,               /* CELLADDRESS  */
  YYSYMBOL_EQ = 12,                        /* EQ  */
  YYSYMBOL_NEQ = 13,                       /* NEQ  */
  YYSYMBOL_LT = 14,                        /* LT  */
  YYSYMBOL_GT = 15,                        /* GT  */
  YYSYMBOL_GTE = 16,                       /* GTE  */
  YYSYMBOL_LTE = 17,                       /* LTE  */
  YYSYMBOL_STRING = 18,                    /* STRING  */
  YYSYMBOL_MINUSSIGN = 19,                 /* MINUSSIGN  */
  YYSYMBOL_PROPERTY_REF = 20,              /* PROPERTY_REF  */
  YYSYMBOL_DOCUMENT = 21,                  /* DOCUMENT  */
  YYSYMBOL_OBJECT = 22,                    /* OBJECT  */
  YYSYMBOL_EXPONENT = 23,                  /* EXPONENT  */
  YYSYMBOL_MISSINGREF = 24,                /* MISSINGREF  */
  YYSYMBOL_25_ = 25,                       /* '?'  */
  YYSYMBOL_26_ = 26,                       /* ':'  */
  YYSYMBOL_27_ = 27,                       /* '+'  */
  YYSYMBOL_28_ = 28,                       /* '*'  */
  YYSYMBOL_29_ = 29,                       /* '/'  */
  YYSYMBOL_30_ = 30,                       /* '%'  */
  YYSYMBOL_NUM_AND_UNIT = 31,              /* NUM_AND_UNIT  */
  YYSYMBOL_32_ = 32,                       /* '^'  */
  YYSYMBOL_NEG = 33,                       /* NEG  */
  YYSYMBOL_POS = 34,                       /* POS  */
  YYSYMBOL_35_ = 35,                       /* ')'  */
  YYSYMBOL_36_ = 36,                       /* '('  */
  YYSYMBOL_37_ = 37,                       /* ','  */
  YYSYMBOL_38_ = 38,                       /* ';'  */
  YYSYMBOL_39_ = 39,                       /* '.'  */
  YYSYMBOL_40_ = 40,                       /* '#'  */
  YYSYMBOL_41_ = 41,                       /* '['  */
  YYSYMBOL_42_ = 42,                       /* ']'  */
  YYSYMBOL_YYACCEPT = 43,                  /* $accept  */
  YYSYMBOL_input = 44,                     /* input  */
  YYSYMBOL_unit_num = 45,                  /* unit_num  */
  YYSYMBOL_exp = 46,                       /* exp  */
  YYSYMBOL_num = 47,                       /* num  */
  YYSYMBOL_args = 48,                      /* args  */
  YYSYMBOL_range = 49,                     /* range  */
  YYSYMBOL_us_building_unit = 50,          /* us_building_unit  */
  YYSYMBOL_other_unit = 51,                /* other_unit  */
  YYSYMBOL_unit_exp = 52,                  /* unit_exp  */
  YYSYMBOL_integer = 53,                   /* integer  */
  YYSYMBOL_id_or_cell = 54,                /* id_or_cell  */
  YYSYMBOL_identifier = 55,                /* identifier  */
  YYSYMBOL_iden = 56,                      /* iden  */
  YYSYMBOL_indexer = 57,                   /* indexer  */
  YYSYMBOL_indexable = 58,                 /* indexable  */
  YYSYMBOL_document = 59,                  /* document  */
  YYSYMBOL_object = 60                     /* object  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;




#ifdef short
# undef short
#endif

/* On compilers that do not define __PTRDIFF_MAX__ etc., make sure
   <limits.h> and (if available) <stdint.h> are included
   so that the code can choose integer types of a good width.  */

#ifndef __PTRDIFF_MAX__
# include <limits.h> /* INFRINGES ON USER NAME SPACE */
# if defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stdint.h> /* INFRINGES ON USER NAME SPACE */
#  define YY_STDINT_H
# endif
#endif

/* Narrow types that promote to a signed type and that can represent a
   signed or unsigned integer of at least N bits.  In tables they can
   save space and decrease cache pressure.  Promoting to a signed type
   helps avoid bugs in integer arithmetic.  */

#ifdef __INT_LEAST8_MAX__
typedef __INT_LEAST8_TYPE__ yytype_int8;
#elif defined YY_STDINT_H
typedef int_least8_t yytype_int8;
#else
typedef signed char yytype_int8;
#endif

#ifdef __INT_LEAST16_MAX__
typedef __INT_LEAST16_TYPE__ yytype_int16;
#elif defined YY_STDINT_H
typedef int_least16_t yytype_int16;
#else
typedef short yytype_int16;
#endif

/* Work around bug in HP-UX 11.23, which defines these macros
   incorrectly for preprocessor constants.  This workaround can likely
   be removed in 2023, as HPE has promised support for HP-UX 11.23
   (aka HP-UX 11i v2) only through the end of 2022; see Table 2 of
   <https://h20195.www2.hpe.com/V2/getpdf.aspx/4AA4-7673ENW.pdf>.  */
#ifdef __hpux
# undef UINT_LEAST8_MAX
# undef UINT_LEAST16_MAX
# define UINT_LEAST8_MAX 255
# define UINT_LEAST16_MAX 65535
#endif

#if defined __UINT_LEAST8_MAX__ && __UINT_LEAST8_MAX__ <= __INT_MAX__
typedef __UINT_LEAST8_TYPE__ yytype_uint8;
#elif (!defined __UINT_LEAST8_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST8_MAX <= INT_MAX)
typedef uint_least8_t yytype_uint8;
#elif !defined __UINT_LEAST8_MAX__ && UCHAR_MAX <= INT_MAX
typedef unsigned char yytype_uint8;
#else
typedef short yytype_uint8;
#endif

#if defined __UINT_LEAST16_MAX__ && __UINT_LEAST16_MAX__ <= __INT_MAX__
typedef __UINT_LEAST16_TYPE__ yytype_uint16;
#elif (!defined __UINT_LEAST16_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST16_MAX <= INT_MAX)
typedef uint_least16_t yytype_uint16;
#elif !defined __UINT_LEAST16_MAX__ && USHRT_MAX <= INT_MAX
typedef unsigned short yytype_uint16;
#else
typedef int yytype_uint16;
#endif

#ifndef YYPTRDIFF_T
# if defined __PTRDIFF_TYPE__ && defined __PTRDIFF_MAX__
#  define YYPTRDIFF_T __PTRDIFF_TYPE__
#  define YYPTRDIFF_MAXIMUM __PTRDIFF_MAX__
# elif defined PTRDIFF_MAX
#  ifndef ptrdiff_t
#   include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  endif
#  define YYPTRDIFF_T ptrdiff_t
#  define YYPTRDIFF_MAXIMUM PTRDIFF_MAX
# else
#  define YYPTRDIFF_T long
#  define YYPTRDIFF_MAXIMUM LONG_MAX
# endif
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned
# endif
#endif

#define YYSIZE_MAXIMUM                                  \
  YY_CAST (YYPTRDIFF_T,                                 \
           (YYPTRDIFF_MAXIMUM < YY_CAST (YYSIZE_T, -1)  \
            ? YYPTRDIFF_MAXIMUM                         \
            : YY_CAST (YYSIZE_T, -1)))

#define YYSIZEOF(X) YY_CAST (YYPTRDIFF_T, sizeof (X))


/* Stored state numbers (used for stacks). */
typedef yytype_uint8 yy_state_t;

/* State numbers in computations.  */
typedef int yy_state_fast_t;

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif


#ifndef YY_ATTRIBUTE_PURE
# if defined __GNUC__ && 2 < __GNUC__ + (96 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_PURE __attribute__ ((__pure__))
# else
#  define YY_ATTRIBUTE_PURE
# endif
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# if defined __GNUC__ && 2 < __GNUC__ + (7 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_UNUSED __attribute__ ((__unused__))
# else
#  define YY_ATTRIBUTE_UNUSED
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YY_USE(E) ((void) (E))
#else
# define YY_USE(E) /* empty */
#endif

/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
#if defined __GNUC__ && ! defined __ICC && 406 <= __GNUC__ * 100 + __GNUC_MINOR__
# if __GNUC__ * 100 + __GNUC_MINOR__ < 407
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")
# else
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")              \
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# endif
# define YY_IGNORE_MAYBE_UNINITIALIZED_END      \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif

#if defined __cplusplus && defined __GNUC__ && ! defined __ICC && 6 <= __GNUC__
# define YY_IGNORE_USELESS_CAST_BEGIN                          \
    _Pragma ("GCC diagnostic push")                            \
    _Pragma ("GCC diagnostic ignored \"-Wuseless-cast\"")
# define YY_IGNORE_USELESS_CAST_END            \
    _Pragma ("GCC diagnostic pop")
#endif
#ifndef YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_END
#endif


#define YY_ASSERT(E) ((void) (0 && (E)))

#if !defined yyoverflow

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
             && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* !defined yyoverflow */

#if (! defined yyoverflow \
     && (! defined __cplusplus \
         || (defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yy_state_t yyss_alloc;
  YYSTYPE yyvs_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (YYSIZEOF (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (YYSIZEOF (yy_state_t) + YYSIZEOF (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYPTRDIFF_T yynewbytes;                                         \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * YYSIZEOF (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / YYSIZEOF (*yyptr);                        \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, YY_CAST (YYSIZE_T, (Count)) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYPTRDIFF_T yyi;                      \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  41
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   445

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  43
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  18
/* YYNRULES -- Number of rules.  */
#define YYNRULES  77
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  141

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   282


/* YYTRANSLATE(TOKEN-NUM) -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, with out-of-bounds checking.  */
#define YYTRANSLATE(YYX)                                \
  (0 <= (YYX) && (YYX) <= YYMAXUTOK                     \
   ? YY_CAST (yysymbol_kind_t, yytranslate[YYX])        \
   : YYSYMBOL_YYUNDEF)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex.  */
static const yytype_int8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,    40,     2,    30,     2,     2,
      36,    35,    28,    27,    37,     2,    39,    29,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,    26,    38,
       2,     2,     2,    25,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,    41,     2,    42,    32,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      31,    33,    34
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_uint8 yyrline[] =
{
       0,    84,    84,    85,    88,    89,    92,    93,    94,    95,
      96,    97,    98,    99,   100,   101,   102,   103,   104,   105,
     106,   107,   108,   109,   110,   111,   112,   113,   114,   115,
     118,   119,   120,   121,   123,   124,   125,   126,   127,   128,
     131,   135,   136,   138,   139,   140,   141,   142,   143,   144,
     147,   148,   152,   153,   157,   158,   162,   167,   172,   178,
     185,   192,   199,   203,   204,   205,   206,   207,   208,   209,
     210,   214,   215,   216,   220,   221,   225,   226
};
#endif

/** Accessing symbol of state STATE.  */
#define YY_ACCESSING_SYMBOL(State) YY_CAST (yysymbol_kind_t, yystos[State])

#if YYDEBUG || 0
/* The user-facing name of the symbol whose (internal) number is
   YYSYMBOL.  No bounds checking.  */
static const char *yysymbol_name (yysymbol_kind_t yysymbol) YY_ATTRIBUTE_UNUSED;

/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "\"end of file\"", "error", "\"invalid token\"", "FUNC", "ONE", "NUM",
  "IDENTIFIER", "UNIT", "USUNIT", "INTEGER", "CONSTANT", "CELLADDRESS",
  "EQ", "NEQ", "LT", "GT", "GTE", "LTE", "STRING", "MINUSSIGN",
  "PROPERTY_REF", "DOCUMENT", "OBJECT", "EXPONENT", "MISSINGREF", "'?'",
  "':'", "'+'", "'*'", "'/'", "'%'", "NUM_AND_UNIT", "'^'", "NEG", "POS",
  "')'", "'('", "','", "';'", "'.'", "'#'", "'['", "']'", "$accept",
  "input", "unit_num", "exp", "num", "args", "range", "us_building_unit",
  "other_unit", "unit_exp", "integer", "id_or_cell", "identifier", "iden",
  "indexer", "indexable", "document", "object", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-33)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-78)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
     102,   205,   -33,   -33,   -15,   -33,   -33,   -33,   -33,   -33,
     -32,   205,   -33,   205,   102,    17,    31,   -33,   413,     5,
     -33,   -33,    87,    -5,    -4,     1,    57,     4,    20,   205,
     413,   107,   -33,   -10,   -33,   -33,   368,    62,   -33,    24,
     -33,   -33,   205,   205,   205,   205,   205,   205,   205,   205,
     205,   205,   102,   205,   205,     5,   126,    35,     5,     5,
      11,   157,   -33,    50,    66,   -33,    44,    46,   -33,   205,
     205,    47,   -33,   -33,    47,    19,    19,    19,    19,    19,
      19,    95,   392,    95,    56,    56,    35,    56,   -33,    92,
      35,    35,   -33,   -33,    13,   -33,   168,   221,   -33,   -33,
     -33,   -33,    75,    83,   -33,   413,   -33,   413,   -33,   -33,
     -33,   205,   -33,   -33,   205,   242,     0,   -33,    81,    47,
     413,   284,   205,   -33,   205,   -33,   263,    93,   -33,   -33,
     -33,   305,   326,   205,   -33,    47,   -33,   -33,   347,   -33,
     -33
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int8 yydefact[] =
{
       0,     0,    30,    31,    52,    42,    41,    32,    33,    53,
       8,     0,    10,     0,     0,     0,     0,     7,     2,     6,
      44,    43,     3,    54,     9,    55,    26,     0,     0,     0,
      34,     0,    35,    54,    11,    12,     0,     0,    52,     0,
      57,     1,     0,     0,     0,     0,     0,     0,     0,     0,
       0,     0,     0,     0,     0,     0,    44,     4,     0,     0,
       0,     0,    71,     0,     0,    72,     0,     0,    27,     0,
       0,     0,    29,    49,     0,    20,    21,    22,    23,    24,
      25,    14,     0,    13,    15,    16,    18,    17,    19,     0,
      46,    45,    51,    50,     0,    47,     0,     0,    62,    73,
      76,    77,     0,     0,    59,    36,    38,    37,    39,    40,
      56,     0,     5,    48,     0,     0,     0,    63,     0,     0,
      28,     0,     0,    65,     0,    64,     0,     0,    60,    58,
      66,     0,     0,     0,    67,     0,    69,    68,     0,    61,
      70
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int8 yypgoto[] =
{
     -33,   -33,   -33,    32,    33,   -33,    34,   -18,   -33,     2,
      39,    -1,   -33,   -33,    69,   -33,   -33,    71
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int8 yydefgoto[] =
{
       0,    16,    17,    36,    19,    31,    32,    20,    21,    37,
      95,    23,    24,    25,    62,    26,    27,    28
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_int16 yytable[] =
{
      33,    56,    22,     1,     2,     3,     4,   -76,   -74,     7,
       8,     9,     5,     6,    40,    92,    71,    92,    10,    11,
      93,    57,    93,    38,    12,   -75,   124,    13,     9,   -77,
      94,    41,    18,    30,   -77,    39,    29,    61,    48,    15,
      63,    55,   125,    34,    66,    35,    50,    51,    52,    53,
      38,    54,    38,    38,    86,     9,    98,     9,     9,    67,
      90,    91,   100,    74,   103,   101,   104,    60,    33,    33,
     109,   112,    99,   110,    75,    76,    77,    78,    79,    80,
      81,    82,    83,    84,    85,    87,    88,    38,    54,    89,
      58,    59,     9,    97,    60,    65,    64,    73,    61,   127,
       6,   105,   107,   106,   108,     1,     2,     3,     4,     5,
       6,     7,     8,     9,   118,    58,    59,   128,   129,    60,
      10,    11,   119,    51,    52,    53,    12,    54,   115,    13,
       2,     3,   135,   113,   139,     7,     8,   102,    14,     0,
       0,    15,    68,   120,    69,    70,   121,     0,   126,     0,
       0,     0,     0,     0,   131,     0,   132,     0,     0,     0,
       1,     2,     3,     4,     0,   138,     7,     8,     9,     0,
       0,     1,     2,     3,     4,    10,    11,     7,     8,     9,
       0,    12,     0,    96,    13,     0,    10,    11,     0,     0,
       0,     0,    12,    29,   114,    13,    15,     0,     0,     0,
       0,     0,     0,     0,    29,     0,     0,    15,     1,     2,
       3,     4,     0,     0,     7,     8,     9,     0,     0,     0,
       0,     0,     0,    10,    11,     0,     0,     0,     0,    12,
       0,     0,    13,    42,    43,    44,    45,    46,    47,     0,
      48,    29,     0,     0,    15,     0,    49,   116,    50,    51,
      52,    53,     0,    54,    42,    43,    44,    45,    46,    47,
       0,    48,     0,   117,     0,     0,     0,    49,   122,    50,
      51,    52,    53,     0,    54,    42,    43,    44,    45,    46,
      47,     0,    48,     0,   123,     0,     0,     0,    49,   133,
      50,    51,    52,    53,     0,    54,    42,    43,    44,    45,
      46,    47,     0,    48,     0,   134,     0,     0,     0,    49,
       0,    50,    51,    52,    53,     0,    54,    42,    43,    44,
      45,    46,    47,     0,    48,     0,   130,     0,     0,     0,
      49,     0,    50,    51,    52,    53,     0,    54,    42,    43,
      44,    45,    46,    47,     0,    48,     0,   136,     0,     0,
       0,    49,     0,    50,    51,    52,    53,     0,    54,    42,
      43,    44,    45,    46,    47,     0,    48,     0,   137,     0,
       0,     0,    49,     0,    50,    51,    52,    53,     0,    54,
      42,    43,    44,    45,    46,    47,     0,    48,     0,   140,
       0,     0,     0,    49,     0,    50,    51,    52,    53,     0,
      54,     0,     0,    72,    42,    43,    44,    45,    46,    47,
       0,    48,     0,     0,     0,     0,     0,    49,   111,    50,
      51,    52,    53,     0,    54,    42,    43,    44,    45,    46,
      47,     0,    48,     0,     0,     0,     0,     0,    49,     0,
      50,    51,    52,    53,     0,    54
};

static const yytype_int16 yycheck[] =
{
       1,    19,     0,     3,     4,     5,     6,    39,    40,     9,
      10,    11,     7,     8,    15,     4,    26,     4,    18,    19,
       9,    19,     9,     6,    24,    40,    26,    27,    11,    39,
      19,     0,     0,     1,    39,    18,    36,    41,    19,    39,
      39,    36,    42,    11,    40,    13,    27,    28,    29,    30,
       6,    32,     6,     6,    52,    11,     6,    11,    11,    39,
      58,    59,    18,    39,    18,    66,    67,    32,    69,    70,
      71,    89,     6,    74,    42,    43,    44,    45,    46,    47,
      48,    49,    50,    51,    52,    53,    54,     6,    32,    56,
      28,    29,    11,    61,    32,    26,    39,    35,    41,    18,
       8,    69,    70,    69,    70,     3,     4,     5,     6,     7,
       8,     9,    10,    11,    39,    28,    29,   118,   119,    32,
      18,    19,    39,    28,    29,    30,    24,    32,    96,    27,
       4,     5,    39,    94,   135,     9,    10,    66,    36,    -1,
      -1,    39,    35,   111,    37,    38,   114,    -1,   116,    -1,
      -1,    -1,    -1,    -1,   122,    -1,   124,    -1,    -1,    -1,
       3,     4,     5,     6,    -1,   133,     9,    10,    11,    -1,
      -1,     3,     4,     5,     6,    18,    19,     9,    10,    11,
      -1,    24,    -1,    26,    27,    -1,    18,    19,    -1,    -1,
      -1,    -1,    24,    36,    26,    27,    39,    -1,    -1,    -1,
      -1,    -1,    -1,    -1,    36,    -1,    -1,    39,     3,     4,
       5,     6,    -1,    -1,     9,    10,    11,    -1,    -1,    -1,
      -1,    -1,    -1,    18,    19,    -1,    -1,    -1,    -1,    24,
      -1,    -1,    27,    12,    13,    14,    15,    16,    17,    -1,
      19,    36,    -1,    -1,    39,    -1,    25,    26,    27,    28,
      29,    30,    -1,    32,    12,    13,    14,    15,    16,    17,
      -1,    19,    -1,    42,    -1,    -1,    -1,    25,    26,    27,
      28,    29,    30,    -1,    32,    12,    13,    14,    15,    16,
      17,    -1,    19,    -1,    42,    -1,    -1,    -1,    25,    26,
      27,    28,    29,    30,    -1,    32,    12,    13,    14,    15,
      16,    17,    -1,    19,    -1,    42,    -1,    -1,    -1,    25,
      -1,    27,    28,    29,    30,    -1,    32,    12,    13,    14,
      15,    16,    17,    -1,    19,    -1,    42,    -1,    -1,    -1,
      25,    -1,    27,    28,    29,    30,    -1,    32,    12,    13,
      14,    15,    16,    17,    -1,    19,    -1,    42,    -1,    -1,
      -1,    25,    -1,    27,    28,    29,    30,    -1,    32,    12,
      13,    14,    15,    16,    17,    -1,    19,    -1,    42,    -1,
      -1,    -1,    25,    -1,    27,    28,    29,    30,    -1,    32,
      12,    13,    14,    15,    16,    17,    -1,    19,    -1,    42,
      -1,    -1,    -1,    25,    -1,    27,    28,    29,    30,    -1,
      32,    -1,    -1,    35,    12,    13,    14,    15,    16,    17,
      -1,    19,    -1,    -1,    -1,    -1,    -1,    25,    26,    27,
      28,    29,    30,    -1,    32,    12,    13,    14,    15,    16,
      17,    -1,    19,    -1,    -1,    -1,    -1,    -1,    25,    -1,
      27,    28,    29,    30,    -1,    32
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,     3,     4,     5,     6,     7,     8,     9,    10,    11,
      18,    19,    24,    27,    36,    39,    44,    45,    46,    47,
      50,    51,    52,    54,    55,    56,    58,    59,    60,    36,
      46,    48,    49,    54,    46,    46,    46,    52,     6,    18,
      54,     0,    12,    13,    14,    15,    16,    17,    19,    25,
      27,    28,    29,    30,    32,    36,    50,    52,    28,    29,
      32,    41,    57,    39,    39,    57,    40,    39,    35,    37,
      38,    26,    35,    35,    39,    46,    46,    46,    46,    46,
      46,    46,    46,    46,    46,    46,    52,    46,    46,    47,
      52,    52,     4,     9,    19,    53,    26,    46,     6,     6,
      18,    54,    60,    18,    54,    46,    49,    46,    49,    54,
      54,    26,    50,    53,    26,    46,    26,    42,    39,    39,
      46,    46,    26,    42,    26,    42,    46,    18,    54,    54,
      42,    46,    46,    26,    42,    39,    42,    42,    46,    54,
      42
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    43,    44,    44,    45,    45,    46,    46,    46,    46,
      46,    46,    46,    46,    46,    46,    46,    46,    46,    46,
      46,    46,    46,    46,    46,    46,    46,    46,    46,    46,
      47,    47,    47,    47,    48,    48,    48,    48,    48,    48,
      49,    50,    51,    52,    52,    52,    52,    52,    52,    52,
      53,    53,    54,    54,    55,    55,    56,    56,    56,    56,
      56,    56,    56,    57,    57,    57,    57,    57,    57,    57,
      57,    58,    58,    58,    59,    59,    60,    60
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     1,     1,     2,     4,     1,     1,     1,     1,
       1,     2,     2,     3,     3,     3,     3,     3,     3,     3,
       3,     3,     3,     3,     3,     3,     1,     3,     5,     3,
       1,     1,     1,     1,     1,     1,     3,     3,     3,     3,
       3,     1,     1,     1,     1,     3,     3,     3,     4,     3,
       1,     1,     1,     1,     1,     1,     4,     2,     5,     3,
       5,     7,     3,     3,     4,     4,     5,     5,     6,     6,
       7,     2,     2,     3,     1,     1,     1,     1
};


enum { YYENOMEM = -2 };

#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYNOMEM         goto yyexhaustedlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                    \
  do                                                              \
    if (yychar == YYEMPTY)                                        \
      {                                                           \
        yychar = (Token);                                         \
        yylval = (Value);                                         \
        YYPOPSTACK (yylen);                                       \
        yystate = *yyssp;                                         \
        goto yybackup;                                            \
      }                                                           \
    else                                                          \
      {                                                           \
        yyerror (YY_("syntax error: cannot back up")); \
        YYERROR;                                                  \
      }                                                           \
  while (0)

/* Backward compatibility with an undocumented macro.
   Use YYerror or YYUNDEF. */
#define YYERRCODE YYUNDEF


/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)




# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Kind, Value); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo,
                       yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  FILE *yyoutput = yyo;
  YY_USE (yyoutput);
  if (!yyvaluep)
    return;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/*---------------------------.
| Print this symbol on YYO.  |
`---------------------------*/

static void
yy_symbol_print (FILE *yyo,
                 yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  YYFPRINTF (yyo, "%s %s (",
             yykind < YYNTOKENS ? "token" : "nterm", yysymbol_name (yykind));

  yy_symbol_value_print (yyo, yykind, yyvaluep);
  YYFPRINTF (yyo, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yy_state_t *yybottom, yy_state_t *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yy_state_t *yyssp, YYSTYPE *yyvsp,
                 int yyrule)
{
  int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %d):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       YY_ACCESSING_SYMBOL (+yyssp[yyi + 1 - yynrhs]),
                       &yyvsp[(yyi + 1) - (yynrhs)]);
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, Rule); \
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args) ((void) 0)
# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif






/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg,
            yysymbol_kind_t yykind, YYSTYPE *yyvaluep)
{
  YY_USE (yyvaluep);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yykind, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  switch (yykind)
    {
    case YYSYMBOL_exp: /* exp  */
            { delete ((*yyvaluep).expr); }
        break;

    case YYSYMBOL_num: /* num  */
            { delete ((*yyvaluep).expr); }
        break;

    case YYSYMBOL_args: /* args  */
            { std::vector<Expression*>::const_iterator i = ((*yyvaluep).arguments).begin(); while (i != ((*yyvaluep).arguments).end()) { delete *i; ++i; } }
        break;

    case YYSYMBOL_range: /* range  */
            { delete ((*yyvaluep).expr); }
        break;

    case YYSYMBOL_unit_exp: /* unit_exp  */
            { delete ((*yyvaluep).expr); }
        break;

    case YYSYMBOL_indexer: /* indexer  */
            { delete ((*yyvaluep).component); }
        break;

    case YYSYMBOL_indexable: /* indexable  */
            { delete ((*yyvaluep).expr); }
        break;

      default:
        break;
    }
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/* Lookahead token kind.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;
/* Number of syntax errors so far.  */
int yynerrs;




/*----------.
| yyparse.  |
`----------*/

int
yyparse (void)
{
    yy_state_fast_t yystate = 0;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus = 0;

    /* Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* Their size.  */
    YYPTRDIFF_T yystacksize = YYINITDEPTH;

    /* The state stack: array, bottom, top.  */
    yy_state_t yyssa[YYINITDEPTH];
    yy_state_t *yyss = yyssa;
    yy_state_t *yyssp = yyss;

    /* The semantic value stack: array, bottom, top.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs = yyvsa;
    YYSTYPE *yyvsp = yyvs;

  int yyn;
  /* The return value of yyparse.  */
  int yyresult;
  /* Lookahead symbol kind.  */
  yysymbol_kind_t yytoken = YYSYMBOL_YYEMPTY;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;



#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yychar = YYEMPTY; /* Cause a token to be read.  */

  goto yysetstate;


/*------------------------------------------------------------.
| yynewstate -- push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;


/*--------------------------------------------------------------------.
| yysetstate -- set current state (the top of the stack) to yystate.  |
`--------------------------------------------------------------------*/
yysetstate:
  YYDPRINTF ((stderr, "Entering state %d\n", yystate));
  YY_ASSERT (0 <= yystate && yystate < YYNSTATES);
  YY_IGNORE_USELESS_CAST_BEGIN
  *yyssp = YY_CAST (yy_state_t, yystate);
  YY_IGNORE_USELESS_CAST_END
  YY_STACK_PRINT (yyss, yyssp);

  if (yyss + yystacksize - 1 <= yyssp)
#if !defined yyoverflow && !defined YYSTACK_RELOCATE
    YYNOMEM;
#else
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYPTRDIFF_T yysize = yyssp - yyss + 1;

# if defined yyoverflow
      {
        /* Give user a chance to reallocate the stack.  Use copies of
           these so that the &'s don't force the real ones into
           memory.  */
        yy_state_t *yyss1 = yyss;
        YYSTYPE *yyvs1 = yyvs;

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * YYSIZEOF (*yyssp),
                    &yyvs1, yysize * YYSIZEOF (*yyvsp),
                    &yystacksize);
        yyss = yyss1;
        yyvs = yyvs1;
      }
# else /* defined YYSTACK_RELOCATE */
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
        YYNOMEM;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yy_state_t *yyss1 = yyss;
        union yyalloc *yyptr =
          YY_CAST (union yyalloc *,
                   YYSTACK_ALLOC (YY_CAST (YYSIZE_T, YYSTACK_BYTES (yystacksize))));
        if (! yyptr)
          YYNOMEM;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
#  undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;

      YY_IGNORE_USELESS_CAST_BEGIN
      YYDPRINTF ((stderr, "Stack size increased to %ld\n",
                  YY_CAST (long, yystacksize)));
      YY_IGNORE_USELESS_CAST_END

      if (yyss + yystacksize - 1 <= yyssp)
        YYABORT;
    }
#endif /* !defined yyoverflow && !defined YYSTACK_RELOCATE */


  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;


/*-----------.
| yybackup.  |
`-----------*/
yybackup:
  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yypact_value_is_default (yyn))
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either empty, or end-of-input, or a valid lookahead.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token\n"));
      yychar = yylex ();
    }

  if (yychar <= YYEOF)
    {
      yychar = YYEOF;
      yytoken = YYSYMBOL_YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else if (yychar == YYerror)
    {
      /* The scanner already issued an error message, process directly
         to error recovery.  But do not keep the error token as
         lookahead, it is too special and may lead us to an endless
         loop in error recovery. */
      yychar = YYUNDEF;
      yytoken = YYSYMBOL_YYerror;
      goto yyerrlab1;
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yytable_value_is_error (yyn))
        goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);
  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

  /* Discard the shifted token.  */
  yychar = YYEMPTY;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     '$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
  case 2: /* input: exp  */
                                                { ScanResult = std::unique_ptr<Expression>((yyvsp[0].expr)); valueExpression = true;                                        }
    break;

  case 3: /* input: unit_exp  */
                                                { ScanResult = std::unique_ptr<Expression>((yyvsp[0].expr)); unitExpression = true;                                         }
    break;

  case 4: /* unit_num: num unit_exp  */
                                                { (yyval.expr) = new OperatorExpression(DocumentObject, (yyvsp[-1].expr), OperatorExpression::UNIT, (yyvsp[0].expr));  }
    break;

  case 5: /* unit_num: num us_building_unit num us_building_unit  */
                                                                         { (yyval.expr) = new OperatorExpression(DocumentObject, new OperatorExpression(DocumentObject, (yyvsp[-3].expr), OperatorExpression::UNIT, (yyvsp[-2].expr)), OperatorExpression::ADD, new OperatorExpression(DocumentObject, (yyvsp[-1].expr), OperatorExpression::UNIT, (yyvsp[0].expr)));}
    break;

  case 6: /* exp: num  */
                                                { (yyval.expr) = (yyvsp[0].expr);                                                                        }
    break;

  case 7: /* exp: unit_num  */
                                                { (yyval.expr) = (yyvsp[0].expr);                                                                        }
    break;

  case 8: /* exp: STRING  */
                                                { (yyval.expr) = new StringExpression(DocumentObject, (yyvsp[0].string));                                  }
    break;

  case 9: /* exp: identifier  */
                                                { (yyval.expr) = new VariableExpression(DocumentObject, (yyvsp[0].path));                                }
    break;

  case 10: /* exp: MISSINGREF  */
                                                { (yyval.expr) = VariableExpression::missingReference(DocumentObject);                     }
    break;

  case 11: /* exp: MINUSSIGN exp  */
                                                { (yyval.expr) = new OperatorExpression(DocumentObject, (yyvsp[0].expr), OperatorExpression::NEG, new NumberExpression(DocumentObject, Quantity(-1))); }
    break;

  case 12: /* exp: '+' exp  */
                                                { (yyval.expr) = new OperatorExpression(DocumentObject, (yyvsp[0].expr), OperatorExpression::POS, new NumberExpression(DocumentObject, Quantity(1))); }
    break;

  case 13: /* exp: exp '+' exp  */
                                                { (yyval.expr) = new OperatorExpression(DocumentObject, (yyvsp[-2].expr), OperatorExpression::ADD, (yyvsp[0].expr));   }
    break;

  case 14: /* exp: exp MINUSSIGN exp  */
                                                { (yyval.expr) = new OperatorExpression(DocumentObject, (yyvsp[-2].expr), OperatorExpression::SUB, (yyvsp[0].expr));   }
    break;

  case 15: /* exp: exp '*' exp  */
                                                { (yyval.expr) = new OperatorExpression(DocumentObject, (yyvsp[-2].expr), OperatorExpression::MUL, (yyvsp[0].expr));   }
    break;

  case 16: /* exp: exp '/' exp  */
                                                { (yyval.expr) = new OperatorExpression(DocumentObject, (yyvsp[-2].expr), OperatorExpression::DIV, (yyvsp[0].expr));   }
    break;

  case 17: /* exp: exp '%' exp  */
                                                { (yyval.expr) = new OperatorExpression(DocumentObject, (yyvsp[-2].expr), OperatorExpression::MOD, (yyvsp[0].expr));   }
    break;

  case 18: /* exp: exp '/' unit_exp  */
                                                { (yyval.expr) = new OperatorExpression(DocumentObject, (yyvsp[-2].expr), OperatorExpression::DIV, (yyvsp[0].expr));   }
    break;

  case 19: /* exp: exp '^' exp  */
                                                { (yyval.expr) = new OperatorExpression(DocumentObject, (yyvsp[-2].expr), OperatorExpression::POW, (yyvsp[0].expr));   }
    break;

  case 20: /* exp: exp EQ exp  */
                                                { (yyval.expr) = new OperatorExpression(DocumentObject, (yyvsp[-2].expr), OperatorExpression::EQ, (yyvsp[0].expr));    }
    break;

  case 21: /* exp: exp NEQ exp  */
                                                { (yyval.expr) = new OperatorExpression(DocumentObject, (yyvsp[-2].expr), OperatorExpression::NEQ, (yyvsp[0].expr));   }
    break;

  case 22: /* exp: exp LT exp  */
                                                { (yyval.expr) = new OperatorExpression(DocumentObject, (yyvsp[-2].expr), OperatorExpression::LT, (yyvsp[0].expr));    }
    break;

  case 23: /* exp: exp GT exp  */
                                                { (yyval.expr) = new OperatorExpression(DocumentObject, (yyvsp[-2].expr), OperatorExpression::GT, (yyvsp[0].expr));    }
    break;

  case 24: /* exp: exp GTE exp  */
                                                { (yyval.expr) = new OperatorExpression(DocumentObject, (yyvsp[-2].expr), OperatorExpression::GTE, (yyvsp[0].expr));   }
    break;

  case 25: /* exp: exp LTE exp  */
                                                { (yyval.expr) = new OperatorExpression(DocumentObject, (yyvsp[-2].expr), OperatorExpression::LTE, (yyvsp[0].expr));   }
    break;

  case 26: /* exp: indexable  */
                                                { (yyval.expr) = (yyvsp[0].expr);                                                                        }
    break;

  case 27: /* exp: FUNC args ')'  */
                                                { (yyval.expr) = new FunctionExpression(DocumentObject, (yyvsp[-2].func).first, std::move((yyvsp[-2].func).second), (yyvsp[-1].arguments));}
    break;

  case 28: /* exp: exp '?' exp ':' exp  */
                                                { (yyval.expr) = new ConditionalExpression(DocumentObject, (yyvsp[-4].expr), (yyvsp[-2].expr), (yyvsp[0].expr));                     }
    break;

  case 29: /* exp: '(' exp ')'  */
                                                { (yyval.expr) = (yyvsp[-1].expr); }
    break;

  case 30: /* num: ONE  */
                                                { (yyval.expr) = new NumberExpression(DocumentObject, Quantity((yyvsp[0].fvalue)));                        }
    break;

  case 31: /* num: NUM  */
                                                { (yyval.expr) = new NumberExpression(DocumentObject, Quantity((yyvsp[0].fvalue)));                        }
    break;

  case 32: /* num: INTEGER  */
                                                { (yyval.expr) = new NumberExpression(DocumentObject, Quantity((double)(yyvsp[0].ivalue)));                }
    break;

  case 33: /* num: CONSTANT  */
                                                { (yyval.expr) = new ConstantExpression(DocumentObject, (yyvsp[0].constant).name, Quantity((yyvsp[0].constant).fvalue));      }
    break;

  case 34: /* args: exp  */
                                                { (yyval.arguments).push_back((yyvsp[0].expr));                                                               }
    break;

  case 35: /* args: range  */
                                                { (yyval.arguments).push_back((yyvsp[0].expr));                                                               }
    break;

  case 36: /* args: args ',' exp  */
                                                { (yyvsp[-2].arguments).push_back((yyvsp[0].expr));  (yyval.arguments) = (yyvsp[-2].arguments);                                                     }
    break;

  case 37: /* args: args ';' exp  */
                                                { (yyvsp[-2].arguments).push_back((yyvsp[0].expr));  (yyval.arguments) = (yyvsp[-2].arguments);                                                     }
    break;

  case 38: /* args: args ',' range  */
                                                { (yyvsp[-2].arguments).push_back((yyvsp[0].expr));  (yyval.arguments) = (yyvsp[-2].arguments);                                                     }
    break;

  case 39: /* args: args ';' range  */
                                                { (yyvsp[-2].arguments).push_back((yyvsp[0].expr));  (yyval.arguments) = (yyvsp[-2].arguments);                                                     }
    break;

  case 40: /* range: id_or_cell ':' id_or_cell  */
                                                { (yyval.expr) = new RangeExpression(DocumentObject, (yyvsp[-2].string), (yyvsp[0].string));                               }
    break;

  case 41: /* us_building_unit: USUNIT  */
                                                { (yyval.expr) = new UnitExpression(DocumentObject, (yyvsp[0].quantity).scaler, (yyvsp[0].quantity).unitStr );                }
    break;

  case 42: /* other_unit: UNIT  */
                                                { (yyval.expr) = new UnitExpression(DocumentObject, (yyvsp[0].quantity).scaler, (yyvsp[0].quantity).unitStr );                }
    break;

  case 43: /* unit_exp: other_unit  */
                                                { (yyval.expr) = (yyvsp[0].expr); }
    break;

  case 44: /* unit_exp: us_building_unit  */
                                                { (yyval.expr) = (yyvsp[0].expr); }
    break;

  case 45: /* unit_exp: unit_exp '/' unit_exp  */
                                                { (yyval.expr) = new OperatorExpression(DocumentObject, (yyvsp[-2].expr), OperatorExpression::DIV, (yyvsp[0].expr));   }
    break;

  case 46: /* unit_exp: unit_exp '*' unit_exp  */
                                                { (yyval.expr) = new OperatorExpression(DocumentObject, (yyvsp[-2].expr), OperatorExpression::MUL, (yyvsp[0].expr));   }
    break;

  case 47: /* unit_exp: unit_exp '^' integer  */
                                                { (yyval.expr) = new OperatorExpression(DocumentObject, (yyvsp[-2].expr), OperatorExpression::POW, new NumberExpression(DocumentObject, Quantity((double)(yyvsp[0].ivalue))));   }
    break;

  case 48: /* unit_exp: unit_exp '^' MINUSSIGN integer  */
                                                { (yyval.expr) = new OperatorExpression(DocumentObject, (yyvsp[-3].expr), OperatorExpression::POW, new OperatorExpression(DocumentObject, new NumberExpression(DocumentObject, Quantity((double)(yyvsp[0].ivalue))), OperatorExpression::NEG, new NumberExpression(DocumentObject, Quantity(-1))));   }
    break;

  case 49: /* unit_exp: '(' unit_exp ')'  */
                                                { (yyval.expr) = (yyvsp[-1].expr);                                                                        }
    break;

  case 50: /* integer: INTEGER  */
                 { (yyval.ivalue) = (yyvsp[0].ivalue); }
    break;

  case 51: /* integer: ONE  */
             { (yyval.ivalue) = (yyvsp[0].fvalue); }
    break;

  case 52: /* id_or_cell: IDENTIFIER  */
                                            { (yyval.string) = std::move((yyvsp[0].string)); }
    break;

  case 53: /* id_or_cell: CELLADDRESS  */
                                            { (yyval.string) = std::move((yyvsp[0].string)); }
    break;

  case 54: /* identifier: id_or_cell  */
                                            { (yyval.path) = ObjectIdentifier(DocumentObject); (yyval.path) << ObjectIdentifier::SimpleComponent((yyvsp[0].string)); }
    break;

  case 55: /* identifier: iden  */
                                            { (yyval.path) = std::move((yyvsp[0].path)); }
    break;

  case 56: /* iden: '.' STRING '.' id_or_cell  */
                                            { /* Path to property of a sub-object of the current object*/
                                                (yyval.path) = ObjectIdentifier(DocumentObject,true);
                                                (yyval.path).setDocumentObjectName(DocumentObject,false,ObjectIdentifier::String(std::move((yyvsp[-2].string)),true),true);
                                                (yyval.path).addComponent(ObjectIdentifier::SimpleComponent((yyvsp[0].string)));
                                            }
    break;

  case 57: /* iden: '.' id_or_cell  */
                                            { /* Path to property of the current document object */
                                                (yyval.path) = ObjectIdentifier(DocumentObject,true);
                                                (yyval.path).setDocumentObjectName(DocumentObject);
                                                (yyval.path).addComponent(ObjectIdentifier::SimpleComponent((yyvsp[0].string)));
                                            }
    break;

  case 58: /* iden: object '.' STRING '.' id_or_cell  */
                                            { /* Path to property of a sub-object */
                                                (yyval.path) = ObjectIdentifier(DocumentObject);
                                                (yyval.path).setDocumentObjectName(std::move((yyvsp[-4].string_or_identifier)), true, ObjectIdentifier::String(std::move((yyvsp[-2].string)),true),true);
                                                (yyval.path).addComponent(ObjectIdentifier::SimpleComponent((yyvsp[0].string)));
                                                (yyval.path).resolveAmbiguity();
                                            }
    break;

  case 59: /* iden: object '.' id_or_cell  */
                                            { /* Path to property of a given document object */
                                                (yyval.path) = ObjectIdentifier(DocumentObject);
                                                (yyvsp[-2].string_or_identifier).checkImport(DocumentObject);
                                                (yyval.path).addComponent(ObjectIdentifier::SimpleComponent((yyvsp[-2].string_or_identifier)));
                                                (yyval.path).addComponent(ObjectIdentifier::SimpleComponent((yyvsp[0].string)));
                                                (yyval.path).resolveAmbiguity();
                                            }
    break;

  case 60: /* iden: document '#' object '.' id_or_cell  */
                                            { /* Path to property from an external document, within a named document object */
                                                (yyval.path) = ObjectIdentifier(DocumentObject);
                                                (yyval.path).setDocumentName(std::move((yyvsp[-4].string_or_identifier)), true);
                                                (yyval.path).setDocumentObjectName(std::move((yyvsp[-2].string_or_identifier)), true);
                                                (yyval.path).addComponent(ObjectIdentifier::SimpleComponent((yyvsp[0].string)));
                                                (yyval.path).resolveAmbiguity();
                                            }
    break;

  case 61: /* iden: document '#' object '.' STRING '.' id_or_cell  */
                                            {   (yyval.path) = ObjectIdentifier(DocumentObject);
                                                (yyval.path).setDocumentName(std::move((yyvsp[-6].string_or_identifier)), true);
                                                (yyval.path).setDocumentObjectName(std::move((yyvsp[-4].string_or_identifier)), true, ObjectIdentifier::String(std::move((yyvsp[-2].string)),true));
                                                (yyval.path).addComponent(ObjectIdentifier::SimpleComponent((yyvsp[0].string)));
                                                (yyval.path).resolveAmbiguity();
                                            }
    break;

  case 62: /* iden: iden '.' IDENTIFIER  */
                                            { (yyval.path)= std::move((yyvsp[-2].path)); (yyval.path).addComponent(ObjectIdentifier::SimpleComponent((yyvsp[0].string))); }
    break;

  case 63: /* indexer: '[' exp ']'  */
                                            { (yyval.component) = Expression::createComponent((yyvsp[-1].expr));   }
    break;

  case 64: /* indexer: '[' exp ':' ']'  */
                                            { (yyval.component) = Expression::createComponent((yyvsp[-2].expr),0,0,true); }
    break;

  case 65: /* indexer: '[' ':' exp ']'  */
                                            { (yyval.component) = Expression::createComponent(0,(yyvsp[-1].expr)); }
    break;

  case 66: /* indexer: '[' ':' ':' exp ']'  */
                                            { (yyval.component) = Expression::createComponent(0,0,(yyvsp[-1].expr)); }
    break;

  case 67: /* indexer: '[' exp ':' exp ']'  */
                                            { (yyval.component) = Expression::createComponent((yyvsp[-3].expr),(yyvsp[-1].expr));}
    break;

  case 68: /* indexer: '[' exp ':' ':' exp ']'  */
                                            { (yyval.component) = Expression::createComponent((yyvsp[-4].expr),0,(yyvsp[-1].expr)); }
    break;

  case 69: /* indexer: '[' ':' exp ':' exp ']'  */
                                            { (yyval.component) = Expression::createComponent(0,(yyvsp[-3].expr),(yyvsp[-1].expr)); }
    break;

  case 70: /* indexer: '[' exp ':' exp ':' exp ']'  */
                                            { (yyval.component) = Expression::createComponent((yyvsp[-5].expr),(yyvsp[-3].expr),(yyvsp[-1].expr));}
    break;

  case 71: /* indexable: identifier indexer  */
                                            { (yyval.expr) = new VariableExpression(DocumentObject,(yyvsp[-1].path)); (yyval.expr)->addComponent((yyvsp[0].component)); }
    break;

  case 72: /* indexable: indexable indexer  */
                                            { (yyvsp[-1].expr)->addComponent(std::move((yyvsp[0].component))); (yyval.expr) = (yyvsp[-1].expr); }
    break;

  case 73: /* indexable: indexable '.' IDENTIFIER  */
                                            { (yyvsp[-2].expr)->addComponent(Expression::createComponent((yyvsp[0].string))); (yyval.expr) = (yyvsp[-2].expr); }
    break;

  case 74: /* document: STRING  */
                                            { (yyval.string_or_identifier) = ObjectIdentifier::String(std::move((yyvsp[0].string)), true); }
    break;

  case 75: /* document: IDENTIFIER  */
                                            { (yyval.string_or_identifier) = ObjectIdentifier::String(std::move((yyvsp[0].string)), false, true);}
    break;

  case 76: /* object: STRING  */
                                            { (yyval.string_or_identifier) = ObjectIdentifier::String(std::move((yyvsp[0].string)), true); }
    break;

  case 77: /* object: id_or_cell  */
                                            { (yyval.string_or_identifier) = ObjectIdentifier::String(std::move((yyvsp[0].string)), false);}
    break;



      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
  YY_SYMBOL_PRINT ("-> $$ =", YY_CAST (yysymbol_kind_t, yyr1[yyn]), &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;

  *++yyvsp = yyval;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */
  {
    const int yylhs = yyr1[yyn] - YYNTOKENS;
    const int yyi = yypgoto[yylhs] + *yyssp;
    yystate = (0 <= yyi && yyi <= YYLAST && yycheck[yyi] == *yyssp
               ? yytable[yyi]
               : yydefgoto[yylhs]);
  }

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
yyerrlab:
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == YYEMPTY ? YYSYMBOL_YYEMPTY : YYTRANSLATE (yychar);
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
      yyerror (YY_("syntax error"));
    }

  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
         error, discard it.  */

      if (yychar <= YYEOF)
        {
          /* Return failure if at end of input.  */
          if (yychar == YYEOF)
            YYABORT;
        }
      else
        {
          yydestruct ("Error: discarding",
                      yytoken, &yylval);
          yychar = YYEMPTY;
        }
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:
  /* Pacify compilers when the user code never invokes YYERROR and the
     label yyerrorlab therefore never appears in user code.  */
  if (0)
    YYERROR;
  ++yynerrs;

  /* Do not reclaim the symbols of the rule whose action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

  /* Pop stack until we find a state that shifts the error token.  */
  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
        {
          yyn += YYSYMBOL_YYerror;
          if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYSYMBOL_YYerror)
            {
              yyn = yytable[yyn];
              if (0 < yyn)
                break;
            }
        }

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
        YYABORT;


      yydestruct ("Error: popping",
                  YY_ACCESSING_SYMBOL (yystate), yyvsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END


  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", YY_ACCESSING_SYMBOL (yyn), yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturnlab;


/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturnlab;


/*-----------------------------------------------------------.
| yyexhaustedlab -- YYNOMEM (memory exhaustion) comes here.  |
`-----------------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  goto yyreturnlab;


/*----------------------------------------------------------.
| yyreturnlab -- parsing is finished, clean up and return.  |
`----------------------------------------------------------*/
yyreturnlab:
  if (yychar != YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  YY_ACCESSING_SYMBOL (+*yyssp), yyvsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif

  return yyresult;
}


