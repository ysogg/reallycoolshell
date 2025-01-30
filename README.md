# COOL Assignment from Systems Programming Class
Course schedule/workload was kind of weird so didn't get as much time as I wanted to work on this.
TODO:
- Update README
- Fix existing linux code (some things aren't proeprly tested for edge cases and most things will likely not work in combination with other functions). Going to leave the Darwin stuff in, should work for now but going forward it will likely be removed.
- Cleanup uncaught FDs
- Split functions up into proper separated files
- Secret other bullet point ?

# IPC in the Shell using Pipes

In this assignment we will write a shell supporting inter process communication (IPC) based on pipes and signals.

# Overview

You have just started your new job in Embedded Worlds, where you build systems for remotely operated devices. Your company builds its reputation on quality: robust code that efficiently uses resources on small-memory embedded (IoT) systems.

Your boss has asked you to build a product called the "Superior Quality Unbeatably Awesome Shell" or "squash", which will be used to run commands in a limited operating environment on a configurable remote sensor platform.

Embedded Worlds has a variety of existing installs, spanning Linux, MacOSX and Microsoft Windows.  The shell you build must compile and run on all of these environments, allowing execution of any installed commands on the platform.

Note that because the platform is a small remotely operated device, "Windows Subsystem for Linux" is not installed in the Microsoft environment -- it will be up to you to leverage the system tools within native Windows itself for that platform.

# Code provided

Previous employees of Embedded Worlds began this project, and there is a code framework that has been provided.  For this assignment you are expected to expand and complete the starting code framework in order to reach the assignment goals.

The final project should be all your own work.  If any external tools, algorithms *etc*. contribute to your work, this must be disclosed in a file called `CONTRIBUTIONS.md` within your submission.

We will coordinate the code using [`git(1)`](https://man7.org/linux/man-pages/man1/git.1.html).

The starting code framework can be checked out into the current working directory by running this command:

	git clone 'git@gitlab.socs.uoguelph.ca:3050F24/${USER}/A1' .

In the above command, the variable `${USER}` refers to your login.  If run on the `linux.socs` machines, or the `mac.socs.uoguelph.ca` machine the `${USER}` variable should already match your login.  If you are working on some other machine, you will need to replace that portion of the string with your U of G login.

In the current situation, you will need to use [`git(1)`](https://man7.org/linux/man-pages/man1/git.1.html) to coordinate your source code between the MacOSX platform and the Linux/Windows shared network drives.


# Features required of your shell

The "squash" shell will allow running of other programs in an environment that supports all of the following operations:

1) running processes in the "foreground" -- this will be an extended version of ideas and assignments from the CIS\*3110/Operating Systems course. (A1)

2) pipe output between multiple processes in a chain (A1)

3) report appropriate error messages when any system call fails (A1)

4) support the "builtin" commands: `cd` and `exit` (A1)

5) simple variable substitution within the shell command line (A1)

6) handle file redirection using `>` and `<` (A2)

7) compile portably -- you code must support compilation and execution of the `squash` shell on all of Linux, MacOSX and Windows operating systems. (A2)

8) filename "globbing" on "UNIX-alike" platforms (A2)

9) run commands in the "background" (A2)


These features will be delivered over two milestones: A1 and A2.  All features up to \#5 in the list above are expected to be working *on Linux* as part of the A1 deliverable; all features are expected for the A2 deliverable and will be tested on *all three platforms.*

Below are further descriptions of each of these activities.

# Run processes in the foreground

This is the standard use of the command line you are familiar with.

On MacOSX and Linux, the older system call interface to create processes is `fork(2)` and `exec(3)`.  More recently, the mechanism of `posix_spawn(2)` has been added.  Part of this assignment will be deciding which of these two strategies best lets you meet the goals of your task.

If the intention is to immediately call `exec(3)` to run a separate program, then `posix_spawn(2)` can perform both steps in a single call, and this can be more efficient than `fork(2)/exec(3)` as it avoids unnecessary page duplication.

On UN\*X based platforms, a child process is cleaned up using a call to  `wait(2)`.  This allows the programmer to figure out both when the created process has completed, and what the exit status is.  (This cycle should be a review of material from CIS\*3110/Operating Systems, though many of you may find `posix_spawn(2)` as a combined `fork(2)/exec(3)` call to be new.)


On the Windows platform, these system calls do not exist in the same form.  You will need to use one of the `CreatProcess()` family of functions there.  Once a process is created the `PROCESS_INFORMATION` structure can be used with `WaitForSingleObject()` in a strategy somewhat analogous to the use of `wait(2)` on UN\*X platforms.  An example of the use of process creation and waiting on Microsoft Windows is available [in the Win32 system documentation](https://learn.microsoft.com/en-us/windows/win32/procthread/creating-processes).


Regardless of the platform, when a child command exits, your program must determine the exit status.  You should indicate whether the program crashed, or exited normally.  If the program exited normally, indicate what the exit status was.  Use the following formats for these three cases:

	Child(<PID>) did not exit (crashed?)
	Child(<PID>) exited -- success (<status>)
	Child(<PID>) exited -- failure (<status>)

Replace `<PID>` with the process id of the child, and `<status>` with value of the exit status byte of the program.

Do **not** waste your time thinking about using the function `system(3)`.  Implementations using `system(3)` instead of `fork(2)`, `exec(3)` and `wait(2)` cannot be made to properly support piping, and will not receive marks for this functionality.

Before each command is run, the command line should be printed with each separate command line token enclosed in quotes. For example if the command `ls *.c` is run, this might appear as:

	$ ls -l *.c
	"ls" "-l" "squash_main.c" "squash_run.c" "squash_tokenize.c"


# Pipe output between multiple processes in a chain

Implement the functionality to run pipe-based "filters", where the output from one command can be passed into the next.  You should be able to have arbitrarily many "pipes" on the command line:

	cat /etc/services | grep telnet | grep udp | more

In the `pipeToMore.c` example in the course directory, there is a simple example demonstrating how to capture the `stdin` from a child process.

This is the conceptually most difficult task in this assignment.  I recommend that you think about it, get piping between two processes working, and then tackle chains of multiple processes.


For this section of the assignment, you will likely want to refer to the manual pages (a.k.a "man pages") on a Linux machine for the following functions and system calls:

* `dup(2)`
* `pipe(2)`


Remember that if you want to look at a manual page in a given chapter, you can do so by giving the chapter number as the first argument, for example: `man 2 pipe`


**Question:**
How do you think programs like "`more`" manage to disambiguate between stdin (data) and input from the keyboard?


# Handle file redirection using `>` and `<`

For this task, you will extend your thinking around pipes from the previous task.  Here, you want a program for which stdin is redirected to read from a file descriptor as in your pipe based work, but this file descriptor must be supplied data by reading it from the named file (rather than being written to a pipe by a program).

Redirecting standard output is much the same, but in the opposite direction, of course.

We will not redirect standard error.  In fact, you are recommended to not touch file descriptor 2/`stderr` in this assignment so that you can always use debugging `fprintf()` statements directed to to stderr and know that they will appear on the screen.  An example might be:

	fprintf(stderr, "Debug: x contains %d\n", x);


You can assume that you code need only handle one (optional) redirection in each direction on a given command line.  You can also assume that redirection will occur only at the very end of the command line.

Redirection using '<' affects only the first command on the command line.  Redirection using '>' affects only the last.


Here are some examples of valid redirection:
	
	sort datafile.txt | grep magic > output.txt
	sort | grep magic < datafile.txt
	sort | grep magic < datafile.txt > output.txt
	sort | grep magic > output.txt < datafile.txt

Anything with more than one `<` character or more than one `>` character is invalid, and our shell will not support redirection from any other position.  The following is therefore legal in `bash` but not in our shell, `squash`

	sort < datafile.txt | grep magic > another.txt


# Simple variable substitution within the shell command line

Include in your shell a simple variable substitution scheme.  This should work as follows:

* Lines that contain an equals sign (ASCII character `0x3d` which is '`=`') will be of the form

	variablename=some kind of value

and should cause variable "`variablename`" to be assigned the value "`some kind of value`".  Note that you don't need to support quotes, so you can assume that everything after the equals sign until the newline is part of the value.

Valid names for variables are described by the following regular expression: 

	`[A-Za-z_][A-Za-z0-9_]*`


(That is, they start with letter or underscore, then letters, underscores and/or numbers).


Lines that contain a dollar sign (ASCII character `0x24`, '`$`') contain a substitution region right after the dollar sign that is marked with scrolled-brackets like this: `${variablename}`

If the scroll bracket right after the dollar sign is missing, this is an error.

This means that you can set variables with lines like this:

	VAR=a value

and use the variables in lines like this:

	echo ((${VAR}))

which should print the string "`((a value))`" using the `/bin/echo` command found on the system `$PATH`.


# Filename globbing "globbing" on "UNIX-alike" platforms

This is done on the UNIX-alike platforms using the function `glob(3)`.  Your globbing should support matching of all of the POSIX wildcard matches, as described on the Linux manual page for `glob(3)`.

The tradition of shells on Windows is that the **commands themselves** do the globbing, and the shell simply passes all arguments as typed.  For this assignment we will confirm to this practice.

Therefore, if you are considering how to handle the regular expressions yourself, you are very much overthinking this part of the assignment.  Be sure to read the `glob(3)` manual page carefully.

Key things to note about the `glob(3)` function:

* it takes a *function pointer* as an argument; and
* deals in *lists of string values* stored in a `char **` array like structure, just like the `execFullCommandLine()` function (and for that matter, `exec(3)`).  **All of these lists of pointers are terminated with a NULL entry at the end of the list -- a design that you can count on.**  (This is a common design pattern in C code: `argv` has this property as well!)


# Builtin commands

We will support the commands `cd` and `exit` not by running an external command using `exec(3)`, but directly within our program.  See the `chdir(2)` system call for a strong hint on how to manage the directory change.  For `exit` you should exit with the given status value, or zero if no status is given.

**Question:**
Why does `cd` **need** to be built-in, rather than be a program like `ls`?

# Run processes in the "background"

Modern operating systems can run multiple processes at once, and
most shells (even Windows!) now support the ability to get another
prompt before the previous command completes.

**Question:**
What needs to happen in your shell in order to allow the execution of another process *before the previous child process finishes*?  Does this change your strategy to manage your child processing using "waiting"?


We will indicate that we want a process to start "in the background" by putting the `&' character after the command we want to run (as the end-most character on the line, as is done in the system shell).

**Question:**
What is the issue if a "background" process needs user input (`stdin`)?  Is this a problem?  Why or why not?


# Report appropriate error messages when any system call fail

If any of the system calls you use in your code encounter a problem, an appropriate error message should be produced.  For example, if the user attempts to run a command that doesn't exist, then the attempt to create a process for this non-existent command should indicate that no such command is found.

You will likely wish to look into `strerror(3)` or `perror(3)` to help with this.

# Compile portably

Linux and MacOSX are both UNIX based operating systems, and many of the the tools in this assignment are common to both.  Each operating system has fallen for "embrace and extend" however, so pay attention to which features of the functions you require are POSIX standard, and which may include platform specific extensions that may lead you astray.

Windows *will* require code specific to that platform in order to meet assignment objectives.

The *same code* must compile on all three platforms to meet this objective.

Regularly compile and test your code under all three target operating systems to be sure that you continue to meet this objective.

# Assumptions:

You may make the following simplifying assumptions:

* the total length of an expanded line will never exceed 8kb;
* the total number of expanded tokens on a line will never exceed 512 and
* the total number of variables will never exceed 128.

This will allow you to manage these resources using fixed size tables and buffers if you wish.


Note that these limits are provided to simplify your programming task by reducing the amount of memory management code you will need to write as part of your assignment.


In addition to the buffer limits above, you may also assume that:

* it is always valid to separate tokens by white space; multiple consecutive white space characters can be handled in any way you like; and
* quotes can be ignored -- they are not use them to delimit strings as they are in `bash(1)`.  For example, the following command will print the quotes out verbatim:

	echo "Hello"

You are **not required** to *depend* upon these limiting assumptions, but no input violating these assumptions will be supplied during grading.  As with any well behaved system utility, if you **do** rely upon this or any limit, your code should detect any input that exceeds the limit and report this with an error message, rather than, for example, crashing.

As with any system programming solution, reliability is key:  unreliable system code is essentially useless system code.  Grading in this course will reflect whether your code crashes, *etc.*


# Example script

The following is an example of the sorts of commands expected to exist in the input file (note that every line that is not a variable assignment reflects the use of an executable program already installed in the Linux environment):

	A=Apples
	P=Pears
	C=Crab ${A}
	test=0

	echo Things I like:
	echo ${A}s ${B}

	echo Hello there!
	uptime
	grep foo m*.c
	ls -1 /usr/include/*.h | grep std | more
	vmstat 5 5 &
	ls
	date
	echo What a great script!


As mentioned above in the globbing section, wildcards need not be applied on Windows -- this script would therefore attempt to run the command `grep` on a file literally called "`m*.c`".

Note that the system programs you run within your shell will of course vary based on the platform -- if the command `grep` is not an installed program under Microsoft Windows, then an error message would be printed.

# Getting started, parsing and general structure

Starter code has been provided for you.  This code will handle the prompt, and will break the input up into arrays of tokens for you.

Use this code to get started, so that you can focus on the system interface aspects of this assignment, and not get tied up with command-line options and parsing.  (Parsing is itself an interesting and challenging task, so this code has been given to you in order that you can focus on the task in hand.)

Command line flags have been set up for you: in particular, running `squash` with multiple '`-v`' flags will increase verbose printing of values as the given code runs, showing you things like the tokens from the command lines.


The work you will need to do can likely be done entirely within the `squash_run.c` file, as the other two files, `squash_main.c` and `squash_tokenize.c`, deal with command line arguments to the squash program and input line parsing respectively.


Note that the `execFullCommandLine()` function is passed all of the tokens on the command line, as parsed from the shell input.  If there are pipes, for example, these will appear as tokens.  You should be able to achieve your first objective of running simple commands almost immediately, but you will have to recognize when pipes are present and further break up the full list of tokens into subcommands in order to make pipes work.



# Overall assignment approach

This assignment has several parts, many of which can be approached at least somewhat independently of the others.  This is intended to allow you to work productively even if you get stuck on one part by focusing your energies on another part of the assignment while you think through the issues.

It is therefore expected that you start early, and not squander the period of time you have to work on the project.  You should be able complete at least some of the tasks within the first week of the assignment, and build on these over the remaining weeks.  Don't try to do the whole thing at the last minute -- it is a large body of work, which is why you have been given a significant period of time for its completion.


Some of the tasks should be attainable directly using your experience in CIS\*3110, so you should be able to productively get underway immediately.


# Memory Management


A small portion of the overall mark will be assigned based on whether the program you submit manages memory well.

Using the `valgrind(1)` program you can detect whether your program is leaking memory under Linux.  We do not have a similar tool available under MacOSX and Windows.

*Question:*
How can you ensure that you do not have leaks on these other platforms?

The other likely leak is in the world of file descriptors -- if the number for a given file descriptor rises as more lines of commands with pipes are run, you are leaking file descriptors, and should be sure to `close(2)` them properly.  (Failure to do so will mean that your program will run out of available file descriptors after a few commands with pipes in them.)


# Testing Framework

Make up a testing framework for your shell.  This should consist of a series of test files that you pipe into the shell, as well as the expected output.  Each test should be named according to the pattern `test<N>.cmd`, the expected output should be named `test<N>.expected`.  This will allow the following set of commands to be run as part of your test framework:

	squash < test1.cmd > test1.output
	diff test1.expected test1.output

If the only differences in the `diff` output are the PID numbers, the test is passed.

# Assignment questions

Throughout this assignment, you have been asked several questions.  These are marked by "`Question:`" and can be found that way.  Put the text of the question and your answer to each in a file called "`QUESTIONS.txt`" and submit this with your code.


# Resources

Several resources are provided for you to help you get started.

1) Starter code, as described above, is provided to ensure that your assignment has the right command line options, and to parse the scripts you will read.

2) The course directory, available on any of the Linux machines as `/home/courses/cis3050` has examples to help you on your way.  Specifically for this assignment, take a look at

	* `pipeToMore.c` for an example of using a pipe
	* `vssh.c` for a simple `fork()`/`exec()`/`wait()` usage
	* `globbing/globletters.c` and `globbing/globsource.c` for examples of using `glob(3)`

4) Labs - examples from the course directory will be discussed in the lab period in order to answer questions and provide help.  If you are at sea with this or any assignment, be sure to ask questions for directed assistance at the lab and at advising times.

3) Instructor and TA advising


# System calls and function reference

You should familiarize yourself with the documentation for all of the following system calls and functions for use in this assignment:

* `fork(2)`,
* `wait(2)`,
* `posix_spawn(2)`,
* `exec(3)`,
* `exit(2)`,
* `pipe(2)`,
* `dup(2)`, and
* `glob(3)`

In addition, the functions `fileno(3)` and `isatty(3)` are used to control when the prompt is printed.  We will cover these later in the course.


# Individual work

Use the provided code to parse the command line and get started.

Use the examples in the course directory to get an understanding of how the various components fit together.  You may reuse code from any example in this directory.

All other submitted code is expected to be your own work, written specifically for this assignment, and any code included from elsewhere (such as the examples given) **must be noted and their source cited**.  All parts of your program should be written directly by you.

This implies that:

* communication with other programmers about *general strategies* is fine, but any direct sharing of code is not allowed
* using Chat-GPT or similar engines to create the code is similarly not allowed.  Using Chat-GPT to write an assignment solution is a bad idea in any case because:
	* what you need out of this course is to get these strategies into your *own* brain
	* it writes generally bad and unmaintainable code
	* all the "boilerplate" that Chat-GPT is good at providing has already been provided for you



# Submission

To submit your code, do the following:

* bundle all of the code, documentation (including your `QUESTIONS.txt`) and testing files that you want to submit into a single `tar(3)` archive

	* **do NOT include executable or object files**
	* **do NOT use RAR or ZIP compression** -- just use `tar(3)`
		- if your archive is larger than a few dozen kilobytes, you have included something silly -- the source code and documentation should be small

* copy the `.tar` archive to a new directory, unpack it and type `make`.  This should build your shell, and the result should be an executable called `squash`.

* run the resulting executable using your test framework to ensure that it actually does what you want

	* **Test that unpacking, building and running your code works as you expect - if nothing builds, nothing will be marked, so be sure to do this simple test!**

* submit the tar file through the Courselink dropbox

* **Remember that due dates and course policy are listed in the course outline**


# Validating your submission

Don't forget to **unpack and test** the contents of your tar file to be sure that what you are handing in is actually what you expect!  By submitting your assignment you are effectively *deploying your program* -- anyone receiving your program wants to know that you have ensured that it works!

# Development Timeline

This assignment has many parts, many of which are independent from each other.  Several of these will require some thought, and you will encounter challenges that will require you to set that portion of the assignment aside and think about it.

The intention is that you can be working on other portions of the assignment while you think.  This is why you have been given such a significant amount of time for this assignment.

You should aim to have some of the A2 items "roughed in" by the time A1 has been submitted.  In particular, you will find that "porting" code to Windows takes some thought.
