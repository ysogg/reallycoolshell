#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>

#include "os_defs.h"

#include "squash_run.h"
#include "squash_tokenize.h"


/**
 * Print a prompt if the input is coming from a TTY
 */
static void prompt(FILE *pfp, FILE *ifp)
{
	if (isatty(fileno(ifp))) {
		fputs(PROMPT_STRING, pfp);
	}
}

/**
 * Actually do the work
 */
int execFullCommandLine(
		FILE *ofp,
		char ** const tokens,
		int nTokens,
		int verbosity)
{
	if (verbosity > 0) {
		fprintf(stderr, " + ");
		fprintfTokens(stderr, tokens, 1);
	}

/** Now actually do something with this command, or command set */
	//Output cmds broken up as "x" "y" "z"
	for (int i = 0; i < nTokens; i++) {
		if (i == nTokens-1) {
			printf("\"%s\"", tokens[i]);
		} else {
			printf("\"%s\" ", tokens[i]);
		}
	}
	printf("\n");

	// Custom cmds
	int numOfCmds = 2;
	int manualNum = 0;
	char* manualCmds[numOfCmds];
	manualCmds[0] = "cd";
	manualCmds[1] = "exit";

	for (int i = 0; i < numOfCmds; i++) {
		if (strcmp(manualCmds[i], tokens[0]) == 0) {
			manualNum = i + 1;
		}
	}

	switch(manualNum) {
		case 1:
			chdir(tokens[1]);
			return 0;
		case 2:
			printf("Exiting...\n");
			exit(0);
		default:
			break;
	}


//Fork everything else
	pid_t pid = fork();
	if (pid == -1) {
		printf("\nFailed to fork :(");
		return 1;
	} else if (pid == 0) {
		if (execvp(tokens[0], tokens) < 0) {
			printf("\nCouldn't execute cmd");
		}
		exit(0);
	} else {
		wait(NULL);
		return 0;
	}

	return 1;
}

/**
 * Load each line and perform the work for it
 */
int
runScript(
		FILE *ofp, FILE *pfp, FILE *ifp,
		const char *filename, int verbosity
	)
{
	char linebuf[LINEBUFFERSIZE];
	char *tokens[MAXTOKENS];
	int lineNo = 1;
	int nTokens, executeStatus = 0;

	fprintf(stderr, "SHELL PID %ld\n", (long) getpid());

	prompt(pfp, ifp);
	while ((nTokens = parseLine(ifp,
				tokens, MAXTOKENS,
				linebuf, LINEBUFFERSIZE, verbosity - 3)) > 0) {
		lineNo++;

		if (nTokens > 0) {

			executeStatus = execFullCommandLine(ofp, tokens, nTokens, verbosity);

			if (executeStatus < 0) {
				fprintf(stderr, "Failure executing '%s' line %d:\n    ",
						filename, lineNo);
				fprintfTokens(stderr, tokens, 1);
				return executeStatus;
			}
		}
		prompt(pfp, ifp);
	}

	return (0);
}


/**
 * Open a file and run it as a script
 */
int
runScriptFile(FILE *ofp, FILE *pfp, const char *filename, int verbosity)
{
	FILE *ifp;
	int status;

	ifp = fopen(filename, "r");
	if (ifp == NULL) {
		fprintf(stderr, "Cannot open input script '%s' : %s\n",
				filename, strerror(errno));
		return -1;
	}

	status = runScript(ofp, pfp, ifp, filename, verbosity);
	fclose(ifp);
	return status;
}

