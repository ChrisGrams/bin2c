/******************************************************************************
File:     src/bin2c.c
Author:   CNG
Created:  August 21, 2025

Description: Converts any file into a C byte array.

Usage:
  bin2c [options] --infile=<path> --outfile=<path>
    --infile=<path>   - path to input file or - for stdin
    --outfile=<path>  - path to output file or - for stdout

Options:
    --name=<name>     - array variable name (default: data)
    -o                - overwrite the destination file if it exists
    -s                - if set, the byte array will be static
    --width=<width>   - number of octets per line
                        0 places the entire array on one line
                        maximum width is 255 (default: 8)

    -h
    --help            - display this help message and exit
    -q
    --quiet           - don't print summary
    -v
    --version         - display version info and exit
******************************************************************************/

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*****************************************************************************/

#define ERROR_LIST                                                            \
X(ERR_PTR_NULL,                 "NULL pointer")                               \
                                                                              \
X(ERR_BUF_NOT_EMPTY,            "Expected empty buffer")                      \
                                                                              \
X(ERR_ARGS_INVALID,             "Invalid argument")                           \
                                                                              \
X(ERR_ARGS_SHORT_INVALID,       "Invalid short argument option specified")    \
X(ERR_ARGS_SHORT_NONE,          "Expected short argument after '-'")          \
                                                                              \
X(ERR_ARGS_EXP_VAR_NAME,        "Expected variable name after -n option")     \
X(ERR_ARGS_EXP_W,               "Expected width specifier after -w option")   \
                                                                              \
X(ERR_ARGS_TOO_MANY,            "Too many arguments")                         \
                                                                              \
X(ERR_CONV_INT_RANGE,           "Number not in range")                        \
X(ERR_CONV_INT_UNKNOWN,         "Unknown error processing integer value")     \
                                                                              \
X(ERR_IN_PATH_NONE,             "Source file path not specified")             \
X(ERR_IN_FILE_NONE,             "Source file does not exist")                 \
X(ERR_OUT_PATH_NONE,            "Destination path not specified")             \
X(ERR_OUT_FILE_EXISTS,          "Destination file already exists")            \
                                                                              \
X(ERR_IN_FILE_OPEN,             "Unable to open input file")                  \
X(ERR_OUT_FILE_OPEN,            "Unable to open output file")                 \
                                                                              \
X(ERR_IN_FILE_READ,             "Unable to read input file")                  \
                                                                              \
X(ERR_ID_INVALID,               "Array variable identifier is not valid")     \
X(ERR_ID_C_KEYWORD,             "Array variable identifier is a C keyword")   \
                                                                              \
X(ERR_MEM_ALLOC,                "Unable to allocate required memory")

#define CHR_NOT_FOUND         SIZE_MAX


/*****************************************************************************/

const char* c_kws[] = 
{
  "auto",     "break",      "case",           "char",           "const",
  "continue", "default",    "do",             "double",         "else",
  "enum",     "extern",     "float",          "for",            "goto",
  "if",       "int",        "long",           "register",       "return",
  "short",    "signed",     "sizeof",         "static",         "struct",
  "switch",   "typedef",    "union",          "unsigned",       "void",
  "volatile", "while",      "inline",         "restrict",       "_Bool",
  "_Complex", "_Imaginary", "_Alignas",       "_Alignof",       "_Atomic",
  "_Generic", "_Noreturn",  "_Static_assert", "_Thread_local",  NULL
};

const char* err_str[] =
{
  "OK",

#define X(C,M) M,
  ERROR_LIST
#undef X

  "Unknown error"
};

const char* hlp_str =
 "Convert a file into a C byte array.\n"
 "Usage:\n"
 "  bin2c [options] --infile=<path> [--outfile=<path>]\n"
 "    --infile=<path>   - path to input file\n"
 "    --outfile=<path>  - path to output file (omit to print to stdout)\n"
 "\n"
 "Options:\n"
 "    --name=<name>     - array variable name (default: data)\n"
 "    -o                - overwrite the destination file if it exists\n"
 "    -s                - if set, the byte array will be static\n"
 "    --width=<width>   - number of octets per line\n"
 "                        0 places the entire array on one line\n"
 "                        maximum width is 255 (default: 8)\n"
 "\n"
 "    -h\n"
 "    --help            - display this help message and exit\n"
 "    -q\n"
 "    --quiet           - don't print summary\n"
 "    -v\n"
 "    --version         - display version info and exit\n";

const char* ver_str = "bin2c 2025-08-27 Chris Grams";

/*****************************************************************************/

typedef enum
{
  ARG_TYPE_UNTAGGED = 0,  /*  no '-' or '--' tag                             */
  ARG_TYPE_SHORT,         /* tagged with a '-'                               */
  ARG_TYPE_LONG           /* tagged with a '--'                              */
} arg_type;

typedef struct buffer
{
  uint8_t* ptr;
  size_t sz;
} buffer;

typedef enum
{
  ERR_MIN = 0,
  ERR_OK  = ERR_MIN,

#define X(C,M) C,
  ERROR_LIST
#undef X

  ERR_UNKNOWN,
  ERR_MAX = ERR_UNKNOWN
} Err;

typedef struct options
{
  uint8_t opt_h;        /*  help option                                      */
  uint8_t opt_o;        /*  overwrite destination file option                */
  uint8_t opt_q;        /*  don't print summary to stdout                    */
  uint8_t opt_s;        /*  static variable option                           */
  uint8_t opt_v;        /*  version option                                   */

  const char* name;     /*  variable name                                    */
  uint8_t width;        /*  column width                                     */
  const char* path_in;  /*  input file path                                  */
const char* path_out; /*  output file path                                 */
} options;

/*****************************************************************************/

/*  String compare of specified length, if n is not SIZE_MAX (CHR_NOT_FOUND).*/
int compare_str(const char* s1, const char* s2, size_t n);

/*  If a file exists at path, returns 1, otherwise returns 0.                */
int file_exists(const char* path);

/*  Open a file; done this way to silence Windows CRT warnings.              */
FILE* file_open(const char* path, const char* mode);

/*  Attempt to read a file and return a memory buffer with the contents.     */
Err file_read(const char* path, buffer* buf);

/*  Free memory allocated for a buffer and de-init the buffer.               */
void free_buffer(buffer* buf);

/*  Generate the output file using specified options.                        */
Err generate_outfile(options* opts);

/*  Get error string if defined for the error, or a generic error string.    */
const char* get_err_str(Err err);

/*  Identify the argument type.                                              */
Err id_arg_type(const char* arg, arg_type* t);

/*  Allocate memory and initialize the buffer.                               */
Err init_buffer(buffer* buf, size_t sz);

/*  Initialize options struct to default values.                             */
Err init_opts(options* opts);

/*  Returns 1 if c is in alphanumeric and ascii.                             */
int is_ascii_alnum(char c);

/*  Returns 1 if c is alpha and ascii.                                       */
int is_ascii_alpha(char c);

/*  Return 1 if the C identifier is valid, or return 0.                      */
int is_valid_c_identifier(const char* id);

/*  Parse command line options, returning an error on failure or ERR_OK.     */
Err parse_opts(options* opts, int argc, const char* argv[]);

/*  Parse one long option that begins with a '--'.
 *  Expects arg to point to the beginning of the option name, not a dash.    */
Err parse_long_opt(options* opts, const char* arg);

/*  Parse one or more short options that begin with a single dash.
 *  Expects arg to point to the beginning of the first option, not a dash.   */
Err parse_short_opt(options* opts, const char* arg);

/*  If err is not ERR_OK, print it and exit the process.                     */
void print_error_and_exit(Err err);

/*  Print the help message, then exit the process.                           */
void print_help_and_exit();

/*  Print summary of options.                                                */
void print_options_summary(options* opts);

/*  Print version info, then exit the process.                               */
void print_version_and_exit();

/*  Split assignment arguments into key, value pairs.                        */
Err split_assignment_arg(const char* arg,
  const char** key, size_t* key_len, const char** value);

/*  Convert a string to an 8 bit integer.                                    */
Err str_to_uint8(const char* s, uint8_t* i);

/*  Get the distance to the first occurence of char c from pointer s.        */
size_t strchr_distance(const char* s, char c);

/*  Validate command line options and return an error code or ERR_OK.        */
Err validate_opts(options* opts);

/*****************************************************************************/

void free_buffer(buffer* buf)
{
  if (buf)
  {
    free(buf->ptr);
    buf->ptr = NULL;
    buf->sz = 0;
  }
}

Err init_buffer(buffer* buf, size_t sz)
{
  if (!buf) return ERR_PTR_NULL;
  if (buf->ptr || buf->sz != 0) return ERR_BUF_NOT_EMPTY;
  buf->ptr = (uint8_t*)calloc(1, sz);
  if (!buf->ptr) return ERR_MEM_ALLOC;
  buf->sz = sz;
  return ERR_OK;
}

int compare_str(const char* s1, const char* s2, size_t n)
{
  return n == SIZE_MAX ? strcmp(s1, s2) : strncmp(s1, s2, n);
}

int file_exists(const char* path)
{
  FILE* f = file_open(path, "rb");
  if (f)
  {
    fclose(f);
    return 1;
  }

  return 0;
}

FILE* file_open(const char* path, const char* mode)
{
  FILE* f = NULL;

/*  For windows, silence CRT secure warnings                                 */
#if defined(__clang__) && defined(_WIN32)
  errno_t err = fopen_s(&f, path, mode);
  if (err) f = NULL;
#else
  f = fopen(path, mode);
#endif

  return f;
}

Err file_read(const char* path, buffer* buf)
{
  Err e = ERR_OK;
  FILE* f = NULL;
  long file_sz;
  size_t read_sz;

  if (!buf) return ERR_PTR_NULL;

  f = file_open(path, "rb");
  if (!f) return ERR_IN_FILE_OPEN;

  /*  Get file size in bytes                                                 */
  fseek(f, 0, SEEK_END);
  file_sz = (size_t)ftell(f);

  if (file_sz == -1)
  {
    fclose(f);
    return ERR_IN_FILE_READ;
  }

  rewind(f);

  e = init_buffer(buf, (size_t)file_sz);
  if (e == ERR_OK)
  {
    read_sz = fread(buf->ptr, 1, buf->sz, f);

    if (read_sz != buf->sz)
    {
      free_buffer(buf);
      e = ERR_IN_FILE_READ;
    }
  }

  fclose(f);

  return e;
}

Err generate_outfile(options* opts)
{
  buffer buf = { NULL, 0 };
  FILE* out = NULL;
  Err result = ERR_OK;

  if (!opts) return ERR_PTR_NULL;

  result = file_read(opts->path_in, &buf);
  if (result != ERR_OK) return result;

  out = opts->path_out ? file_open(opts->path_out, "w") : stdout;
  if (!out)
  {
    free(buf.ptr);
    return ERR_OUT_FILE_OPEN;
  }

  fprintf(out, "%sconst unsigned char %s[] = \n{%s",
    opts->opt_s? "static " : "", opts->name, opts->width ? "\n" : "");
  for (size_t i = 0; i < buf.sz; ++i)
  {
    fprintf(out, "0x%02X%s", buf.ptr[i], (i < buf.sz - 1) ? ", " : "");
    if (opts->width > 0 && (i + 1) % opts->width == 0) fprintf(out, "\n");
  }
  fprintf(out, "};\n\n");
  fprintf(out, "%sconst unsigned int %s_len = %zu;\n\n",
    opts->opt_s ? "static " : "", opts->name, buf.sz);

  if (opts->path_out) fclose(out);
  free(buf.ptr);
  return ERR_OK;
}

const char* get_err_str(Err err)
{ return err_str[(err >= ERR_MIN && err < ERR_MAX) ? err : ERR_UNKNOWN]; }

Err id_arg_type(const char* arg, arg_type* t)
{
  if (!arg || !t) return ERR_PTR_NULL;

  if (arg[0] != '-')      *t = ARG_TYPE_UNTAGGED; /*  does not start with '-'*/
  else if (arg[1] == '-') *t = ARG_TYPE_LONG;     /*  starts with '--'       */
  else                    *t = ARG_TYPE_SHORT;    /*  starts with '-'        */

  return ERR_OK;
}

Err init_opts(options* opts)
{
  if (!opts) return ERR_PTR_NULL;

  *opts = (options)
  {
    0,        /*  help option                                                */
    0,        /*  overwrite destination file option                          */
    0,        /*  don't print summary to stdout                              */
    0,        /*  static variable option                                     */
    0,        /*  version option                                             */

    "data",   /*  variable name                                              */
    8,        /*  column width                                               */
    NULL,     /*  input file path                                            */
    NULL      /*  output file path                                           */
  };

  return ERR_OK;
}

int is_ascii_alnum(char c) { return isascii(c) && isalnum(c); }

int is_ascii_alpha(char c) { return isascii(c) && isalpha(c); }

Err is_valid_c_identifier(const char* id)
{
  if (!id) return ERR_PTR_NULL;

  if (!is_ascii_alpha(id[0])) return ERR_ID_INVALID;

  for (size_t i = 1; id[i] != '\0'; ++i)
    if (!is_ascii_alnum(id[i]) && id[i] != '_') return ERR_ID_INVALID;

  for (const char** kw = c_kws; *kw != NULL; ++kw)
    if (strcmp(*kw, id) == 0) return ERR_ID_C_KEYWORD;

  return ERR_OK;
}

Err parse_opts(options* opts, int argc, const char* argv[])
{
  Err e;
  arg_type t;

  if ((e = init_opts(opts)) != ERR_OK) return e;

  for (int argi = 1; argi < argc; ++argi)
  {
    if ((e = id_arg_type(argv[argi], &t)) != ERR_OK) break;

    if (t == ARG_TYPE_LONG)
    {
      if ((e = parse_long_opt(opts, argv[argi] + 2)) != ERR_OK) break;
    }
    else if (t == ARG_TYPE_SHORT)
    {
      if ((e = parse_short_opt(opts, argv[argi] + 1)) != ERR_OK) break;
    }
    else
    {
      e = ERR_ARGS_INVALID;
      break;
    }
  }

  return e;
}

Err parse_long_opt(options* opts, const char* arg)
{
  Err e = ERR_OK;
  const char* key;
  size_t key_len;
  const char* value;

  if (!opts || !arg) return ERR_PTR_NULL;

  split_assignment_arg(arg, &key, &key_len, &value);

  if (compare_str(key, "help", key_len) == 0)
  { opts->opt_h = 1; }
  else if (compare_str(key, "quiet", key_len) == 0)
  { opts->opt_q = 1; }
  else if (compare_str(key, "version", key_len) == 0) 
  { opts->opt_v = 1; }
  else if (compare_str(key, "infile", key_len) == 0)
  { opts->path_in = value; }
  else if (compare_str(key, "outfile", key_len) == 0)
  { opts->path_out = value; }
  else if (compare_str(key, "name", key_len) == 0)
  { opts->name = value; }
  else if (compare_str(key, "width", key_len) == 0)
  { e = str_to_uint8(value, &opts->width); }
  else
  { e = ERR_ARGS_INVALID; }

  return e;
}

Err parse_short_opt(options* opts, const char* arg)
{
  Err e = ERR_OK;

  if (!opts || !arg)  e = ERR_PTR_NULL;
  if (*arg == '\0')   e = ERR_ARGS_SHORT_NONE;

  while (*arg != '\0' && e == ERR_OK)
  {
    switch(*arg)
    {
      case 'h': opts->opt_h = 1; break;
      case 'o': opts->opt_o = 1; break;
      case 'q': opts->opt_q = 1; break;
      case 's': opts->opt_s = 1; break;
      case 'v': opts->opt_v = 1; break;

      default: e = ERR_ARGS_SHORT_INVALID; break;
    }

    arg++;
  }

  return e;
}

void print_error_and_exit(Err err)
{
  if (err == ERR_OK) return;
  printf("%s\n", get_err_str(err));
  exit(1);
}

void print_help_and_exit()
{
  printf("%s\n", hlp_str);
  exit(0);
}

void print_options_summary(options* opts)
{
  if (!opts || opts->opt_h || opts->opt_q) return;

  if (!opts->path_out) printf("/*\n");
  printf("read file '%s'\n", opts->path_in);

  if (!opts->path_out)
  {
    printf("output to stdout\n");
  }
  else
  {
    printf("%swrite file '%s'\n", opts->opt_o ? "over" : "", opts->path_out);
  }

  printf("store data in %sbyte array '%s'\n", opts->opt_s ? "static " : "",
    opts->name);
  if (!opts->path_out) printf("*/\n");
}

void print_version_and_exit()
{
  printf("%s\n", ver_str);
  exit(0);
}

Err split_assignment_arg(const char* arg,
  const char** key, size_t* key_len, const char** value)
{
  if (!arg || !key || !key_len || !value) return ERR_PTR_NULL;
  
  /*  skip up to two dashes */
  *key = arg;
  for (int i = 0; i < 2; ++i)
  {
    if (**key != '-') break;
    (*key)++;
  }

  *key_len = strchr_distance(*key, '=');
  if (*key_len == CHR_NOT_FOUND)
  {
    *key_len = strlen(*key);
    *value = NULL;
  }
  else
  {
    *value = *key + *key_len + 1;
    (*key_len)--; /*  exclude '=' from key length */
  }

  return ERR_OK;
}

Err str_to_uint8(const char* s, uint8_t* i)
{
  unsigned long val;

  if (!s || !i) return ERR_PTR_NULL;

  errno = 0;
  val = strtoul(s, NULL, 10);
  if (errno == ERANGE || (errno == 0 && val > 255)) return ERR_CONV_INT_RANGE;
  if (errno != 0) return ERR_CONV_INT_UNKNOWN;

  *i = (uint8_t)val;

  return ERR_OK;
}

size_t strchr_distance(const char* s, char c)
{
  const char* s2;
  if (!s) return CHR_NOT_FOUND;
  s2 = strchr(s, c);
  return s2 ? (size_t)(s2 - s) : CHR_NOT_FOUND;
}

Err validate_opts(options* opts)
{
  Err err;

  if (!opts) return ERR_PTR_NULL;

  /*  check for both input file path specified and if that file exists       */
  if (!opts->path_in) return ERR_IN_PATH_NONE;
  if (!file_exists(opts->path_in)) return ERR_IN_FILE_NONE;

  /*  if the array variable name option is set, check the specified name     */
  err = is_valid_c_identifier(opts->name);
  if (err != ERR_OK) return err;

  /*  if the overwrite option is set, an output file path must be specified  */
  if (opts->opt_o)
  {
    if (!opts->path_out) return ERR_OUT_PATH_NONE;
  }
  /*  if the overwrite option is not set, the file must not exist            */
  else if (opts->path_out && file_exists(opts->path_out))
  {
    return ERR_OUT_FILE_EXISTS;
  }

  return ERR_OK;
}

/*****************************************************************************/

int main(int argc, const char* argv[])
{
  Err err = ERR_OK;
  options opts;

  if (argc == 1) print_help_and_exit();

  print_error_and_exit(parse_opts(&opts, argc, argv));

  if (opts.opt_h) print_help_and_exit();
  else if (opts.opt_v) print_version_and_exit();

  print_error_and_exit(validate_opts(&opts));
  print_options_summary(&opts);
  print_error_and_exit(generate_outfile(&opts));

  return 0;
}

