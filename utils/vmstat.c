/* Print vm statistics

   Copyright (C) 1996 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include <stdio.h>
#include <stddef.h>
#include <argp.h>
#include <error.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <version.h>

#include <mach.h>
#include <mach/vm_statistics.h>
#include <mach/default_pager.h>
#include <hurd.h>

char *argp_program_version = STANDARD_HURD_VERSION (vmstat);

static const struct argp_option options[] = {
  {"terse",	't', 0, 0, "Use short one-line output format", 1 },
  {"no-header", 'H', 0, 0, "Don't print a descriptive header line"},
  {"prefix",    'p', 0, 0, "Always display a description before stats"},
  {"no-prefix", 'P', 0, 0, "Never display a description before stats"},
  {"pages",     'v', 0, 0, "Display sizes in pages"},
  {"kilobytes", 'k', 0, 0, "Display sizes in 1024 byte blocks"},
  {"bytes",     'b', 0, 0, "Display sizes in bytes"},

  /* A header for all the individual field options.  */
  { 0,0,0,0, "Selecting which statistics to show:", 2},

  {0}
};
static const char *args_doc = "[PERIOD [COUNT [HEADER_INTERVAL]]]";
static const char *doc = "Show system virtual memory statistics"
"\vIf PERIOD is supplied, then terse mode is"
" selected, and the output repeated every PERIOD seconds, with cumulative"
" fields given the difference from the last output.  If COUNT is given"
" and non-zero, only that many lines are output.  HEADER_INTERVAL"
" defaults to 23, and if not zero, is the number of repeats after which a"
" blank line and the header will be reprinted (as well as the totals for"
" cumulative fields).";

/* We use this one type to represent all values printed by this program.  It
   should be signed, and hopefully large enough (it may need to be larger
   than what the system returns values in, as we represent some quantities as
   bytes instead of pages)!  */
typedef long long val_t;

/* What a given number describes.  */
enum val_type
{
  COUNT,			/* As-is.  */
  SIZE,				/* Use the most convenient unit, with suffix */
  PAGESZ,			/* Like SIZE, but never changed to PAGES.  */
  PCENT,			/* Append `%'.  */
};

/* Print a number of type TYPE.  If SIZE_UNITS is non-zero, then values of
   type SIZE are divided by that amount and printed without a suffix.  FWIDTH
   is the width of the field to print it in, right-justified.  If SIGN is
   true, the value is always printed with a sign, even if it's positive.  */
static void
print_val (val_t val, enum val_type type,
	   size_t size_units, int fwidth, int sign)
{
  if (type == PCENT)
    printf (sign ? "%+*d%%" : "%*d%%", fwidth - 1, val);
  else if ((type == SIZE || type == PAGESZ) && size_units == 0)
    {
      float fval = val;
      char *units = " KMGT", *u = units;

      while (fval > 1024)
	{
	  fval /= 1024;
	  u++;
	}

      printf ((fval >= 1000
	       ? (sign ? "%+*.0f%c" : "%*.0f%c")
	       : (sign ? "%+*.3g%c"  : "%*.3g%c")),
	      fwidth - 1, fval, *u);
    }
  else
    {
      if ((type == SIZE || type == PAGESZ) && size_units > 0)
	val /= size_units;
      printf (sign ? "%+*d" : "%*d", fwidth, val);
    }
}

/* How this field changes with time.  */
enum field_change_type
{
  VARY,				/* Can go up or down.  */
  CONST,			/* Always the same.  */
  CUMUL,			/* Monotonic increasing.  */
};

struct vm_state;		/* fwd */

struct field
{
  /* Name of the field; used for the option name.  */
  char *name;

  /* A descriptive title used for long output format.  */
  char *desc;

  /* Terse header used for the columnar style output.  */
  char *hdr;

  /* Type of this field.  */
  enum field_change_type change_type;

  /* How to display the number associated with this field.
     If this is anything but `DIMLESS', then it can be overriden by the
     user.  */
  enum val_type type;

  /* True if we display this field by default (user can always override). */
  int standard :1;

  /* Offset of the integer_t field in a vm_statistics structure.  -1 if a
     computed field (in which case the COMPUTE field should be filled in).  */
  int offs;

  /* How to compute this field.  If 0, get_vmstats_value is used.  This
     function should return a negative number if there's some problem getting
     the field.  */
  val_t (*compute)(struct vm_state *state, const struct field *field);
};

/* State about system vm from which we compute the above defined fields.  */
struct vm_state 
{
  /* General vm statistics.  */
  struct vm_statistics vmstats;

  /* default pager port (must be privileged to fetch this).  */
  mach_port_t def_pager;
  struct default_pager_info def_pager_info;
};

static error_t
vm_state_refresh (struct vm_state *state)
{
  error_t err = vm_statistics (mach_task_self (), &state->vmstats);

  if (err)
    return err;

  /* Mark the info as invalid, but leave DEF_PAGER alone.  */
  bzero (&state->def_pager_info, sizeof state->def_pager_info);

  return 0;
}

static val_t
get_vmstats_field (struct vm_state *state, const struct field *field)
{
  val_t val =
    (val_t)(*(integer_t *)((char *)&state->vmstats + field->offs));
  if (field->type == SIZE)
    val *= state->vmstats.pagesize;
  return val;
}

static val_t
get_size (struct vm_state *state, const struct field *field)
{
  return
    (state->vmstats.free_count + state->vmstats.active_count
     + state->vmstats.inactive_count + state->vmstats.wire_count)
    * state->vmstats.pagesize;
}

static val_t
vm_state_get_field (struct vm_state *state, const struct field *field)
{
  return (field->compute ?: get_vmstats_field) (state, field);
}

static val_t
get_cache_hit_ratio (struct vm_state *state, const struct field *field)
{
  return state->vmstats.hits * 100 / state->vmstats.lookups;
}

/* Makes sure STATE contains a default pager port and associated info, and
   returns 0 if not (after printing an error).  */
static int
ensure_def_pager_info (struct vm_state *state)
{
  error_t err;

  if (state->def_pager == MACH_PORT_NULL)
    {
      mach_port_t host;

      err = get_privileged_ports (&host, 0);
      if (err)
	{
	  error (0, err, "get_privileged_ports");
	  return 0;
	}

      err = vm_set_default_memory_manager (host, &state->def_pager);
      mach_port_deallocate (mach_task_self (), host);

      if (err)
	{
	  error (0, err, "vm_set_default_memory_manager");
	  return 0;
	}
    }

  err = default_pager_info (state->def_pager, &state->def_pager_info);
  if (err)
    error (0, err, "default_pager_info");

  return (err == 0);
}

static val_t
get_swap_size (struct vm_state *state, const struct field *field)
{
  return
    ensure_def_pager_info (state) ? state->def_pager_info.dpi_total_space : -1;
}

static val_t
get_swap_free (struct vm_state *state, const struct field *field)
{
  return
    ensure_def_pager_info (state) ? state->def_pager_info.dpi_free_space : -1;
}

static val_t
get_swap_page_size (struct vm_state *state, const struct field *field)
{
  return
    ensure_def_pager_info (state) ? state->def_pager_info.dpi_page_size : -1;
}

static val_t
get_swap_active (struct vm_state *state, const struct field *field)
{
  return
    ensure_def_pager_info (state)
    ? (state->def_pager_info.dpi_total_space
       - state->def_pager_info.dpi_free_space)
    : -1;
}

/* Returns the byte offset of the field FIELD in a vm_statistics structure. */
#define _F(field_name)  offsetof (struct vm_statistics, field_name)

/* vm_statistics fields we know about.  */
static const struct field fields[] =
{
  {"pagesize",	   "Pagesize",	   " pgsz",  CONST,PAGESZ, 1,_F(pagesize)},
  {"size",	   "Size",	   " size",  CONST,SIZE, 1,0,get_size},
  {"free",	   "Free",	   " free",  VARY, SIZE, 1,_F(free_count)},
  {"active",	   "Active",	   " actv",  VARY, SIZE, 1,_F(active_count)},
  {"inactive",	   "Inactive", 	   "inact",  VARY, SIZE, 1,_F(inactive_count)},
  {"wired",	   "Wired",    	   "wired",  VARY, SIZE, 1,_F(wire_count)},
  {"zero-filled",  "Zeroed",	   "zeroed", CUMUL,SIZE, 1,_F(zero_fill_count)},
  {"reactivated",  "Reactivated",  "react",  CUMUL,SIZE, 1,_F(reactivations)},
  {"pageins",	   "Pageins",	   "pgins",  CUMUL,SIZE, 1,_F(pageins)},
  {"pageouts",     "Pageouts",	   "pgouts", CUMUL,SIZE, 1,_F(pageouts)},
  {"faults",	   "Faults",	   "pfaults",CUMUL,COUNT,1,_F(faults)},
  {"cow-faults",  "Cow faults",    "cowpfs", CUMUL,COUNT,1,_F(cow_faults)},
  {"cache-lookups","Cache lookups","clkups", CUMUL,COUNT,0,_F(lookups)},
  {"cache-hits",   "Cache hits",   "chits",  CUMUL,COUNT,0,_F(hits)},
  {"cache-hit-ratio","Cache hit ratio","chrat",VARY,PCENT,1,-1,get_cache_hit_ratio},
  {"swap-size",    "Swap size",	   "swsize", CONST,SIZE, 1,0,get_swap_size},
  {"swap-active",  "Swap active",  "swactv", VARY, SIZE, 0,0,get_swap_active},
  {"swap-free",    "Swap free",	   "swfree", VARY, SIZE, 1,0,get_swap_free},
  {"swap-pagesize","Swap pagesize","swpgsz", CONST,PAGESZ, 0,0,get_swap_page_size},
  {0}
};
#undef _F

int
main (int argc, char **argv)
{
  error_t err;
  const struct field *field;
  struct vm_state state;
  int num_fields = 0;		/* Number of vm_fields known. */
  unsigned long output_fields = 0; /* A bit per field, from 0. */
  int count = 1;		/* Number of repeats.  */
  unsigned period = 0;		/* Seconds between terse mode repeats.  */
  unsigned hdr_interval = 22;	/*  */
  ssize_t size_units = 0;	/* -1 means `pages' */
  int terse = 0, print_heading = 1, print_prefix = -1;

  /* Parse our options...  */
  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      if (key < 0)
	/* A field option.  */
	output_fields |= (1 << (-1 - key));
      else
	switch (key)
	  {
	  case 't': terse = 1; break;
	  case 'p': print_prefix = 1; break;
	  case 'P': print_prefix = 0; break;
	  case 'H': print_heading = 0; break;
	  case 'b': size_units = 1; break;
	  case 'v': size_units = -1; break;
	  case 'k': size_units = 1024; break;

	  case ARGP_KEY_ARG:
	    terse = 1;
	    switch (state->arg_num)
	      {
	      case 0:
		period = atoi (arg); count = 0; break;
	      case 1:
		count = atoi (arg); break;
	      case 2:
		hdr_interval = atoi (arg); break;
	      default:
		return ARGP_ERR_UNKNOWN;
	      }
	    break;

	  default:
	    return ARGP_ERR_UNKNOWN;
	  }
      return 0;
    }
  struct argp_option *field_opts;
  int field_opts_size;
  struct argp field_argp = { 0, parse_opt };
  const struct argp *parents[] = { &field_argp, 0 };
  const struct argp argp = { options, parse_opt, args_doc, doc, parents };

  /* See how many fields we know about.  */
  for (field = fields; field->name; field++)
    num_fields++;

  /* Construct an options vector for them.  */
  field_opts_size = ((num_fields + 1) * sizeof (struct argp_option));
  field_opts = alloca (field_opts_size);
  bzero (field_opts, field_opts_size);

  for (field = fields; field->name; field++)
    {
      int which = field - fields;
      struct argp_option *opt = &field_opts[which];

      opt->name = field->name;
      opt->key = -1 - which;	/* options are numbered -1 ... -(N - 1).  */
      opt->doc = field->desc;
      opt->group = 2;
    }
  /* No need to terminate FIELD_OPTS because the bzero above's done so.  */

  field_argp.options = field_opts;

  /* Parse our arguments.  */
  argp_parse (&argp, argc, argv, 0, 0, 0);

  if (output_fields == 0)
    /* Show default fields.  */
    for (field = fields; field->name; field++)
      if (field->standard)
	output_fields |= (1 << (field - fields));

  /* Returns an appropiate SIZE_UNITS for printing FIELD.  */
#define SIZE_UNITS(field)							\
  (size_units >= 0								\
   ? size_units									\
   : ((field)->type == PAGESZ ? 0 : state.vmstats.pagesize))

    /* Prints SEP if the variable FIRST is 0, otherwise, prints START (if
       it's non-zero), and sets first to 0.  */
#define PSEP(sep, start) \
    (first ? (first = 0, (start && fputs (start, stdout))) : fputs (sep, stdout))
#define PVAL(val, field, width, sign) \
    print_val (val, (field)->type, SIZE_UNITS (field), width, sign)
	
  /* Actually fetch the statistics.  */
  bzero (&state, sizeof (state)); /* Initialize STATE.  */
  err = vm_state_refresh (&state);
  if (err)
    error (2, err, "vm_state_refresh");

  if (terse)
    /* Terse (one-line) output mode.  */
    {
      int first_hdr = 1, first, repeats;
      struct vm_state prev_state;
      int const_fields = 0;

      if (count == 0)
	count = -1;

      /* We only show const fields once per page, so find out which ones
	 those are.  */
      for (field = fields; field->name; field++)
	if ((output_fields & (1 << (field - fields)))
	    && field->change_type == CONST)
	  const_fields |= (1 << (field - fields));
      output_fields &= ~const_fields;

      if (const_fields)
	hdr_interval--;		/* Allow room for the constant fields.  */

      do
	{
	  if (first_hdr)
	    first_hdr = 0;
	  else
	    putchar ('\n');

	  if (const_fields)
	    /* Output constant fields on a line preceding the header.  */
	    {
	      for (field = fields, first = 1; field->name; field++)
		if (const_fields & (1 << (field - fields)))
		  {
		    val_t val = vm_state_get_field (&state, field);
		    if (val < 0)
		      /* Couldn't fetch this field, don't try again.  */
		      const_fields &= ~(1 << (field - fields));
		    else
		      {
			PSEP (", ", "(");
			printf ("%s: ", field->desc);
			PVAL (val, field, 0, 0);
		      }
		  }
	      if (! first)
		puts (")");
	    }

	  if (print_heading)
	    {
	      for (field = fields, first = 1; field->name; field++)
		if (output_fields & (1 << (field - fields)))
		  {
		    PSEP (" ", 0);
		    fputs (field->hdr, stdout);
		  }
	      putchar ('\n');
	    }
	
	  prev_state = state;

	  for (repeats = 0
	       ; count && repeats < hdr_interval && count
	       ; repeats++, count--)
	    {
	      /* Output the fields.  */
	      for (field = fields, first = 1; field->name; field++)
		if (output_fields & (1 << (field - fields)))
		  {
		    int sign = 0;
		    int width = strlen (field->hdr);
		    val_t val = vm_state_get_field (&state, field);

		    if (repeats && field->change_type == CUMUL)
		      {
			sign = 1;
			val -= vm_state_get_field (&prev_state, field);
		      }

		    PSEP (" ", 0);
		    PVAL (val, field, width, sign);
		  }
	      putchar ('\n');

	      prev_state = state;

	      if (period)
		{
		  sleep (period);
		  err = vm_state_refresh (&state);
		  if (err)
		    error (2, err, "vm_state_refresh");
		}
	    }
	}
      while (count);
    }
  else
    /* Verbose output.  */
    {
      int max_desc_width = 0;

      if (print_prefix < 0)
	/* By default, only print a prefix if there are multiple fields. */
	print_prefix = (output_fields & (output_fields - 1));

      if (print_prefix)
	/* Find the widest description string, so we can align the output. */
	for (field = fields; field->name; field++)
	  if (output_fields & (1 << (field - fields)))
	    {
	      int desc_len = strlen (field->desc);
	      if (desc_len > max_desc_width)
		max_desc_width = desc_len;
	    }

      for (field = fields; field->name; field++)
	if (output_fields & (1 << (field - fields)))
	  {
	    val_t val = vm_state_get_field (&state, field);
	    int fwidth = 0;
	    if (print_prefix)
	      {
		printf ("%s:", field->desc);
		fwidth = max_desc_width + 5 - strlen (field->desc);
	      }
	    PVAL (val, field, fwidth, 0);
	    putchar ('\n');
	  }
    }

  exit (0);
}
