#include <stdio.h>
// #include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "os_defs.h"

#if defined ( OS_UNIX )
#include <unistd.h>
#endif

#if defined ( OS_WINDOWS )
/* getopt windows port by superwills
*/
#include "getopt.h"
int     opterr = 1,             /* if error message should be printed */
        optind = 1,             /* index into parent argv vector */
        optopt,                 /* character checked for validity */
        optreset;               /* reset getopt */
char    *optarg;                /* argument associated with option */

#define BADCH   (int)'?'
#define BADARG  (int)':'
#define EMSG    ""

/*
 * getopt --
 *      Parse argc/argv argument vector.
 */
int getopt(int nargc, char * const nargv[], const char *ostr) {
  static char *place = EMSG;              /* option letter processing */
  const char *oli;                              /* option letter list index */

  if (optreset || !*place) {              /* update scanning pointer */
    optreset = 0;
    if (optind >= nargc || *(place = nargv[optind]) != '-') {
      place = EMSG;
      return (-1);
    }
    if (place[1] && *++place == '-') {      /* found "--" */
      ++optind;
      place = EMSG;
      return (-1);
    }
  }                                       /* option letter okay? */
  if ((optopt = (int)*place++) == (int)':' ||
    !(oli = strchr(ostr, optopt))) {
      /*
      * if the user didn't specify '-' as an option,
      * assume it means -1.
      */
      if (optopt == (int)'-')
        return (-1);
      if (!*place)
        ++optind;
      if (opterr && *ostr != ':')
        (void)printf("illegal option -- %c\n", optopt);
      return (BADCH);
  }
  if (*++oli != ':') {                    /* don't need argument */
    optarg = NULL;
    if (!*place)
      ++optind;
  }
  else {                                  /* need an argument */
    if (*place)                     /* no white space */
      optarg = place;
    else if (nargc <= ++optind) {   /* no arg */
      place = EMSG;
      if (*ostr == ':')
        return (BADARG);
      if (opterr)
        (void)printf("option requires an argument -- %c\n", optopt);
      return (BADCH);
    }
    else                            /* white space */
      optarg = nargv[optind];
    place = EMSG;
    ++optind;
  }
  return (optopt);                        /* dump back option letter */
}
#endif

#include "squash_run.h"

static int
printHelp(char *progname)
{
	printf("%s <options> [ <files> ]\n", progname);
	printf("\n");
	printf("Run scripts from files, or stdin if no file specified\n");
	printf("\n");
	printf("Options:\n");
	printf("-o <file> : place output in <file>\n");
	printf("-v        : be more verbose\n");
	printf("\n");
	exit (1);
}

int
main(int argc, char **argv)
{
	FILE *ofp = stdout;
	FILE *promptfp = stdout;
	int verbosity = 0;
	int status = 0;
	int i, ch;

	while ((ch = getopt(argc, argv, "hvo:")) != -1) {
		switch (ch) {
		case 'v':
			verbosity++;
			break;

		case 'o':
			if ((ofp = fopen(optarg, "w")) == NULL) {
				(void) fprintf(stderr,
						"failed opening output file '%s' : %s\n",
						optarg, strerror(errno));
				exit(-1);
			}
      #if defined ( OS_UNIX )
      dup2(fileno(ofp), STDOUT_FILENO);
			#endif
      break;

		case '?':
		case 'h':
		default:
			printHelp(argv[0]);
			break;
		}
	}

	/*
	 * skip arguments processed by getopt -- note that the first
	 * remaining item in argv is now in position 0
	 */
	argc -= optind;
	argv += optind;

	if (argc > 0) {
		for (i = 0; i < argc; i++) {
			status = runScriptFile(ofp, promptfp, argv[i], verbosity);
			if (status != 0)	exit (status);
		}
		exit (0);
	} 

	if (runScript(ofp, promptfp, stdin, "stdin", verbosity) < 0)
		return (-1);

	return 0;
}
