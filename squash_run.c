#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
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

int statusMessageHandler(pid_t pid, int status) {
printf("CHECK STATUS INT: %d\n", status);
	if (WIFEXITED(status)) {
		int exitStatus = WEXITSTATUS(status);
			if (exitStatus == 0) {
				printf("Child(%d) exited -- success (%d)\n", pid, exitStatus);
			} else {
				printf("Child(%d) exited -- failure (%d)\n", pid, exitStatus);
			}
		} else {
			printf("Child(%d) did not exit\n", pid);
		}
//		printf("Status: %d\n", pid);
		return 0;
}

int execPipedCommandLine(char ** const tokens, int nTokens, int numPipes) {
//Once it encounters the token '|' it should fork

	int pipefds[2];
	if (pipe(pipefds) < 0) {
		perror("pipe");
		exit(1);
	}

	int origIn = dup(STDIN_FILENO);
	int origOut = dup(STDOUT_FILENO);

	//Piping loop
	/* Concat all tokens for each pipe into a string
	 * Take the string and tokenise it into *execCmd[]
	 * Kind of roundabout but it works to remove pipe char from exec command
	 * Do this for each command (i.e. num pipe chars + 1)
	*/
	int toksSincePipe = 0;
	for (int i = 0; i < numPipes + 1; i++) {
//REPLACE pipedCmd with proper max args
		char pipedCmd[1][100];
		char *execCmd[100];
		int count = 0;

		//Remember
		strcpy(pipedCmd[0], "");

		for (int j = toksSincePipe; j < nTokens; j++) {
			if (strcmp(tokens[j], "|") == 0 || strcmp(tokens[j], "\0") == 0) {
				toksSincePipe=j+1;
				//strcpy(pipedCmd[count], "\0");
//				printf("%d\n", toksSincePipe);
				break;
			} else {
//				strcpy(pipedCmd[count], tokens[j]);
				//make pipedcmd one string->
				strcat(pipedCmd[0], tokens[j]);
				strcat(pipedCmd[0], " ");
				toksSincePipe++;
//				printf("%s \n", pipedCmd[count]);
				//count++;
			}
		}

		//Take prev and build exec command
		int x = 0;
		execCmd[0] = strtok(pipedCmd[0], " ");
		while (execCmd[x] != NULL) {
			execCmd[++x] = strtok(NULL, " ");
		}


		//Different file descriptor cases

		if (i == 0) {
			pipe(pipefds);
			if (dup2(pipefds[1], STDOUT_FILENO) < 0) {
				perror("dup");
				exit(1);
			}
			close(pipefds[1]);
		} else if (i == numPipes) {
			dup2(pipefds[0], STDIN_FILENO);
			dup2(origOut, 1);
		} else {
			dup2(pipefds[0], STDIN_FILENO);
			pipe(pipefds);
			if (dup2(pipefds[1], STDOUT_FILENO) < 0) {
				perror("dup");
				exit(1);
			}
			close(pipefds[1]);
		}

		//Start executing

		pid_t pid = fork();
		int status;

		if (pid < 0) {
			perror("fork");
			exit(1);
		} else if (pid == 0) {
			if (execvp(execCmd[0], execCmd) < 0) {
				perror("exec");
				exit(1);
			}
		} else {
			waitpid(pid, &status, 0);
			dup2(origIn, STDIN_FILENO);
			dup2(origOut, STDOUT_FILENO);

	// Exit status messages
			int status;
			waitpid(pid, &status, 0);
//These freaks are not exiting properly, but piping does work
printf("COMPARE STATUS INT: %d\n", status);
			statusMessageHandler(pid, status);
			/*if (WIFEXITED(status)) {
				int exitStatus = WEXITSTATUS(status);
				if (exitStatus == 0) {
					printf("Child(%d) exited -- success (%d)\n", pid, exitStatus);
				} else {
					printf("Child(%d) exited -- failure (%d)\n", pid, exitStatus);
				}
			} else {
				printf("Child(%d) did not exit", pid);
			}
			printf("Status: %d\n", pid);*/
		}

	}
	return 1;
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

	//Setup pipe
	int numPipes = 0;
	for (int i = 0; i < nTokens; i++) {
		if (strcmp(tokens[i], "|") == 0) {
			numPipes++;
		}
	}
	if (numPipes > 0) {
		execPipedCommandLine(tokens, nTokens, numPipes);
		return 0;
	}


	//Fork everything else
	pid_t pid = fork();
	if (pid < 0) {
		printf("\nFailed to fork :(");
		perror("fork");
		exit(1);
	} else if (pid == 0) {
		//Child
		if (execvp(tokens[0], tokens) < 0) {
			printf("\nCouldn't execute cmd");
			perror("exec");
			exit(1);
		}
		exit(0);
	} else {
		//Parent

		// Exit status messages
		int status;
		waitpid(pid, &status, 0);
		statusMessageHandler(pid, status);
	/*if (WIFEXITED(status)) {
			int exitStatus = WEXITSTATUS(status);
			if (exitStatus == 0) {
				printf("Child(%d) exited -- success (%d)\n", pid, exitStatus);
			} else {
				printf("Child(%d) exited -- failure (%d)\n", pid, exitStatus);
			}
		} else {
			printf("Child(%d) did not exit", pid);
		}
		printf("Status: %d\n", pid);
		return 0;*/
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

