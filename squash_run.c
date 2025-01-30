#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
// #include <unistd.h>
#include <sys/types.h>
// #include <sys/wait.h>
#include <ctype.h>
// #include <regex.h>
// #include <glob.h>

#include "os_defs.h"

#include "squash_run.h"
#include "squash_tokenize.h"

#if defined ( OS_UNIX )
#include <unistd.h>
#include <sys/wait.h>
#include <regex.h>
#include <glob.h>
#endif

typedef struct {
	char name[LINEBUFFERSIZE];
	char value[MAXTOKENS][LINEBUFFERSIZE];
} VarDec;

VarDec VarList[MAX_VARS];
int ListIndex = 0;
char buffer[LINEBUFFERSIZE];
int BG_Count = 0;

/* TODO
 * Glob: DONE
 * Redirect: DONE
 * BG:
 * - Fix output to be in line with bash
    -> This means cleaning up when executing another cmd
 * Port:
 * - Check if redirect works
 * - Check if var sub works (only diff should be no regex.h on win)
 * - Get BG to work
*/

/**
 * Print a prompt if the input is coming from a TTY
 */
static void prompt(FILE *pfp, FILE *ifp)
{
	if (isatty(fileno(ifp))) {
		fputs(PROMPT_STRING, pfp);
	}
}

#if defined ( OS_UNIX )
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

void catchBG() {
	pid_t pid;
	int status;
	while (BG_Count > 0) {
		pid = waitpid(-1, &status, 0);
		if (pid > 0) {
			BG_Count--;
			printf("Ended bg\n");
			statusMessageHandler(pid, status);
		}
	}
}

void runInBackground(char** tokens, int nTokens) {
	BG_Count++;
	pid_t pid = fork();
	pid_t sub;
	int status;

	if (pid < 0) {
		perror("fork");
		return;
	} else if (pid == 0) {
		//nice to have
		// if (BG_Count > 0) {
		// 	waitpid(-1, &status, WNOHANG);
		// }
		
		// printf("QUick\n");
		// for (int i = 0; i < nTokens; i++) {
		// 	printf("chk: %s\n", tokens[i]);
		// }
		
		if (execvp(tokens[0], tokens) < 0) {
			perror("exec");
			exit(1);
		} 
		printf("\n");
		// waitpid(-1, &status, WNOHANG);
		exit(0);
	} else {
		// while (1) {
		// 	sub = waitpid(-1, &status, WNOHANG);
		// 	if (sub == 0 || sub == 1) {
		// 		break;
		// 	}
		// }
		// statusMessageHandler(pid, status);
		//maybe while loop in parent signal handler may be overkill for this
	}
}
#endif


int assignVariable(char ** const tokens, int nTokens, int tokPos) {
	char *varName = tokens[tokPos-1];
	char varValue[MAXTOKENS][LINEBUFFERSIZE];
	int valueLength = nTokens - tokPos;
	#if defined ( OS_UNIX )
	regex_t pattern;

	if (regcomp(&pattern, "[A-Za-z_][A-Za-z0-9_]*", 0) != 0) {
		printf("\nregex err\n");
		exit(1);
	}

	if (regexec(&pattern, varName, 0, NULL, 0) != 0) {
		printf("\nbad variable name\n");
		return 1;
	}
	#endif

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
	return 0;

	// fprintf(stderr, "Testing save\n");
	// for (int i = 0; i < ListIndex; i++) {
	// 	if (VarList[i].name != NULL) {
	// 		printf("%s: ", VarList[i].name);
	// 		for (int j = 0; j < valueLength; j++) {
	// 			printf("%s ", VarList[i].value[j]);
	// 		}
	// 		printf("\n");
	// 	}
	// }
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

#if defined ( OS_UNIX )
char** newGlob(char** tokens, int globPos, int nTokens, int* retLen) {
	int newLen;
	int numFills = 0;
	
	glob_t globbuf;
	globbuf.gl_offs = 0;
	glob(tokens[globPos], GLOB_DOOFFS, NULL, &globbuf);

	for (int i = 0; i < nTokens; i++) {
		if (strcmp(tokens[i], tokens[globPos]) == 0) {
			numFills++;
		}
	}

	newLen = ((nTokens-numFills) + (numFills * globbuf.gl_pathc));

	int tmpIndex = 0;
	char** tmpTok = malloc(newLen * sizeof(char*));
	for (int i = 0; i < nTokens; i++) {
		if (strcmp(tokens[i], tokens[globPos]) == 0) {
			for (int j = 0; j < globbuf.gl_pathc; j++) {
				tmpTok[tmpIndex] = malloc((strlen(globbuf.gl_pathv[j])+1) * sizeof(char));
				strcpy(tmpTok[tmpIndex], globbuf.gl_pathv[j]);
				tmpIndex++;
			}	
		} else {
			tmpTok[tmpIndex] = malloc((strlen(tokens[i])+1) * sizeof(char));
			strcpy(tmpTok[tmpIndex], tokens[i]);
			tmpIndex++;
		}	
	}

	globfree(&globbuf);

	*retLen = newLen;
	return tmpTok;
}
#endif

#if defined ( OS_UNIX )
void redirection(char** tokens, int direction ,int pos) {
	FILE* fp; 

	if (tokens[pos+1] != NULL) {
		fp = fopen(tokens[pos+1], "r+");
		if (fp == NULL) {
			printf("Invalid filename at cmd: %d\n", pos+1);
			exit(1);
		}
		if (direction == 0) {
			dup2(fileno(fp), STDIN_FILENO);
		} else if (direction == 1) {
			dup2(fileno(fp), STDOUT_FILENO);
		}
		tokens[pos] = NULL;
		tokens[pos+1] = NULL;
	}
	fclose(fp);
}
#endif

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

	#if defined ( OS_UNIX )
	int origIn = dup(STDIN_FILENO);
	int origOut = dup(STDOUT_FILENO);
	#endif

	// strcpy(buffer, "");
	//Make a copy of tokens so that globbing can modify token list
	char** tokensCpy = malloc((nTokens+1) * sizeof(char*));
	for (int i = 0; i < nTokens; i++) {
		tokensCpy[i] = malloc((strlen(tokens[i])+1) * sizeof(char));
		strcpy(tokensCpy[i], tokens[i]);
	}
	tokensCpy[nTokens] = NULL;
	
	// -- VAR ASSIGN -- //
	for (int i = 0; i < nTokens; i++) {
		if (strcmp(tokens[i], "=") == 0) {
			varAssign = i;
			break;
		}
	}

	// printf("1\n");
	
	#if defined ( OS_UNIX )
	// -- GLOB -- //
	if (varAssign == 0) {
		for (int i = 0; i < nTokens; i++) {
			if ( (strstr(tokensCpy[i], "*") != NULL) || (strstr(tokensCpy[i], "?") != NULL) || 
				(strstr(tokensCpy[i], "[") != NULL && strstr(tokensCpy[i], "]") != NULL)) {
				// nTokens = newGlob(tokensCpy, i, nTokens);
				int newLen;
				char** tmpTok = newGlob(tokensCpy, i, nTokens, &newLen);

				tokensCpy = realloc(tokensCpy, (newLen+1)*sizeof(char*));
				for (int i = 0; i < newLen; i++) {
					tokensCpy[i] = malloc((strlen(tmpTok[i])+1) * sizeof(char));
					strcpy(tokensCpy[i], tmpTok[i]);
				}
				tokensCpy[newLen] = NULL;
				nTokens = newLen;
			
			//Cleanup tmpTok mem
			for (int i = 0; i < nTokens; i++) {
				if (tmpTok[i] != NULL) {
					free(tmpTok[i]);
				}
			}
			free(tmpTok);
			}
		}
	}
	#endif

	// printf("2\n");
	//-- VAR SUB -- //
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
		// #if defined ( OS_UNIX )
		fprintf(stderr, "TEST1\n");
		assignVariable(tokensCpy, nTokens, varAssign);
		// #endif
		return 0;
	}

	//Check if it should be run in background process
	int backgroundCheck = 0;
	if (tokensCpy[nTokens-1][0] == '&' && strlen(tokensCpy[nTokens-1]) == 1) {
		tokensCpy[nTokens-1] = NULL;
		nTokens--;
		#if defined ( OS_UNIX )
		runInBackground(tokensCpy, nTokens);
		#endif
		backgroundCheck = 1;
	}


	// Custom cmds
	int numOfCmds = 2;
	int manualNum = 0;
	char *manualCmds[2] = {"cd", "exit"};
	// manualCmds[0] = "cd";
	// manualCmds[1] = "exit";

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
			#if defined ( OS_UNIX )
			catchBG();
			#endif
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

	#if defined ( OS_UNIX )
	// -- Redirection -- //
	int redirIn = 0;
	int redirOut = 0;
	int trackPipes = 0;
	int updateNTokens = -1;
	if (numPipes > 0) {
		for (int i = 0; i < nTokens; i++) {
			//Keep track of num pipes passed
			if (strcmp(tokensCpy[i], "|")) {
				trackPipes++;
			} else {
				if (strcmp(tokensCpy[i], "<") == 0) {
					redirIn++;
					//Redirect before pipe, improper format
					if (trackPipes < numPipes) {
						printf("Improper redirect format\n");
						exit(1);
					}
					if (redirIn > 1) {
						printf("Too many redirect chars\n");
						exit(1);
					} else {
						if (updateNTokens == -1) updateNTokens = i-2;
						#if defined ( OS_UNIX )
						redirection(tokensCpy, 0, i);
						#endif
						#if defined ( OS_WINDOWS )
						winRedirection(tokensCpy, 0, i);
						#endif
						i++;
					}
				} else if (strcmp(tokensCpy[i], ">") == 0) {
					redirOut++;
					//Redirect before pipe, improper format
					if (trackPipes < numPipes) {
						printf("Improper redirect format\n");
						exit(1);
					}
					if (redirOut > 1) {
						printf("Too many redirect chars\n");
						exit(1);
					} else {
						if (updateNTokens == -1) updateNTokens = i-2;
						#if defined ( OS_UNIX )
						redirection(tokensCpy, 1, i);
						#endif
						#if defined ( OS_WINDOWS )
						winRedirection(tokensCpy, 1, i);
						#endif
						i++;
					}
				}

			}
		}
	} else {
		for (int i = 0; i < nTokens; i++) {
			if (strcmp(tokensCpy[i], "<") == 0) {
				redirIn++;
				if (redirIn > 1) {
					printf("Too many redirect chars\n");
					exit(1);
				} else {
					if (updateNTokens == -1) updateNTokens = i-2;
					#if defined ( OS_UNIX )
					redirection(tokensCpy, 0, i);
					#endif
					#if defined ( OS_WINDOWS )
					winRedirection(tokensCpy, 0, i);
					#endif
					i++;
				}
			} else if (strcmp(tokensCpy[i], ">") == 0) {
				redirOut++;
				if (redirOut > 1) {
					printf("Too many redirect chars\n");
					exit(1);
				} else {
					if (updateNTokens == -1) updateNTokens = i-2;
					#if defined ( OS_UNIX )
					redirection(tokensCpy, 1, i);
					#endif
					#if defined ( OS_WINDOWS )
					winRedirection(tokensCpy, 1, i);
					#endif
					i++;
				}
			}
		}
	}
	if (updateNTokens != -1) {
		nTokens = updateNTokens;
	}
	#endif

	if (numPipes > 0) {
		#if defined ( OS_UNIX )
		execPipedCommandLine(tokensCpy, nTokens, numPipes);
		#endif
		return 0;
	}

	// printf("Check token list b4 exec\n");
	// for(int i = 0; i < nTokens; i++) {
	// 	printf("tok: %s\n", tokensCpy[i]);
	// }
	#if defined ( OS_UNIX )
	//Fork everything else normally
	if (backgroundCheck == 0) {
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
				free(tokensCpy);
				exit(1);
			}
			exit(0);
		} else {
			//Parent
			for (int i = 0; i < nTokens; i++) {
				if (tokensCpy[i] != NULL) {
					free(tokensCpy[i]);
				}
			}
			free(tokensCpy);
			// Exit status messages
			dup2(origIn, STDIN_FILENO);
			dup2(origOut, STDOUT_FILENO);
			int status;
			waitpid(pid, &status, 0);
			statusMessageHandler(pid, status);
		}
	}
	#endif

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

