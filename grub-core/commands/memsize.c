/*
 *  memsize.c - Iterate through GRUB memory map to get the total amount of
 *  available RAM. The memory map does NOT reflect the total physical
 *  amount, so use with care.
 *  It can export the value to the environment, to make great use of tests.
 */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2003,2007  Free Software Foundation, Inc.
 *  Copyright (C) 2003  NIIBE Yutaka <gniibe@m17n.org>
 *
 *  GRUB is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  GRUB is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GRUB.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <grub/types.h>
#include <grub/misc.h>
#include <grub/memory.h>
#include <grub/err.h>
#include <grub/env.h>
#include <grub/dl.h>
#include <grub/extcmd.h>
#include <grub/i18n.h>

GRUB_MOD_LICENSE ("GPLv3+");

const char *default_varname = "memsize";
grub_extcmd_t cmd;
grub_uint64_t grub_avail_mem_bytes = 0;

static const struct grub_arg_option grub_options_memsize[] =
  {
    {"byte", 'b', GRUB_ARG_OPTION_OPTIONAL,
      N_("Print and export the vaule as number of bytes."), 0, 0},
    {"kibi", 'K', GRUB_ARG_OPTION_OPTIONAL,
      N_("Print and export the value as kibibytes."), 0, 0},
    {"mebi", 'M', GRUB_ARG_OPTION_OPTIONAL,
      N_("Print and export the value as mebibytes."), 0, 0},
    {"gibi", 'G', GRUB_ARG_OPTION_OPTIONAL,
      N_("Print and export the value as gibibytes."), 0, 0},
    {"quiet", 'q', GRUB_ARG_OPTION_OPTIONAL,
      N_("Do not print anyting, just set a variable quietly."), 0, 0},
    {"set", 's', GRUB_ARG_OPTION_OPTIONAL,
      N_("Set a variable to the vaule as the unit specified."),
      N_("VARNAME"), ARG_TYPE_STRING},
    {0, 0, 0, 0, 0, 0},
};

/* Corresponding strings of the units */
const char* unit_strs[] = {
  "B", "KiB", "MiB", "GiB",
};

/* Corresponding vaules to convert */
const int conversions[] = {
  0, 10, 20, 30
};

enum grub_options_memsize
  {
    UNIT_BYTES,
    UNIT_BIN_KIB,
    UNIT_BIN_MIB,
    UNIT_BIN_GIB,
    FLAG_QUIET,
    HAS_CUSTOM_VARIABLE,
};

#ifdef GRUB_MACHINE_EMU
__attribute__ ((unused))
#endif
static int
memsize_hook (grub_uint64_t addr __attribute__ ((unused)),
	      grub_uint64_t size, grub_memory_type_t type, void *data)
{
  /*
   * Iterate through GRUB memory map.
   * FIXME: Which type of memory region should we consider to add up?
   */
  grub_uint64_t *total = (grub_uint64_t *) data;
  if (type == GRUB_MEMORY_AVAILABLE) {
      *total += size;
  }
  return GRUB_ERR_NONE;
}

static grub_err_t
grub_cmd_memsize (grub_extcmd_context_t ctxt,
		  int argc __attribute__ ((unused)),
		  char **args __attribute__ ((unused)))
{
  /* Parsed argument list. */
  struct grub_arg_list *state = ctxt->state;
  /* Length of the variable name. */
  grub_size_t len = 0;
  /* Buffer of variable name. */
  char buf[64] = {0};
  /* Requested unit to convert. */
  enum grub_options_memsize unit = UNIT_BIN_MIB;
  enum grub_options_memsize opt = UNIT_BYTES;
  /* Flag to make sure only one unit is specified. */
  int unit_switch_is_set = 0;
  /* Total amount of available RAM. */
  grub_uint64_t avail_mem = 0;
  /* The converted value. */
  grub_uint64_t converted_avail_mem = 0;
  char *converted_avail_mem_str = 0;
  /* Record errors during grub_machine_mmap_iterate. */
#ifdef GRUB_MACHINE_EMU
  __attribute__ ((unused))
#endif
  grub_err_t err;
  /* Sanity checks */
  /* Only one unit switch is allowed. */
  for (; opt < FLAG_QUIET; opt++)
    {
      if (state[opt].set != 0)
        {
          if (unit_switch_is_set != 0)
            return grub_error (GRUB_ERR_BAD_ARGUMENT,
			       N_("Only one unit switch is allowed"));
          else
            {
	      unit = opt;
	      unit_switch_is_set = 1;
            }
	}
    }
  /* Check if the specified variable name is valid. */
  if (state[HAS_CUSTOM_VARIABLE].set != 0)
    {
      char *str = state[HAS_CUSTOM_VARIABLE].arg;
      len = grub_strlen (str);
      if (len > 63) {
        return grub_error (GRUB_ERR_BAD_ARGUMENT,
			   N_("Variable names should not exceed 63 characters"));
      } else if (len == 0) {
        return grub_error (GRUB_ERR_BAD_ARGUMENT,
			   N_("Variable name expected but not specified"));
      }
      /* [0-9a-zA-Z_] for variable names */
      for (char *c = str; c < str + len; c++)
      {
        if (   !(*c >= '0' && *c <= '9')
  	  && !(*c >= 'a' && *c <= 'z')
  	  && !(*c >= 'A' && *c <= 'Z')
  	  && !(*c == '_'))
          return grub_error(GRUB_ERR_BAD_ARGUMENT,
			    N_("Invalid variable name"));
      }
      grub_strncpy (buf, str, len);
    }
  else
    {
      len = grub_strlen (default_varname);
      grub_strncpy (buf, default_varname, len);
    }
  /* Iterate through GRUB's memory map. */
#ifndef GRUB_MACHINE_EMU
  err = grub_machine_mmap_iterate (memsize_hook, (void *) &avail_mem);
  if (err != GRUB_ERR_NONE)
    return err;
#endif
  /*
   * Convert the vaule to the specified unit, and export it to the
   * environment.
   * Can not use division, which might requires libgcc!
   */
  converted_avail_mem = avail_mem >> conversions[unit];
  /*
   * Unfortunately the test command can only perform comparisons across
   * signed ints, or the test command will fail. We need to check this in
   * order to make it usable to test command.
   */
  if (converted_avail_mem > GRUB_INT_MAX)
    {
      return grub_error (GRUB_ERR_BAD_NUMBER,
			 N_("Value is too large (%"
			    PRIuGRUB_UINT64_T " > %"
			    PRIdGRUB_INT32_T ")"),
			 converted_avail_mem,
			 GRUB_INT_MAX);
    }
  if (state[FLAG_QUIET].set == 0)
    {
      grub_printf (N_("The amount of available RAM is %"
		      PRIuGRUB_UINT64_T " %s.\n"),
		   converted_avail_mem, unit_strs[unit]);
    }
  converted_avail_mem_str =
    grub_xasprintf ("%" PRIuGRUB_UINT64_T,
		    converted_avail_mem);
  if (!converted_avail_mem_str)
    return grub_error (GRUB_ERR_OUT_OF_MEMORY,
		       N_("Can't allocate space for value string"));

  grub_env_set (buf, converted_avail_mem_str);
  grub_env_export (buf);

  return GRUB_ERR_NONE;
}

GRUB_MOD_INIT(memsize)
{
  cmd =
    grub_register_extcmd ("memsize",
    			  grub_cmd_memsize,
			  GRUB_COMMAND_FLAG_EXTCMD,
			  N_("[-b|-K|-M|-G] [-q] [--set VARNAME]"),
			  N_("Get the amount of system RAM available to GRUB, "
			     "and export the integer value to the environment. "
			     "The vaule will be printed out by default. "
			     "-q disables the output. "
			     "The default variable name is `memsize'. "
			     "The unit can be b for bytes,"
			     "or K, M, G for binary units. The default unit is MiB."),
			  grub_options_memsize);
}

GRUB_MOD_FINI(memsize)
{
  grub_unregister_extcmd (cmd);
}
