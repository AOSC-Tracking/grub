/* pause.c - Wait for a keystroke with or without custom messages  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2006,2007,2010  Free Software Foundation, Inc.
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

#include <grub/dl.h>
#include <grub/misc.h>
#include <grub/extcmd.h>
#include <grub/err.h>
#include <grub/i18n.h>
#include <grub/term.h>

GRUB_MOD_LICENSE ("GPLv3+");

static grub_err_t
grub_cmd_pause (grub_extcmd_context_t ctxt __attribute__ ((unused)),
		int argc, char **args)
{
  if (argc != 1)
    grub_printf ("%s", N_("Press any key to continue..."));
  else
    grub_printf ("%s", args[0]);

  (void) grub_getkey ();
  return GRUB_ERR_NONE;
}

static grub_extcmd_t cmd;

GRUB_MOD_INIT(pause)
{
  cmd = grub_register_extcmd ("pause", grub_cmd_pause, 0,
			      "STRING", N_("Wait for a key stroke with a message."),
			      0);
}

GRUB_MOD_FINI(pause)
{
  grub_unregister_extcmd (cmd);
}
