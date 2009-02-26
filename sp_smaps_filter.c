/*
 * This file is part of sp-smaps
 *
 * Copyright (C) 2004-2007 Nokia Corporation. 
 *
 * Contact: Eero Tamminen <eero.tamminen@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License 
 * version 2 as published by the Free Software Foundation. 
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

/* ========================================================================= *
 * File: sp_smaps_filter.c
 * 
 * Author: Simo Piiroinen
 * 
 * -------------------------------------------------------------------------
 *
 * History:
 *
 * 25-Feb-2009 Simo Piiroinen
 * - lists unrecognized keys without exiting
 *
 * 18-Jan-2007 Simo Piiroinen
 * - fixed usage output
 * - code cleanup
 * 
 * 17-Oct-2006 Simo Piiroinen
 * - initial version
 * ========================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <assert.h>
#include <math.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <libsysperf/csv_table.h>
#include <libsysperf/array.h>

#include <libsysperf/msg.h>
#include <libsysperf/argvec.h>
#include <libsysperf/str_array.h>

#include "symtab.h"

#if 0
# define INLINE static inline
# define STATIC static
#else
# define INLINE static
# define STATIC static
#endif

#define TOOL_NAME "sp_smaps_filter"
#include "release.h"

/* ------------------------------------------------------------------------- *
 * Runtime Manual
 * ------------------------------------------------------------------------- */

static const manual_t app_man[]=
{
  MAN_ADD("NAME",
          TOOL_NAME"  --  smaps capture file analysis tool\n"
          )
  MAN_ADD("SYNOPSIS",
          ""TOOL_NAME" [options] <capture file> ... \n"
          )
  MAN_ADD("DESCRIPTION",
          "This tool is used for processing capture files.\n"
	  "The following processing modes are available:\n"
	  "\n"
	  "flatten:\n"
	  "  heuristically detect and remove threads\n"
	  "  input  - capture file\n"
	  "  output - capture file\n"
	  "\n"
	  "normalize:\n"
	  "  thread removal and conversion to csv format\n"
	  "  input  - capture file\n"
	  "  output - csv file\n"
	  "\n"
	  "appvals:\n"
	  "  thread removal and output main per application values\n"
	  "  input  - capture file\n"
	  "  output - csv file\n"
	  "\n"
	  "analyze:\n"
	  "  thread removal and conversion to html format\n"
	  "  input  - capture file\n"
	  "  output - html index + sub pages in separate dir\n"
	  "\n"
	  "diff:\n"
	  "  thread removal and comparison of memory usage values\n"
	  "  input  - capture files\n"
	  "  output - csv or html file\n"
          )
  MAN_ADD("OPTIONS", 0)

  MAN_ADD("EXAMPLES",
          "% "TOOL_NAME" -m flatten *.cap\n"
	  "  writes capture format output without threads -> *.flat\n"
	  "\n"
          "% "TOOL_NAME" -m normalize *.cap\n"
	  "  writes csv format output -> *.csv\n"
	  "\n"
          "% "TOOL_NAME" -m appcals *.cap\n"
	  "  writes csv format summary -> *.apps\n"
	  "\n"
	  "% "TOOL_NAME" -m analyze *.cap\n"
	  "  writes browsable html analysis index -> *.html\n"
	  "\n"
	  "% "TOOL_NAME" -m diff *.cap -o diff.sys.csv\n"
	  "  difference report in csv minimum details\n"
	  "\n"
	  "% "TOOL_NAME" -m diff *.cap -o diff.obj.csv\n"
	  "  difference report in csv maximum details\n"
	  "\n"
	  "% "TOOL_NAME" -m diff *.cap -o diff.pid.html -tapp\n"
	  "  difference report in html details to pid.level\n"
	  "                            appcolumn output trimmed\n"
          )

  MAN_ADD("NOTES",
	  "The filtering mode defaults to analyze, unless the program\n"
	  "is invoked via symlink in which case the mode determined\n"
	  "after the last underscore in invocation name, i.e.\n"
	  "  % ln -s sp_smaps_filter sp_smaps_diff\n"
	  "  % sp_smaps_diff ...\n"
	  "is equal to\n"
	  "  % sp_smaps_filter -mdiff ...\n"
          "")
  
  MAN_ADD("COPYRIGHT",
          "Copyright (C) 2004-2007 Nokia Corporation.\n\n"
	  "This is free software.  You may redistribute copies of it under the\n"
	  "terms of the GNU General Public License v2 included with the software.\n"
	  "There is NO WARRANTY, to the extent permitted by law.\n"
          )
  MAN_ADD("SEE ALSO",
          "sp_smaps_snapshot (1)\n"
          )
  MAN_END
};
/* ------------------------------------------------------------------------- *
 * Commandline Arguments
 * ------------------------------------------------------------------------- */

enum
{
  opt_noswitch = -1,
  opt_help,
  opt_vers,

  opt_verbose,
  opt_quiet,
  opt_silent,

  opt_input,
  opt_output,

  opt_filtmode,
  
  opt_difflevel,
  opt_trimlevel,
  
  
};
static const option_t app_opt[] =
{
  /* - - - - - - - - - - - - - - - - - - - *
   * Standard usage, version & verbosity
   * - - - - - - - - - - - - - - - - - - - */

  OPT_ADD(opt_help,
          "h", "help", 0,
          "This help text\n"),

  OPT_ADD(opt_vers,
          "V", "version", 0,
          "Tool version\n"),

  OPT_ADD(opt_verbose,
          "v", "verbose", 0,
          "Enable diagnostic messages\n"),

  OPT_ADD(opt_quiet,
          "q", "quiet", 0,
          "Disable warning messages\n"),

  OPT_ADD(opt_silent,
          "s", "silent", 0,
          "Disable all messages\n"),

  /* - - - - - - - - - - - - - - - - - - - *
   * Application options
   * - - - - - - - - - - - - - - - - - - - */

  OPT_ADD(opt_input,
          "f", "input", "<source path>",
          "Add capture file for processing.\n" ),

  OPT_ADD(opt_output,
          "o", "output", "<destination path>",
          "Override default output path.\n" ),


  OPT_ADD(opt_filtmode,
          "m", "mode", "<filter mode>",
          "One of:\n"
	  "  flatten\n"
	  "  normalize\n"
	  "  analyze\n"
	  "  appvals\n"
	  "  diff\n"),


  /* - - - - - - - - - - - - - - - - - - - *
   * diff options
   * - - - - - - - - - - - - - - - - - - - */


  OPT_ADD(opt_difflevel,
          "l", "difflevel", "<level>",
	  "Display results at level:\n"
          "  0 = capture\n"
          "  1 = command\n"
          "  2 = command, pid\n"
          "  3 = command, pid, type\n"
          "  4 = command, pid, type, path\n"),

  OPT_ADD(opt_trimlevel,
          "t", "trimlevel", "<level>",
	  "Omit repeating classification data:\n"
          "  1 = command\n"
          "  2 = command, pid\n"
          "  3 = command, pid, type\n"
          "  4 = command, pid, type, path\n"),
  
  /* - - - - - - - - - - - - - - - - - - - *
   * Sentinel
   * - - - - - - - - - - - - - - - - - - - */

  OPT_END
};

#include <argz.h>

typedef struct unknown_t unknown_t;

/* ------------------------------------------------------------------------- *
 * unknown_t
 * ------------------------------------------------------------------------- */

struct unknown_t
{
  char  *un_data;
  size_t un_size;
};

#define UNKNOWN_INIT { 0, 0 }


/* ========================================================================= *
 * unknown_t  --  methods
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * unknown_ctor
 * ------------------------------------------------------------------------- */

void 
unknown_ctor(unknown_t *self)
{
  self->un_data = 0;
  self->un_size = 0;
}

/* ------------------------------------------------------------------------- *
 * unknown_dtor
 * ------------------------------------------------------------------------- */

void 
unknown_dtor(unknown_t *self)
{
  free(self->un_data);
}

/* ------------------------------------------------------------------------- *
 * unknown_create
 * ------------------------------------------------------------------------- */

unknown_t *
unknown_create(void)
{
  unknown_t *self = calloc(1, sizeof *self);
  unknown_ctor(self);
  return self;
}

/* ------------------------------------------------------------------------- *
 * unknown_delete
 * ------------------------------------------------------------------------- */

void 
unknown_delete(unknown_t *self)
{
  if( self != 0 )
  {
    unknown_dtor(self);
    free(self);
  }
}

/* ------------------------------------------------------------------------- *
 * unknown_delete_cb
 * ------------------------------------------------------------------------- */

void 
unknown_delete_cb(void *self)
{
  unknown_delete(self);
}

/* ------------------------------------------------------------------------- *
 * unknown_add
 * ------------------------------------------------------------------------- */

int
unknown_add(unknown_t *self, const char *txt)
{
  char *entry = 0;
  while( (entry = argz_next(self->un_data, self->un_size, entry)) != 0 )
  {
    if( !strcmp(entry, txt) )
    {
      return 0;
    }
  }
  argz_add(&self->un_data, &self->un_size, txt);
  return 1;
}

/* ========================================================================= *
 * utilities
 * ========================================================================= */

void cstring_set(char **dest, char *srce)
{
  free(*dest); *dest = srce ? strdup(srce) : 0;
}

INLINE int path_isdir(const char *path)
{
  struct stat st;
  if( stat(path, &st) == 0 && S_ISDIR(st.st_mode) )
  {
    return 1;
  }
  return 0;
}

INLINE char *path_basename(const char *path)
{
  char *r = strrchr(path, '/');
  return r ? (r+1) : (char *)path;
}

INLINE char *path_extension(const char *path)
{
  char *b = path_basename(path);
  char *e = strrchr(b, '.');
  return e ? e : strchr(b,0);
}

INLINE int path_compare(const char *p1, const char *p2)
{
  int r;
  
  r = (*p1 != '[') - (*p2 != '[');
  if (r) return r;
  
  r = (strstr(p1,".cache")!=0) - (strstr(p2,".cache")!=0);
  
  if( r == 0 )
  {
    r = strcasecmp(path_basename(p1), path_basename(p2));
  }
  
  return r ? r : strcasecmp(p1, p2);
// QUARANTINE   return strcmp(path_basename(p1), path_basename(p2));
}

// QUARANTINE INLINE unsigned umax(unsigned a, unsigned b)
// QUARANTINE {
// QUARANTINE   return (a>b)?a:b;
// QUARANTINE }

// QUARANTINE INLINE unsigned umax3(unsigned a, unsigned b, unsigned c)
// QUARANTINE {
// QUARANTINE   return umax(umax(a,b),c);
// QUARANTINE }

INLINE double fmax3(unsigned a, unsigned b, unsigned c)
{
  return fmax(fmax(a,b),c);
}

INLINE double pow2(double a) 
{
  return a*a; 
}

// QUARANTINE INLINE unsigned usum(unsigned a, unsigned b)
// QUARANTINE {
// QUARANTINE   return a+b; 
// QUARANTINE }
// QUARANTINE INLINE unsigned umax(unsigned a, unsigned b) 
// QUARANTINE {
// QUARANTINE   return (a>b)?a:b;
// QUARANTINE }

INLINE void pusum(unsigned *a, unsigned b)
{
  *a += b;
}
INLINE void pumax(unsigned *a, unsigned b) 
{
  if( *a < b ) *a=b;
}

/* ------------------------------------------------------------------------- *
 * array_find_lower  --  bsearch lower bound from sorted array_t
 * ------------------------------------------------------------------------- */

int 
array_find_lower(array_t *self, int lo, int hi, int (*fn)(const void*))
{
  while( lo < hi )
  {
    int i = (lo + hi) / 2;
    int r = fn(self->data[i]);
    if( r >= 0 ) { hi = i+0; } else { lo = i+1; }
  }
  return lo;
}

/* ------------------------------------------------------------------------- *
 * array_find_upper  --  bsearch upper bound from sorted array_t
 * ------------------------------------------------------------------------- */

int 
array_find_upper(array_t *self, int lo, int hi, int (*fn)(const void*))
{
  while( lo < hi )
  {
    int i = (lo + hi) / 2;
    int r = fn(self->data[i]);
    if( r > 0 ) { hi = i+0; } else { lo = i+1; }
  }
  return lo;
}

/* ------------------------------------------------------------------------- *
 * uval  --  return number as string or "-" for zero values
 * ------------------------------------------------------------------------- */

const char *uval(unsigned n)
{
  static char temp[512];
  snprintf(temp, sizeof temp, "%u", n);
  return n ? temp : "-";
}

/* ------------------------------------------------------------------------- *
 * array_move  --  TODO: this should be in libsysperf
 * ------------------------------------------------------------------------- */

INLINE void 
array_move(array_t *self, array_t *from)
{
  array_minsize(self, self->size + from->size);
  for( size_t i = 0; i < from->size; ++i )
  {
    self->data[self->size++] = from->data[i];
  }
  from->size = 0;
}

/* ------------------------------------------------------------------------- *
 * xstrfmt  --  sprintf to dynamically allocated buffer
 * ------------------------------------------------------------------------- */

STATIC char *
xstrfmt(char **pstr, const char *fmt, ...)
{
  char *res = 0;
  char tmp[256];
  va_list va;
  int nc;
  
  va_start(va, fmt);
  nc = vsnprintf(tmp, sizeof tmp, fmt, va);
  va_end(va);
  
  if( nc >= 0 )
  {
    if( nc < sizeof tmp )
    {
      res = strdup(tmp);
    }
    else
    {
      res = malloc(nc + 1);
      va_start(va, fmt);
      vsnprintf(res, nc+1, fmt, va);
      va_end(va);
    }
  }
  
  free(*pstr);
  return *pstr = res;
}

/* ------------------------------------------------------------------------- *
 * slice  --  split string at separator char
 * ------------------------------------------------------------------------- */

STATIC char *
slice(char **ppos, int sep)
{
  char *pos = *ppos;
  
  while( (*pos > 0) && (*pos <= 32) )
  {
    ++pos;
  }
  char *res = pos;
  
  if( sep < 0 )
  {
    for( ; *pos; ++pos )
    {
      if( *(unsigned char *)pos <= 32 )
      {
	*pos++ = 0; break;
      }
    }
  }
  else
  {
    for( ; *pos; ++pos )
    {
      if( *(unsigned char *)pos == sep )
      {
	*pos++ = 0; break;
      }
    }
  }
  
  *ppos = pos;
  return res;
}
 

/* ========================================================================= *
 * Custom Objects
 * ========================================================================= */

typedef struct analyze_t analyze_t;

/* - - - - - - - - - - - - - - - - - - - *
 * container classes
 * - - - - - - - - - - - - - - - - - - - */

typedef struct smapsfilt_t smapsfilt_t;
typedef struct smapssnap_t smapssnap_t;
typedef struct smapsproc_t smapsproc_t;
typedef struct smapsmapp_t smapsmapp_t;

/* - - - - - - - - - - - - - - - - - - - *
 * rawdata classes
 * - - - - - - - - - - - - - - - - - - - */

typedef struct pidinfo_t pidinfo_t; // Name,Pid,PPid, ...
typedef struct mapinfo_t mapinfo_t; // head,tail,prot, ...
typedef struct meminfo_t meminfo_t; // Size,RSS,Shared_Clean, ...

/* ------------------------------------------------------------------------- *
 * meminfo_t
 * ------------------------------------------------------------------------- */

struct meminfo_t
{
  unsigned Size;
  unsigned Rss;  
  unsigned Shared_Clean;
  unsigned Shared_Dirty;
  unsigned Private_Clean;
  unsigned Private_Dirty;
};

void       meminfo_ctor              (meminfo_t *self);
void       meminfo_dtor              (meminfo_t *self);
void       meminfo_accumulate_appdata(meminfo_t *self, const meminfo_t *that);
void       meminfo_accumulate_libdata(meminfo_t *self, const meminfo_t *that);
void       meminfo_parse             (meminfo_t *self, char *line);

meminfo_t *meminfo_create            (void);
void       meminfo_delete            (meminfo_t *self);
void       meminfo_delete_cb         (void *self);

/* ------------------------------------------------------------------------- *
 * meminfo_total
 * ------------------------------------------------------------------------- */

INLINE unsigned 
meminfo_total(const meminfo_t *self)
{
  return (self->Shared_Clean  +
	  self->Shared_Dirty  +
	  self->Private_Clean +
	  self->Private_Dirty);
}

INLINE unsigned 
meminfo_cowest(const meminfo_t *self)
{
   // TODO: COW estimate, is this what is wanted?
  return (self->Shared_Clean + self->Shared_Dirty);
}

/* ------------------------------------------------------------------------- *
 * mapinfo_t
 * ------------------------------------------------------------------------- */

struct mapinfo_t
{
  unsigned   head;
  unsigned   tail;
  char      *prot;
  unsigned   offs;
  char      *node;
  unsigned   flgs;
  char      *path;
  char      *type;
};

void       mapinfo_ctor     (mapinfo_t *self);
void       mapinfo_dtor     (mapinfo_t *self);

mapinfo_t *mapinfo_create   (void);
void       mapinfo_delete   (mapinfo_t *self);
void       mapinfo_delete_cb(void *self);


/* ------------------------------------------------------------------------- *
 * pidinfo_t
 * ------------------------------------------------------------------------- */

struct pidinfo_t
{
  char    *Name;
  int      Pid;
  int      PPid;
  int      Threads;
  unsigned VmSize;
  unsigned VmLck;
  unsigned VmRSS;
  unsigned VmData;
  unsigned VmStk;
  unsigned VmExe;
  unsigned VmLib;
  unsigned VmPTE;
};

void       pidinfo_ctor     (pidinfo_t *self);
void       pidinfo_dtor     (pidinfo_t *self);
void       pidinfo_parse    (pidinfo_t *self, char *line);

pidinfo_t *pidinfo_create   (void);
void       pidinfo_delete   (pidinfo_t *self);
void       pidinfo_delete_cb(void *self);

/* ------------------------------------------------------------------------- *
 * smapsmapp_t
 * ------------------------------------------------------------------------- */

struct smapsmapp_t
{
  int       smapsmapp_uid; // create -> load order enumeration
  
  mapinfo_t smapsmapp_map;
  meminfo_t smapsmapp_mem;
  
  int       smapsmapp_AID;
  int       smapsmapp_PID;
  int       smapsmapp_LID;
  int       smapsmapp_TID;
  int       smapsmapp_EID;
};

void         smapsmapp_ctor     (smapsmapp_t *self);
void         smapsmapp_dtor     (smapsmapp_t *self);

smapsmapp_t *smapsmapp_create   (void);
void         smapsmapp_delete   (smapsmapp_t *self);
void         smapsmapp_delete_cb(void *self);

/* ------------------------------------------------------------------------- *
 * smapsproc_t
 * ------------------------------------------------------------------------- */

struct smapsproc_t
{
  int          smapsproc_uid;
  
  int          smapsproc_AID;
  int          smapsproc_PID;
  
  /* - - - - - - - - - - - - - - - - - - - *
   * smaps data & hierarchy
   * - - - - - - - - - - - - - - - - - - - */

  pidinfo_t    smapsproc_pid;
  array_t      smapsproc_mapplist; // -> smapsmapp_t *

  /* - - - - - - - - - - - - - - - - - - - *
   * process hierarchy info
   * 
   * Note: Owenership of childprocess data
   *       remains with smapssnap_t!
   * - - - - - - - - - - - - - - - - - - - */
  
  smapsproc_t *smapsproc_parent;
  array_t      smapsproc_children; // -> smapsproc_t *
};

void         smapsproc_ctor     (smapsproc_t *self);
void         smapsproc_dtor     (smapsproc_t *self);

smapsproc_t *smapsproc_create   (void);
void         smapsproc_delete   (smapsproc_t *self);
void         smapsproc_delete_cb(void *self);


/* ------------------------------------------------------------------------- *
 * smapssnap_t
 * ------------------------------------------------------------------------- */

struct smapssnap_t
{
  char       *smapssnap_source;
  int         smapssnap_format;
  array_t     smapssnap_proclist; // -> smapsproc_t *
  smapsproc_t smapssnap_rootproc;
};

enum {
  SNAPFORMAT_OLD, // head /proc/[1-9]*/smaps > snapshot.cap
  SNAPFORMAT_NEW, // sp_smaps_snapshot -o snapshot.cap
};

void         smapssnap_ctor     (smapssnap_t *self);
void         smapssnap_dtor     (smapssnap_t *self);

smapssnap_t *smapssnap_create   (void);
void         smapssnap_delete   (smapssnap_t *self);
void         smapssnap_delete_cb(void *self);


/* ------------------------------------------------------------------------- *
 * analyze_t  --  temporary book keeping structure for smaps snapshot analysis
 * ------------------------------------------------------------------------- */

struct analyze_t
{
  // smaps data for all processes will be collected here
  
  array_t  *mapp_tab;
  
  // enumeration tables
  
  symtab_t *appl_tab;  // application names
  symtab_t *type_tab;  // mapping types: code, data, anon, ...
  symtab_t *path_tab;  // mapping paths
  symtab_t *summ_tab;  // app instance + mapping path

  int ntypes;	       // enumeration counts
  int nappls;
  int npaths;
  int groups;
  
  const char **stype;  // enumeration -> string lookup tables
  const char **sappl;
  const char **spath;

  int *grp_app;	       // group enum -> appid / libid lookup tables
  int *grp_lib;

  // memory usage accumulation tables
  
  meminfo_t *grp_mem; // [groups * ntypes];
  meminfo_t *app_mem; // [nappls * ntypes];
  meminfo_t *lib_mem; // [npaths * ntypes];
  meminfo_t *sysest;  // [ntypes]
  meminfo_t *sysmax;  // [ntypes]
  meminfo_t *appmax;  // [ntypes]
};

void       analyze_ctor                  (analyze_t *self);
void       analyze_dtor                  (analyze_t *self);
analyze_t *analyze_create                (void);
void       analyze_delete                (analyze_t *self);
void       analyze_delete_cb             (void *self);
void       analyze_enumerate_data        (analyze_t *self, smapssnap_t *snap);
void       analyze_accumulate_data       (analyze_t *self);
void       analyze_emit_page_table       (analyze_t *self, FILE *file, const meminfo_t *mtab);
void       analyze_emit_xref_header      (analyze_t *self, FILE *file, const char *type);
void       analyze_get_apprange          (analyze_t *self, int lo, int hi, int *plo, int *phi, int aid);
void       analyze_get_librange          (analyze_t *self, int lo, int hi, int *plo, int *phi, int lid);
int        analyze_emit_lib_html         (analyze_t *self, smapssnap_t *snap, const char *work);
int        analyze_emit_app_html         (analyze_t *self, smapssnap_t *snap, const char *work);
void       analyze_emit_smaps_table      (analyze_t *self, FILE *file, meminfo_t *v);
void       analyze_emit_table_header     (analyze_t *self, FILE *file, const char *title);
void       analyze_emit_process_hierarchy(analyze_t *self, FILE *file, smapsproc_t *proc, const char *work);
void       analyze_emit_application_table(analyze_t *self, FILE *file, const char *work);
void       analyze_emit_library_table    (analyze_t *self, FILE *file, const char *work);
int        analyze_emit_main_page        (analyze_t *self, smapssnap_t *snap, const char *path);

/* ------------------------------------------------------------------------- *
 * smapsfilt_t
 * ------------------------------------------------------------------------- */

enum
{
  FM_FLATTEN,
  FM_NORMALIZE,
  FM_ANALYZE,
  FM_APPVALS,
  FM_DIFF,
};

struct smapsfilt_t
{
  int         smapsfilt_filtmode;
  int         smapsfilt_difflevel;
  int         smapsfilt_trimlevel;
  str_array_t smapsfilt_inputs;
  char       *smapsfilt_output;
  
  array_t smapsfilt_snaplist; // -> smapssnap_t *
};



void         smapsfilt_ctor     (smapsfilt_t *self);
void         smapsfilt_dtor     (smapsfilt_t *self);

smapsfilt_t *smapsfilt_create   (void);
void         smapsfilt_delete   (smapsfilt_t *self);
void         smapsfilt_delete_cb(void *self);




/* ========================================================================= *
 * meminfo_t  --  methods
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * meminfo_ctor
 * ------------------------------------------------------------------------- */

void 
meminfo_ctor(meminfo_t *self)
{
  self->Size = 0;
  self->Rss  = 0;
  self->Shared_Clean  = 0;
  self->Shared_Dirty  = 0;
  self->Private_Clean = 0;
  self->Private_Dirty = 0;
}

/* ------------------------------------------------------------------------- *
 * meminfo_dtor
 * ------------------------------------------------------------------------- */

void 
meminfo_dtor(meminfo_t *self)
{
}

/* ------------------------------------------------------------------------- *
 * meminfo_parse
 * ------------------------------------------------------------------------- */

void
meminfo_parse(meminfo_t *self, char *line)
{
  char *key = slice(&line, ':');
  char *val = slice(&line,  -1);
  
  if( !strcmp(key, "Size") ) 
  {
    self->Size	= strtoul(val, 0, 10);
  }
  else if( !strcmp(key, "Rss") ) 
  {
    self->Rss	= strtoul(val, 0, 10);
  }
  else if( !strcmp(key, "Shared_Clean") ) 
  {
    self->Shared_Clean	= strtoul(val, 0, 10);
  }
  else if( !strcmp(key, "Shared_Dirty") )
  {
    self->Shared_Dirty	= strtoul(val, 0, 10);
  }
  else if( !strcmp(key, "Private_Clean") ) 
  {
    self->Private_Clean	= strtoul(val, 0, 10);
  }
  else if( !strcmp(key, "Private_Dirty") )
  {
    self->Private_Dirty	= strtoul(val, 0, 10);
  }
  else
  {
    static unknown_t unkn = UNKNOWN_INIT;
    if( unknown_add(&unkn, key) )
    {
      fprintf(stderr, "%s: Unknown key: '%s' = '%s'\n", __FUNCTION__, key, val);
    }
  }
}

/* ------------------------------------------------------------------------- *
 * meminfo_accumulate_appdata
 * ------------------------------------------------------------------------- */

void
meminfo_accumulate_appdata(meminfo_t *self, const meminfo_t *that)
{
  pusum(&self->Size,          that->Size);         
  pusum(&self->Rss, 	      that->Rss);          
  pusum(&self->Shared_Clean,  that->Shared_Clean); 
  pusum(&self->Shared_Dirty,  that->Shared_Dirty); 
  pusum(&self->Private_Clean, that->Private_Clean);
  pusum(&self->Private_Dirty, that->Private_Dirty);
}

/* ------------------------------------------------------------------------- *
 * meminfo_accumulate_libdata
 * ------------------------------------------------------------------------- */

void
meminfo_accumulate_libdata(meminfo_t *self, const meminfo_t *that)
{
  pumax(&self->Size,          that->Size);         
  pumax(&self->Rss, 	      that->Rss);          
  pumax(&self->Rss, 	      that->Rss);          
  pumax(&self->Shared_Clean,  that->Shared_Clean); 
  pumax(&self->Shared_Dirty,  that->Shared_Dirty); 
  pusum(&self->Private_Clean, that->Private_Clean);
  pusum(&self->Private_Dirty, that->Private_Dirty);
}

/* ------------------------------------------------------------------------- *
 * meminfo_accumulate_maxdata
 * ------------------------------------------------------------------------- */

void
meminfo_accumulate_maxdata(meminfo_t *self, const meminfo_t *that)
{
  pumax(&self->Size,          that->Size);         
  pumax(&self->Rss, 	      that->Rss);          
  pumax(&self->Shared_Clean,  that->Shared_Clean); 
  pumax(&self->Shared_Dirty,  that->Shared_Dirty); 
  pumax(&self->Private_Clean, that->Private_Clean);
  pumax(&self->Private_Dirty, that->Private_Dirty);
}


/* ------------------------------------------------------------------------- *
 * meminfo_create
 * ------------------------------------------------------------------------- */

meminfo_t *
meminfo_create(void)
{
  meminfo_t *self = calloc(1, sizeof *self);
  meminfo_ctor(self);
  return self;
}

/* ------------------------------------------------------------------------- *
 * meminfo_delete
 * ------------------------------------------------------------------------- */

void 
meminfo_delete(meminfo_t *self)
{
  if( self != 0 )
  {
    meminfo_dtor(self);
    free(self);
  }
}

/* ------------------------------------------------------------------------- *
 * meminfo_delete_cb
 * ------------------------------------------------------------------------- */

void 
meminfo_delete_cb(void *self)
{
  meminfo_delete(self);
}


/* ========================================================================= *
 * mapinfo_t  --  methods
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * mapinfo_ctor
 * ------------------------------------------------------------------------- */

void 
mapinfo_ctor(mapinfo_t *self)
{
  self->head	= 0;
  self->tail	= 0;
  self->prot	= 0;
  self->offs	= 0;
  self->node	= 0;
  self->flgs	= 0;
  self->path	= 0;
  self->type    = 0;
}

/* ------------------------------------------------------------------------- *
 * mapinfo_dtor
 * ------------------------------------------------------------------------- */

void 
mapinfo_dtor(mapinfo_t *self)
{
  free(self->prot);
  free(self->node);
  free(self->path);
  free(self->type);
}

/* ------------------------------------------------------------------------- *
 * mapinfo_create
 * ------------------------------------------------------------------------- */

mapinfo_t *
mapinfo_create(void)
{
  mapinfo_t *self = calloc(1, sizeof *self);
  mapinfo_ctor(self);
  return self;
}

/* ------------------------------------------------------------------------- *
 * mapinfo_delete
 * ------------------------------------------------------------------------- */

void 
mapinfo_delete(mapinfo_t *self)
{
  if( self != 0 )
  {
    mapinfo_dtor(self);
    free(self);
  }
}

/* ------------------------------------------------------------------------- *
 * mapinfo_delete_cb
 * ------------------------------------------------------------------------- */

void 
mapinfo_delete_cb(void *self)
{
  mapinfo_delete(self);
}


/* ========================================================================= *
 * pidinfo_t  --  methods
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * pidinfo_ctor
 * ------------------------------------------------------------------------- */

void 
pidinfo_ctor(pidinfo_t *self)
{
  self->Name	= strdup("<noname>");
  self->PPid	= 0;
  self->Threads	= 0;
  self->VmSize	= 0;
  self->VmLck	= 0;
  self->VmRSS	= 0;
  self->VmData	= 0;
  self->VmStk	= 0;
  self->VmExe	= 0;
  self->VmLib	= 0;
  self->VmPTE	= 0;
}

/* ------------------------------------------------------------------------- *
 * pidinfo_dtor
 * ------------------------------------------------------------------------- */

void 
pidinfo_dtor(pidinfo_t *self)
{
  free(self->Name);
}

/* ------------------------------------------------------------------------- *
 * pidinfo_parse
 * ------------------------------------------------------------------------- */

void
pidinfo_parse(pidinfo_t *self, char *line)
{
  char *key = slice(&line, ':');
  char *val = slice(&line,  -1);
  
  if( !strcmp(key, "Name") )
  {
    while( *val == '-' ) ++val;
    xstrset(&self->Name, val);
  }
  else if( !strcmp(key, "Pid") ) 
  {
    self->Pid	= strtol(val, 0, 10);
  }
  else if( !strcmp(key, "PPid") ) 
  {
    self->PPid	= strtol(val, 0, 10);
  }
  else if( !strcmp(key, "Threads") ) 
  {
    self->Threads	= strtol(val, 0, 10);
  }
  else if( !strcmp(key, "VmSize") ) 
  {
    self->VmSize	= strtoul(val, 0, 10);
  }
  else if( !strcmp(key, "VmLck") ) 
  {
    self->VmLck	= strtoul(val, 0, 10);
  }
  else if( !strcmp(key, "VmRSS") ) 
  {
    self->VmRSS	= strtoul(val, 0, 10);
  }
  else if( !strcmp(key, "VmData") ) 
  {
    self->VmData	= strtoul(val, 0, 10);
  }
  else if( !strcmp(key, "VmStk") ) 
  {
    self->VmStk	= strtoul(val, 0, 10);
  }
  else if( !strcmp(key, "VmExe") ) 
  {
    self->VmExe	= strtoul(val, 0, 10);
  }
  else if( !strcmp(key, "VmLib") ) 
  {
    self->VmLib	= strtoul(val, 0, 10);
  }
  else if( !strcmp(key, "VmPTE") ) 
  {
    self->VmPTE	= strtoul(val, 0, 10);
  }
  else
  {
    static unknown_t unkn = UNKNOWN_INIT;
    if( unknown_add(&unkn, key) )
    {
      fprintf(stderr, "%s: Unknown key: '%s' = '%s'\n", __FUNCTION__, key, val);
    }
  }
}

/* ------------------------------------------------------------------------- *
 * pidinfo_create
 * ------------------------------------------------------------------------- */

pidinfo_t *
pidinfo_create(void)
{
  pidinfo_t *self = calloc(1, sizeof *self);
  pidinfo_ctor(self);
  return self;
}

/* ------------------------------------------------------------------------- *
 * pidinfo_delete
 * ------------------------------------------------------------------------- */

void 
pidinfo_delete(pidinfo_t *self)
{
  if( self != 0 )
  {
    pidinfo_dtor(self);
    free(self);
  }
}

/* ------------------------------------------------------------------------- *
 * pidinfo_delete_cb
 * ------------------------------------------------------------------------- */

void 
pidinfo_delete_cb(void *self)
{
  pidinfo_delete(self);
}


/* ========================================================================= *
 * smapsmapp_t  --  methods
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * smapsmapp_ctor
 * ------------------------------------------------------------------------- */

void 
smapsmapp_ctor(smapsmapp_t *self)
{
  static int uid = 0;
  self->smapsmapp_uid = uid++;

  self->smapsmapp_AID = -1;
  self->smapsmapp_PID = -1;
  self->smapsmapp_LID = -1;
  self->smapsmapp_TID = -1;
  self->smapsmapp_EID = -1;
  
  mapinfo_ctor(&self->smapsmapp_map);
  meminfo_ctor(&self->smapsmapp_mem);
}

/* ------------------------------------------------------------------------- *
 * smapsmapp_dtor
 * ------------------------------------------------------------------------- */

void 
smapsmapp_dtor(smapsmapp_t *self)
{
  mapinfo_dtor(&self->smapsmapp_map);
  meminfo_dtor(&self->smapsmapp_mem);
}

/* ------------------------------------------------------------------------- *
 * smapsmapp_create
 * ------------------------------------------------------------------------- */

smapsmapp_t *
smapsmapp_create(void)
{
  smapsmapp_t *self = calloc(1, sizeof *self);
  smapsmapp_ctor(self);
  return self;
}

/* ------------------------------------------------------------------------- *
 * smapsmapp_delete
 * ------------------------------------------------------------------------- */

void 
smapsmapp_delete(smapsmapp_t *self)
{
  if( self != 0 )
  {
    smapsmapp_dtor(self);
    free(self);
  }
}

/* ------------------------------------------------------------------------- *
 * smapsmapp_delete_cb
 * ------------------------------------------------------------------------- */

void 
smapsmapp_delete_cb(void *self)
{
  smapsmapp_delete(self);
}

/* ========================================================================= *
 * smapsproc_t  --  methods
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * smapsproc_ctor
 * ------------------------------------------------------------------------- */

void 
smapsproc_ctor(smapsproc_t *self)
{
  static int uid = 0;
  self->smapsproc_uid = uid++;
  
  self->smapsproc_AID = -1;
  self->smapsproc_PID = -1;
  
  pidinfo_ctor(&self->smapsproc_pid);
  array_ctor(&self->smapsproc_mapplist, smapsmapp_delete_cb);
  
  self->smapsproc_parent = 0;
  array_ctor(&self->smapsproc_children, 0);
}

/* ------------------------------------------------------------------------- *
 * smapsproc_dtor
 * ------------------------------------------------------------------------- */

void 
smapsproc_dtor(smapsproc_t *self)
{
  pidinfo_dtor(&self->smapsproc_pid);
  array_dtor(&self->smapsproc_mapplist);
  array_dtor(&self->smapsproc_children);
}

/* ------------------------------------------------------------------------- *
 * smapsproc_are_same
 * ------------------------------------------------------------------------- */

int
smapsproc_are_same(smapsproc_t *self, smapsproc_t *that)
{
  if( strcmp(self->smapsproc_pid.Name, that->smapsproc_pid.Name) ) return 0;
  
#if 01
# define cp(v) if( self->smapsproc_pid.v != that->smapsproc_pid.v ) return 0;
  cp(VmSize)
  cp(VmLck)
  cp(VmRSS)
  cp(VmData)
  cp(VmStk)
  cp(VmExe)
  cp(VmLib)
  cp(VmPTE)
# undef cp
#endif
  
  return 1;
}

/* ------------------------------------------------------------------------- *
 * smapsproc_adopt_children
 * ------------------------------------------------------------------------- */

void 
smapsproc_adopt_children(smapsproc_t *self, smapsproc_t *that)
{
  /* - - - - - - - - - - - - - - - - - - - *
   * fix parent pointers
   * - - - - - - - - - - - - - - - - - - - */

  for( size_t i = 0; i < that->smapsproc_children.size; ++i )
  {
    smapsproc_t *child = that->smapsproc_children.data[i];
    child->smapsproc_parent = self;
    child->smapsproc_pid.PPid = self->smapsproc_pid.Pid;
  }
  
  /* - - - - - - - - - - - - - - - - - - - *
   * move children to new parent
   * - - - - - - - - - - - - - - - - - - - */

  array_move(&self->smapsproc_children, &that->smapsproc_children);
}

/* ------------------------------------------------------------------------- *
 * smapsproc_collapse_threads
 * ------------------------------------------------------------------------- */

void 
smapsproc_collapse_threads(smapsproc_t *self)
{
  /* - - - - - - - - - - - - - - - - - - - *
   * recurse depth first
   * - - - - - - - - - - - - - - - - - - - */

  for( int i = 0; i < self->smapsproc_children.size; ++i )
  {
    smapsproc_t *that = self->smapsproc_children.data[i];
    smapsproc_collapse_threads(that);
  }

  /* - - - - - - - - - - - - - - - - - - - *
   * heuristic: children that are similar
   * enough to parent are actually threads
   * - - - - - - - - - - - - - - - - - - - */

  for( int i = 0; i < self->smapsproc_children.size; ++i )
  {
    smapsproc_t *that = self->smapsproc_children.data[i];
    
    if( smapsproc_are_same(self, that) )
    {
      fprintf(stderr, "REPARENT: %d\n", that->smapsproc_pid.Pid);

      /* - - - - - - - - - - - - - - - - - - - *
       * adopt grandchildren
       * - - - - - - - - - - - - - - - - - - - */

      smapsproc_adopt_children(self, that);

      /* - - - - - - - - - - - - - - - - - - - *
       * disassosiate parent & child
       * - - - - - - - - - - - - - - - - - - - */

      self->smapsproc_children.data[i] = 0;
      self->smapsproc_pid.Threads += that->smapsproc_pid.Threads;
      that->smapsproc_parent = 0;
    }
  }
  array_compact(&self->smapsproc_children);
  
}


/* ------------------------------------------------------------------------- *
 * smapsproc_add_mapping
 * ------------------------------------------------------------------------- */

smapsmapp_t  *
smapsproc_add_mapping(smapsproc_t *self,
		      unsigned head,
		      unsigned tail,
		      const char *prot,
		      unsigned offs,
		      const char *node,
		      unsigned flgs,
		      const char *path)
{
  
  smapsmapp_t *mapp = smapsmapp_create();
 
  mapp->smapsmapp_map.head = head;
  mapp->smapsmapp_map.tail = tail;
  mapp->smapsmapp_map.offs = offs;
  mapp->smapsmapp_map.flgs = flgs;
  
  if( path == 0 || *path == 0 )
  {
    path = "[anon]";
  }

  xstrset(&mapp->smapsmapp_map.prot, prot);
  xstrset(&mapp->smapsmapp_map.node, node);
  xstrset(&mapp->smapsmapp_map.path, path);
  
  if( *path == '[' )
  {
    char temp[32];
    ++path;
    snprintf(temp, sizeof temp, "%.*s", (int)strcspn(path,"]"), path);
    xstrset(&mapp->smapsmapp_map.type, temp);
  }
  else
  {
    xstrset(&mapp->smapsmapp_map.type, strchr(prot, 'x') ? "code" : "data");
  }
  
  array_add(&self->smapsproc_mapplist, mapp);
  return mapp;
}

/* ------------------------------------------------------------------------- *
 * smapsproc_create
 * ------------------------------------------------------------------------- */

smapsproc_t *
smapsproc_create(void)
{
  smapsproc_t *self = calloc(1, sizeof *self);
  smapsproc_ctor(self);
  return self;
}

/* ------------------------------------------------------------------------- *
 * smapsproc_delete
 * ------------------------------------------------------------------------- */

void 
smapsproc_delete(smapsproc_t *self)
{
  if( self != 0 )
  {
    smapsproc_dtor(self);
    free(self);
  }
}

/* ------------------------------------------------------------------------- *
 * smapsproc_delete_cb
 * ------------------------------------------------------------------------- */

void 
smapsproc_delete_cb(void *self)
{
  smapsproc_delete(self);
}

/* ------------------------------------------------------------------------- *
 * smapsproc_compare_pid_cb
 * ------------------------------------------------------------------------- */

int smapsproc_compare_pid_cb(const void *a1, const void *a2)
{
  const smapsproc_t *p1 = *(const smapsproc_t **)a1;
  const smapsproc_t *p2 = *(const smapsproc_t **)a2;
  return p1->smapsproc_pid.Pid - p2->smapsproc_pid.Pid;
}

/* ------------------------------------------------------------------------- *
 * smapsproc_compare_name_pid_cb
 * ------------------------------------------------------------------------- */

int smapsproc_compare_name_pid_cb(const void *a1, const void *a2)
{
  const smapsproc_t *p1 = *(const smapsproc_t **)a1;
  const smapsproc_t *p2 = *(const smapsproc_t **)a2;
  int r = strcasecmp(p1->smapsproc_pid.Name, p2->smapsproc_pid.Name);
  return r ? r : (p1->smapsproc_pid.Pid - p2->smapsproc_pid.Pid);
}


/* ========================================================================= *
 * smapssnap_t  --  methods
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * smapssnap_ctor
 * ------------------------------------------------------------------------- */

void 
smapssnap_ctor(smapssnap_t *self)
{
  self->smapssnap_source = strdup("<unset>");
  self->smapssnap_format = SNAPFORMAT_OLD;
  
  array_ctor(&self->smapssnap_proclist, smapsproc_delete_cb);
  smapsproc_ctor(&self->smapssnap_rootproc);
}

/* ------------------------------------------------------------------------- *
 * smapssnap_dtor
 * ------------------------------------------------------------------------- */

void 
smapssnap_dtor(smapssnap_t *self)
{
  free(self->smapssnap_source);
  array_dtor(&self->smapssnap_proclist);
  smapsproc_dtor(&self->smapssnap_rootproc);
}

/* ------------------------------------------------------------------------- *
 * smapssnap_get_source
 * ------------------------------------------------------------------------- */

const char *
smapssnap_get_source(smapssnap_t *self)
{
  return self->smapssnap_source;
}

/* ------------------------------------------------------------------------- *
 * smapssnap_set_source
 * ------------------------------------------------------------------------- */

void
smapssnap_set_source(smapssnap_t *self, const char *path)
{
  xstrset(&self->smapssnap_source, path);
}

/* ------------------------------------------------------------------------- *
 * smapssnap_create_hierarchy
 * ------------------------------------------------------------------------- */

void
smapssnap_create_hierarchy(smapssnap_t *self)
{
  /* - - - - - - - - - - - - - - - - - - - *
   * binsearch utility function
   * - - - - - - - - - - - - - - - - - - - */

  auto smapsproc_t *find(int pid)
  {
    int lo = 0, hi = self->smapssnap_proclist.size;
    while( lo < hi )
    {
      int i = (lo + hi) / 2;
      smapsproc_t *p = self->smapssnap_proclist.data[i];
      
      if( p->smapsproc_pid.Pid > pid ) { hi = i+0; continue; }
      if( p->smapsproc_pid.Pid < pid ) { lo = i+1; continue; }
      return p;
    }
    return 0;
  }

  /* - - - - - - - - - - - - - - - - - - - *
   * sort processes by PID
   * - - - - - - - - - - - - - - - - - - - */

  array_sort(&self->smapssnap_proclist, smapsproc_compare_pid_cb);
  
  /* - - - - - - - - - - - - - - - - - - - *
   * find parent for every process
   * - - - - - - - - - - - - - - - - - - - */

  for( size_t i = 0; i < self->smapssnap_proclist.size; ++i )
  {
    smapsproc_t *cur = self->smapssnap_proclist.data[i];
    smapsproc_t *par = find(cur->smapsproc_pid.PPid);
    
    assert( cur->smapsproc_parent == 0 );
    
    if( par == 0 || par == cur )
    {
      if( cur->smapsproc_pid.PPid != 0 )
      {
	fprintf(stderr, "PPID %d not found\n", cur->smapsproc_pid.PPid);
      }
      par = &self->smapssnap_rootproc;
    }

    cur->smapsproc_parent = par;
    array_add(&par->smapsproc_children, cur);
  }
}

/* ------------------------------------------------------------------------- *
 * smapssnap_collapse_threads
 * ------------------------------------------------------------------------- */

void
smapssnap_collapse_threads(smapssnap_t *self)
{
  smapsproc_collapse_threads(&self->smapssnap_rootproc);
  
  for( size_t i = 0; i < self->smapssnap_proclist.size; ++i )
  {
    smapsproc_t *cur = self->smapssnap_proclist.data[i];
    
    if( cur->smapsproc_parent == 0 )
    {
      self->smapssnap_proclist.data[i] = 0;
      smapsproc_delete(cur);
    }
  }
  array_compact(&self->smapssnap_proclist);
}


/* ------------------------------------------------------------------------- *
 * smapssnap_add_process
 * ------------------------------------------------------------------------- */

smapsproc_t *
smapssnap_add_process(smapssnap_t *self, int pid)
{
  smapsproc_t *proc;
  
  for( int i = self->smapssnap_proclist.size; i-- > 0; )
  {
    proc = self->smapssnap_proclist.data[i];
    if( proc->smapsproc_pid.Pid == pid )
    {
      return proc;
    }
  }
  proc = smapsproc_create();
  proc->smapsproc_pid.Pid = pid;
  array_add(&self->smapssnap_proclist, proc);  
  return proc;
}

/* ------------------------------------------------------------------------- *
 * smapssnap_load_cap
 * ------------------------------------------------------------------------- */

int
smapssnap_load_cap(smapssnap_t *self, const char *path)
{
  auto int hexterm(const char *s)
  {
    while( isxdigit(*s) )
    {
      ++s;
    }
    return *s;
  }
  
  int          error = -1;
  FILE        *file  = 0;
  smapsproc_t *proc  = 0;
  smapsmapp_t *mapp  = 0;
  char        *data  = 0;
  size_t       size  = 0;

  smapssnap_set_source(self, path);  
  
  if( (file = fopen(path, "r")) == 0 )
  {
    perror(path); goto cleanup;
  }
  
  while( getline(&data, &size, file) >= 0 )
  {
    data[strcspn(data, "\r\n")] = 0;
    
    if( *data == 0 )
    {
      // ignore empty lines
    }
    else if( !strncmp(data, "==>", 3) )
    {
      // ==> /proc/1/smaps <==
      
      proc = 0;
      mapp = 0;
      
      char *pos = data;
      
      while( *pos && strcmp(slice(&pos, '/'), "proc") ) { }      
      int pid = strtol(slice(&pos, '/'), 0, 10);
      if( pid > 0 && !strcmp(slice(&pos, -1), "smaps") )
      {
	proc = smapssnap_add_process(self, pid);
      }
      else
      {
	assert( 0 );
      }
    }
    else if( *data == '#' )
    {
      // #Name: init__2_
      // #Pid: 1
      // #PPid: 0
      // #Threads: 1

      assert( proc != 0 );
      pidinfo_parse(&proc->smapsproc_pid, data+1);
      self->smapssnap_format = SNAPFORMAT_NEW;
    }
    else if( hexterm(data) == '-' )
    {
      // 08048000-08051000 r-xp 00000000 03:03 2060370    /sbin/init
      
      assert( proc != 0 );
      
      char *pos = data;
      unsigned head = strtoul(slice(&pos, '-'), 0, 16);
      unsigned tail = strtoul(slice(&pos,  -1), 0, 16);
      char    *prot = slice(&pos,  -1);
      unsigned offs = strtoul(slice(&pos,  -1), 0, 16);
      char    *node = slice(&pos,  -1);
      unsigned flgs = strtoul(slice(&pos,  -1), 0, 10);
      char    *path = slice(&pos,  0);
      
      mapp = smapsproc_add_mapping(proc, head, tail, prot,
				   offs, node, flgs, path);
    }
    else
    {
      // Size:                36 kB
      // Rss:                 36 kB
      // Shared_Clean:         0 kB
      // Shared_Dirty:         0 kB
      // Private_Clean:       36 kB
      // Private_Dirty:        0 kB
      
      assert( mapp != 0 );
      meminfo_parse(&mapp->smapsmapp_mem, data);
    }
  }  
  
  error = 0;
  
  cleanup:
  
  free(data);
  
  if( file ) fclose(file);
  
  return error;
}

/* ------------------------------------------------------------------------- *
 * smapssnap_save_cap
 * ------------------------------------------------------------------------- */

int
smapssnap_save_cap(smapssnap_t *self, const char *path)
{
  int          error = -1;
  FILE        *file  = 0;

  if( (file = fopen(path, "w")) == 0 )
  {
    perror(path); goto cleanup;
  }

  array_sort(&self->smapssnap_proclist, smapsproc_compare_pid_cb);
  
  for( int p = 0; p < self->smapssnap_proclist.size; ++p )
  {
    const smapsproc_t *proc = self->smapssnap_proclist.data[p];
    const pidinfo_t   *pi = &proc->smapsproc_pid;
    
    fprintf(file, "==> /proc/%d/smaps <==\n", pi->Pid);
    
#define Ps(v) fprintf(file, "#%s: %s\n", #v, pi->v)
#define Pi(v) fprintf(file, "#%s: %d\n", #v, pi->v)
#define Pu(v) fprintf(file, "#%s: %u\n", #v, pi->v)
    
    Ps(Name);
    Pi(Pid);
    Pi(PPid);
    Pi(Threads);
    
    if( pi->VmSize || pi->VmLck || pi->VmRSS ||	pi->VmData || 
	pi->VmStk  || pi->VmExe || pi->VmLib  || pi->VmPTE )
    {
      Pu(VmSize);
      Pu(VmLck);
      Pu(VmRSS);
      Pu(VmData);
      Pu(VmStk);
      Pu(VmExe);
      Pu(VmLib);
      Pu(VmPTE);
    }
#undef Pu
#undef Pi
#undef Ps

    for( int m = 0; m < proc->smapsproc_mapplist.size; ++m )
    {
      const smapsmapp_t *mapp = proc->smapsproc_mapplist.data[m];
      const mapinfo_t   *map  = &mapp->smapsmapp_map;
      const meminfo_t   *mem  = &mapp->smapsmapp_mem;

      fprintf(file, "%08x-%08x %s %08x %s %-10u %s\n",
	      map->head, map->tail, map->prot,
	      map->offs, map->node, map->flgs,
	      map->path);
      
#define Pu(v) fprintf(file, "%-14s %8u kB\n", #v":", mem->v)

      Pu(Size);
      Pu(Rss);  
      Pu(Shared_Clean);
      Pu(Shared_Dirty);
      Pu(Private_Clean);
      Pu(Private_Dirty);
      
#undef Pu
    }
    fprintf(file, "\n");
  }
  
  error = 0;
  
  cleanup:
  
  
  if( file ) fclose(file);
  
  return error;
}

/* ------------------------------------------------------------------------- *
 * smapssnap_save_csv
 * ------------------------------------------------------------------------- */

int
smapssnap_save_csv(smapssnap_t *self, const char *path)
{
  int   error = -1;
  FILE *file  = 0;
  
  if( (file = fopen(path, "w")) == 0 )
  {
    perror(path); goto cleanup;
  }
  
  /* - - - - - - - - - - - - - - - - - - - *
   * arrange data by process name & pid
   * - - - - - - - - - - - - - - - - - - - */

  array_sort(&self->smapssnap_proclist, smapsproc_compare_pid_cb);

// QUARANTINE   array_sort(&self->snapshot_process_list, 
// QUARANTINE 	     smapsproc_compare_name_pid_cb);

  
  /* - - - - - - - - - - - - - - - - - - - *
   * output csv header
   * - - - - - - - - - - - - - - - - - - - */

  fprintf(file, "generator=%s %s\n", "PROGNAME", "PROGVERS");
  fprintf(file, "\n");

  /* - - - - - - - - - - - - - - - - - - - *
   * output csv labels
   * - - - - - - - - - - - - - - - - - - - */

  fprintf(file, 
	  "name,pid,ppid,threads,"
	  "head,tail,prot,offs,node,flag,path,"
	  "size,rss,shacln,shadty,pricln,pridty,"
	  "pri,sha,cln,cow\n");
  
  /* - - - - - - - - - - - - - - - - - - - *
   * output csv table
   * - - - - - - - - - - - - - - - - - - - */

  for( size_t i = 0; i < self->smapssnap_proclist.size; ++i )
  {
    const smapsproc_t *proc = self->smapssnap_proclist.data[i];
    const pidinfo_t   *pid  = &proc->smapsproc_pid;
    
    for( size_t k = 0; k < proc->smapsproc_mapplist.size; ++k )
    {
      const smapsmapp_t *mapp = proc->smapsproc_mapplist.data[k];
      const mapinfo_t   *map  = &mapp->smapsmapp_map;
      const meminfo_t   *mem  = &mapp->smapsmapp_mem;
      
      fprintf(file, "%s,%d,%d,%d,",
	      pid->Name,
	      pid->Pid,
	      pid->PPid,
	      pid->Threads);


      fprintf(file, "%u,%u,%s,%u,%s,%u,%s,",
	      map->head, map->tail, map->prot,
	      map->offs, map->node, map->flgs,
	      map->path);
      
      fprintf(file, "%u,%u,%u,%u,%u,%u,",
	      mem->Size,
	      mem->Rss,
	      mem->Shared_Clean,
	      mem->Shared_Dirty,
	      mem->Private_Clean,
	      mem->Private_Dirty);

      fprintf(file, "%u,%u,%u,%u\n",
	      mem->Private_Dirty,
	      mem->Shared_Dirty,
	      mem->Private_Clean + mem->Shared_Clean,
	      0u);
    }
  }

  /* - - - - - - - - - - - - - - - - - - - *
   * terminate csv table
   * - - - - - - - - - - - - - - - - - - - */

  fprintf(file, "\n");
  
  /* - - - - - - - - - - - - - - - - - - - *
   * success
   * - - - - - - - - - - - - - - - - - - - */

  error = 0;
  
  /* - - - - - - - - - - - - - - - - - - - *
   * cleanup & return
   * - - - - - - - - - - - - - - - - - - - */

  cleanup:
  
  if( file )
  {
    fclose(file);
  }
  
  return error;
}


/* ------------------------------------------------------------------------- *
 * smapssnap_save_html
 * ------------------------------------------------------------------------- */

// QUARANTINE int
// QUARANTINE smapssnap_save_html(smapssnap_t *self, const char *path)
// QUARANTINE {
// QUARANTINE   int       error = -1;
// QUARANTINE   analyze_t *az   = analyze_create();
// QUARANTINE 
// QUARANTINE   /* - - - - - - - - - - - - - - - - - - - *
// QUARANTINE    * enumerate & accumulate data
// QUARANTINE    * - - - - - - - - - - - - - - - - - - - */
// QUARANTINE   
// QUARANTINE   analyze_enumerate_data(az, self);
// QUARANTINE 
// QUARANTINE   analyze_accumulate_data(az);
// QUARANTINE   
// QUARANTINE   /* - - - - - - - - - - - - - - - - - - - *
// QUARANTINE    * output html pages
// QUARANTINE    * - - - - - - - - - - - - - - - - - - - */
// QUARANTINE 
// QUARANTINE   error = analyze_emit_main_page(az, self, path);
// QUARANTINE   
// QUARANTINE 
// QUARANTINE   analyze_delete(az);
// QUARANTINE   return error;
// QUARANTINE }


/* ------------------------------------------------------------------------- *
 * smapssnap_create
 * ------------------------------------------------------------------------- */

smapssnap_t *
smapssnap_create(void)
{
  smapssnap_t *self = calloc(1, sizeof *self);
  smapssnap_ctor(self);
  return self;
}

/* ------------------------------------------------------------------------- *
 * smapssnap_delete
 * ------------------------------------------------------------------------- */

void 
smapssnap_delete(smapssnap_t *self)
{
  if( self != 0 )
  {
    smapssnap_dtor(self);
    free(self);
  }
}

/* ------------------------------------------------------------------------- *
 * smapssnap_delete_cb
 * ------------------------------------------------------------------------- */

void 
smapssnap_delete_cb(void *self)
{
  smapssnap_delete(self);
}

//#include "scrap.txt"

// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX



/* ========================================================================= *
 * analyze_t  --  methods
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * analyze_ctor
 * ------------------------------------------------------------------------- */

void 
analyze_ctor(analyze_t *self)
{
  self->mapp_tab = array_create(0); /* ownership of data not taken */
  
  self->appl_tab = symtab_create();
  self->type_tab = symtab_create();
  self->path_tab = symtab_create();
  self->summ_tab = symtab_create();

  self->ntypes = 0;
  self->nappls = 0;
  self->npaths = 0;
  self->groups = 0;

  self->stype  = 0;
  self->sappl  = 0;
  self->spath  = 0;

  self->grp_app = 0;
  self->grp_lib = 0;
  
}

/* ------------------------------------------------------------------------- *
 * analyze_dtor
 * ------------------------------------------------------------------------- */

void 
analyze_dtor(analyze_t *self)
{
  array_delete(self->mapp_tab);

  symtab_delete(self->appl_tab);
  symtab_delete(self->type_tab);
  symtab_delete(self->path_tab);
  symtab_delete(self->summ_tab);
  
  free(self->stype);
  free(self->sappl);
  free(self->spath);

  free(self->grp_app);
  free(self->grp_lib);
  
  free(self->app_mem);
  free(self->grp_mem);
  free(self->lib_mem);
  free(self->sysest);
  free(self->sysmax);
  free(self->appmax);
  
}

/* ------------------------------------------------------------------------- *
 * analyze_grp_mem
 * ------------------------------------------------------------------------- */

INLINE meminfo_t *
analyze_grp_mem(analyze_t *self, int gid, int tid)
{
  assert( 0 <= gid && gid < self->groups );
  assert( 0 <= tid && tid < self->ntypes );
  return &self->grp_mem[tid + gid * self->ntypes];
}

/* ------------------------------------------------------------------------- *
 * analyze_lib_mem
 * ------------------------------------------------------------------------- */

INLINE meminfo_t *
analyze_lib_mem(analyze_t *self, int lid, int tid)
{
  assert( 0 <= lid && lid < self->npaths );
  assert( 0 <= tid && tid < self->ntypes );
  return &self->lib_mem[tid + lid * self->ntypes];
}

/* ------------------------------------------------------------------------- *
 * analyze_app_mem
 * ------------------------------------------------------------------------- */

INLINE meminfo_t *
analyze_app_mem(analyze_t *self, int aid, int tid)
{
  assert( 0 <= aid && aid < self->nappls );
  assert( 0 <= tid && tid < self->ntypes );
  return &self->app_mem[tid + aid * self->ntypes];
}

/* ------------------------------------------------------------------------- *
 * analyze_sysest
 * ------------------------------------------------------------------------- */

INLINE meminfo_t *
analyze_sysest(analyze_t *self, int tid)
{
  assert( 0 <= tid && tid < self->ntypes );
  return &self->sysest[tid];
}

/* ------------------------------------------------------------------------- *
 * analyze_sysmax
 * ------------------------------------------------------------------------- */

INLINE meminfo_t *
analyze_sysmax(analyze_t *self, int tid)
{
  assert( 0 <= tid && tid < self->ntypes );
  return &self->sysmax[tid];
}

/* ------------------------------------------------------------------------- *
 * analyze_appmax
 * ------------------------------------------------------------------------- */

INLINE meminfo_t *
analyze_appmax(analyze_t *self, int tid)
{
  assert( 0 <= tid && tid < self->ntypes );
  return &self->appmax[tid];
}


/* ------------------------------------------------------------------------- *
 * analyze_create
 * ------------------------------------------------------------------------- */

analyze_t *
analyze_create(void)
{
  analyze_t *self = calloc(1, sizeof *self);
  analyze_ctor(self);
  return self;
}

/* ------------------------------------------------------------------------- *
 * analyze_delete
 * ------------------------------------------------------------------------- */

void 
analyze_delete(analyze_t *self)
{
  if( self != 0 )
  {
    analyze_dtor(self);
    free(self);
  }
}

/* ------------------------------------------------------------------------- *
 * analyze_delete_cb
 * ------------------------------------------------------------------------- */

void 
analyze_delete_cb(void *self)
{
  analyze_delete(self);
}

/* ------------------------------------------------------------------------- *
 * analyze_enumerate_data
 * ------------------------------------------------------------------------- */

void 
analyze_enumerate_data(analyze_t *self, smapssnap_t *snap)
{
  auto int local_compare_path(const void *a1, const void *a2)
  {
    const smapsmapp_t *m1 = *(const smapsmapp_t **)a1;
    const smapsmapp_t *m2 = *(const smapsmapp_t **)a2;
    return path_compare(m1->smapsmapp_map.path, m2->smapsmapp_map.path);
  }
  
  
  char temp[512];
  
  /* - - - - - - - - - - - - - - - - - - - *
   * sort process list by: app name & pid
   * - - - - - - - - - - - - - - - - - - - */
  
  array_sort(&snap->smapssnap_proclist, smapsproc_compare_name_pid_cb);
  
  /* - - - - - - - - - - - - - - - - - - - *
   * Enumerate:
   * - application names
   * - application instances
   * - mapping types
   * 
   * Collect all smaps data to one array
   * while at it.
   * - - - - - - - - - - - - - - - - - - - */
  
  symtab_enumerate(self->type_tab, "total");
  symtab_enumerate(self->type_tab, "code");
  symtab_enumerate(self->type_tab, "data");
  symtab_enumerate(self->type_tab, "heap");
  symtab_enumerate(self->type_tab, "anon");
  symtab_enumerate(self->type_tab, "stack");
  
  for( size_t i = 0; i < snap->smapssnap_proclist.size; ++i )
  {
    smapsproc_t *proc = snap->smapssnap_proclist.data[i];
    
    /* - - - - - - - - - - - - - - - - - - - *
     * application <- app name
     * - - - - - - - - - - - - - - - - - - - */
    
    // QUARANTINE     proc->smapsproc_AID = symtab_enumerate(self->appl_tab, proc->smapsproc_pid.Name);
    
    /* - - - - - - - - - - - - - - - - - - - *
     * app instance <- app name + PID
     * - - - - - - - - - - - - - - - - - - - */
    
    snprintf(temp, sizeof temp, "%s (%d)", 
	     proc->smapsproc_pid.Name, 
	     proc->smapsproc_pid.Pid);
    
    proc->smapsproc_PID = symtab_enumerate(self->appl_tab, temp);
    
    // FIXME: use PID, not AID
    proc->smapsproc_AID = proc->smapsproc_PID;
    
    for( size_t k = 0; k < proc->smapsproc_mapplist.size; ++k )
    {
      smapsmapp_t *mapp = proc->smapsproc_mapplist.data[k];
      
      mapp->smapsmapp_AID = proc->smapsproc_AID;
      mapp->smapsmapp_PID = proc->smapsproc_PID;
      mapp->smapsmapp_TID = symtab_enumerate(self->type_tab, mapp->smapsmapp_map.type);
      array_add(self->mapp_tab, mapp);
    }
  }
  
  /* - - - - - - - - - - - - - - - - - - - *
   * sort smaps data by file path
   * - - - - - - - - - - - - - - - - - - - */
  
  array_sort(self->mapp_tab, local_compare_path);
  
  /* - - - - - - - - - - - - - - - - - - - *
   * Enumerate:
   * - mapping paths
   * - application instance + path pairs
   * - - - - - - - - - - - - - - - - - - - */
  
  for( size_t k = 0; k < self->mapp_tab->size; ++k )
  {
    smapsmapp_t *mapp = self->mapp_tab->data[k];
    
    /* - - - - - - - - - - - - - - - - - - - *
     * mapping path
     * - - - - - - - - - - - - - - - - - - - */
    
    mapp->smapsmapp_LID = symtab_enumerate(self->path_tab, mapp->smapsmapp_map.path);
    
    /* - - - - - - - - - - - - - - - - - - - *
     * application + mapping path
     * - - - - - - - - - - - - - - - - - - - */
    
    snprintf(temp, sizeof temp, "app%03d::lib%03d",
	     mapp->smapsmapp_AID,
	     mapp->smapsmapp_LID);
    mapp->smapsmapp_EID = symtab_enumerate(self->summ_tab, temp);
  }
  
  /* - - - - - - - - - - - - - - - - - - - *
   * reverse lookup tables for enums
   * - - - - - - - - - - - - - - - - - - - */
  
  self->ntypes = self->type_tab->symtab_count;
  self->nappls = self->appl_tab->symtab_count;
  self->npaths = self->path_tab->symtab_count;
  self->groups = self->summ_tab->symtab_count;
  
  self->stype  = calloc(self->ntypes, sizeof *self->stype);
  self->sappl  = calloc(self->nappls, sizeof *self->sappl);
  self->spath  = calloc(self->npaths, sizeof *self->spath);
  
  for( int i = 0; i < self->ntypes; ++i )
  {
    symbol_t *s = &self->type_tab->symtab_entry[i];
    self->stype[s->symbol_val] = s->symbol_key;
  }
  for( int i = 0; i < self->nappls; ++i )
  {
    symbol_t *s = &self->appl_tab->symtab_entry[i];
    self->sappl[s->symbol_val] = s->symbol_key;
  }
  for( int i = 0; i < self->npaths; ++i )
  {
    symbol_t *s = &self->path_tab->symtab_entry[i];
    self->spath[s->symbol_val] = s->symbol_key;
  }
  
  /* - - - - - - - - - - - - - - - - - - - *
   * group -> appl and/or path mapping
   * - - - - - - - - - - - - - - - - - - - */
  
  self->grp_app = calloc(self->groups, sizeof *self->grp_app);
  self->grp_lib = calloc(self->groups, sizeof *self->grp_lib);
  
  for( int g = 0; g < self->groups; ++g )
  {
    self->grp_app[g] = self->grp_lib[g] = -1;
  }
  
  for( size_t k = 0; k < self->mapp_tab->size; ++k )
  {
    smapsmapp_t *mapp = self->mapp_tab->data[k];
    int g = mapp->smapsmapp_EID;
    int a = mapp->smapsmapp_AID;
    int p = mapp->smapsmapp_LID;
    
    // QUARANTINE     printf("G:%d -> A:%d, L:%d\n", g, a, p);
    
    assert( self->grp_app[g] == -1 || self->grp_app[g] == a );
    assert( self->grp_lib[g] == -1 || self->grp_lib[g] == p );
    
    self->grp_app[g] = a;
    self->grp_lib[g] = p;
  }
  
  for( int g = 0; g < self->groups; ++g )
  {
    assert( self->grp_app[g] != -1 );
    assert( self->grp_lib[g] != -1 );
  }
}

/* ------------------------------------------------------------------------- *
 * analyze_accumulate_data
 * ------------------------------------------------------------------------- */

void 
analyze_accumulate_data(analyze_t *self)
{
  /* - - - - - - - - - - - - - - - - - - - *
   * allocate accumulation tables
   * - - - - - - - - - - - - - - - - - - - */

  self->grp_mem = calloc(self->groups * self->ntypes, sizeof *self->grp_mem);
  self->app_mem = calloc(self->nappls * self->ntypes, sizeof *self->app_mem);
  self->lib_mem = calloc(self->npaths * self->ntypes, sizeof *self->lib_mem);
  
  self->sysest  = calloc(self->ntypes, sizeof *self->sysest);
  self->sysmax  = calloc(self->ntypes, sizeof *self->sysmax);
  self->appmax  = calloc(self->ntypes, sizeof *self->appmax);

  /* - - - - - - - - - - - - - - - - - - - *
   * accumulate raw smaps data by
   * process + map path by type grouping
   * - - - - - - - - - - - - - - - - - - - */
  
  for( size_t k = 0; k < self->mapp_tab->size; ++k )
  {
    smapsmapp_t *mapp = self->mapp_tab->data[k];

    meminfo_t   *srce = &mapp->smapsmapp_mem;
    
    meminfo_t   *dest = analyze_grp_mem(self,
					mapp->smapsmapp_EID,
					mapp->smapsmapp_TID);

    pusum(&dest->Size,          srce->Size);         
    pusum(&dest->Rss, 	        srce->Rss);          
    pusum(&dest->Shared_Clean,  srce->Shared_Clean); 
    pusum(&dest->Shared_Dirty,  srce->Shared_Dirty); 
    pusum(&dest->Private_Clean, srce->Private_Clean);
    pusum(&dest->Private_Dirty, srce->Private_Dirty);
  }
  
  /* - - - - - - - - - - - - - - - - - - - *
   * accumulate grouped smaps data to
   * application instance & library
   * - - - - - - - - - - - - - - - - - - - */
  
  for( int g = 0; g < self->groups; ++g )
  {
    int a = self->grp_app[g];
    int p = self->grp_lib[g];
    
    // Note: t=0 -> "total"
    for( int t = 1; t < self->ntypes; ++t )
    {
      meminfo_t   *srce = analyze_grp_mem(self, g, t);
      meminfo_t   *dest;

      /* - - - - - - - - - - - - - - - - - - - *
       * process+library/type -> process/type
       * - - - - - - - - - - - - - - - - - - - */

      dest = analyze_app_mem(self, a, t);
      
      pusum(&dest->Size,          srce->Size);         
      pusum(&dest->Rss, 	  srce->Rss);          
      pusum(&dest->Shared_Clean,  srce->Shared_Clean); 
      pusum(&dest->Shared_Dirty,  srce->Shared_Dirty); 
      pusum(&dest->Private_Clean, srce->Private_Clean);
      pusum(&dest->Private_Dirty, srce->Private_Dirty);

      /* - - - - - - - - - - - - - - - - - - - *
       * process+library/type -> library/type
       * - - - - - - - - - - - - - - - - - - - */
      
      dest = analyze_lib_mem(self, p, t);

      pumax(&dest->Size,          srce->Size);         
      pumax(&dest->Rss, 	  srce->Rss);          
      pumax(&dest->Shared_Clean,  srce->Shared_Clean); 
      pumax(&dest->Shared_Dirty,  srce->Shared_Dirty); 
      pusum(&dest->Private_Clean, srce->Private_Clean);
      pusum(&dest->Private_Dirty, srce->Private_Dirty);
    }
  }
  
  /* - - - - - - - - - - - - - - - - - - - *
   * application instance totals
   * - - - - - - - - - - - - - - - - - - - */

  for( int i = 0; i < self->nappls; ++i )
  {
    meminfo_t *dest = analyze_app_mem(self, i, 0);
    
    for( int t = 1; t < self->ntypes; ++t )
    {
      meminfo_t *srce = analyze_app_mem(self, i, t);
      meminfo_accumulate_appdata(dest, srce);
    }
  }

  /* - - - - - - - - - - - - - - - - - - - *
   * library path totals
   * - - - - - - - - - - - - - - - - - - - */

  for( int i = 0; i < self->npaths; ++i )
  {
    meminfo_t *dest = analyze_lib_mem(self, i, 0);
    
    for( int t = 1; t < self->ntypes; ++t )
    {
      meminfo_t *srce = analyze_lib_mem(self, i, t);
      meminfo_accumulate_appdata(dest, srce);
    }
  }

  /* - - - - - - - - - - - - - - - - - - - *
   * application data -> appl estimates
   * - - - - - - - - - - - - - - - - - - - */


  for( int i = 0; i < self->nappls; ++i )
  {
    for( int t = 1; t < self->ntypes; ++t )
    {
      meminfo_t *dest, *srce;

      srce = analyze_app_mem(self, i, t);
      
      dest = analyze_appmax(self, t);
      meminfo_accumulate_maxdata(dest, srce);

      dest = analyze_sysmax(self, t);
      meminfo_accumulate_appdata(dest, srce);
    }
  }
  
  /* - - - - - - - - - - - - - - - - - - - *
   * library data -> system estimates
   * - - - - - - - - - - - - - - - - - - - */

  for( int i = 0; i < self->npaths; ++i )
  {
    for( int t = 1; t < self->ntypes; ++t )
    {
      meminfo_t *dest = analyze_sysest(self, t);
      meminfo_t *srce = analyze_lib_mem(self, i, t);
      meminfo_accumulate_appdata(dest, srce);
    }
  }

  /* - - - - - - - - - - - - - - - - - - - *
   * system estimate totals
   * - - - - - - - - - - - - - - - - - - - */

  for( int t = 1; t < self->ntypes; ++t )
  {
    meminfo_t *dest, *srce;
    
    srce = analyze_sysest(self, t);
    dest = analyze_sysest(self, 0);
    meminfo_accumulate_appdata(dest, srce);

    srce = analyze_sysmax(self, t);
    dest = analyze_sysmax(self, 0);
    meminfo_accumulate_appdata(dest, srce);

    srce = analyze_appmax(self, t);
    dest = analyze_appmax(self, 0);
    meminfo_accumulate_appdata(dest, srce);
  }
}


#define TP " bgcolor=\"#ffffbf\" "
#define LT " bgcolor=\"#bfffff\" "
#define D1 " bgcolor=\"#f4f4f4\" "
#define D2 " bgcolor=\"#ffffff\" "

/* ------------------------------------------------------------------------- *
 * analyze_emit_page_table
 * ------------------------------------------------------------------------- */

void
analyze_emit_page_table(analyze_t *self, FILE *file, const meminfo_t *mtab)
{
  fprintf(file, "<table border=1>\n");
  fprintf(file, "<tr>\n");
  fprintf(file, "<th rowspan=2>\n");
  fprintf(file, "<th"TP"colspan=2>%s\n", "Dirty");
  fprintf(file, "<th"TP"rowspan=2>%s\n", "Clean");
  fprintf(file, "<th"TP"rowspan=2>%s\n", "Resident");
  fprintf(file, "<th"TP"rowspan=2>%s\n", "Size");
  fprintf(file, "<th"TP"rowspan=2>%s\n", "COW");
  
  fprintf(file, "<tr>\n");
  fprintf(file, "<th"TP">%s\n", "Private");
  fprintf(file, "<th"TP">%s\n", "Shared");
  
  for( int t = 0; t < self->ntypes; ++t )
  {
    const meminfo_t *m = &mtab[t];
    const char *bg = ((t/3)&1) ? D1 : D2;
    
    fprintf(file, "<tr>\n");
    fprintf(file, "<th"LT" align=left>%s\n", self->stype[t]);
    fprintf(file, "<td %s align=right>%s\n", bg, uval(m->Private_Dirty));
    fprintf(file, "<td %s align=right>%s\n", bg, uval(m->Shared_Dirty));
    fprintf(file, "<td %s align=right>%s\n", bg, uval(m->Private_Clean + 
						      m->Shared_Clean));
    fprintf(file, "<td %s align=right>%s\n", bg, uval(m->Rss));
    fprintf(file, "<td %s align=right>%s\n", bg, uval(m->Size));
    fprintf(file, "<td %s align=right>%s\n", bg, uval(meminfo_cowest(m)));
  }
  fprintf(file, "</table>\n");
}

/* ------------------------------------------------------------------------- *
 * analyze_emit_xref_header
 * ------------------------------------------------------------------------- */

void
analyze_emit_xref_header(analyze_t *self, FILE *file, const char *type)
{
  fprintf(file, "<tr>\n");
  fprintf(file, "<th"TP"rowspan=2>%s\n", type);
  fprintf(file, "<th"TP"rowspan=2>%s\n", "Type");
  fprintf(file, "<th"TP"rowspan=2>%s\n", "Prot");
  fprintf(file, "<th"TP"rowspan=2>%s\n", "Size");
  fprintf(file, "<th"TP"rowspan=2>%s\n", "Rss");
  fprintf(file, "<th"TP"colspan=2>%s\n", "Dirty");
  fprintf(file, "<th"TP"colspan=2>%s\n", "Clean");
  
  fprintf(file, "<tr>\n");
  fprintf(file, "<th"TP">%s\n", "Private");
  fprintf(file, "<th"TP">%s\n", "Shared");
  fprintf(file, "<th"TP">%s\n", "Private");
  fprintf(file, "<th"TP">%s\n", "Shared");
}

/* ------------------------------------------------------------------------- *
 * analyze_get_apprange
 * ------------------------------------------------------------------------- */

void 
analyze_get_apprange(analyze_t *self,int lo, int hi, int *plo, int *phi, int aid)
{
  auto int cmp_app(const void *p)
  {
    const smapsmapp_t *m = p; return m->smapsmapp_AID - aid;
  }
  
  *plo = array_find_lower(self->mapp_tab, lo, hi, cmp_app);
  *phi = array_find_upper(self->mapp_tab, lo, hi, cmp_app);
}

/* ------------------------------------------------------------------------- *
 * analyze_get_librange
 * ------------------------------------------------------------------------- */

void 
analyze_get_librange(analyze_t *self,int lo, int hi, int *plo, int *phi, int lid)
{
  auto int cmp_lib(const void *p)
  {
    const smapsmapp_t *m = p; return m->smapsmapp_LID - lid;
  }
  
  *plo = array_find_lower(self->mapp_tab, lo, hi, cmp_lib);
  *phi = array_find_upper(self->mapp_tab, lo, hi, cmp_lib);
}

/* ------------------------------------------------------------------------- *
 * analyze_emit_lib_html
 * ------------------------------------------------------------------------- */

int
analyze_emit_lib_html(analyze_t *self, smapssnap_t *snap, const char *work)
{
  auto int local_lib_app_compare(const void *a1, const void *a2)
  {
    const smapsmapp_t *m1 = *(const smapsmapp_t **)a1;
    const smapsmapp_t *m2 = *(const smapsmapp_t **)a2;
    
    int r;
    
    /* - - - - - - - - - - - - - - - - - - - *
     * primary sorting: must be LID then AID
     * for the range searching below to work!
     * - - - - - - - - - - - - - - - - - - - */

    if( (r = m1->smapsmapp_LID - m2->smapsmapp_LID) != 0 ) return r;
    if( (r = m1->smapsmapp_AID - m2->smapsmapp_AID) != 0 ) return r;
    
    /* - - - - - - - - - - - - - - - - - - - *
     * secondary sorting: convenience order
     * - - - - - - - - - - - - - - - - - - - */
    
    if( (r = m1->smapsmapp_TID - m2->smapsmapp_TID) != 0 ) return r;
    if( (r = m2->smapsmapp_mem.Rss - m1->smapsmapp_mem.Rss) ) return r;

    return 0;
  }
  
  int   error = -1;
  FILE *file  = 0;
  
  char  temp[512];
  
  /* - - - - - - - - - - - - - - - - - - - *
   * sort smaps data to bsearchable order
   * - - - - - - - - - - - - - - - - - - - */

  array_sort(self->mapp_tab, local_lib_app_compare);

  /* - - - - - - - - - - - - - - - - - - - *
   * write html page for each library
   * - - - - - - - - - - - - - - - - - - - */

  for( int l = 0; l < self->npaths; ++l )
  {
    smapsmapp_t *m; int t,a;
    
    /* - - - - - - - - - - - - - - - - - - - *
     * open output file
     * - - - - - - - - - - - - - - - - - - - */

    snprintf(temp, sizeof temp, "%s/lib%03d.html", work, l);
    //printf(">> %s\n", temp);
    
    if( (file = fopen(temp, "w")) == 0 )
    {
      goto cleanup;
    }

    /* - - - - - - - - - - - - - - - - - - - *
     * html header
     * - - - - - - - - - - - - - - - - - - - */

    fprintf(file, "<html>\n");
    fprintf(file, "<head>\n");
    fprintf(file, "<title>%s</title>\n", path_basename(self->spath[l]));
    fprintf(file, "</head>\n");
    fprintf(file, "<body>\n");

    /* - - - - - - - - - - - - - - - - - - - *
     * summary table
     * - - - - - - - - - - - - - - - - - - - */

    fprintf(file, "<h1>%s: %s</h1>\n", "Library", self->spath[l]);
    analyze_emit_page_table(self, file, analyze_lib_mem(self, l, 0));

    /* - - - - - - - - - - - - - - - - - - - *
     * application xref
     * - - - - - - - - - - - - - - - - - - - */

    fprintf(file, "<h1>%s XREF</h1>\n", "Application");
    fprintf(file, "<table border=1>\n");
    
    analyze_emit_xref_header(self, file, "Application");
    
    int alo,ahi, blo,bhi;
    
    analyze_get_librange(self, 0, self->mapp_tab->size, &alo, &ahi, l);
    
    for( int base=alo; alo < ahi; alo = bhi )
    {
      m = self->mapp_tab->data[alo];
      a = m->smapsmapp_AID;
      
      analyze_get_apprange(self, alo,ahi,&blo,&bhi,a);

      for( size_t i = blo; i < bhi; ++i )
      {
	m = self->mapp_tab->data[i];
	t = m->smapsmapp_TID;
	
	fprintf(file, "<tr>\n");
	
	if( i == blo )
	{
	  fprintf(file, 
		  "<th"LT"rowspan=%d align=left>"
		  "<a href=\"lib%03d.html\">%s</a>\n",
		  bhi-blo, l, path_basename(self->sappl[a]));
	}

	const char *bg = (((i-base)/3)&1) ? D1 : D2;
	fprintf(file, "<td%s align=left>%s\n", bg, m->smapsmapp_map.type);
	fprintf(file, "<td%s align=left>%s\n", bg, m->smapsmapp_map.prot);
	fprintf(file, "<td%s align=right>%s\n", bg, uval(m->smapsmapp_mem.Size));
	fprintf(file, "<td%s align=right>%s\n", bg, uval(m->smapsmapp_mem.Rss));
	fprintf(file, "<td%s align=right>%s\n", bg, uval(m->smapsmapp_mem.Private_Dirty));
	fprintf(file, "<td%s align=right>%s\n", bg, uval(m->smapsmapp_mem.Shared_Dirty));
	fprintf(file, "<td%s align=right>%s\n", bg, uval(m->smapsmapp_mem.Private_Clean));
	fprintf(file, "<td%s align=right>%s\n", bg, uval(m->smapsmapp_mem.Shared_Clean));
      }
    }
    
    fprintf(file, "</table>\n");

    /* - - - - - - - - - - - - - - - - - - - *
     * html footer
     * - - - - - - - - - - - - - - - - - - - */

    fprintf(file, "</body>\n");
    fprintf(file, "</html>\n");
    
    fclose(file); file = 0;
  }
  error = 0;
  cleanup:
  
  if( file != 0 )
  {
    fclose(file);
  }
  
  return error;
}

/* ------------------------------------------------------------------------- *
 * analyze_emit_app_html
 * ------------------------------------------------------------------------- */

int
analyze_emit_app_html(analyze_t *self, smapssnap_t *snap, const char *work)
{
  auto int local_app_lib_compare(const void *a1, const void *a2)
  {
    const smapsmapp_t *m1 = *(const smapsmapp_t **)a1;
    const smapsmapp_t *m2 = *(const smapsmapp_t **)a2;
    
    int r;
    
    /* - - - - - - - - - - - - - - - - - - - *
     * primary sorting: must be AID then LID
     * for the range searching below to work!
     * - - - - - - - - - - - - - - - - - - - */

    if( (r = m1->smapsmapp_AID - m2->smapsmapp_AID) != 0 ) return r;
    if( (r = m1->smapsmapp_LID - m2->smapsmapp_LID) != 0 ) return r;
    
    /* - - - - - - - - - - - - - - - - - - - *
     * secondary sorting: convenience order
     * - - - - - - - - - - - - - - - - - - - */
    
    if( (r = m1->smapsmapp_TID - m2->smapsmapp_TID) != 0 ) return r;
    if( (r = m2->smapsmapp_mem.Rss - m1->smapsmapp_mem.Rss) ) return r;

    return 0;
  }
  
  int error = -1;
  char temp[512];
  FILE *file = 0;
  
  /* - - - - - - - - - - - - - - - - - - - *
   * sort smaps data to bsearchable order
   * - - - - - - - - - - - - - - - - - - - */
  
  array_sort(self->mapp_tab, local_app_lib_compare);

  /* - - - - - - - - - - - - - - - - - - - *
   * write html page for each application
   * - - - - - - - - - - - - - - - - - - - */

  for( int a = 0; a < self->nappls; ++a )
  {
    smapsmapp_t *m; int t,l;

    /* - - - - - - - - - - - - - - - - - - - *
     * open file
     * - - - - - - - - - - - - - - - - - - - */

    snprintf(temp, sizeof temp, "%s/app%03d.html", work, a);
    //printf(">> %s\n", temp);
    if( (file = fopen(temp, "w")) == 0 )
    {
      goto cleanup;
    }
    
    /* - - - - - - - - - - - - - - - - - - - *
     * html header
     * - - - - - - - - - - - - - - - - - - - */

    fprintf(file, "<html>\n");
    fprintf(file, "<head>\n");
    fprintf(file, "<title>%s</title>\n", self->sappl[a]);
    fprintf(file, "</head>\n");
    fprintf(file, "<body>\n");
    
    /* - - - - - - - - - - - - - - - - - - - *
     * summary table
     * - - - - - - - - - - - - - - - - - - - */
    
    fprintf(file, "<h1>%s: %s</h1>\n", "Application", self->sappl[a]);
    analyze_emit_page_table(self, file, analyze_app_mem(self, a, 0));
    
    /* - - - - - - - - - - - - - - - - - - - *
     * library xref
     * - - - - - - - - - - - - - - - - - - - */
    
    fprintf(file, "<h1>%s XREF</h1>\n", "Mapping");
    fprintf(file, "<table border=1>\n");
    
    analyze_emit_xref_header(self, file, "Object");
    
    int alo,ahi, blo,bhi;
    
    analyze_get_apprange(self, 0, self->mapp_tab->size, &alo, &ahi, a);
    
    for( int base=alo; alo < ahi; alo = bhi )
    {
      m = self->mapp_tab->data[alo];
      l = m->smapsmapp_LID;
      
      analyze_get_librange(self, alo,ahi,&blo,&bhi,l);
      
      for( size_t i = blo; i < bhi; ++i )
      {
	m = self->mapp_tab->data[i];
	t = m->smapsmapp_TID;
	
	fprintf(file, "<tr>\n");
	
	if( i == blo )
	{
	  fprintf(file, 
		  "<th"LT"rowspan=%d align=left>"
		  "<a href=\"lib%03d.html\">%s</a>\n",
		  bhi-blo, l, path_basename(self->spath[l]));
	}
	
	const char *bg = (((i-base)/3)&1) ? D1 : D2;
	
	fprintf(file, "<td%s align=left>%s\n", bg, m->smapsmapp_map.type);
	fprintf(file, "<td%s align=left>%s\n", bg, m->smapsmapp_map.prot);
	fprintf(file, "<td%s align=right>%s\n", bg, uval(m->smapsmapp_mem.Size));
	fprintf(file, "<td%s align=right>%s\n", bg, uval(m->smapsmapp_mem.Rss));
	fprintf(file, "<td%s align=right>%s\n", bg, uval(m->smapsmapp_mem.Private_Dirty));
	fprintf(file, "<td%s align=right>%s\n", bg, uval(m->smapsmapp_mem.Shared_Dirty));
	fprintf(file, "<td%s align=right>%s\n", bg, uval(m->smapsmapp_mem.Private_Clean));
	fprintf(file, "<td%s align=right>%s\n", bg, uval(m->smapsmapp_mem.Shared_Clean));
      }
    }
    
    fprintf(file, "</table>\n");
    
    /* - - - - - - - - - - - - - - - - - - - *
     * html footer
     * - - - - - - - - - - - - - - - - - - - */

    fprintf(file, "</body>\n");
    fprintf(file, "</html>\n");
    fclose(file); file = 0;
  }
  
  error = 0;
  cleanup:
  
  if( file != 0 )
  {
    fclose(file);
  }
  
  return error;
}

/* ------------------------------------------------------------------------- *
 * analyze_emit_smaps_table
 * ------------------------------------------------------------------------- */

void 
analyze_emit_smaps_table(analyze_t *self, FILE *file, meminfo_t *v)
{
  fprintf(file, "<table border=1>\n");
  fprintf(file, "<tr>\n");
  fprintf(file, "<th"TP"rowspan=2>%s\n", "Class");
  fprintf(file, "<th"TP"colspan=2>%s\n", "Dirty");
  fprintf(file, "<th"TP"rowspan=2>%s\n", "Clean");
  fprintf(file, "<th"TP"rowspan=2>%s\n", "Resident");
  fprintf(file, "<th"TP"rowspan=2>%s\n", "Size");
  fprintf(file, "<th"TP"rowspan=2>%s\n", "COW");
  
  fprintf(file, "<tr>\n");
  fprintf(file, "<th"TP">%s\n", "Private");
  fprintf(file, "<th"TP">%s\n", "Shared");
  
  for( int t = 0; t < self->ntypes; ++t )
  {
    meminfo_t *m = &v[t];//&app_mem[a][t];
    const char *bg = ((t/3)&1) ? D1 : D2;
    
    fprintf(file, "<tr>\n");
    fprintf(file, "<th"LT" align=left>%s\n", self->stype[t]);
    fprintf(file, "<td %s align=right>%s\n", bg, uval(m->Private_Dirty));
    fprintf(file, "<td %s align=right>%s\n", bg, uval(m->Shared_Dirty));
    fprintf(file, "<td %s align=right>%s\n", bg, uval(m->Private_Clean + 
						      m->Shared_Clean));
    fprintf(file, "<td %s align=right>%s\n", bg, uval(m->Rss));
    fprintf(file, "<td %s align=right>%s\n", bg, uval(m->Size));
    fprintf(file, "<td %s align=right>%s\n", bg, uval(meminfo_cowest(m)));
  }
  
  fprintf(file, "</table>\n");
}

/* ------------------------------------------------------------------------- *
 * analyze_emit_table_header
 * ------------------------------------------------------------------------- */

void 
analyze_emit_table_header(analyze_t *self, FILE *file, const char *title)
{
  fprintf(file, "<tr>\n");
  fprintf(file, "<th"TP" rowspan=3>%s\n", title);
  fprintf(file, "<th"TP" colspan=3>%s\n", "RSS / Status");
  fprintf(file, "<th"TP" rowspan=2 colspan=2>%s\n", "Virtual<br>Memory");
  fprintf(file, "<th"TP" rowspan=3>%s\n", "RSS<br>COW<br>Est.");
  fprintf(file, "<th"TP" colspan=%d>%s\n", self->ntypes-1, "RSS / Class");
  
  fprintf(file, "<tr>\n");
  
  fprintf(file, "<th"TP" colspan=2>%s\n", "Dirty");
  fprintf(file, "<th"TP" rowspan=2>%s\n", "Clean");
  for( int i = 1; i < self->ntypes; ++i )
  {
    fprintf(file, "<th"TP" rowspan=2>%s\n", self->stype[i]);
  }
  fprintf(file, "<tr>\n");
  fprintf(file, "<th"TP">%s\n", "Private");
  fprintf(file, "<th"TP">%s\n", "Shared");
  fprintf(file, "<th"TP">%s\n", "RSS");
  fprintf(file, "<th"TP">%s\n", "Size");
}

/* ------------------------------------------------------------------------- *
 * analyze_emit_process_hierarchy  --  recursively dump clickable process tree
 * ------------------------------------------------------------------------- */

void 
analyze_emit_process_hierarchy(analyze_t *self, FILE *file, smapsproc_t *proc,
			       const char *work)
{
  if( proc->smapsproc_children.size )
  {
    fprintf(file, "<ul>\n");
    for( int i = 0; i < proc->smapsproc_children.size; ++i )
    {
      smapsproc_t *sub = proc->smapsproc_children.data[i];
      
      fprintf(file, "<li><a href=\"%s/app%03d.html\">%s (%d)</a>\n",
	      work,
	      sub->smapsproc_AID,
	      sub->smapsproc_pid.Name,
	      sub->smapsproc_pid.Pid);
      
      analyze_emit_process_hierarchy(self, file, sub, work);
    }
    fprintf(file, "</ul>\n");
  }
}

/* ------------------------------------------------------------------------- *
 * analyze_emit_application_table
 * ------------------------------------------------------------------------- */

void
analyze_emit_application_table(analyze_t *self, FILE *file, const char *work)
{ 
  int lut[self->nappls];
  
  for( int i = 0; i < self->nappls; ++i )
  {
    lut[i] = i;
  }
  
  auto int lut_cmp(const void *a1, const void *a2)
  {
    const meminfo_t *m1 = analyze_app_mem(self, *(const int *)a1, 0);
    const meminfo_t *m2 = analyze_app_mem(self, *(const int *)a2, 0);
    
    int r;
    if( (r = m2->Private_Dirty - m1->Private_Dirty) != 0 ) return r;
    if( (r = m2->Shared_Dirty  - m1->Shared_Dirty) != 0 ) return r;
    if( (r = m2->Rss           - m1->Rss) != 0 ) return r;
    
    return m2->Size - m1->Size;
  }
  
  qsort(lut, self->nappls, sizeof *lut, lut_cmp);
  
  fprintf(file, "<table border=1>\n");
  int N = 20; N = (self->nappls + N-1)/N, N = (self->nappls + N-1)/N;
  for( int i = 0; i < self->nappls; ++i )
  {
    int a = lut[i];
    if( i % N == 0 ) analyze_emit_table_header(self, file, "Application");
    
    fprintf(file, "<tr>\n");
    fprintf(file, 
	    "<th bgcolor=\"#bfffff\" align=left>"
	    "<a href=\"%s/app%03d.html\">%s</a>\n",
	    work, a, self->sappl[a]);
    
    meminfo_t *s = analyze_app_mem(self, a, 0);
    
    const char *bg = ((i/3)&1) ? D1 : D2;
    
    fprintf(file, "<td %s align=right>%s\n", bg, uval(s->Private_Dirty));
    fprintf(file, "<td %s align=right>%s\n", bg, uval(s->Shared_Dirty));
    fprintf(file, "<td %s align=right>%s\n", bg, uval(s->Private_Clean + s->Shared_Clean));
    fprintf(file, "<td %s align=right>%s\n", bg, uval(s->Rss));
    fprintf(file, "<td %s align=right>%s\n", bg, uval(s->Size));
    
    fprintf(file, "<td %s align=right>%s\n", bg, uval(meminfo_cowest(s)));
    
    for( int t = 1; t < self->ntypes; ++t )
    {
      meminfo_t *s = analyze_app_mem(self, a, t);
      fprintf(file, "<td %s align=right>%s\n", bg, uval(meminfo_total(s)));
    }
  }
  fprintf(file, "</table>\n");
}

/* ------------------------------------------------------------------------- *
 * analyze_emit_library_table
 * ------------------------------------------------------------------------- */

void
analyze_emit_library_table(analyze_t *self, FILE *file, const char *work)
{
  int lut[self->npaths];
  
  for( int i = 0; i < self->npaths; ++i )
  {
    lut[i] = i;
  }
  
  auto int lut_cmp(const void *a1, const void *a2)
  {
    const meminfo_t *m1 = analyze_lib_mem(self, *(const int *)a1, 0);
    const meminfo_t *m2 = analyze_lib_mem(self, *(const int *)a2, 0);
    int r;
    if( (r = m2->Private_Dirty - m1->Private_Dirty) != 0 ) return r;
    if( (r = m2->Shared_Dirty  - m1->Shared_Dirty) != 0 ) return r;
    if( (r = m2->Rss           - m1->Rss) != 0 ) return r;
    
    return m2->Size - m1->Size;
  }
  
  qsort(lut, self->npaths, sizeof *lut, lut_cmp);
  
  fprintf(file, "<table border=1>\n");
  int N = 20; N = (self->npaths + N-1)/N, N = (self->npaths + N-1)/N;
  for( int i = 0; i < self->npaths; ++i )
  {
    int a = lut[i];
    if( i % N == 0 ) analyze_emit_table_header(self, file, "Library");
    fprintf(file, "<tr>\n");
    fprintf(file, 
	    "<th bgcolor=\"#bfffff\" align=left>"
	    "<a href=\"%s/lib%03d.html\">%s</a>\n",
	    work, a, path_basename(self->spath[a]));
    
    meminfo_t *s = analyze_lib_mem(self, a, 0);
    
    const char *bg = ((i/3)&1) ? D1 : D2;
    
    fprintf(file, "<td %s align=right>%s\n", bg, uval(s->Private_Dirty));
    fprintf(file, "<td %s align=right>%s\n", bg, uval(s->Shared_Dirty));
    fprintf(file, "<td %s align=right>%s\n", bg, uval(s->Private_Clean + s->Shared_Clean));
    fprintf(file, "<td %s align=right>%s\n", bg, uval(s->Rss));
    fprintf(file, "<td %s align=right>%s\n", bg, uval(s->Size));
    
    fprintf(file, "<td %s align=right>%s\n", bg, uval(meminfo_cowest(s)));
    
    for( int t = 1; t < self->ntypes; ++t )
    {
      meminfo_t *s = analyze_lib_mem(self, a, t);
      fprintf(file, "<td %s align=right>%s\n", bg, uval(meminfo_total(s)));
    }
  }
  fprintf(file, "</table>\n");
}

/* ------------------------------------------------------------------------- *
 * analyze_emit_main_page
 * ------------------------------------------------------------------------- */

int
analyze_emit_main_page(analyze_t *self, smapssnap_t *snap, const char *path)
{
  int       error = -1;
  FILE     *file  = 0;

  char      work[512];
  
  /* - - - - - - - - - - - - - - - - - - - *
   * make sure we have directory for
   * library & application specific pages
   * - - - - - - - - - - - - - - - - - - - */

  snprintf(work, sizeof work - 4, "%s", path);
  strcpy(path_extension(work), ".dir");
  
  if( !path_isdir(work) )
  {
    if( mkdir(work, 0777) != 0 )
    {
      perror(work); goto cleanup;
    }
  }
  
  /* - - - - - - - - - - - - - - - - - - - *
   * open output file
   * - - - - - - - - - - - - - - - - - - - */

  if( (file = fopen(path, "w")) == 0 )
  {
    perror(path); goto cleanup;
  }

  /* - - - - - - - - - - - - - - - - - - - *
   * html header
   * - - - - - - - - - - - - - - - - - - - */

  fprintf(file, "<html>\n");
  fprintf(file, "<head>\n");
  fprintf(file, "<title>%s</title>\n", smapssnap_get_source(snap));
  fprintf(file, "</head>\n");
  fprintf(file, "<body>\n");


  /* - - - - - - - - - - - - - - - - - - - *
   * memory usage tables
   * - - - - - - - - - - - - - - - - - - - */

  fprintf(file, "<h1>System Estimates</h1>\n");
  
  fprintf(file, "<h2>System: Memory Use Estimate</h2>\n");
  analyze_emit_smaps_table(self, file, self->sysest);
  fprintf(file, "<p>Private and Size are accurate, the rest are minimums.\n");
  
  fprintf(file, "<h2>System: Memory Use App Totals</h2>\n");
  analyze_emit_smaps_table(self, file, self->sysmax);
  fprintf(file, "<p>Private is accurate, the rest are maximums.\n");
  
  fprintf(file, "<h2>System: Memory Use App Maximums</h2>\n");
  analyze_emit_smaps_table(self, file, self->appmax);
  fprintf(file, "<p>No process has values larger than the ones listed above.\n");
  
  /* - - - - - - - - - - - - - - - - - - - *
   * process hierarchy tree
   * - - - - - - - - - - - - - - - - - - - */

  fprintf(file, "<h1>Process Hierarchy</h1>\n");
  analyze_emit_process_hierarchy(self, file, &snap->smapssnap_rootproc, work);

  /* - - - - - - - - - - - - - - - - - - - *
   * application table
   * - - - - - - - - - - - - - - - - - - - */

  fprintf(file, "<h1>Application Values</h1>\n");
  analyze_emit_application_table(self, file, work);

  /* - - - - - - - - - - - - - - - - - - - *
   * library table
   * - - - - - - - - - - - - - - - - - - - */

  fprintf(file, "<h1>Object Values</h1>\n");
  analyze_emit_library_table(self, file, work);
  
  /* - - - - - - - - - - - - - - - - - - - *
   * html trailer
   * - - - - - - - - - - - - - - - - - - - */

  fprintf(file, "</body>\n");
  fprintf(file, "</html>\n");
  fclose(file); file = 0;

  /* - - - - - - - - - - - - - - - - - - - *
   * application pages
   * - - - - - - - - - - - - - - - - - - - */

  if( analyze_emit_app_html(self, snap, work) )
  {
    goto cleanup;
  }

  /* - - - - - - - - - - - - - - - - - - - *
   * library pages
   * - - - - - - - - - - - - - - - - - - - */
  
  if( analyze_emit_lib_html(self, snap, work) )
  {
    goto cleanup;
  }
  
  /* - - - - - - - - - - - - - - - - - - - *
   * success
   * - - - - - - - - - - - - - - - - - - - */

  error = 0;
  
  /* - - - - - - - - - - - - - - - - - - - *
   * cleanup & return
   * - - - - - - - - - - - - - - - - - - - */

  cleanup:
  
  if( file )
  {
    fclose(file);
  }
  return error;
}

/* ------------------------------------------------------------------------- *
 * analyze_emit_appval_table
 * ------------------------------------------------------------------------- */

void
analyze_emit_appval_table(analyze_t *self, smapssnap_t *snap, FILE *file)
{ 
  char temp[512];
  auto const char *uval(unsigned u) 
  { 
    snprintf(temp, sizeof temp, "%u", u); return temp;
  }

  typedef struct { int id; smapsproc_t *pt; } lut_t;
  lut_t lut[self->nappls];
  
  for( int i = 0; i < self->nappls; ++i )
  {
    lut[i].id = i;
    lut[i].pt = 0;
  }
  
  for( int i = 0; i < snap->smapssnap_proclist.size; ++i )
  {
    smapsproc_t *proc = snap->smapssnap_proclist.data[i];
    int a = proc->smapsproc_AID;
    assert( 0 <= a && a < self->nappls );
    
    assert( lut[a].id == a );
    assert( lut[a].pt == 0 );
    lut[a].pt = proc;
  }
  for( int i = 0; i < self->nappls; ++i )
  {
    assert( lut[i].pt != 0 );
  }
  
  auto int lut_cmp(const void *a1, const void *a2)
  {
    const lut_t *l1 = a1;
    const lut_t *l2 = a2;
    
    const meminfo_t *m1 = analyze_app_mem(self, l1->id, 0);
    const meminfo_t *m2 = analyze_app_mem(self, l2->id, 0);
    
    int r;
    if( (r = m2->Private_Dirty - m1->Private_Dirty) != 0 ) return r;
    if( (r = m2->Shared_Dirty  - m1->Shared_Dirty) != 0 ) return r;
    if( (r = m2->Rss           - m1->Rss) != 0 ) return r;
    
    if( (r = m2->Size - m1->Size) != 0 ) return r;

    return l1->pt->smapsproc_AID - l2->pt->smapsproc_AID;
    //return 0;
  }
  
  qsort(lut, self->nappls, sizeof *lut, lut_cmp);
  
  fprintf(file, "generator = %s %s\n", TOOL_NAME, TOOL_VERS);
  fprintf(file, "\n");
  fprintf(file, "name,pid,ppid,threads,pri,sha,cln,rss,size,cow");
  for( int t = 1; t < self->ntypes; ++t )
  {
    fprintf(file, ",%s", self->stype[t]);
  }
  fprintf(file, "\n");
  
  for( int i = 0; i < self->nappls; ++i )
  {
    int a = lut[i].id;
    smapsproc_t *proc = lut[i].pt;
    
    fprintf(file, "%s", proc->smapsproc_pid.Name);
    fprintf(file, ",%d", proc->smapsproc_pid.Pid);
    fprintf(file, ",%d", proc->smapsproc_pid.PPid);
    fprintf(file, ",%d", proc->smapsproc_pid.Threads);
    
    meminfo_t *s = analyze_app_mem(self, a, 0);
    
    fprintf(file, ",%s", uval(s->Private_Dirty));
    fprintf(file, ",%s", uval(s->Shared_Dirty));
    fprintf(file, ",%s", uval(s->Private_Clean + s->Shared_Clean));
    fprintf(file, ",%s", uval(s->Rss));
    fprintf(file, ",%s", uval(s->Size));
    fprintf(file, ",%s", uval(meminfo_cowest(s)));
    
    for( int t = 1; t < self->ntypes; ++t )
    {
      meminfo_t *s = analyze_app_mem(self, a, t);
      fprintf(file, ",%s", uval(meminfo_total(s)));
    }
    fprintf(file, "\n");
  }
}

/* ------------------------------------------------------------------------- *
 * analyze_emit_appvals
 * ------------------------------------------------------------------------- */

int
analyze_emit_appvals(analyze_t *self, smapssnap_t *snap, const char *path)
{
  int       error = -1;
  FILE     *file  = 0;

  /* - - - - - - - - - - - - - - - - - - - *
   * open output file
   * - - - - - - - - - - - - - - - - - - - */

  if( (file = fopen(path, "w")) == 0 )
  {
    perror(path); goto cleanup;
  }

  /* - - - - - - - - - - - - - - - - - - - *
   * html header
   * - - - - - - - - - - - - - - - - - - - */

// QUARANTINE   fprintf(file, "<html>\n");
// QUARANTINE   fprintf(file, "<head>\n");
// QUARANTINE   fprintf(file, "<title>%s</title>\n", smapssnap_get_source(snap));
// QUARANTINE   fprintf(file, "</head>\n");
// QUARANTINE   fprintf(file, "<body>\n");


  /* - - - - - - - - - - - - - - - - - - - *
   * application table
   * - - - - - - - - - - - - - - - - - - - */

// QUARANTINE   fprintf(file, "<h1>Application Values</h1>\n");
  analyze_emit_appval_table(self, snap, file);

  /* - - - - - - - - - - - - - - - - - - - *
   * html trailer
   * - - - - - - - - - - - - - - - - - - - */

// QUARANTINE   fprintf(file, "</body>\n");
// QUARANTINE   fprintf(file, "</html>\n");
// QUARANTINE   fclose(file); file = 0;

  
  /* - - - - - - - - - - - - - - - - - - - *
   * success
   * - - - - - - - - - - - - - - - - - - - */

  error = 0;
  
  /* - - - - - - - - - - - - - - - - - - - *
   * cleanup & return
   * - - - - - - - - - - - - - - - - - - - */

  cleanup:
  
  if( file )
  {
    fclose(file);
  }
  return error;
}
// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
// XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX


/* ========================================================================= *
 * smapsfilt_t  --  methods
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * smapsfilt_ctor
 * ------------------------------------------------------------------------- */

void 
smapsfilt_ctor(smapsfilt_t *self)
{
  self->smapsfilt_filtmode  = FM_ANALYZE;
  self->smapsfilt_difflevel = -1;
  self->smapsfilt_trimlevel = 0;
  
  self->smapsfilt_output = 0;
  str_array_ctor(&self->smapsfilt_inputs);
  array_ctor(&self->smapsfilt_snaplist, smapssnap_delete_cb);
}

/* ------------------------------------------------------------------------- *
 * smapsfilt_dtor
 * ------------------------------------------------------------------------- */

void 
smapsfilt_dtor(smapsfilt_t *self)
{
  array_dtor(&self->smapsfilt_snaplist);
}

/* ------------------------------------------------------------------------- *
 * smapsfilt_create
 * ------------------------------------------------------------------------- */

smapsfilt_t *
smapsfilt_create(void)
{
  smapsfilt_t *self = calloc(1, sizeof *self);
  smapsfilt_ctor(self);
  return self;
}

/* ------------------------------------------------------------------------- *
 * smapsfilt_delete
 * ------------------------------------------------------------------------- */

void 
smapsfilt_delete(smapsfilt_t *self)
{
  if( self != 0 )
  {
    smapsfilt_dtor(self);
    free(self);
  }
}

/* ------------------------------------------------------------------------- *
 * smapsfilt_delete_cb
 * ------------------------------------------------------------------------- */

void 
smapsfilt_delete_cb(void *self)
{
  smapsfilt_delete(self);
}

typedef struct diffkey_t diffkey_t;
typedef struct diffval_t diffval_t;

/* ------------------------------------------------------------------------- *
 * diffval_t
 * ------------------------------------------------------------------------- */

struct diffval_t
{
  double pri;
  double sha;
  double cln;
};


/* ------------------------------------------------------------------------- *
 * diffkey_t
 * ------------------------------------------------------------------------- */

struct diffkey_t
{
  int       appl;
  int       inst;
  int       type;
  int       path;
  int       cnt;
  
  //diffval_t val[0];
};


/* ========================================================================= *
 * diffval_t  --  methods
 * ========================================================================= */

/* ------------------------------------------------------------------------- *
 * diffval_ctor
 * ------------------------------------------------------------------------- */

void 
diffval_ctor(diffval_t *self)
{
  self->pri = 0;
  self->sha = 0;
  self->cln = 0;
}

/* ------------------------------------------------------------------------- *
 * diffval_dtor
 * ------------------------------------------------------------------------- */

void 
diffval_dtor(diffval_t *self)
{
}

/* ------------------------------------------------------------------------- *
 * diffval_add
 * ------------------------------------------------------------------------- */

void 
diffval_add(diffval_t *self, const diffval_t *that)
{
  self->pri += that->pri;
  self->sha += that->sha;
  self->cln += that->cln;
}



/* ========================================================================= *
 * diffkey_t  --  methods
 * ========================================================================= */

INLINE diffval_t *diffkey_val(diffkey_t *self, int cap)
{
  assert( 0 <= cap && cap < self->cnt );
  return &((diffval_t *)(self+1))[cap];
  
}

INLINE int diffkey_compare(const diffkey_t *k1, const diffkey_t *k2)
{
  int r = 0;
  if( (r = k1->appl - k2->appl) ) return r;
  if( (r = k1->inst - k2->inst) ) return r;
  if( (r = k1->type - k2->type) ) return r;
  if( (r = k1->path - k2->path) ) return r;
  return 0;
}

double diffkey_rank(diffkey_t *self, diffval_t *res)
{
  double pri = 0, sha = 0, cln = 0;
  
  for( int i = 0; i < self->cnt; ++i )
  {
    diffval_t *val = diffkey_val(self, i);
    pri += val->pri;
    sha += val->sha; 
    cln += val->cln;
  }
  
  pri /= self->cnt;
  sha /= self->cnt;
  cln /= self->cnt;
  
  res->pri = 0;
  res->sha = 0;
  res->cln = 0;
  
  for( int i = 0; i < self->cnt; ++i )
  {
    diffval_t *val = diffkey_val(self, i);
    res->pri += pow2(val->pri - pri);
    res->sha += pow2(val->sha - sha);
    res->cln += pow2(val->cln - cln);
  }

  res->pri = sqrt(res->pri / self->cnt);
  res->sha = sqrt(res->sha / self->cnt);
  res->cln = sqrt(res->cln / self->cnt);
  
  return fmax3(res->pri, res->sha, res->cln);
}

/* ------------------------------------------------------------------------- *
 * diffkey_create
 * ------------------------------------------------------------------------- */

diffkey_t *
diffkey_create(const diffkey_t *templ)
{
  size_t     size = sizeof(diffkey_t) + templ->cnt * sizeof(diffval_t);
  diffkey_t *self = calloc(1, size);
  
  *self = *templ;
  
// QUARANTINE   self->appl = -1;
// QUARANTINE   self->inst = -1;
// QUARANTINE   self->type = -1;
// QUARANTINE   self->path = -1;
// QUARANTINE   self->cnt = ncaps;

  for( int i = 0; i < self->cnt; ++i )
  {
    diffval_ctor(diffkey_val(self, i));
  }
  return self;
}

/* ------------------------------------------------------------------------- *
 * diffkey_delete
 * ------------------------------------------------------------------------- */

void 
diffkey_delete(diffkey_t *self)
{
  if( self != 0 )
  {
    for( int i = 0; i < self->cnt; ++i )
    {
      diffval_dtor(diffkey_val(self, i));
    }
    free(self);
  }
}

/* ------------------------------------------------------------------------- *
 * diffkey_delete_cb
 * ------------------------------------------------------------------------- */

void 
diffkey_delete_cb(void *self)
{
  diffkey_delete(self);
}


int
smapsfilt_diff(smapsfilt_t *self, const char *path, 
	       int diff_lev, int html_diff, int trim_cols)
{
  int error = -1;
  
  /* - - - - - - - - - - - - - - - - - - - *
   * sort operator for process data
   * - - - - - - - - - - - - - - - - - - - */

  auto int cmp_app_pid(const void *a1, const void *a2)
  {
    const smapsproc_t *p1 = *(const smapsproc_t **)a1;
    const smapsproc_t *p2 = *(const smapsproc_t **)a2;
    int r;
    if( (r = p1->smapsproc_AID - p2->smapsproc_AID) != 0 ) return r;
    if( (r = p1->smapsproc_PID - p2->smapsproc_PID) != 0 ) return r;
    return 0;
  }

  /* - - - - - - - - - - - - - - - - - - - *
   * difference data handling
   * - - - - - - - - - - - - - - - - - - - */

  int         diff_cnt = 0;
  int         diff_max = 256;
  diffkey_t **diff_tab = calloc(diff_max, sizeof *diff_tab);

  auto void diff_ins(const diffkey_t *key, const diffval_t *val, int cap)
  {
    for( int lo = 0, hi = diff_cnt;; )
    {
      if( lo == hi )
      {
	if( diff_cnt == diff_max )
	{
	  diff_tab = realloc(diff_tab, (diff_max *= 2) * sizeof *diff_tab);
	}
	memmove(&diff_tab[lo+1], &diff_tab[lo+0], 
		(diff_cnt - lo) * sizeof *diff_tab);
	diff_cnt += 1;
	diff_tab[lo] = diffkey_create(key);
	diffval_add(diffkey_val(diff_tab[lo],cap), val);
	break;
      }
      int i = (lo + hi) / 2;
      int r = diffkey_compare(diff_tab[i], key);
      if( r < 0 ) { lo = i + 1; continue; }
      if( r > 0 ) { hi = i + 0; continue; }
      diffval_add(diffkey_val(diff_tab[i],cap), val);
      break;
    }
  }
  
  symtab_t *appl_tab = symtab_create();
  symtab_t *type_tab = symtab_create();
  symtab_t *path_tab = symtab_create();
  
  /* - - - - - - - - - - - - - - - - - - - *
   * initialize symbol tables
   * - - - - - - - - - - - - - - - - - - - */

  symtab_enumerate(type_tab, "code");
  symtab_enumerate(type_tab, "data");
  symtab_enumerate(type_tab, "heap");
  symtab_enumerate(type_tab, "anon");
  symtab_enumerate(type_tab, "stack");

  for( int i = 0; i < self->smapsfilt_snaplist.size; ++i )
  {
    smapssnap_t *snap = self->smapsfilt_snaplist.data[i];
    
    for( int k = 0; k < snap->smapssnap_proclist.size; ++k )
    {
      smapsproc_t *proc = snap->smapssnap_proclist.data[k];
      symtab_enumerate(appl_tab, proc->smapsproc_pid.Name);

      for( int j = 0; j < proc->smapsproc_mapplist.size; ++j )
      {
	smapsmapp_t *mapp = proc->smapsproc_mapplist.data[j];
	symtab_enumerate(path_tab, mapp->smapsmapp_map.path);
      }
    }
  }
  symtab_renum(appl_tab);
  symtab_renum(path_tab);

  /* - - - - - - - - - - - - - - - - - - - *
   * enumerate data, normalize pids to
   * application instances while at it
   * - - - - - - - - - - - - - - - - - - - */
  
  #define P(x) fprintf(stderr, "%s: %g\n", #x, (double)(x));

  int inst_cnt = 0;
  
  for( int i = 0; i < self->smapsfilt_snaplist.size; ++i )
  {
    smapssnap_t *snap = self->smapsfilt_snaplist.data[i];
    
// QUARANTINE     P(snap->smapssnap_proclist.size);
    
    for( int k = 0; k < snap->smapssnap_proclist.size; ++k )
    {
      smapsproc_t *proc = snap->smapssnap_proclist.data[k];
      proc->smapsproc_PID = proc->smapsproc_pid.Pid;
      proc->smapsproc_AID = symtab_enumerate(appl_tab, 
					     proc->smapsproc_pid.Name);
    }

    array_sort(&snap->smapssnap_proclist, cmp_app_pid);
    
    int aid = -1, pid = -1, cnt = 0;
    for( int k = 0; k < snap->smapssnap_proclist.size; ++k )
    {
      smapsproc_t *proc = snap->smapssnap_proclist.data[k];
      if( aid != proc->smapsproc_AID )
      {
	aid = proc->smapsproc_AID, pid = proc->smapsproc_PID, cnt = 0;
      }
      else if( pid != proc->smapsproc_PID )
      {
	pid = proc->smapsproc_PID, ++cnt;
	if( inst_cnt < cnt ) inst_cnt = cnt;
      }
      proc->smapsproc_PID = cnt;
      for( int j = 0; j < proc->smapsproc_mapplist.size; ++j )
      {
	smapsmapp_t *mapp = proc->smapsproc_mapplist.data[j];
	mapp->smapsmapp_AID = proc->smapsproc_AID;
	mapp->smapsmapp_PID = proc->smapsproc_PID;
	mapp->smapsmapp_TID = symtab_enumerate(type_tab,
					       mapp->smapsmapp_map.type);
	mapp->smapsmapp_LID = symtab_enumerate(path_tab,
					       mapp->smapsmapp_map.path);
      }
    }
  }
  
  /* - - - - - - - - - - - - - - - - - - - *
   * reverse lookup tables
   * - - - - - - - - - - - - - - - - - - - */

  int appl_cnt = appl_tab->symtab_count;
  int type_cnt = type_tab->symtab_count;
  int path_cnt = path_tab->symtab_count;
  
  const char *appl_str[appl_cnt];
  const char *type_str[type_cnt];
  const char *path_str[path_cnt];
  
  for( int i = 0; i < appl_cnt; ++i )
  {
    //appl_str[i] = appl_tab->symtab_entry[i].symbol_key;
    
    symbol_t *s = &appl_tab->symtab_entry[i];
    appl_str[s->symbol_val] = s->symbol_key;
  }
  for( int i = 0; i < type_cnt; ++i )
  {
    //type_str[i] = type_tab->symtab_entry[i].symbol_key;
    symbol_t *s = &type_tab->symtab_entry[i];
    type_str[s->symbol_val] = s->symbol_key;
  }
  for( int i = 0; i < path_cnt; ++i )
  {
    //path_str[i] = path_tab->symtab_entry[i].symbol_key;
    symbol_t *s = &path_tab->symtab_entry[i];
    path_str[s->symbol_val] = s->symbol_key;
  }
  
// QUARANTINE   P(appl_cnt);
// QUARANTINE   P(inst_cnt);
// QUARANTINE   P(type_cnt);
// QUARANTINE   P(path_cnt);

  /* - - - - - - - - - - - - - - - - - - - *
   * accumulate stats
   * - - - - - - - - - - - - - - - - - - - */
  
  diffkey_t key;
  diffval_t val;
  
  key.appl = -1;
  key.inst = -1;
  key.type = -1;
  key.path = -1;
  key.cnt  = self->smapsfilt_snaplist.size;
  
  for( int i = 0; i < self->smapsfilt_snaplist.size; ++i )
  {
    smapssnap_t *snap = self->smapsfilt_snaplist.data[i];
    for( int k = 0; k < snap->smapssnap_proclist.size; ++k )
    {
      smapsproc_t *proc = snap->smapssnap_proclist.data[k];
      for( int j = 0; j < proc->smapsproc_mapplist.size; ++j )
      {
	smapsmapp_t *mapp = proc->smapsproc_mapplist.data[j];
	
	switch( diff_lev )
	{
	default:
	case 4: key.path = mapp->smapsmapp_LID;
	case 3: key.type = mapp->smapsmapp_TID;
	case 2: key.inst = mapp->smapsmapp_PID;
	case 1: key.appl = mapp->smapsmapp_AID;
	case 0: break;
	}
	val.pri = mapp->smapsmapp_mem.Private_Dirty;
	val.sha = mapp->smapsmapp_mem.Shared_Dirty;
	val.cln = (mapp->smapsmapp_mem.Shared_Clean +
		   mapp->smapsmapp_mem.Private_Clean);
	diff_ins(&key, &val, i);
      }
    }
  }
  
  /* - - - - - - - - - - - - - - - - - - - *
   * output results
   * - - - - - - - - - - - - - - - - - - - */

  double min_rank = 4;

  FILE *file = 0;
  
  char ***out_row = calloc(diff_cnt * 3, sizeof *out_row);
  int    out_cnt = 0;
  int    out_dta = 4 + 1 + self->smapsfilt_snaplist.size + 1;
  
  if( trim_cols > 4 ) trim_cols = 4;

  if( (file = fopen(path, "w")) == 0 )
  {
    perror(path); goto cleanup;
  }
  
  auto void diff_emit_table(void)
  {
    if( trim_cols > 0 )
    {
      int N[out_cnt];
      for( int i = 1; i < out_cnt; ++i )
      {
	N[i] = 0;
	for( int k = 0; k <= trim_cols; ++k )
	{
	  N[i] = k;
	  if( out_row[i-0][k] == 0 ) break;
	  if( out_row[i-1][k] == 0 ) break;
	  if( strcmp(out_row[i-0][k], out_row[i-1][k]) ) break;
	}
      }
      for( int i = 1; i < out_cnt; ++i )
      {
	for( int k = 0; k < N[i]; ++k )
	{
	  if( html_diff )
	  {
	    free(out_row[i][k]);
	    out_row[i][k] = 0;
	  }
	  else
	  {
	    out_row[i][k][0] = 0;
	  }
	}
      }
    }
    
    if( html_diff )
    {
      /* - - - - - - - - - - - - - - - - - - - *
       * header
       * - - - - - - - - - - - - - - - - - - - */

      fprintf(file,
	      "<html><head><title>SMAPS DIFF</title></head><body>\n"
	      "<h1>SMAPS DIFF</h1>\n"
	      "<p>\n");

      for( int i = 0; i < self->smapsfilt_snaplist.size; ++i )
      {
	smapssnap_t *snap = self->smapsfilt_snaplist.data[i];
	fprintf(file, "CAP%d = %s<br>\n", i+1, snap->smapssnap_source);
      }

      fprintf(file,
	      "<table border=1>\n"
	      "<tr>\n");

      if( diff_lev >= 1 ) fprintf(file, "<th>Cmd");
      if( diff_lev >= 2 ) fprintf(file, "<th>Pid");
      if( diff_lev >= 3 ) fprintf(file, "<th>Type");
      if( diff_lev >= 4 ) fprintf(file, "<th>Path");
      fprintf(file, "<th>Value");
      for( int i = 0; i < self->smapsfilt_snaplist.size; ++i )
      {
	fprintf(file, "<th>CAP%d", i+1);
      }
      fprintf(file, "<th>RANK\n");
      
      /* - - - - - - - - - - - - - - - - - - - *
       * table data
       * - - - - - - - - - - - - - - - - - - - */

      for( int i = 0; i < out_cnt; ++i )
      {
	fprintf(file, "<tr>\n");
	for( int k = 0; k < out_dta; ++k )
	{
	  if( out_row[i][k] != 0 )
	  {
	    int j;
	    int r = (k == 1) || (k >= 5);
	    for( j = i+1; j < out_cnt; ++j )
	    {
	      if( out_row[j][k] ) break;
	    }
	    if( (j -= i) > 1 )
	    {
	      fprintf(file, "<td%s valign=top rowspan=%d>%s",
		      r ? " align=right" : "", j, out_row[i][k]);
	    }
	    else
	    {
	      fprintf(file, "<td%s>%s",
		      r ? " align=right" : "", out_row[i][k]);
	    }
	  }
	}
      }
      
      /* - - - - - - - - - - - - - - - - - - - *
       * trailer
       * - - - - - - - - - - - - - - - - - - - */

      fprintf(file,
	      "</table>\n"
	      "</body>\n"
	      "</html>\n");
    }
    else
    {
      /* - - - - - - - - - - - - - - - - - - - *
       * header
       * - - - - - - - - - - - - - - - - - - - */

      fprintf(file, "generator = %s %s\n", TOOL_NAME, TOOL_VERS);
      for( int i = 0; i < self->smapsfilt_snaplist.size; ++i )
      {
	smapssnap_t *snap = self->smapsfilt_snaplist.data[i];
	fprintf(file, "CAP%d = %s\n", i+1, snap->smapssnap_source);
      }
      fprintf(file, "\n");
      
      if( diff_lev >= 1 ) fprintf(file, "Cmd,");
      if( diff_lev >= 2 ) fprintf(file, "Pid,");
      if( diff_lev >= 3 ) fprintf(file, "Type,");
      if( diff_lev >= 4 ) fprintf(file, "Path,");
      fprintf(file, "Value,");
      for( int i = 0; i < self->smapsfilt_snaplist.size; ++i )
      {
	fprintf(file, "CAP%d,", i+1);
      }
      fprintf(file, "RANK\n");
      
      /* - - - - - - - - - - - - - - - - - - - *
       * table
       * - - - - - - - - - - - - - - - - - - - */

      for( int i = 0; i < out_cnt; ++i )
      {
	for( int n=0, k = 0; k < out_dta; ++k )
	{
	  if( out_row[i][k] != 0 )
	  {
	    fprintf(file, "%s%s", n++ ? "," : "", out_row[i][k]);
	  }
	}
      }
      
      /* - - - - - - - - - - - - - - - - - - - *
       * trailer
       * - - - - - - - - - - - - - - - - - - - */

      fprintf(file, "\n");
    }
  }
  
  auto void diff_emit_entry(diffkey_t *k, const char *name,
			    double rank, const double *data)
  {
    char **out = calloc(out_dta, sizeof *out);
    if( k->appl >= 0 ) xstrfmt(&out[0], "%s", appl_str[k->appl]);
    if( k->inst >= 0 ) xstrfmt(&out[1], "%d", k->inst);
    if( k->type >= 0 ) xstrfmt(&out[2], "%s", type_str[k->type]);
    if( k->path >= 0 ) xstrfmt(&out[3], "%s", path_str[k->path]);
    xstrfmt(&out[4],"%s", name);
    for( int j = 0; j < k->cnt; ++j )
    {
      xstrfmt(&out[5+j], "%g", data[j]);
    }
    xstrfmt(&out[5+k->cnt], "%.1f\n", rank);
    out_row[out_cnt++] = out;
  }
  
  for( size_t i = 0; i < diff_cnt; ++i )
  {
    diffkey_t *k = diff_tab[i];
    if( diffkey_rank(k, &val) >= min_rank )
    {
      double d[k->cnt];
      if( val.pri >= min_rank )
      {
	for( int j = 0; j < k->cnt; ++j ) {
	  d[j] = diffkey_val(k, j)->pri; 
	}
	diff_emit_entry(k, "pri", val.pri, d);
      }
      if( val.sha >= min_rank )
      {
	for( int j = 0; j < k->cnt; ++j ) {
	  d[j] = diffkey_val(k, j)->sha;
	}
	diff_emit_entry(k, "sha", val.sha, d);
      }
      if( val.cln >= min_rank )
      {
	for( int j = 0; j < k->cnt; ++j ) {
	  d[j] = diffkey_val(k, j)->cln;
	}
	diff_emit_entry(k, "cln", val.cln, d);
      }
    }
    diffkey_delete(diff_tab[i]);
  }
  
  diff_emit_table();
  
  error = 0;
  
  cleanup:
  
  symtab_delete(appl_tab);
  symtab_delete(type_tab);
  symtab_delete(path_tab);
  free(diff_tab);
  
  if( file != 0 ) fclose(file);
  
  return error;
  
}

/* ------------------------------------------------------------------------- *
 * smapsfilt_handle_arguments
 * ------------------------------------------------------------------------- */

int parse_level(const char *text)
{
  int level = 2;
  
  if( !strcmp(text, "sys") )
  {
    level = 0;
  }
  else if( !strcmp(text, "app") )
  {
    level = 1;
  }
  else if( !strcmp(text, "pid") )
  {
    level = 2;
  }
  else if( !strcmp(text, "sec") )
  {
    level = 3;
  }
  else if( !strcmp(text, "obj") )
  {
    level = 4;
  }
  else
  {
    char *e = 0;
    int   n = strtol(text, &e, 0);
    if( e > text && *e == 0 )
    {
      level = n;
    }
  }
  
  return level;
}

static void 
smapsfilt_handle_arguments(smapsfilt_t *self, int ac, char **av)
{
  argvec_t *args = argvec_create(ac, av, app_opt, app_man);

  while( !argvec_done(args) )
  {
    int       tag  = 0;
    char     *par  = 0;

    if( !argvec_next(args, &tag, &par) )
    {
      msg_error("(use --help for usage)\n");
      exit(1);
    }

    switch( tag )
    {
    case opt_help:
      argvec_usage(args);
      exit(EXIT_SUCCESS);

    case opt_vers:
      printf("%s\n", TOOL_VERS);
      exit(EXIT_SUCCESS);

    case opt_verbose:
      msg_incverbosity();
      break;

    case opt_quiet:
      msg_decverbosity();
      break;

    case opt_silent:
      msg_setsilent();
      break;

    case opt_input:
    case opt_noswitch:
      str_array_add(&self->smapsfilt_inputs, par);
      break;

    case opt_output:
      cstring_set(&self->smapsfilt_output, par);
      break;
      
    case opt_filtmode:
      {
	static const struct 
	{
	  const char *name; int mode;
	} lut[] = 
	{
	  {"flatten",   FM_FLATTEN },
	  {"normalize", FM_NORMALIZE },
	  {"analyze",   FM_ANALYZE },
	  {"appvals",   FM_APPVALS },
	  {"diff",      FM_DIFF },
	};
	for( int i = 0; ; ++i )
	{
	  if( i == sizeof lut / sizeof *lut )
	  {
	    msg_fatal("unknown mode '%s'\n", par);
	  }
	  if( !strcmp(lut[i].name, par) )
	  {
	    self->smapsfilt_filtmode = lut[i].mode;
	    break;
	  }
	}
      }
      break;
      
      
    case opt_difflevel:
      self->smapsfilt_difflevel = parse_level(par);
      break;
      
    case opt_trimlevel:
      self->smapsfilt_trimlevel = parse_level(par);
      break;
    default:
      abort();
    }
  }

  argvec_delete(args);
}

static void
smapsfilt_load_inputs(smapsfilt_t *self)
{
  for( int i = 0; i < self->smapsfilt_inputs.size; ++i )
  {
    const char *path = self->smapsfilt_inputs.data[i];
    
    smapssnap_t *snap = smapssnap_create();
    smapssnap_load_cap(snap, path);
    array_add(&self->smapsfilt_snaplist, snap);

// QUARANTINE     smapssnap_save_cap(snap, "out1.cap");
// QUARANTINE     smapssnap_save_csv(snap, "out1.csv");

    smapssnap_create_hierarchy(snap);
    smapssnap_collapse_threads(snap);

// QUARANTINE     smapssnap_save_cap(snap, "out2.cap");
// QUARANTINE     smapssnap_save_csv(snap, "out2.csv");
// QUARANTINE     smapssnap_save_html(snap, "out2.html");
  }
  
}

char *path_slice_extension(char *path)
{
  char *e = path_extension(path);
  if( *e != 0 )
  {
    *e++ = 0;
  }
  return e;
}

char *path_make_output(const char *def, 
		       const char *src, const char *ext)
{
  char *res = 0;
  if( def == 0 )
  {
    const char *end = path_extension(src);
    xstrfmt(&res, "%.*s%s", (int)(end - src), src, ext);
  }
  else
  {
    res = strdup(def);
  }
  return res;
}


static void
smapsfilt_write_outputs(smapsfilt_t *self)
{
  switch( self->smapsfilt_filtmode )
  {
  case FM_DIFF:
    if( self->smapsfilt_output == 0 )
    {
      msg_fatal("output path must be specified for diff\n");
    }
    
    if( self->smapsfilt_snaplist.size < 2 )
    {
      msg_warning("diffing less than two captures is pretty meaningless\n");
    }
    
    {
      char *work = strdup(self->smapsfilt_output);
      char *ext  = path_slice_extension(work);
      int   html = !strcmp(ext, "html");
      
      int   level = self->smapsfilt_difflevel;
      int   trim  = self->smapsfilt_trimlevel;
      
      if( level < 0 )
      {
	level = parse_level(path_slice_extension(work));
      }
      
      smapsfilt_diff(self, self->smapsfilt_output, level, html, trim);
      
      free(work);
    }
    break;
    
  case FM_FLATTEN:
    if( self->smapsfilt_output != 0 && self->smapsfilt_snaplist.size != 1 )
    {
      msg_fatal("forcing output path allowed with one source file only!\n");
    }
    
    for( int i = 0; i < self->smapsfilt_snaplist.size; ++i )
    {
      smapssnap_t *snap = self->smapsfilt_snaplist.data[i];
      char *dest = path_make_output(self->smapsfilt_output,
				    snap->smapssnap_source,
				    ".flat");
      smapssnap_save_cap(snap, dest);
      free(dest);
    }
    break;
    
  case FM_NORMALIZE:
    if( self->smapsfilt_output != 0 && self->smapsfilt_snaplist.size != 1 )
    {
      msg_fatal("forcing output path allowed with one source file only!\n");
    }
    
    for( int i = 0; i < self->smapsfilt_snaplist.size; ++i )
    {
      smapssnap_t *snap = self->smapsfilt_snaplist.data[i];
      char *dest = path_make_output(self->smapsfilt_output,
				    snap->smapssnap_source,
				    ".csv");
      smapssnap_save_csv(snap, dest);
      free(dest);
    }
    break;
    
  case FM_ANALYZE:
    
    if( self->smapsfilt_output != 0 && self->smapsfilt_snaplist.size != 1 )
    {
      msg_fatal("forcing output path allowed with one source file only!\n");
    }
    
    for( int i = 0; i < self->smapsfilt_snaplist.size; ++i )
    {
      smapssnap_t *snap = self->smapsfilt_snaplist.data[i];
      char *dest = path_make_output(self->smapsfilt_output,
				    snap->smapssnap_source,
				    ".html");
      
      analyze_t *az   = analyze_create();
      analyze_enumerate_data(az, snap);
      analyze_accumulate_data(az);
      int error = analyze_emit_main_page(az, snap, dest);
      analyze_delete(az);
      //smapssnap_save_html(snap, dest);
      free(dest);
      assert( error == 0 );
    }
    break;
    
  case FM_APPVALS:
    
    if( self->smapsfilt_output != 0 && self->smapsfilt_snaplist.size != 1 )
    {
      msg_fatal("forcing output path allowed with one source file only!\n");
    }
    
    for( int i = 0; i < self->smapsfilt_snaplist.size; ++i )
    {
      smapssnap_t *snap = self->smapsfilt_snaplist.data[i];
      char *dest = path_make_output(self->smapsfilt_output,
				    snap->smapssnap_source,
				    ".apps");
      
      analyze_t *az   = analyze_create();
      analyze_enumerate_data(az, snap);
      analyze_accumulate_data(az);
      int error = analyze_emit_appvals(az, snap, dest);
      analyze_delete(az);
      free(dest);
      assert( error == 0 );
    }
    break;
    
  default:
    msg_fatal("unimplemented mode %d\n",self->smapsfilt_filtmode);
    break;
  }
}

/* ========================================================================= *
 * main  --  program entry point
 * ========================================================================= */

int main(int ac, char **av)
{  
  smapsfilt_t *app = smapsfilt_create();
  smapsfilt_handle_arguments(app, ac, av);
  smapsfilt_load_inputs(app);
  smapsfilt_write_outputs(app);
  smapsfilt_delete(app);
  return 0;
}

/* - - - - - - - - - - - - - - - - - - - *
 * sp_smaps_snapshot
 * 
 * sp_smaps_normalize
 * sp_smaps_flatten
 * 
 * sp_smaps_analyze
 * sp_smaps_appvals
 * 
 * sp_smaps_diff
 * 
 * - - - - - - - - - - - - - - - - - - - */
