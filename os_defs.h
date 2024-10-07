#ifndef __OS_DETECTION_DEFS_HEADER__
#define __OS_DETECTION_DEFS_HEADER__

/* System include files. */
#ifndef MAKEDEPEND
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#endif


/**
 ** You will have to add more code here -- use the -E or /E flag of
 ** your compiler to see what it defines by default, in order to
 ** be able to figure out what operating system/compiler combo you are
 ** on.  You should be able to define one of the following macros to
 ** identify each of the indicated systems:
 **	OS_BSD
 **		defined only if the environment is some form of BSD
 **		(this one is done for you)
 **	OS_WINDOWS
 **		defined only if the environment is some Windows variant
 **		(Windows 10, etc.)
 **	OS_LINUX
 **		defined only if the environment is some version of Linux
 **	OS_DARWIN
 **		defined only if the environment is some version of Darwin/MacOSX
 **/

/*
 * Compilation Environment Identification
 */
#if defined( __NetBSD__ ) || defined( __OpenBSD__ ) || defined( __FreeBSD__ )

#   define OS_BSD



#elif defined( __linux__ )	// add code to identify Linux

#   define OS_LINUX



#elif defined( __APPLE__ )	// add code to identify MacOSX

#   define OS_DARWIN



#elif defined( _WIN32 )	// add code to identify Windows

#   define OS_WINDOWS



#else
#   error Unknown operating system -- need more defines in os_defs.h!
#endif



/** if we are on one of the Unix type platforms, define OS_UNIX */
#if defined( OS_FREEBSD ) || defined( OS_LINUX ) || defined( OS_DARWIN )

#  define OS_UNIX

#endif


/**
 **	Set up for common requirements, including or defining things
 ** that make portability hard if they are missing.
 **/
#ifndef MAKEDEPEND
#include <limits.h>
#ifdef OS_WINDOWS_NT
#include <stddef.h>
#endif
#endif

#endif /* __OS_DETECTION_DEFS_HEADER__ */
