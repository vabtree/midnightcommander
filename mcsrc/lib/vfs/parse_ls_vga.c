/*
   Routines for parsing output from the 'ls' command.

   Copyright (C) 1988-2014
   Free Software Foundation, Inc.

   Copyright (C) 1995, 1996 Miguel de Icaza

   Written by:
   Miguel de Icaza, 1995, 1996 
   Slava Zanko <slavazanko@gmail.com>, 2011

   This file is part of the Midnight Commander.

   The Midnight Commander is free software: you can redistribute it
   and/or modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation, either version 3 of the License,
   or (at your option) any later version.

   The Midnight Commander is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file
 * \brief Source: Utilities for VFS modules
 * \author Miguel de Icaza
 * \date 1995, 1996
 */

#include <config.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "lib/global.h"
#include "lib/unixcompat.h"     /* makedev */
#include "lib/widget.h"         /* message() */

#include "utilvfs.h"

/*** global variables ****************************************************************************/

/*** file scope macro definitions ****************************************************************/

/* Parsing code is used by ftpfs, fish and extfs */
#define MAXCOLS         30

/*** file scope type declarations ****************************************************************/

/*** file scope variables ************************************************************************/

static char *columns[MAXCOLS];  /* Points to the string in column n */
static int column_ptr[MAXCOLS]; /* Index from 0 to the starting positions of the columns */
static size_t vfs_parce_ls_final_num_spaces = 0;

/*** file scope functions ************************************************************************/
/* --------------------------------------------------------------------------------------------- */

static int
is_num (int idx)
{
    char *column = columns[idx];

    if (!column || column[0] < '0' || column[0] > '9')
        return 0;

    return 1;
}

/* --------------------------------------------------------------------------------------------- */
/* Return 1 for MM-DD-YY and MM-DD-YYYY */

static int
is_dos_date (const char *str)
{
    int len;

    if (!str)
        return 0;

    len = strlen (str);
    if (len != 8 && len != 10)
        return 0;

    if (str[2] != str[5])
        return 0;

    if (!strchr ("\\-/", (int) str[2]))
        return 0;

    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
is_week (const char *str, struct tm *tim)
{
    static const char *week = "SunMonTueWedThuFriSat";
    const char *pos;

    if (!str)
        return 0;

    pos = strstr (week, str);
    if (pos != NULL)
    {
        if (tim != NULL)
            tim->tm_wday = (pos - week) / 3;
        return 1;
    }
    return 0;
}

/* --------------------------------------------------------------------------------------------- */

static int
is_month (const char *str, struct tm *tim)
{
    static const char *month = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *pos;

    if (!str)
        return 0;

    pos = strstr (month, str);
    if (pos != NULL)
    {
        if (tim != NULL)
            tim->tm_mon = (pos - month) / 3;
        return 1;
    }
    return 0;
}

/* --------------------------------------------------------------------------------------------- */
/**
 * Check for possible locale's abbreviated month name (Jan..Dec).
 * Any 3 bytes long string without digit, control and punctuation characters.
 * isalpha() is locale specific, so it cannot be used if current
 * locale is "C" and ftp server use Cyrillic.
 * NB: It is assumed there are no whitespaces in month.
 */
static int
is_localized_month (const char *month)
{
    int i = 0;

    if (!month)
        return 0;

    while ((i < 3) && *month && !isdigit ((unsigned char) *month)
           && !iscntrl ((unsigned char) *month) && !ispunct ((unsigned char) *month))
    {
        i++;
        month++;
    }
    return ((i == 3) && (*month == 0));
}

/* --------------------------------------------------------------------------------------------- */

static int
is_time (const char *str, struct tm *tim)
{
    const char *p, *p2;

    if (str == NULL)
        return 0;

    p = strchr (str, ':');
    p2 = strrchr (str, ':');
    if (p != NULL && p2 != NULL)
    {
        if (p != p2)
        {
            if (sscanf (str, "%2d:%2d:%2d", &tim->tm_hour, &tim->tm_min, &tim->tm_sec) != 3)
                return 0;
        }
        else
        {
            if (sscanf (str, "%2d:%2d", &tim->tm_hour, &tim->tm_min) != 2)
                return 0;
        }
    }
    else
        return 0;

    return 1;
}

/* --------------------------------------------------------------------------------------------- */

static int
is_year (char *str, struct tm *tim)
{
    long year;

    if (!str)
        return 0;

    if (strchr (str, ':'))
        return 0;

    if (strlen (str) != 4)
        return 0;

    /* cppcheck-suppress invalidscanf */
    if (sscanf (str, "%ld", &year) != 1)
        return 0;

    if (year < 1900 || year > 3000)
        return 0;

    tim->tm_year = (int) (year - 1900);

    return 1;
}

/* --------------------------------------------------------------------------------------------- */
/*** public functions ****************************************************************************/
/* --------------------------------------------------------------------------------------------- */

gboolean
vfs_parse_filetype (const char *s, size_t * ret_skipped, mode_t * ret_type)
{
    mode_t type;

    switch (*s)
    {
    case 'd':
        type = S_IFDIR;
        break;
    case 'b':
        type = S_IFBLK;
        break;
    case 'c':
        type = S_IFCHR;
        break;
    case 'l':
        type = S_IFLNK;
        break;
#ifdef S_IFSOCK
    case 's':
        type = S_IFSOCK;
        break;
#else
    case 's':
        type = S_IFIFO;
        break;
#endif
#ifdef S_IFDOOR                 /* Solaris door */
    case 'D':
        type = S_IFDOOR;
        break;
#else
    case 'D':
        type = S_IFIFO;
        break;
#endif
    case 'p':
        type = S_IFIFO;
        break;
#ifdef S_IFNAM                  /* Special named files */
    case 'n':
        type = S_IFNAM;
        break;
#else
    case 'n':
        type = S_IFREG;
        break;
#endif
    case 'm':                  /* Don't know what these are :-) */
    case '-':
    case '?':
        type = S_IFREG;
        break;
    default:
        return FALSE;
    }

    *ret_type = type;
    *ret_skipped = 1;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
vfs_parse_fileperms (const char *s, size_t * ret_skipped, mode_t * ret_perms)
{
    const char *p;
    mode_t perms;

    p = s;
    perms = 0;

    switch (*p++)
    {
    case '-':
        break;
    case 'r':
        perms |= S_IRUSR;
        break;
    default:
        return FALSE;
    }
    switch (*p++)
    {
    case '-':
        break;
    case 'w':
        perms |= S_IWUSR;
        break;
    default:
        return FALSE;
    }
    switch (*p++)
    {
    case '-':
        break;
    case 'S':
        perms |= S_ISUID;
        break;
    case 's':
        perms |= S_IXUSR | S_ISUID;
        break;
    case 'x':
        perms |= S_IXUSR;
        break;
    default:
        return FALSE;
    }
    switch (*p++)
    {
    case '-':
        break;
    case 'r':
        perms |= S_IRGRP;
        break;
    default:
        return FALSE;
    }
    switch (*p++)
    {
    case '-':
        break;
    case 'w':
        perms |= S_IWGRP;
        break;
    default:
        return FALSE;
    }
    switch (*p++)
    {
    case '-':
        break;
    case 'S':
        perms |= S_ISGID;
        break;
    case 'l':
        perms |= S_ISGID;
        break;                  /* found on Solaris */
    case 's':
        perms |= S_IXGRP | S_ISGID;
        break;
    case 'x':
        perms |= S_IXGRP;
        break;
    default:
        return FALSE;
    }
    switch (*p++)
    {
    case '-':
        break;
    case 'r':
        perms |= S_IROTH;
        break;
    default:
        return FALSE;
    }
    switch (*p++)
    {
    case '-':
        break;
    case 'w':
        perms |= S_IWOTH;
        break;
    default:
        return FALSE;
    }
    switch (*p++)
    {
    case '-':
        break;
    case 'T':
        perms |= S_ISVTX;
        break;
    case 't':
        perms |= S_IXOTH | S_ISVTX;
        break;
    case 'x':
        perms |= S_IXOTH;
        break;
    default:
        return FALSE;
    }
    if (*p == '+')
    {                           /* ACLs on Solaris, HP-UX and others */
        p++;
    }

    *ret_skipped = p - s;
    *ret_perms = perms;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
vfs_parse_filemode (const char *s, size_t * ret_skipped, mode_t * ret_mode)
{
    const char *p;
    mode_t type, perms;
    size_t skipped;

    p = s;

    if (!vfs_parse_filetype (p, &skipped, &type))
        return FALSE;
    p += skipped;

    if (!vfs_parse_fileperms (p, &skipped, &perms))
        return FALSE;
    p += skipped;

    *ret_skipped = p - s;
    *ret_mode = type | perms;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
vfs_parse_raw_filemode (const char *s, size_t * ret_skipped, mode_t * ret_mode)
{
    const char *p;
    mode_t remote_type = 0, local_type, perms = 0;

    p = s;

    /* isoctal */
    while (*p >= '0' && *p <= '7')
    {
        perms *= 010;
        perms += (*p - '0');
        ++p;
    }

    if (*p++ != ' ')
        return FALSE;

    while (*p >= '0' && *p <= '7')
    {
        remote_type *= 010;
        remote_type += (*p - '0');
        ++p;
    }

    if (*p++ != ' ')
        return FALSE;

    /* generated with:
       $ perl -e 'use Fcntl ":mode";
       my @modes = (S_IFDIR, S_IFBLK, S_IFCHR, S_IFLNK, S_IFREG);
       foreach $t (@modes) { printf ("%o\n", $t); };'
       TODO: S_IFDOOR, S_IFIFO, S_IFSOCK (if supported by os)
       (see vfs_parse_filetype)
     */

    switch (remote_type)
    {
    case 020000:
        local_type = S_IFCHR;
        break;
    case 040000:
        local_type = S_IFDIR;
        break;
    case 060000:
        local_type = S_IFBLK;
        break;
    case 0120000:
        local_type = S_IFLNK;
        break;
    case 0100000:
    default:                   /* don't know what is it */
        local_type = S_IFREG;
        break;
    }

    *ret_skipped = p - s;
    *ret_mode = local_type | perms;
    return TRUE;
}

/* --------------------------------------------------------------------------------------------- */
/** This function parses from idx in the columns[] array */

int
vfs_parse_filedate (int idx, time_t * t)
{
    char *p;
    struct tm tim;
    int d[3];
    int got_year = 0;
    int l10n = 0;               /* Locale's abbreviated month name */
    time_t current_time;
    struct tm *local_time;

    /* Let's setup default time values */
    current_time = time (NULL);
    local_time = localtime (&current_time);
    tim.tm_mday = local_time->tm_mday;
    tim.tm_mon = local_time->tm_mon;
    tim.tm_year = local_time->tm_year;

    tim.tm_hour = 0;
    tim.tm_min = 0;
    tim.tm_sec = 0;
    tim.tm_isdst = -1;          /* Let mktime() try to guess correct dst offset */

    p = columns[idx++];

    /* We eat weekday name in case of extfs */
    if (is_week (p, &tim))
        p = columns[idx++];

    /* Month name */
    if (is_month (p, &tim))
    {
        /* And we expect, it followed by day number */
        if (is_num (idx))
            tim.tm_mday = (int) atol (columns[idx++]);
        else
            return 0;           /* No day */

    }
    else
    {
        /* We expect:
           3 fields max or we'll see oddities with certain file names.
           So both year and time is not allowed.
           Mon DD hh:mm[:ss]
           Mon DD YYYY
           But in case of extfs we allow these date formats:
           MM-DD-YY hh:mm[:ss]
           where Mon is Jan-Dec, DD, MM, YY two digit day, month, year,
           YYYY four digit year, hh, mm, ss two digit hour, minute or second. */

        /* Special case with MM-DD-YY or MM-DD-YYYY */
        if (is_dos_date (p))
        {
            p[2] = p[5] = '-';

            /* cppcheck-suppress invalidscanf */
            if (sscanf (p, "%2d-%2d-%d", &d[0], &d[1], &d[2]) == 3)
            {
                /* Months are zero based */
                if (d[0] > 0)
                    d[0]--;

                if (d[2] > 1900)
                {
                    d[2] -= 1900;
                }
                else
                {
                    /* Y2K madness */
                    if (d[2] < 70)
                        d[2] += 100;
                }

                tim.tm_mon = d[0];
                tim.tm_mday = d[1];
                tim.tm_year = d[2];
                got_year = 1;
            }
            else
                return 0;       /* sscanf failed */
        }
        else
        {
            /* Locale's abbreviated month name followed by day number */
            if (is_localized_month (p) && (is_num (idx++)))
                l10n = 1;
            else
                return 0;       /* unsupported format */
        }
    }

    /* Here we expect to find time or year */
    if (is_num (idx) && (is_time (columns[idx], &tim) || (got_year = is_year (columns[idx], &tim))))
        idx++;
    else
        return 0;               /* Neither time nor date */

    /*
     * If the date is less than 6 months in the past, it is shown without year
     * other dates in the past or future are shown with year but without time
     * This does not check for years before 1900 ... I don't know, how
     * to represent them at all
     */
    if (!got_year && local_time->tm_mon < 6
        && local_time->tm_mon < tim.tm_mon && tim.tm_mon - local_time->tm_mon >= 6)

        tim.tm_year--;

    *t = mktime (&tim);
    if (l10n || (*t < 0))
        *t = 0;
    return idx;
}

/* --------------------------------------------------------------------------------------------- */

int
vfs_split_text (char *p)
{
    char *original = p;
    int numcols;

    memset (columns, 0, sizeof (columns));

    for (numcols = 0; *p && numcols < MAXCOLS; numcols++)
    {
        while (*p == ' ' || *p == '\r' || *p == '\n')
        {
            *p = 0;
            p++;
        }
        columns[numcols] = p;
        column_ptr[numcols] = p - original;
        while (*p && *p != ' ' && *p != '\r' && *p != '\n')
            p++;
    }
    return numcols;
}

/* --------------------------------------------------------------------------------------------- */

void
vfs_parse_ls_lga_init (void)
{
    vfs_parce_ls_final_num_spaces = 1;
}

/* --------------------------------------------------------------------------------------------- */

size_t
vfs_parse_ls_lga_get_final_spaces (void)
{
    return vfs_parce_ls_final_num_spaces;
}

/* --------------------------------------------------------------------------------------------- */

gboolean
vfs_parse_ls_lga (const char *p, struct stat * s, char **filename, char **linkname,
                  size_t * num_spaces)
{
    int idx, idx2, num_cols;
    int i;
    char *p_copy = NULL;
    char *t = NULL;
    const char *line = p;
    size_t skipped;
    mode_t mode;

    if (strncmp (p, "total", 5) == 0)
        return FALSE;

    mode = s->st_mode;          /* WIN32, st_mode type may not be mode_t */
    if (!vfs_parse_filetype (p, &skipped, &mode))
        goto error;
    s->st_mode = mode;
    p += skipped;

    if (*p == ' ')              /* Notwell 4 */
        p++;
    if (*p == '[')
    {
        if (strlen (p) <= 8 || p[8] != ']')
            goto error;
        /* Should parse here the Notwell permissions :) */
        if (S_ISDIR (s->st_mode))
            s->st_mode |= (S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR | S_IXUSR | S_IXGRP | S_IXOTH);
        else
            s->st_mode |= (S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR);
        p += 9;
    }
    else
    {
        size_t lc_skipped;
        mode_t perms;

        if (!vfs_parse_fileperms (p, &lc_skipped, &perms))
            goto error;
        p += lc_skipped;
        s->st_mode |= perms;
    }

    p_copy = g_strdup (p);
    num_cols = vfs_split_text (p_copy);

    s->st_nlink = atol (columns[0]);
    if (s->st_nlink <= 0)
        goto error;

    if (!is_num (1))
        s->st_uid = vfs_finduid (columns[1]);
    else
        s->st_uid = (uid_t) atol (columns[1]);

    /* Mhm, the ls -lg did not produce a group field */
    for (idx = 3; idx <= 5; idx++)
        if (is_month (columns[idx], NULL) || is_week (columns[idx], NULL)
            || is_dos_date (columns[idx]) || is_localized_month (columns[idx]))
            break;

    if (idx == 6 || (idx == 5 && !S_ISCHR (s->st_mode) && !S_ISBLK (s->st_mode)))
        goto error;

    /* We don't have gid */
    if (idx == 3 || (idx == 4 && (S_ISCHR (s->st_mode) || S_ISBLK (s->st_mode))))
        idx2 = 2;
    else
    {
        /* We have gid field */
        if (is_num (2))
            s->st_gid = (gid_t) atol (columns[2]);
        else
            s->st_gid = vfs_findgid (columns[2]);
        idx2 = 3;
    }

    /* This is device */
    if (S_ISCHR (s->st_mode) || S_ISBLK (s->st_mode))
    {
        int maj, min;

        /* Corner case: there is no whitespace(s) between maj & min */
        if (!is_num (idx2) && idx2 == 2)
        {
            /* cppcheck-suppress invalidscanf */
            if (!is_num (++idx2) || sscanf (columns[idx2], " %d,%d", &maj, &min) != 2)
                goto error;
        }
        else
        {
            /* cppcheck-suppress invalidscanf */
            if (!is_num (idx2) || sscanf (columns[idx2], " %d,", &maj) != 1)
                goto error;

            /* cppcheck-suppress invalidscanf */
            if (!is_num (++idx2) || sscanf (columns[idx2], " %d", &min) != 1)
                goto error;
        }
#ifdef HAVE_STRUCT_STAT_ST_RDEV
        s->st_rdev = makedev (maj, min);
#endif
        s->st_size = 0;

    }
    else
    {
        /* Common file size */
        if (!is_num (idx2))
            goto error;

        s->st_size = (off_t) g_ascii_strtoll (columns[idx2], NULL, 10);
#ifdef HAVE_STRUCT_STAT_ST_RDEV
        s->st_rdev = 0;
#endif
    }

    idx = vfs_parse_filedate (idx, &s->st_mtime);
    if (!idx)
        goto error;
    /* Use resulting time value */
    s->st_atime = s->st_ctime = s->st_mtime;
    /* s->st_dev and s->st_ino must be initialized by vfs_s_new_inode () */
#ifdef HAVE_STRUCT_STAT_ST_BLKSIZE
    s->st_blksize = 512;
#endif
#ifdef HAVE_STRUCT_STAT_ST_BLOCKS
    s->st_blocks = (s->st_size + 511) / 512;
#endif

    if (num_spaces != NULL)
    {
        *num_spaces = column_ptr[idx] - column_ptr[idx - 1] - strlen (columns[idx - 1]);
        if (DIR_IS_DOTDOT (columns[idx]))
            vfs_parce_ls_final_num_spaces = *num_spaces;
    }

    for (i = idx + 1, idx2 = 0; i < num_cols; i++)
        if (strcmp (columns[i], "->") == 0)
        {
            idx2 = i;
            break;
        }

    if (((S_ISLNK (s->st_mode) || (num_cols == idx + 3 && s->st_nlink > 1)))    /* Maybe a hardlink? (in extfs) */
        && idx2)
    {

        if (filename)
        {
            *filename = g_strndup (p + column_ptr[idx], column_ptr[idx2] - column_ptr[idx] - 1);
        }
        if (linkname)
        {
            t = g_strdup (p + column_ptr[idx2 + 1]);
            *linkname = t;
        }
    }
    else
    {
        /* Extract the filename from the string copy, not from the columns
         * this way we have a chance of entering hidden directories like ". ."
         */
        if (filename)
        {
            /*
             * filename = g_strdup (columns [idx++]);
             */

            t = g_strdup (p + column_ptr[idx]);
            *filename = t;
        }
        if (linkname)
            *linkname = NULL;
    }

    if (t)
    {
        int p2 = strlen (t);
        if ((--p2 > 0) && (t[p2] == '\r' || t[p2] == '\n'))
            t[p2] = 0;
        if ((--p2 > 0) && (t[p2] == '\r' || t[p2] == '\n'))
            t[p2] = 0;
    }

    g_free (p_copy);
    return TRUE;

  error:
    {
        static int errorcount = 0;

        if (++errorcount < 5)
        {
            message (D_ERROR, _("Cannot parse:"), "%s", (p_copy && *p_copy) ? p_copy : line);
        }
        else if (errorcount == 5)
            message (D_ERROR, MSG_ERROR, _("More parsing errors will be ignored."));
    }

    g_free (p_copy);
    return FALSE;
}

/* --------------------------------------------------------------------------------------------- */
