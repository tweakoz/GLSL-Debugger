/******************************************************************************

Copyright (C) 2006-2009 Institute for Visualization and Interactive Systems
(VIS), Universität Stuttgart.
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

  * Redistributions of source code must retain the above copyright notice, this
    list of conditions and the following disclaimer.

  * Redistributions in binary form must reproduce the above copyright notice, this
	list of conditions and the following disclaimer in the documentation and/or
	other materials provided with the distribution.

  * Neither the name of the name of VIS, Universität Stuttgart nor the names
	of its contributors may be used to endorse or promote products derived from
	this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*******************************************************************************/

#ifndef DEBUG_LIB_INTERNAL_H
#define DEBUG_LIB_INTERNAL_H

#include <sys/types.h>
#include <pthread.h>

#include <gl_interpose/debuglibExport.h>
#include "../debugger/debuglib.h"
#include "streamRecorder.h"
#include "queries.h"
#include <glsldebug_utils/hash.h>
#include <enumerants_runtime/functionPointerTypes.inc>

#define TRANSFORM_FEEDBACK_BUFFER_SIZE (1<<24)

#ifdef __cplusplus
extern "C" {
#endif /* _CPP */

typedef struct {
	void (*(*origGlXGetProcAddress)(const GLubyte *))(void);
	StreamRecorder recordedStream;
	int errorCheckAllowed;

	pthread_mutex_t lock;

	Hash queries;
} Globals;

DBGLIBLOCAL int checkGLVersionSupported(int majorVersion, int minorVersion);
DBGLIBLOCAL int checkGLExtensionSupported(const char *extension);

typedef enum {TFBVersion_None, TFBVersion_NV, TFBVersion_EXT} TFBVersion;  
DBGLIBLOCAL TFBVersion getTFBVersion();

DBGLIBLOCAL void (*getOrigFunc(const char *fname))(void);

#define ORIG_GL(fname) ((PFN##fname##PROC)getOrigFunc(#fname))

DBGLIBLOCAL DbgRec *getThreadRecord(pid_t pid);

/* check GL error code */
DBGLIBLOCAL int glError(void);

/* set shm with result == DBG_ERROR_CODE and error */
DBGLIBLOCAL void setErrorCode(int error);

/* check GL error code and
   set shm with result == DBG_ERROR_CODE and gl error if an error has
   occured; else do nothing
*/
DBGLIBLOCAL int setGLErrorCode(void);

DBGLIBLOCAL void (*glXGetProcAddressHook(const GLubyte *n))(void);

DBGLIBLOCAL void (*getDbgFunction(void))(void);
	
DBGLIBLOCAL void storeFunctionCall(const char *fname, int numArgs, ...);
	
DBGLIBLOCAL void storeResultOrError(unsigned int error, void *result, int type);

DBGLIBLOCAL void storeResult(void *result, int type);

DBGLIBLOCAL void stop(void);

DBGLIBLOCAL int getDbgOperation(void);

DBGLIBLOCAL void setExecuting(void);

DBGLIBLOCAL int keepExecuting(const char *calledName);

DBGLIBLOCAL int checkGLErrorInExecution(void);

DBGLIBLOCAL void executeDefaultDbgOperation(int op);

/* work-around for external debug functions */
/* TODO: do we need debug functions at all? */
#ifdef DBGLIB_EXTERNAL
#  undef ORIG_GL
#  define ORIG_GL(fname) ((PFN##fname##PROC)DEBUGLIB_EXTERNAL_getOrigFunc(#fname))
#  define setErrorCode  DEBUGLIB_EXTERNAL_setErrorCode
#endif
DBGLIBEXPORT void DEBUGLIB_EXTERNAL_setErrorCode(int error);
DBGLIBEXPORT void (*DEBUGLIB_EXTERNAL_getOrigFunc(const char *fname))(void);

#ifdef DEBUG
#  define DMARK dbgPrint(DBGLVL_DEBUG, "DMARK %s: %s (%i)\n", __FILE__, __FUNCTION__, __LINE__);
#else
#  define DMARK
#endif

#ifdef __cplusplus
} //extern "C" {
#endif /* _CPP */

#endif
