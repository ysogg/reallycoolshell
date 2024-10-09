#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ctype.h>
#include <regex.h>

#include "os_defs.h"

#include "squash_run.h"
#include "squash_tokenize.h"


typedef struct {
	char name[30];
	char value[30][30];
} VarDec;

VarDec VarList[MAX_VARS];
int ListIndex = 0;

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
	if (WIFEXITED(status)) {
		int exitStatus = WEXITSTATUS(status);
			if (exitStatus == 0) {
				printf("Child(%d) exited -- success (%d)\n", pid, exitStatus);
			} else {
				printf("Child(%d) exited -- failure (%d)\n", pid, exitStatus);
			}
		} else {
			printf("Child(%d) did not exit (crashed?)\n", pid);
		}
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
		char pipedCmd[1][100];
		char *execCmd[100];
		int count = 0;

		//Remember
		strcpy(pipedCmd[0], "");

		for (int j = toksSincePipe; j < nTokens; j++) {
			if (strcmp(tokens[j], "|") == 0 || strcmp(tokens[j], "\0") == 0) {
				toksSincePipe=j+1;
				break;
			} else {
				strcat(pipedCmd[0], tokens[j]);
				strcat(pipedCmd[0], " ");
				toksSincePipe++;
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

			statusMessageHandler(pid, status);
		}

	}
	return 1;
}

int assignVariable(char ** const tokens, int nTokens, int tokPos) {
	char *varName = tokens[tokPos-1];
	char varValue[30][30];
	int valueLength = nTokens - tokPos;
	regex_t pattern;

	if (regcomp(&pattern, "[A-Za-z_][A-Za-z0-9_]*", 0) != 0) {
		printf("\nregex err\n");
		exit(1);
	}

	if (regexec(&pattern, varName, 0, NULL, 0) != 0) {
		printf("\nbad variable name\n");
		return 1;
	}

	VarDec newVar;
	strcpy(newVar.name, varName);
	for (int i = 0; i < valueLength; i++) {
		if (tokPos + 1 == nTokens || tokens[tokPos + 1] == NULL) {
			break;
		}
		strcpy(newVar.value[i], tokens[tokPos + 1]);
		tokPos++;
	}
	strcpy(newVar.value[tokPos], "\0");
	VarList[ListIndex] = newVar;
	ListIndex++;

/*	printf("Testing save\n");
	for (int i = 0; i < ListIndex; i++) {
		if (VarList[i].name != NULL) {
			printf("%s: ", VarList[i].name);
			for (int j = 0; j < valueLength; j++) {
				printf("%s ", VarList[i].value[j]);
			}
			printf("\n");
		}
	}*/
	return 0;
}

char* subVariable(char holdToken[30], int start, int end) {
		char varNameLookup[30];
		char tmpStr[30];
		char duplicateToken[30];
		char holdBackHalf[30];
		int subIndex = -1;

		start = start+2;
		strcpy(duplicateToken, holdToken);
		duplicateToken[strlen(duplicateToken) - end] = '\0';
		strncpy(holdBackHalf, holdToken + end + 1, strlen(holdToken) - end);
		holdToken[end] = '\0';
		strncpy(varNameLookup, holdToken + start, end - start + 1);

		//LOOKUP
		for (int i = 0; i < ListIndex; i++) {
			if (strcmp(varNameLookup, VarList[i].name) == 0) {
				subIndex = i;
				strcpy(tmpStr, VarList[i].value[0]);
				for (int j = 1; j < sizeof(VarList[i].value) / sizeof(char[20]); j++) {
					if (strcmp(VarList[i].value[j], "\0") == 0) {
						break;
					}
					strcat(tmpStr, " ");
					strcat(tmpStr, VarList[i].value[j]);
				}
				strcat(tmpStr, holdBackHalf);
				holdToken[start-2] = '\0';
				strcat(holdToken, tmpStr);
			}
		}
		if (subIndex == -1) {
			//Rebuild token
			strcpy(holdToken, duplicateToken);
			strcat(holdToken, "{");
			strcat(holdToken, varNameLookup);
			strcat(holdToken, "}");
			strcat(holdToken, holdBackHalf);
		}

	return holdToken;
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
	int varAssign = 0;
	int tokToSub = -1;
	int confirmProperVar = 0;
	int numVars = 0;
	char holdToken[30];
	int startOfVar = 0;
	int endOfVar = 0;

	for (int i = 0; i < nTokens; i++) {
		if (strcmp(tokens[i], "=") == 0) {
			varAssign = i;
			break;
		}
	}

	for (int i = 0; i < nTokens; i++) {
		if (varAssign == 0) {
			for (int j = 0; j < strlen(tokens[i]); j++) {
				if (tokens[i][j] == '$') {
					if (tokens[i][j+1] == '{') {
						startOfVar = j;
						strcpy(holdToken, tokens[i]);

						for (int k = j; k < strlen(tokens[i]); k++) {
							if (tokens[i][k] == '}') {
								//Fill this var
								endOfVar = k;
								strcpy(holdToken, subVariable(holdToken, startOfVar, endOfVar));
								break;
							}
						}
					} else {
						printf("Bad variable substitution format\n");
						return 1;
					}
				strcpy(tokens[i], holdToken);
				strcpy(holdToken, "");
				}
			}
		}
	}

	//Output tokens in quotes
	for (int i = 0; i < nTokens; i++) {
		if (i == nTokens-1) {
			printf("\"%s\"", tokens[i]);
		} else {
			printf("\"%s\" ", tokens[i]);
		}
	}
	printf("\n");

	//Store vars as name string : token list with n elements
	if (varAssign > 0) {
		assignVariable(tokens, nTokens, varAssign);
		return 0;
	}


	// Custom cmds
	int numOfCmds = 2;
	int manualNum = 0;
	char *manualCmds[numOfCmds];
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

	//Setup for if cmdline has pipes
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


	//Fork everything else normally
	pid_t pid = fork();
	if (pid < 0) {
		printf("\nFailed to fork :(\n");
		perror("fork");
		exit(1);
	} else if (pid == 0) {
		//Child
		if (execvp(tokens[0], tokens) < 0) {
			printf("\nCouldn't execute cmd\n");
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

