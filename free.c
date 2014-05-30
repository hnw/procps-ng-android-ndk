/*
 * free.c - free(1)
 * procps-ng utility to display free memory information
 *
 * Copyright (C) 1992-2012
 *
 * Mostly new, Sami Kerola <kerolasa@iki.fi>		15 Apr 2011
 * All new, Robert Love <rml@tech9.net>			18 Nov 2002
 * Original by Brian Edmonds and Rafal Maszkowski	14 Dec 1992
 *
 * Copyright 2003 Robert Love
 * Copyright 2004 Albert Cahalan
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "config.h"
#include "proc/sysinfo.h"
#include "proc/version.h"
#include "c.h"
#include "nls.h"
#include "strutils.h"
#include "fileutils.h"

#include <locale.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifndef SIZE_MAX
#define SIZE_MAX		32
#endif

#define FREE_HUMANREADABLE	(1 << 1)
#define FREE_LOHI		(1 << 2)
#define FREE_OLDFMT		(1 << 3)
#define FREE_TOTAL		(1 << 4)
#define FREE_SI			(1 << 5)
#define FREE_REPEAT		(1 << 6)
#define FREE_REPEATCOUNT	(1 << 7)

struct commandline_arguments {
	int exponent;		/* demanded in kilos, magas... */
	float repeat_interval;	/* delay in seconds */
	int repeat_counter;	/* number of repeats */
};

/* function prototypes */
static void usage(FILE * out);
double power(unsigned int base, unsigned int expo);
static const char *scale_size(unsigned long size, int flags, struct commandline_arguments args);

static void __attribute__ ((__noreturn__))
    usage(FILE * out)
{
        fputs(USAGE_HEADER, out);
	fprintf(out,
	      _(" %s [options]\n"), program_invocation_short_name);
	fputs(USAGE_OPTIONS, out);
	fputs(_(" -b, --bytes         show output in bytes\n"), out);
	fputs(_(" -k, --kilo          show output in kilobytes\n"), out);
	fputs(_(" -m, --mega          show output in megabytes\n"), out);
	fputs(_(" -g, --giga          show output in gigabytes\n"), out);
	fputs(_("     --tera          show output in terabytes\n"), out);
	fputs(_(" -h, --human         show human-readable output\n"), out);
	fputs(_("     --si            use powers of 1000 not 1024\n"), out);
	fputs(_(" -l, --lohi          show detailed low and high memory statistics\n"), out);
	fputs(_(" -o, --old           use old format (without -/+buffers/cache line)\n"), out);
	fputs(_(" -t, --total         show total for RAM + swap\n"), out);
	fputs(_(" -s N, --seconds N   repeat printing every N seconds\n"), out);
	fputs(_(" -c N, --count N     repeat printing N times, then exit\n"), out);
	fputs(USAGE_SEPARATOR, out);
	fputs(_("     --help     display this help and exit\n"), out);
	fputs(USAGE_VERSION, out);
	fprintf(out, USAGE_MAN_TAIL("free(1)"));

	exit(out == stderr ? EXIT_FAILURE : EXIT_SUCCESS);
}

double power(unsigned int base, unsigned int expo)
{
	return (expo == 0) ? 1 : base * power(base, expo - 1);
}

/* idea of this function is copied from top size scaling */
static const char *scale_size(unsigned long size, int flags, struct commandline_arguments args)
{
	static char nextup[] = { 'B', 'K', 'M', 'G', 'T', 0 };
	static char buf[BUFSIZ];
	int i;
	char *up;
	float base;

	if (flags & FREE_SI)
		base = 1000.0;
	else
		base = 1024.0;

	/* default output */
	if (args.exponent == 0 && !(flags & FREE_HUMANREADABLE)) {
		snprintf(buf, sizeof(buf), "%ld", size);
		return buf;
	}

	if (!(flags & FREE_HUMANREADABLE)) {
		if (args.exponent == 1) {
			/* in bytes, which can not be in SI */
			snprintf(buf, sizeof(buf), "%lld", ((long long int)size) * 1024);
			return buf;
		}
		if (args.exponent == 2) {
			if (!(flags & FREE_SI))
				snprintf(buf, sizeof(buf), "%ld", size);
			else
				snprintf(buf, sizeof(buf), "%ld", (long int)(size / 0.9765625));
			return buf;
		}
		if (args.exponent > 2) {
			/* In desired scale. */
			snprintf(buf, sizeof(buf), "%ld",
				 (long int)(size / power(base, args.exponent - 2))
			    );
			return buf;
		}
	}

	/* human readable output */
	up = nextup;
	for (i = 1; up[0] != '0'; i++, up++) {
		switch (i) {
		case 1:
			if (4 >= snprintf(buf, sizeof(buf), "%ld%c", (long)size * 1024, *up))
				return buf;
			break;
		case 2:

			if (!(flags & FREE_SI)) {
				if (4 >= snprintf(buf, sizeof(buf), "%ld%c", size, *up))
					return buf;
			} else {
				if (4 >=
				    snprintf(buf, sizeof(buf), "%ld%c",
					     (long)(size / 0.9765625), *up))
					return buf;
			}
			break;
		case 3:
		case 4:
		case 5:
			if (4 >=
			    snprintf(buf, sizeof(buf), "%.1f%c",
				     (float)(size / power(base, i - 2)), *up))
				return buf;
			if (4 >=
			    snprintf(buf, sizeof(buf), "%ld%c",
				     (long)(size / power(base, i - 2)), *up))
				return buf;
			break;
		case 6:
			break;
		}
	}
	/*
	 * On system where there is more than petabyte of memory or swap the
	 * output does not fit to column. For incoming few years this should
	 * not be a big problem (wrote at Apr, 2011).
	 */
	return buf;
}

int main(int argc, char **argv)
{
	int c, flags = 0;
	char *endptr;
	struct commandline_arguments args;

	/*
	 * For long options that have no equivalent short option, use a
	 * non-character as a pseudo short option, starting with CHAR_MAX + 1.
	 */
	enum {
		SI_OPTION = CHAR_MAX + 1,
		TERA_OPTION,
		HELP_OPTION
	};

	static const struct option longopts[] = {
		{  "bytes",	no_argument,	    NULL,  'b'		},
		{  "kilo",	no_argument,	    NULL,  'k'		},
		{  "mega",	no_argument,	    NULL,  'm'		},
		{  "giga",	no_argument,	    NULL,  'g'		},
		{  "tera",	no_argument,	    NULL,  TERA_OPTION	},
		{  "human",	no_argument,	    NULL,  'h'		},
		{  "si",	no_argument,	    NULL,  SI_OPTION	},
		{  "lohi",	no_argument,	    NULL,  'l'		},
		{  "old",	no_argument,	    NULL,  'o'		},
		{  "total",	no_argument,	    NULL,  't'		},
		{  "seconds",	required_argument,  NULL,  's'		},
		{  "count",	required_argument,  NULL,  'c'		},
		{  "help",	no_argument,	    NULL,  HELP_OPTION	},
		{  "version",	no_argument,	    NULL,  'V'		},
		{  NULL,	0,		    NULL,  0		}
	};

	/* defaults to old format */
	args.exponent = 0;
	args.repeat_interval = 1000000;
	args.repeat_counter = 0;

#ifdef HAVE_PROGRAM_INVOCATION_NAME
	program_invocation_name = program_invocation_short_name;
#endif
	setlocale (LC_ALL, "");
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
	atexit(close_stdout);

	while ((c = getopt_long(argc, argv, "bkmghlotc:s:V", longopts, NULL)) != -1)
		switch (c) {
		case 'b':
			args.exponent = 1;
			break;
		case 'k':
			args.exponent = 2;
			break;
		case 'm':
			args.exponent = 3;
			break;
		case 'g':
			args.exponent = 4;
			break;
		case TERA_OPTION:
			args.exponent = 5;
			break;
		case 'h':
			flags |= FREE_HUMANREADABLE;
			break;
		case SI_OPTION:
			flags |= FREE_SI;
			break;
		case 'l':
			flags |= FREE_LOHI;
			break;
		case 'o':
			flags |= FREE_OLDFMT;
			break;
		case 't':
			flags |= FREE_TOTAL;
			break;
		case 's':
			flags |= FREE_REPEAT;
			args.repeat_interval = (1000000 * strtof(optarg, &endptr));
			if (errno || optarg == endptr || (endptr && *endptr))
				xerrx(EXIT_FAILURE, _("seconds argument `%s' failed"), optarg);
			if (args.repeat_interval < 1)
				xerrx(EXIT_FAILURE,
				     _("seconds argument `%s' is not positive number"), optarg);
			break;
		case 'c':
			flags |= FREE_REPEAT;
			flags |= FREE_REPEATCOUNT;
			args.repeat_counter = strtol_or_err(optarg,
				_("failed to parse count argument"));
			if (args.repeat_counter < 1)
			  error(EXIT_FAILURE, ERANGE,
				  _("failed to parse count argument: '%s'"), optarg);
			break;
		case HELP_OPTION:
			usage(stdout);
		case 'V':
			printf(PROCPS_NG_VERSION);
			exit(EXIT_SUCCESS);
		default:
			usage(stderr);
		}

	do {

		meminfo();
		/* Translation Hint: You can use 9 character words in
		 * the header, and the words need to be right align to
		 * beginning of a number. */
		printf("%s\n", _("             total       used       free     shared    buffers     cached"));
		printf("%-7s", _("Mem:"));
		printf(" %10s", scale_size(kb_main_total, flags, args));
		printf(" %10s", scale_size(kb_main_used, flags, args));
		printf(" %10s", scale_size(kb_main_free, flags, args));
		printf(" %10s", scale_size(kb_main_shared, flags, args));
		printf(" %10s", scale_size(kb_main_buffers, flags, args));
		printf(" %10s", scale_size(kb_main_cached, flags, args));
		printf("\n");
		/*
		 * Print low vs. high information, if the user requested it.
		 * Note we check if low_total == 0: if so, then this kernel
		 * does not export the low and high stats. Note we still want
		 * to print the high info, even if it is zero.
		 */
		if (flags & FREE_LOHI) {
			printf("%-7s", _("Low:"));
			printf(" %10s", scale_size(kb_low_total, flags, args));
			printf(" %10s", scale_size(kb_low_total - kb_low_free, flags, args));
			printf(" %10s", scale_size(kb_low_free, flags, args));
			printf("\n");

			printf("%-7s", _("High:"));
			printf(" %10s", scale_size(kb_high_total, flags, args));
			printf(" %10s", scale_size(kb_high_total - kb_high_free, flags, args));
			printf(" %10s", scale_size(kb_high_free, flags, args));
			printf("\n");
		}

		if (!(flags & FREE_OLDFMT)) {
			unsigned KLONG buffers_plus_cached = kb_main_buffers + kb_main_cached;
			printf(_("-/+ buffers/cache:"));
			printf(" %10s",
			       scale_size(kb_main_used - buffers_plus_cached, flags, args));
			printf(" %10s",
			       scale_size(kb_main_free + buffers_plus_cached, flags, args));
			printf("\n");
		}
		printf("%-7s", _("Swap:"));
		printf(" %10s", scale_size(kb_swap_total, flags, args));
		printf(" %10s", scale_size(kb_swap_used, flags, args));
		printf(" %10s", scale_size(kb_swap_free, flags, args));
		printf("\n");

		if (flags & FREE_TOTAL) {
			printf("%-7s", _("Total:"));
			printf(" %10s", scale_size(kb_main_total + kb_swap_total, flags, args));
			printf(" %10s", scale_size(kb_main_used + kb_swap_used, flags, args));
			printf(" %10s", scale_size(kb_main_free + kb_swap_free, flags, args));
			printf("\n");
		}
		fflush(stdout);
		if (flags & FREE_REPEATCOUNT) {
			args.repeat_counter--;
			if (args.repeat_counter < 1)
				exit(EXIT_SUCCESS);
		}
		if (flags & FREE_REPEAT) {
			printf("\n");
			usleep(args.repeat_interval);
		}
	} while ((flags & FREE_REPEAT));

	exit(EXIT_SUCCESS);
}
