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

#if defined ( OS_WINDOWS )
#include <windows.h>
#include <stdio.h>
#include <tchar.h>
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
 * Glob:
 * - Works for most stuff
 * - Fix echo *.c *.c (segfault when looping over multiple times)
 * - Also mem access issues I believe
 * Redirect: DONE
 * BG:
 * - Fix output to be in line with bash
    -> This means cleaning up when executing another cmd
 * Port:
 * - Check if redirect works
 * - Check if var sub works
 * - Piped executables -> piping printsomething.exe into the other exe
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

#if defined ( OS_WINDOWS )
void winExec(char** tokens) {
	STARTUPINFO si;
    PROCESS_INFORMATION pi;

	// ZeroMemory( &si, sizeof(si) );
	memset(&si, 0, sizeof(STARTUPINFO));
    si.cb = sizeof(si);
    // ZeroMemory( &pi, sizeof(pi) );
	memset(&pi, 0, sizeof(PROCESS_INFORMATION));

	strcpy(buffer, "");
	strcpy(buffer, tokensToString(buffer, LINEBUFFERSIZE, tokens, 0));

//background?
	// if (!CreateProcess(NULL, (LPSTR)buffer, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
	// 	printf("Create Process failed (%d)\n", GetLastError());
	// 	exit(1);
	// }

	if (!CreateProcess(NULL, (LPSTR)buffer, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
		printf("Create Process failed (%d)\n", GetLastError());
		exit(1);
	}
//also need to run messagehandler for windows execs
	WaitForSingleObject(pi.hProcess, INFINITE);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
}
#endif

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

#if defined ( OS_UNIX ) //only for regex, go in and adjust this func later
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
#endif

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
int globTest(char** tokens, int globTok, int nTokens) {
	/*
	* Copy up to globtok-1 into buffer
	* Copy globbed shit into buffer
	* Copy trailing into buffer
	* copy newline into buffer
	* tokenize
	*/
	// char** tokens = malloc((nTokens+1) * sizeof(char*));
	// loadTokens(tokens, 512, buffer, 0);
	strcpy(buffer, tokensToString(buffer, LINEBUFFERSIZE, tokens, 0));
	strcat(buffer, "\n");

	for (int i = 0; i < nTokens; i++) {
		printf("tok: %s\n", tokens[i]);
	}
	char* tokensBackup[nTokens - globTok - 1];
	// for (int i = 0; i < nTokens - globTok - 1; i++) {
	// 	strcpy(tokensBackup[i], tokens[globTok + i + 1]);
	// }

	strcpy(buffer, tokens[0]);
	for (int i = 1; i < globTok; i++) {
		strcat(buffer, " ");
		strcat(buffer, tokens[i]);
	}

	glob_t globbuf;
	globbuf.gl_offs = 0;
	glob(tokens[globTok], GLOB_DOOFFS, NULL, &globbuf);

	// for (int i = 0; i < nTokens; i++) {
	// 	printf("tok: %s\n", tokens[i]);
	// }

	int newLen = nTokens-1 + globbuf.gl_pathc;
	// for (int i = 0; i < globTok; i++) {
	// 	if (i==0) {
	// 		strcpy(buffer, tokens[i]);
	// 	} else {
	// 		strcat(buffer, " ");
	// 		strcat(buffer, tokens[i]);
	// 	}
	// }

	printf("Check buf: %s\n", buffer);

	for (int i = 0; i < globbuf.gl_pathc; i++) {
		strcat(buffer, " ");
		strcat(buffer, globbuf.gl_pathv[i]);
	}

	printf("Check buf2: %s\n", buffer);

	if (globTok < (nTokens-1)) {
		for (int i = globTok+1; i < (nTokens); i++) {
			strcat(buffer, " ");
			strcat(buffer, tokens[i]);
		}
	}
	strcat(buffer, "\n");
	strcat(buffer, "\0");

	printf("buf b4 return: %s", buffer);
	loadTokens(tokens, 512, buffer, 0);

	// tokens[newLen] = NULL;

	globfree(&globbuf);
	return newLen;
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

	strcpy(buffer, "");
	//Make a copy of tokens so that globbing can modify token list
	char** tokensCpy = malloc((nTokens+1) * sizeof(char*));
	for (int i = 0; i < nTokens; i++) {
		tokensCpy[i] = (char*)malloc(strlen(tokens[i]+1));
		strcpy(tokensCpy[i], tokens[i]);
	}
	tokensCpy[nTokens+1] = NULL;
	// strcpy(buffer, tokensToString(buffer, LINEBUFFERSIZE, tokens, 0));
	// strcat(buffer, "\n");
	// printf("Orig buf: %s", buffer);
	
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
			if (strstr(tokensCpy[i], "*") != NULL) {
				printf("Calling glob\n");
				//Loops works for multiple globs so long as it's not
				// echo *.c *.c    for whatever reason
				nTokens = globTest(tokensCpy, i, nTokens);
				// printf("final buf: %s", buffer);
				// tokensCpy = malloc((nTokens+1) * sizeof(char*));
				// loadTokens(tokensCpy, 512, buffer, 0);
				printf("PAST LOAD\n");
				//recopy original token list back into glob list?
				// break; //should eventually be removed so you can check trailing after initial glob for more globs
			}
		}
	}
	#endif

	// printf("2\n");
	// -- VAR SUB -- //
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
		#if defined ( OS_UNIX )
		assignVariable(tokensCpy, nTokens, varAssign);
		#endif
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
						redirection(tokensCpy, 0, i);
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
						redirection(tokensCpy, 1, i);
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
					redirection(tokensCpy, 0, i);
					i++;
				}
			} else if (strcmp(tokensCpy[i], ">") == 0) {
				redirOut++;
				if (redirOut > 1) {
					printf("Too many redirect chars\n");
					exit(1);
				} else {
					if (updateNTokens == -1) updateNTokens = i-2;
					redirection(tokensCpy, 1, i);
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
				exit(1);
			}
			exit(0);
		} else {
			//Parent

			// Exit status messages
			dup2(origIn, STDIN_FILENO);
			dup2(origOut, STDOUT_FILENO);
			int status;
			waitpid(pid, &status, 0);
			statusMessageHandler(pid, status);
		}
	}
	#endif
	#if defined ( OS_WINDOWS )
		winExec(tokens);
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

