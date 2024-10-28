#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ctype.h>
#include <regex.h>
#include <glob.h>

#include "os_defs.h"

#include "squash_run.h"
#include "squash_tokenize.h"


typedef struct {
	char name[LINEBUFFERSIZE];
	char value[MAXTOKENS][LINEBUFFERSIZE];
} VarDec;

VarDec VarList[MAX_VARS];
int ListIndex = 0;
char *buffer;

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
/* FIX - Open fds after execPipedCommandLine
 * view with valgrind --track-fds=yes
 * need to close open fds
*/
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
		char pipedCmd[1][LINEBUFFERSIZE];
		char *execCmd[LINEBUFFERSIZE];
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
	char varValue[MAXTOKENS][LINEBUFFERSIZE];
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

char* subVariable(char holdToken[LINEBUFFERSIZE], int start, int end) {
		char varNameLookup[LINEBUFFERSIZE];
		char tmpStr[LINEBUFFERSIZE];
		char duplicateToken[LINEBUFFERSIZE];
		char holdBackHalf[LINEBUFFERSIZE];
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

// Shouldn't run on windows
// Returns new list length
char** insertGlobbedTokens(char ** tokens, int* nTokens, int globTok) {
	/* FIX
	 * Confirm what wildcards it needs to match for suresies
	 * Doesn't work on second pass in same shell instance
	*/
	printf("glob tok %d\n", globTok);
	printf("ntok: %d\n", (*nTokens));
	glob_t globbuf;
	glob(tokens[globTok], GLOB_DOOFFS, NULL, &globbuf);

	for (int i = 0; i < globbuf.gl_pathc; i++) {
		printf("%s\n", globbuf.gl_pathv[i]);
	}

	//Copy up to glob tok
	int newLen = (*nTokens)-1 + globbuf.gl_pathc;
	char** tempTokens = malloc(newLen * sizeof(char*));
	printf("globo: %d\n", globTok);
	for (int i = 0; i < globTok; i++) {
		tempTokens[i] = (char*)malloc(strlen(tokens[i]+1));
		strcpy(tempTokens[i], tokens[i]);
		printf("t: %s\n", tempTokens[i]);
	}

	//Insert new tokens in place of globbed string
	for (int i = 0; i < globbuf.gl_pathc; i++) {
		tempTokens[globTok + i] = (char*)malloc(strlen(globbuf.gl_pathv[i]+1));
		strcpy(tempTokens[globTok + i], globbuf.gl_pathv[i]);
	}

//doesn't work on a second pass
	//Copy everything after glob tok
	if (globTok != (*nTokens)-1) {
		int count = 0;
			for (int i = globTok+1; i < (*nTokens); i++) {
				count++;
				tempTokens[count + globbuf.gl_pathc] = (char*)malloc(strlen(tokens[i]+1));
				strcpy(tempTokens[count + globbuf.gl_pathc], tokens[i]);
			}
	}

for (int i = 0; i < newLen; i++) {
		printf("Copied String: %s\n", tempTokens[i]);
	}

	globfree(&globbuf);

	*nTokens = newLen;
	return tempTokens;
}

int globTest(int globTok, int nTokens) {
	char** tokens = malloc(nTokens * sizeof(char*));
	char** tokensBackup = malloc(nTokens * sizeof(char*));
	tokensBackup = tokens; //may not need toksBackup, prob was just the loop, confirm later
	loadTokens(tokens, nTokens, buffer, 0);

	printf("Check tokens\n");
	for (int i = 0; i < nTokens; i++) {
		printf("toks: %s\n", tokens[i]);
	}
	printf("buf#: %d\n", nTokens);

	glob_t globbuf;
	printf("check tok: %s\n", tokens[globTok]);
	glob(tokens[globTok], GLOB_DOOFFS, NULL, &globbuf);

	printf("Check glob\n");
	for (int i = 0; i < globbuf.gl_pathc; i++) {
		printf("%s\n", globbuf.gl_pathv[i]);
	}
	
	printf("Check tokens backup\n");
	for (int i = 0; i < nTokens; i++) {
		printf("toksb: %s\n", tokensBackup[i]);
	}

	printf("CHECK TOKS AT POS 0: %s\n", tokensBackup[0]);

	//clear buf
	*buffer = 0;
	int newLen = nTokens-1 + globbuf.gl_pathc;
	for (int i = 0; i < globTok; i++) {
		if (i==0) {
			printf("in buffer: %s\n", buffer);
			printf("toks at i: %s\n", tokensBackup[i]);
			printf("toks at 0: %s\n", tokensBackup[0]);
			strcpy(buffer, tokensBackup[i]);
			printf("aft buffer: %s\n", buffer);
		} else {
			strcat(buffer, " ");
			strcat(buffer, tokensBackup[i]);
		}
	}

	printf("curr buf: %s\n", buffer);

	for (int i = 0; i < globbuf.gl_pathc; i++) {
		strcat(buffer, " ");
		strcat(buffer, globbuf.gl_pathv[i]);
	}

	printf("curr buf: %s\n", buffer);
	printf("CONFIRM nTokens: %d\n", nTokens);
	if (globTok < nTokens-1) {
		for (int i = globTok; i < nTokens-1; i++) {
			strcat(buffer, " ");
			strcat(buffer, tokensBackup[i]);
		}
	}

	printf("CHECK\n");
	for (int i = 0; i < nTokens; i++) {
		printf("tokens backup: %s\n", tokensBackup[i]);
	}
	printf("newlen#: %d\n", newLen);
	printf("buf b4 return: %s\n", buffer);

	return newLen;
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
	char holdToken[LINEBUFFERSIZE];
	int startOfVar = 0;
	int endOfVar = 0;

	buffer = malloc(LINEBUFFERSIZE * sizeof(char));

	//Make a copy of tokens so that globbing can modify token list
	char** tokensCpy = malloc(nTokens * sizeof(char*));
	for (int i = 0; i < nTokens; i++) {
		tokensCpy[i] = (char*)malloc(strlen(tokens[i]+1));
		strcpy(tokensCpy[i], tokens[i]);
		printf("Copied String: %s\n", tokensCpy[i]);
	}
	printf("buf: %s\n", buffer);
	// memcpy(buffer, tokensToString(buffer, LINEBUFFERSIZE, tokensCpy, 0), LINEBUFFERSIZE);
	buffer = tokensToString(buffer, LINEBUFFERSIZE, tokens, 0);
	printf("Orig buf: %s\n", buffer);
	

	for (int i = 0; i < nTokens; i++) {
		if (strcmp(tokens[i], "=") == 0) {
			varAssign = i;
			break;
		}
	}

	if (varAssign == 0) {
		for (int i = 0; i < nTokens; i++) {
			if (strstr(tokensCpy[i], "*") != NULL) {
				// tokensCpy = insertGlobbedTokens(tokensCpy, &nTokens, i);
				nTokens = globTest(i, nTokens);
				printf("final buf: %s\n", buffer);
				loadTokens(tokensCpy, nTokens, buffer, 0);
				break;
			}
		}
	}

	for (int i = 0; i < nTokens; i++) {
		printf("TEST: %s\n", tokensCpy[i]);
	}
	

	for (int i = 0; i < nTokens; i++) {
		if (varAssign == 0) {
			for (int j = 0; j < strlen(tokensCpy[i]); j++) {
				if (tokensCpy[i][j] == '$') {
					if (tokensCpy[i][j+1] == '{') {
						startOfVar = j;
						strcpy(holdToken, tokensCpy[i]);

						for (int k = j; k < strlen(tokensCpy[i]); k++) {
							if (tokensCpy[i][k] == '}') {
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
				strcpy(tokensCpy[i], holdToken);
				strcpy(holdToken, "");
				}
			}
		}
	}

	//Output tokens in quotes
	for (int i = 0; i < nTokens; i++) {
		if (i == nTokens-1) {
			printf("\"%s\"", tokensCpy[i]);
		} else {
			printf("\"%s\" ", tokensCpy[i]);
		}
	}
	printf("\n");

	//Store vars as name string : token list with n elements
	if (varAssign > 0) {
		assignVariable(tokensCpy, nTokens, varAssign);
		return 0;
	}


	// Custom cmds
	int numOfCmds = 2;
	int manualNum = 0;
	char *manualCmds[numOfCmds];
	manualCmds[0] = "cd";
	manualCmds[1] = "exit";

	for (int i = 0; i < numOfCmds; i++) {
		if (strcmp(manualCmds[i], tokensCpy[0]) == 0) {
			manualNum = i + 1;
		}
	}

	switch(manualNum) {
		case 1:
			chdir(tokensCpy[1]);
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
		if (strcmp(tokensCpy[i], "|") == 0) {
			numPipes++;
		}
	}

	if (numPipes > 0) {
		execPipedCommandLine(tokensCpy, nTokens, numPipes);
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
		if (execvp(tokensCpy[0], tokensCpy) < 0) {
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

