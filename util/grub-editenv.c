/* grub-editenv.c - tool to edit environment block.  */
/*
 *  GRUB  --  GRand Unified Bootloader
 *  Copyright (C) 2008,2009,2010 Free Software Foundation, Inc.
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

#include <config.h>
#include <grub/types.h>
#include <grub/emu/misc.h>
#include <grub/util/misc.h>
#include <grub/lib/envblk.h>
#include <grub/i18n.h>
#include <grub/emu/hostdisk.h>
#include <grub/util/install.h>
#include <grub/emu/getroot.h>
#include <grub/fs.h>
#include <grub/crypto.h>

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#pragma GCC diagnostic ignored "-Wmissing-prototypes"
#pragma GCC diagnostic ignored "-Wmissing-declarations"
#include <argp.h>
#pragma GCC diagnostic error "-Wmissing-prototypes"
#pragma GCC diagnostic error "-Wmissing-declarations"


#include "progname.h"

#define DEFAULT_ENVBLK_PATH DEFAULT_DIRECTORY "/" GRUB_ENVBLK_DEFCFG

static struct argp_option options[] = {
  {0,        0, 0, OPTION_DOC, N_("Commands:"), 1},
  {"create", 0, 0, OPTION_DOC|OPTION_NO_USAGE,
   N_("Create a blank environment block file."), 0},
  {"list",   0, 0, OPTION_DOC|OPTION_NO_USAGE,
   N_("List the current variables."), 0},
  /* TRANSLATORS: "set" is a keyword. It's a summary of "set" subcommand.  */
  {N_("set [NAME=VALUE ...]"), 0, 0, OPTION_DOC|OPTION_NO_USAGE,
   N_("Set variables."), 0},
  /* TRANSLATORS: "unset" is a keyword. It's a summary of "unset" subcommand.  */
  {N_("unset [NAME ...]"),    0, 0, OPTION_DOC|OPTION_NO_USAGE,
   N_("Delete variables."), 0},

  {0,         0, 0, OPTION_DOC, N_("Options:"), -1},
  {"verbose", 'v', 0, 0, N_("print verbose messages."), 0},

  { 0, 0, 0, 0, 0, 0 }
};

/* Print the version information.  */
static void
print_version (FILE *stream, struct argp_state *state)
{
  fprintf (stream, "%s (%s) %s\n", program_name, PACKAGE_NAME, PACKAGE_VERSION);
}
void (*argp_program_version_hook) (FILE *, struct argp_state *) = print_version;

/* Set the bug report address */
const char *argp_program_bug_address = "<"PACKAGE_BUGREPORT">";

static error_t argp_parser (int key, char *arg, struct argp_state *state)
{
  switch (key)
    {
      case 'v':
        verbosity++;
        break;

      case ARGP_KEY_NO_ARGS:
        fprintf (stderr, "%s",
		 _("You need to specify at least one command.\n"));
        argp_usage (state);
        break;

      default:
        return ARGP_ERR_UNKNOWN;
    }

  return 0;
}

#pragma GCC diagnostic ignored "-Wformat-nonliteral"

static char *
help_filter (int key, const char *text, void *input __attribute__ ((unused)))
{
  switch (key)
    {
      case ARGP_KEY_HELP_POST_DOC:
        return xasprintf (text, DEFAULT_ENVBLK_PATH, DEFAULT_ENVBLK_PATH);

      default:
        return (char *) text;
    }
}

#pragma GCC diagnostic error "-Wformat-nonliteral"

struct argp argp = {
  options, argp_parser, N_("FILENAME COMMAND"),
  "\n"N_("\
Tool to edit environment block.")
"\v"N_("\
If FILENAME is `-', the default value %s is used.\n\n\
There is no `delete' command; if you want to delete the whole environment\n\
block, use `rm %s'."),
  NULL, help_filter, NULL
};

struct fs_envblk_spec {
  const char *fs_name;
  off_t offset;
  size_t size;
};
typedef struct fs_envblk_spec fs_envblk_spec_t;

static grub_envblk_t fs_envblk_open (grub_envblk_t envblk);
static void fs_envblk_write (grub_envblk_t envblk);

struct fs_envblk_ops {
  grub_envblk_t (*open) (grub_envblk_t);
  void (*write) (grub_envblk_t);
};
typedef struct fs_envblk_ops fs_envblk_ops_t;

struct fs_envblk {
  fs_envblk_spec_t *spec;
  fs_envblk_ops_t *ops;
  const char *dev;
};
typedef struct fs_envblk *fs_envblk_t;

static fs_envblk_ops_t fs_envblk_ops = {
  .open = fs_envblk_open,
  .write = fs_envblk_write
};

/*
 * fs_envblk_spec describes the file-system specific layout of reserved raw
 * blocks used as environment blocks.
 */
static fs_envblk_spec_t fs_envblk_spec[] = {
  { NULL, 0, 0 }
};

static fs_envblk_t fs_envblk = NULL;

static void
fs_envblk_init (const char *fs_name, const char *dev)
{
  fs_envblk_spec_t *p;

  if (fs_name == NULL || dev == NULL)
    return;

  for (p = fs_envblk_spec; p->fs_name != NULL; p++)
    {
      if (strcmp (fs_name, p->fs_name) == 0)
	{
	  if (fs_envblk == NULL)
	    fs_envblk = xmalloc (sizeof (*fs_envblk));
	  fs_envblk->spec = p;
	  fs_envblk->dev = xstrdup (dev);
	  fs_envblk->ops = &fs_envblk_ops;
	  break;
	}
    }
}

static int
read_env_block_var (const char *varname, const char *value, void *hook_data)
{
  grub_envblk_t *p_envblk = (grub_envblk_t *) hook_data;
  off_t off;
  size_t sz;
  char *p, *buf;
  FILE *fp;

  if (p_envblk == NULL || fs_envblk == NULL)
    return 1;

  if (strcmp (varname, "env_block") != 0)
    return 0;

  off = strtol (value, &p, 10);
  if (*p == '+')
    sz = strtol (p + 1, &p, 10);
  else
    return 0;

  if (*p != '\0' || sz == 0)
    return 0;

  off <<= GRUB_DISK_SECTOR_BITS;
  sz <<= GRUB_DISK_SECTOR_BITS;

  fp = grub_util_fopen (fs_envblk->dev, "rb");
  if (fp == NULL)
    grub_util_error (_("cannot open `%s': %s"), fs_envblk->dev, strerror (errno));

  if (fseek (fp, off, SEEK_SET) < 0)
    grub_util_error (_("cannot seek `%s': %s"), fs_envblk->dev, strerror (errno));

  buf = xmalloc (sz);
  if ((fread (buf, 1, sz, fp)) != sz)
    grub_util_error (_("cannot read `%s': %s"), fs_envblk->dev, strerror (errno));

  fclose (fp);

  *p_envblk = grub_envblk_open (buf, sz);

  return 1;
}

static void
create_env_on_block (void)
{
  FILE *fp;
  char *buf;
  const char *device;
  off_t offset;
  size_t size;

  if (fs_envblk == NULL)
    return;

  device = fs_envblk->dev;
  offset = fs_envblk->spec->offset;
  size = fs_envblk->spec->size;

  fp = grub_util_fopen (device, "r+b");
  if (fp == NULL)
    grub_util_error (_("cannot open `%s': %s"), device, strerror (errno));

  buf = xmalloc (size);
  memcpy (buf, GRUB_ENVBLK_SIGNATURE, sizeof (GRUB_ENVBLK_SIGNATURE) - 1);
  memset (buf + sizeof (GRUB_ENVBLK_SIGNATURE) - 1, '#', size - sizeof (GRUB_ENVBLK_SIGNATURE) + 1);

  if (fseek (fp, offset, SEEK_SET) < 0)
    grub_util_error (_("cannot seek `%s': %s"), device, strerror (errno));

  if (fwrite (buf, 1, size, fp) != size)
    grub_util_error (_("cannot write to `%s': %s"), device, strerror (errno));

  grub_util_file_sync (fp);
  free (buf);
  fclose (fp);
}

static grub_envblk_t
fs_envblk_open (grub_envblk_t envblk)
{
  grub_envblk_t envblk_on_block = NULL;
  char *val;
  off_t offset;
  size_t size;

  if (envblk == NULL)
    return NULL;

  offset = fs_envblk->spec->offset;
  size = fs_envblk->spec->size;

  grub_envblk_iterate (envblk, &envblk_on_block, read_env_block_var);

  if (envblk_on_block != NULL && grub_envblk_size (envblk_on_block) == size)
    return envblk_on_block;

  create_env_on_block ();

  offset = offset >> GRUB_DISK_SECTOR_BITS;
  size =  (size + GRUB_DISK_SECTOR_SIZE - 1) >> GRUB_DISK_SECTOR_BITS;

  val = xasprintf ("%lld+%zu", (long long) offset, size);
  if (grub_envblk_set (envblk, "env_block", val) == 0)
    grub_util_error ("%s", _("environment block too small"));
  grub_envblk_iterate (envblk, &envblk_on_block, read_env_block_var);
  free (val);

  return envblk_on_block;
}

static grub_envblk_t
open_envblk_file (const char *name)
{
  FILE *fp;
  char *buf;
  long loc;
  size_t size;
  grub_envblk_t envblk;

  fp = grub_util_fopen (name, "rb");
  if (! fp)
    {
      /* Create the file implicitly.  */
      grub_util_create_envblk_file (name);
      fp = grub_util_fopen (name, "rb");
      if (! fp)
        grub_util_error (_("cannot open `%s': %s"), name,
			 strerror (errno));
    }

  if (fseek (fp, 0, SEEK_END) < 0)
    grub_util_error (_("cannot seek `%s': %s"), name,
		     strerror (errno));

  loc = ftell (fp);
  if (loc < 0)
    grub_util_error (_("cannot get file location `%s': %s"), name,
		     strerror (errno));

  size = (size_t) loc;

  if (fseek (fp, 0, SEEK_SET) < 0)
    grub_util_error (_("cannot seek `%s': %s"), name,
		     strerror (errno));

  buf = xmalloc (size);

  if (fread (buf, 1, size, fp) != size)
    grub_util_error (_("cannot read `%s': %s"), name,
		     strerror (errno));

  fclose (fp);

  envblk = grub_envblk_open (buf, size);
  if (! envblk)
    grub_util_error ("%s", _("invalid environment block"));

  return envblk;
}

static int
print_var (const char *varname, const char *value,
           void *hook_data __attribute__ ((unused)))
{
  printf ("%s=%s\n", varname, value);
  return 0;
}

static void
list_variables (const char *name)
{
  grub_envblk_t envblk;
  grub_envblk_t envblk_on_block = NULL;

  envblk = open_envblk_file (name);
  grub_envblk_iterate (envblk, &envblk_on_block, read_env_block_var);
  grub_envblk_iterate (envblk, NULL, print_var);
  grub_envblk_close (envblk);
  if (envblk_on_block != NULL)
    {
      grub_envblk_iterate (envblk_on_block, NULL, print_var);
      grub_envblk_close (envblk_on_block);
    }
}

static void
write_envblk (const char *name, grub_envblk_t envblk)
{
  FILE *fp;

  fp = grub_util_fopen (name, "wb");
  if (! fp)
    grub_util_error (_("cannot open `%s': %s"), name,
		     strerror (errno));

  if (fwrite (grub_envblk_buffer (envblk), 1, grub_envblk_size (envblk), fp)
      != grub_envblk_size (envblk))
    grub_util_error (_("cannot write to `%s': %s"), name,
		     strerror (errno));

  if (grub_util_file_sync (fp) < 0)
    grub_util_error (_("cannot sync `%s': %s"), name, strerror (errno));
  fclose (fp);
}

static void
fs_envblk_write (grub_envblk_t envblk)
{
  FILE *fp;
  const char *device;
  off_t offset;
  size_t size;

  if (envblk == NULL)
    return;

  device = fs_envblk->dev;
  offset = fs_envblk->spec->offset;
  size = fs_envblk->spec->size;

  if (grub_envblk_size (envblk) > size)
    grub_util_error ("%s", _("environment block too small"));

  fp = grub_util_fopen (device, "r+b");

  if (fp == NULL)
    grub_util_error (_("cannot open `%s': %s"), device, strerror (errno));

  if (fseek (fp, offset, SEEK_SET) < 0)
    grub_util_error (_("cannot seek `%s': %s"), device, strerror (errno));

  if (fwrite (grub_envblk_buffer (envblk), 1, grub_envblk_size (envblk), fp) != grub_envblk_size (envblk))
    grub_util_error (_("cannot write to `%s': %s"), device, strerror (errno));

  grub_util_file_sync (fp);
  fclose (fp);
}

struct var_lookup_ctx {
  const char *varname;
  bool found;
};
typedef struct var_lookup_ctx var_lookup_ctx_t;

static int
var_lookup_iter (const char *varname, const char *value __attribute__ ((unused)), void *hook_data)
{
  var_lookup_ctx_t *ctx = (var_lookup_ctx_t *) hook_data;

  if (grub_strcmp (ctx->varname, varname) == 0)
    {
      ctx->found = true;
      return 1;
    }
  return 0;
}

static void
set_variables (const char *name, int argc, char *argv[])
{
  grub_envblk_t envblk;
  grub_envblk_t envblk_on_block = NULL;

  envblk = open_envblk_file (name);
  if (fs_envblk != NULL)
    envblk_on_block = fs_envblk->ops->open (envblk);

  while (argc)
    {
      char *p;

      p = strchr (argv[0], '=');
      if (! p)
        grub_util_error (_("invalid parameter %s"), argv[0]);

      *(p++) = 0;

      if ((strcmp (argv[0], "next_entry") == 0) && envblk_on_block != NULL)
	{
	  if (grub_envblk_set (envblk_on_block, argv[0], p) == 0)
	    grub_util_error ("%s", _("environment block too small"));
	  goto next;
	}

      if (strcmp (argv[0], "env_block") == 0)
	{
	  grub_util_warn (_("can't set env_block as it's read-only"));
	  goto next;
	}

      if (grub_envblk_set (envblk, argv[0], p) == 0)
	grub_util_error ("%s", _("environment block too small"));

      if (envblk_on_block != NULL)
	{
	  var_lookup_ctx_t ctx = {
	    .varname = argv[0],
	    .found = false
	  };

	  grub_envblk_iterate (envblk_on_block, &ctx, var_lookup_iter);
	  if (ctx.found == true)
	    grub_envblk_delete (envblk_on_block, argv[0]);
	}
 next:
      argc--;
      argv++;
    }

  write_envblk (name, envblk);
  grub_envblk_close (envblk);

  if (envblk_on_block != NULL)
    {
      fs_envblk->ops->write (envblk_on_block);
      grub_envblk_close (envblk_on_block);
    }
}

static void
unset_variables (const char *name, int argc, char *argv[])
{
  grub_envblk_t envblk;
  grub_envblk_t envblk_on_block = NULL;

  envblk = open_envblk_file (name);

  if (fs_envblk != NULL)
    envblk_on_block = fs_envblk->ops->open (envblk);

  while (argc)
    {
      grub_envblk_delete (envblk, argv[0]);

      if (envblk_on_block != NULL)
	grub_envblk_delete (envblk_on_block, argv[0]);

      argc--;
      argv++;
    }

  write_envblk (name, envblk);
  grub_envblk_close (envblk);

  if (envblk_on_block != NULL)
    {
      fs_envblk->ops->write (envblk_on_block);
      grub_envblk_close (envblk_on_block);
    }
}

static bool
is_abstraction (grub_device_t dev)
{
  if (dev == NULL || dev->disk == NULL)
    return false;

  if (dev->disk->dev->id == GRUB_DISK_DEVICE_DISKFILTER_ID ||
      dev->disk->dev->id == GRUB_DISK_DEVICE_CRYPTODISK_ID)
    return true;

  return false;
}

static void
probe_fs_envblk (fs_envblk_spec_t *spec)
{
  char **grub_devices = NULL;
  char **curdev, **curdrive;
  size_t ndev = 0;
  char **grub_drives = NULL;
  grub_device_t grub_dev = NULL;
  grub_fs_t grub_fs = NULL;
  bool have_abstraction = false;

  grub_util_biosdisk_init (DEFAULT_DEVICE_MAP);
  grub_init_all ();
  grub_gcry_init_all ();

  grub_lvm_fini ();
  grub_mdraid09_fini ();
  grub_mdraid1x_fini ();
  grub_diskfilter_fini ();
  grub_diskfilter_init ();
  grub_mdraid09_init ();
  grub_mdraid1x_init ();
  grub_lvm_init ();

  grub_devices = grub_guess_root_devices (DEFAULT_DIRECTORY);

  if (grub_devices == NULL || grub_devices[0] == NULL)
    {
      grub_util_warn (_("cannot find a device for %s (is /dev mounted?)"), DEFAULT_DIRECTORY);
      goto cleanup;
    }

  for (curdev = grub_devices; *curdev != NULL; curdev++, ndev++)
    grub_util_pull_device (*curdev);

  grub_drives = xcalloc ((ndev + 1), sizeof (grub_drives[0]));

  for (curdev = grub_devices, curdrive = grub_drives; *curdev != NULL; curdev++,
       curdrive++)
    {
      *curdrive = grub_util_get_grub_dev (*curdev);
      if (*curdrive == NULL)
	{
	  grub_util_warn (_("cannot find a GRUB drive for %s.  Check your device.map"),
			  *curdev);
	  goto cleanup;
	}
    }
  *curdrive = NULL;

  grub_dev = grub_device_open (grub_drives[0]);
  if (grub_dev == NULL)
    {
      grub_util_warn (_("cannot open device %s: %s"), grub_drives[0], grub_errmsg);
      grub_errno = GRUB_ERR_NONE;
      goto cleanup;
    }

  grub_fs = grub_fs_probe (grub_dev);
  if (grub_fs == NULL)
    {
      grub_util_warn (_("cannot probe fs for %s: %s"), grub_drives[0], grub_errmsg);
      grub_errno = GRUB_ERR_NONE;
      goto cleanup;
    }

  have_abstraction = is_abstraction (grub_dev);
  for (curdrive = grub_drives + 1; *curdrive != NULL && have_abstraction == false; curdrive++)
    {
      grub_device_t dev = grub_device_open (*curdrive);

      if (dev == NULL)
	continue;
      have_abstraction = is_abstraction (dev);
      grub_device_close (dev);
    }

  if (have_abstraction == false)
    fs_envblk_init (grub_fs->name, grub_devices[0]);

 cleanup:
  if (grub_devices != NULL)
    for (curdev = grub_devices; *curdev != NULL; curdev++)
      free (*curdev);
  free (grub_devices);
  free (grub_drives);
  grub_device_close (grub_dev);
  grub_gcry_fini_all ();
  grub_fini_all ();
  grub_util_biosdisk_fini ();
}

int
main (int argc, char *argv[])
{
  const char *filename;
  char *command;
  int curindex, arg_count;

  grub_util_host_init (&argc, &argv);

  /* Parse our arguments */
  if (argp_parse (&argp, argc, argv, 0, &curindex, 0) != 0)
    {
      fprintf (stderr, "%s", _("Error in parsing command line arguments\n"));
      exit(1);
    }

  arg_count = argc - curindex;

  if (arg_count == 1)
    {
      filename = DEFAULT_ENVBLK_PATH;
      command  = argv[curindex++];
    }
  else
    {
      filename = argv[curindex++];
      if (strcmp (filename, "-") == 0)
        filename = DEFAULT_ENVBLK_PATH;
      command  = argv[curindex++];
    }

  if (strcmp (filename, DEFAULT_ENVBLK_PATH) == 0)
    probe_fs_envblk (fs_envblk_spec);

  if (strcmp (command, "create") == 0)
    grub_util_create_envblk_file (filename);
  else if (strcmp (command, "list") == 0)
    list_variables (filename);
  else if (strcmp (command, "set") == 0)
    set_variables (filename, argc - curindex, argv + curindex);
  else if (strcmp (command, "unset") == 0)
    unset_variables (filename, argc - curindex, argv + curindex);
  else
    {
      char *program = xstrdup(program_name);
      fprintf (stderr, _("Unknown command `%s'.\n"), command);
      argp_help (&argp, stderr, ARGP_HELP_STD_USAGE, program);
      free(program);
      exit(1);
    }

  return 0;
}
