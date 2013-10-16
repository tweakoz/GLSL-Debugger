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

#define _GNU_SOURCE
#include <assert.h>

#include <stdlib.h>

#include <dlfcn.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/shm.h>

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <signal.h>

#include <dirent.h>

#include "../GL/gl.h"
#include "../GL/glext.h"

#include "../GL/glx.h"


#include "../utils/dbgprint.h"
#include "../utils/dlutils.h"
#include "../utils/hash.h"
#include "../glenumerants/glenumerants.h"
#include "debuglib.h"
#include "debuglibInternal.h"
#include "glstate.h"
#include "readback.h"
#include "streamRecorder.h"
#include "streamRecording.h"
#include "memory.h"
#include "shader.h"
#include "initLib.h"
#include "queries.h"

#  define LIBGL "/usr/lib/fglrx/libGL.so"
#  define SO_EXTENSION ".so"

#define USE_DLSYM_HARDCODED_LIB

extern GLFunctionList glFunctions[];

typedef struct {
	LibraryHandle handle;
	const char *fname;
	void (*function)(void);
} DbgFunction;

/* TODO: threads! Should be local to each thread, isn't it? */

static struct {
	int initialized;
#ifdef USE_DLSYM_HARDCODED_LIB
	LibraryHandle libgl;
#endif
	void *(*origdlsym)(void *, const char *);
	
	DbgRec *fcalls;
	DbgFunction *dbgFunctions;
	int numDbgFunctions;
	Hash origFunctions;
} g = {
		0, /* initialized */
#ifdef USE_DLSYM_HARDCODED_LIB
		NULL, /* libgl */
#endif	
		NULL, /* origdlsym */ 
		NULL, /* fcalls */
		NULL, /* dbgFunctions */
		0, /* numDbgFunctions */
		{0, NULL, NULL, NULL} /* origFunctions */
	};


/* global data */
DBGLIBLOCAL Globals G;


static int getShmid()
{
	char *s = getenv("GLSL_DEBUGGER_SHMID");

	if (s) {
		return atoi(s);
	} else {
		dbgPrint(DBGLVL_ERROR, "Oh my god! No Shmid! Set GLSL_DEBUGGER_SHMID!\n");
		exit(1);
	}
}


static void setLogging(void)
{
	int level;


	char *s;

    s = getenv("GLSL_DEBUGGER_LOGDIR");
	if (s) {
		setLogDir(s);
	}
	else {

		setLogDir(NULL);
	}

	startLogging(NULL);


    s = getenv("GLSL_DEBUGGER_LOGLEVEL");
	if (s) {

		level = atoi(s);
		setMaxDebugOutputLevel(level);
		dbgPrint(DBGLVL_INFO, "Log level set to %i\n", level);
    } else {
		setMaxDebugOutputLevel(DBGLVL_ERROR);
		dbgPrint(DBGLVL_WARNING, "Log level not set!\n");
	}
}

static void addDbgFunction(const char *soFile)
{
    LibraryHandle handle = NULL;
	void (*dbgFunc)(void) = NULL;
	const char *provides = NULL; 
	
	if (!(handle = openLibrary(soFile))) {
		dbgPrint(DBGLVL_WARNING, "Opening dbgPlugin \"%s\" failed\n", soFile);
		return;
	}

	if (!(provides = g.origdlsym(handle, "provides"))) {

        dbgPrint(DBGLVL_WARNING, "Could not determine what \"%s\" provides!\n"
		                         "Export the " "\"provides\"-string!\n", soFile);
		closeLibrary(handle);
		return;
	}


    if (!(dbgFunc = (void (*)(void))g.origdlsym(handle, provides))) {

		closeLibrary(handle);
		return;
	}
	g.numDbgFunctions++;
	g.dbgFunctions = realloc(g.dbgFunctions,
	                         g.numDbgFunctions*sizeof(DbgFunction));
	if (!g.dbgFunctions) {
		dbgPrint(DBGLVL_ERROR, "Allocating g.dbgFunctions failed: %s (%d)\n",
				strerror(errno), g.numDbgFunctions*sizeof(DbgFunction));
		closeLibrary(handle);
		exit(1);
	}
	g.dbgFunctions[g.numDbgFunctions-1].handle = handle;
	g.dbgFunctions[g.numDbgFunctions-1].fname = provides;
	g.dbgFunctions[g.numDbgFunctions-1].function = dbgFunc;
}



static void freeDbgFunctions()
{
	int i;

	for (i = 0; i < g.numDbgFunctions; i++) {
        if (g.dbgFunctions[i].handle != NULL) {
            closeLibrary(g.dbgFunctions[i].handle);
            g.dbgFunctions[i].handle = NULL;
        }
	}
}


static int endsWith(const char *s, const char *t)
{
	return strlen(t) < strlen(s) && !strcmp(s + strlen(s) - strlen(t), t);
}

static void loadDbgFunctions(void)
{
	char *file;

	struct dirent *entry;
	struct stat statbuf;
	DIR *dp;
    char *dbgFctsPath = NULL;

    dbgFctsPath = getenv("GLSL_DEBUGGER_DBGFCTNS_PATH");


	if (!dbgFctsPath || dbgFctsPath[0] == '\0') {
		dbgPrint(DBGLVL_ERROR, "No dbgFctsPath! Set GLSL_DEBUGGER_DBGFCTNS_PATH!\n");
		exit(1);
	}
	
	if ((dp = opendir(dbgFctsPath)) == NULL) {
		dbgPrint(DBGLVL_ERROR, "cannot open so directory \"%s\"\n", dbgFctsPath);
		exit(1);
	}

	while((entry = readdir(dp))) {
		if (endsWith(entry->d_name, SO_EXTENSION)) {
			if (! (file = (char *)malloc(strlen(dbgFctsPath) + strlen(entry->d_name) + 2))) {
				dbgPrint(DBGLVL_ERROR, "not enough memory for file template\n");
				exit(1);
			}
			strcpy(file, dbgFctsPath);
			if (dbgFctsPath[strlen(dbgFctsPath)-1] != '/') {
				strcat(file, "/");
			}
			strcat(file, entry->d_name);
			stat(file, &statbuf);
			if (S_ISREG(statbuf.st_mode)) {
				addDbgFunction(file);
			}
			free(file);
		}
	}
	closedir(dp);
}

void __attribute__ ((constructor)) debuglib_init(void)
{
#ifndef RTLD_DEEPBIND
	g.origdlsym = dlsym;
#endif

	setLogging();	
	
#ifdef USE_DLSYM_HARDCODED_LIB	
	if (!(g.libgl = openLibrary(LIBGL))) {
		dbgPrint(DBGLVL_ERROR, "Error opening OpenGL library\n");
		exit(1);
	}
#endif
	
	/* attach to shared mem segment */
	if (!(g.fcalls = shmat(getShmid(), NULL, 0))) {
		dbgPrint(DBGLVL_ERROR, "Could not attach to shared memory segment: %s\n", strerror(errno));
		exit(1);
	}

	pthread_mutex_init(&G.lock, NULL);
	
	hash_create(&g.origFunctions, hashString, compString, 512, 0);

	initQueryStateTracker();
	
#ifdef USE_DLSYM_HARDCODED_LIB	
	/* paranoia mode: ensure that g.origdlsym is initialized */
	dlsym(g.libgl, "glFinish");
	
	G.origGlXGetProcAddress = (void (*(*)(const GLubyte*))(void))g.origdlsym(g.libgl, "glXGetProcAddress");
	if (!G.origGlXGetProcAddress) {
		G.origGlXGetProcAddress = (void (*(*)(const GLubyte*))(void))g.origdlsym(g.libgl, "glXGetProcAddressARB");
		if (!G.origGlXGetProcAddress) {
			dbgPrint(DBGLVL_ERROR, "Hmm, cannot resolve glXGetProcAddress\n");
			exit(1);
		}
	}
#else
	/* paranoia mode: ensure that g.origdlsym is initialized */
	dlsym(RTLD_NEXT, "glFinish");
	
	G.origGlXGetProcAddress = g.origdlsym(RTLD_NEXT, "glXGetProcAddress");
	if (!G.origGlXGetProcAddress) {
		G.origGlXGetProcAddress = g.origdlsym(RTLD_NEXT, "glXGetProcAddressARB");
		if (!G.origGlXGetProcAddress) {
			dbgPrint(DBGLVL_ERROR, "Hmm, cannot resolve glXGetProcAddress\n");
			exit(1);
		}
	}
#endif	

	G.errorCheckAllowed = 1;
	
	initStreamRecorder(&G.recordedStream);
	
	loadDbgFunctions();
	
	g.initialized = 1;
}

void __attribute__ ((destructor)) debuglib_fini(void)
{
	/* detach shared mem segment */
	shmdt(g.fcalls);
	
#ifdef USE_DLSYM_HARDCODED_LIB
	if (g.libgl) {
		closeLibrary(g.libgl);
	}
#endif
	
	freeDbgFunctions();

	hash_free(&g.origFunctions);

	cleanupQueryStateTracker();
	
	clearRecordedCalls(&G.recordedStream);

	quitLogging();

	pthread_mutex_destroy(&G.lock);
}

DbgRec *getThreadRecord(pid_t pid)
{
	int i;
	for (i = 0; i < SHM_MAX_THREADS; i++) {
		if (g.fcalls[i].threadId == 0 || g.fcalls[i].threadId == pid) {
			break;
		}
	}
	if (i == SHM_MAX_THREADS) {
		/* TODO */
		dbgPrint(DBGLVL_ERROR, "Error: max. number of debugable threads exceeded!\n");
		exit(1);
	}
	return &g.fcalls[i];
}

static void printArgument(void *addr, int type)
{
	char *s;

	switch (type) {
	case DBG_TYPE_CHAR:
		dbgPrintNoPrefix(DBGLVL_INFO, "%i, ", *(char*)addr); 
		break;
	case DBG_TYPE_UNSIGNED_CHAR:
		dbgPrintNoPrefix(DBGLVL_INFO, "%i, ", *(unsigned char*)addr); 
		break;
	case DBG_TYPE_SHORT_INT:
		dbgPrintNoPrefix(DBGLVL_INFO, "%i, ", *(short*)addr); 
		break;
	case DBG_TYPE_UNSIGNED_SHORT_INT:
		dbgPrintNoPrefix(DBGLVL_INFO, "%i, ", *(unsigned short*)addr); 
		break;
	case DBG_TYPE_INT:
		dbgPrintNoPrefix(DBGLVL_INFO, "%i, ", *(int*)addr); 
		break;
	case DBG_TYPE_UNSIGNED_INT:
		dbgPrintNoPrefix(DBGLVL_INFO, "%u, ", *(unsigned int*)addr); 
		break;
	case DBG_TYPE_LONG_INT:
		dbgPrintNoPrefix(DBGLVL_INFO, "%li, ", *(long*)addr); 
		break;
	case DBG_TYPE_UNSIGNED_LONG_INT:
		dbgPrintNoPrefix(DBGLVL_INFO, "%lu, ", *(unsigned long*)addr); 
		break;
	case DBG_TYPE_LONG_LONG_INT:
		dbgPrintNoPrefix(DBGLVL_INFO, "%lli, ", *(long long*)addr); 
		break;
	case DBG_TYPE_UNSIGNED_LONG_LONG_INT:
		dbgPrintNoPrefix(DBGLVL_INFO, "%llu, ", *(unsigned long long*)addr); 
		break;
	case DBG_TYPE_FLOAT:
		dbgPrintNoPrefix(DBGLVL_INFO, "%f, ", *(float*)addr); 
		break;
	case DBG_TYPE_DOUBLE:
		dbgPrintNoPrefix(DBGLVL_INFO, "%f, ", *(double*)addr); 
		break;
	case DBG_TYPE_POINTER:
		dbgPrintNoPrefix(DBGLVL_INFO, "%p, ", *(void**)addr); 
		break;
	case DBG_TYPE_BOOLEAN:
		dbgPrintNoPrefix(DBGLVL_INFO, "%s, ", *(GLboolean*)addr ? "TRUE" : "FALSE");
		break;
	case DBG_TYPE_BITFIELD:
		s  = dissectBitfield(*(GLbitfield*)addr);
		dbgPrintNoPrefix(DBGLVL_INFO, "%s, ", s);
		free(s);
		break;
	case DBG_TYPE_ENUM:
		dbgPrintNoPrefix(DBGLVL_INFO, "%s, ", lookupEnum(*(GLenum*)addr));
		break;
	case DBG_TYPE_STRUCT:
		dbgPrintNoPrefix(DBGLVL_INFO, "STRUCT, ");
		break;
	default:	
		dbgPrintNoPrefix(DBGLVL_INFO, "UNKNOWN TYPE [%i], ", type);
	}
}

void storeFunctionCall(const char *fname, int numArgs, ...)
{
	int i;
	va_list argp;
	pid_t pid = getpid();

	DbgRec *rec = getThreadRecord(pid);
	
	rec->threadId = pid;
	rec->result = DBG_FUNCTION_CALL;
	strncpy(rec->fname, fname, SHM_MAX_FUNCNAME);
	rec->numItems = numArgs;

	dbgPrint(DBGLVL_INFO, "STORE CALL: %s(", rec->fname);
	va_start(argp, numArgs);
	for (i = 0; i < numArgs; i++) {
		rec->items[2*i] = (ALIGNED_DATA)va_arg(argp, void*);
		rec->items[2*i + 1] = (ALIGNED_DATA)va_arg(argp, int);
		printArgument((void*)rec->items[2*i], rec->items[2*i + 1]);
	}	
	va_end(argp);
	dbgPrintNoPrefix(DBGLVL_INFO, ")\n");
}

void storeResult(void *result, int type)
{
	pid_t pid = getpid();

	DbgRec *rec = getThreadRecord(pid);

	dbgPrint(DBGLVL_INFO, "STORE RESULT: ");
	printArgument(result, type);
	dbgPrintNoPrefix(DBGLVL_INFO, "\n");
	rec->result = DBG_RETURN_VALUE;
	rec->items[0] = (ALIGNED_DATA)result;
	rec->items[1] = (ALIGNED_DATA)type;
}

void storeResultOrError(unsigned int error, void *result, int type)
{
	pid_t pid = getpid();

	DbgRec *rec = getThreadRecord(pid);

	if (error) {
		setErrorCode(error);
		dbgPrint(DBGLVL_WARNING, "NO RESULT STORED: %u\n", error);
	} else {
		dbgPrint(DBGLVL_INFO, "STORE RESULT: ");
		printArgument(result, type);
		dbgPrintNoPrefix(DBGLVL_INFO, "\n");
		rec->result = DBG_RETURN_VALUE;
		rec->items[0] = (ALIGNED_DATA)result;
		rec->items[1] = (ALIGNED_DATA)type;
	}
}

void stop(void)
{
	dbgPrint(DBGLVL_DEBUG, "RAISED STOP\n");
	raise(SIGSTOP);

}

static void startRecording(void)
{
	DMARK
	clearRecordedCalls(&G.recordedStream);
	setErrorCode(DBG_NO_ERROR);
}

static void replayRecording(int target)
{
	int error;
	DMARK
	error = setSavedGLState(target);
	if (error) {
		setErrorCode(error);
	}
	replayFunctionCalls(&G.recordedStream, 0);
	setErrorCode(glError());
}

static void endReplay(void)
{
	DMARK
	replayFunctionCalls(&G.recordedStream, 1);
	clearRecordedCalls(&G.recordedStream);
	setErrorCode(glError());
}

/* 
	Does all operations necessary to get the result of a given debug shader
	back to the caller, i.e. setup the shader and its environment, replay the
	draw call and readback the result.
	Parameters:
		items[0] : pointer to vertex shader src
		items[1] : pointer to geometry shader src
		items[2] : pointer to fragment shader src
		items[3] : debug target, see DBG_TARGETS below
		if target == DBG_TARGET_FRAGMENT_SHADER:
			items[4] : number of components to read (1:R, 3:RGB, 4:RGBA)
			items[5] : format of readback (GL_FLOAT, GL_INT, GL_UINT)
		if target == DBG_TARGET_VERTEX_SHADER or DBG_TARGET_GEOMETRY_SHADER:
			items[4] : primitive mode
			items[5] : force primitive mode even for geometry shader target
			items[6] : expected size of debugResult (# floats) per vertex
	Returns:	
		if target == DBG_TARGET_FRAGMENT_SHADER:
			result   : DBG_READBACK_RESULT_FRAGMENT_DATA or DBG_ERROR_CODE
					   on error
			items[0] : buffer address
			items[1] : image width
			items[2] : image height
		if target == DBG_TARGET_VERTEX_SHADER or DBG_TARGET_GEOMETRY_SHADER:
			result   : DBG_READBACK_RESULT_VERTEX_DATA or DBG_ERROR_CODE on
					   error
			items[0] : buffer address
			items[1] : number of vertices
			items[2] : number of primitives
*/
static void shaderStep(void)
{
	int error;

	DbgRec *rec = getThreadRecord(getpid());

	const char *vshader = (const char *)rec->items[0];
	const char *gshader = (const char *)rec->items[1];
	const char *fshader = (const char *)rec->items[2];
	int target = (int)rec->items[3];

	dbgPrint(DBGLVL_COMPILERINFO, "SHADER STEP: v=%p g=%p f=%p target=%i\n",
	         vshader, gshader, fshader, target);
			
	dbgPrint(DBGLVL_COMPILERINFO, "############# V-Shader ##############\n%s\n"
	                              "$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\n",
	         vshader);
	dbgPrint(DBGLVL_COMPILERINFO, "############# G-Shader ##############\n%s\n"
	                              "$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\n",
	         gshader);
	dbgPrint(DBGLVL_COMPILERINFO, "############# F-Shader ##############\n%s\n"
	                              "$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$\n",
	         fshader);
	
	if (target == DBG_TARGET_GEOMETRY_SHADER ||
	    target == DBG_TARGET_VERTEX_SHADER) {
		int primitiveMode = (int)rec->items[4];
		int forcePointPrimitiveMode = (int)rec->items[5];
		int numFloatsPerVertex = (int)rec->items[6];
		int numVertices;
		int numPrimitives;
		float *buffer;
		
		/* set debug shader code */
		error = loadDbgShader(vshader, gshader, fshader, target,
		                      forcePointPrimitiveMode);
		if (error) {
			setErrorCode(error);
			return;
		}

		/* replay recorded drawcall */
		error = setSavedGLState(target);
		if (error) {
			setErrorCode(error);
			return;
		}

		/* output primitive mode from (geometry) shader program over writtes
		 * primitive mode of draw call! 
		 */
		if (target == DBG_TARGET_GEOMETRY_SHADER) {
			if (forcePointPrimitiveMode) {
				primitiveMode = GL_POINTS;
			} else {
				primitiveMode = getShaderPrimitiveMode();
			}
		}
		
		/* begin transform feedback */
		error = beginTransformFeedback(primitiveMode);
		if (error) {
			setErrorCode(error);
			return;
		}
		
		replayFunctionCalls(&G.recordedStream, 0);
		error = glError();
		if (error) {
			setErrorCode(error);
			return;
		}
		
		/* readback feedback buffer */
		error = endTransformFeedback(primitiveMode, numFloatsPerVertex, &buffer,
		                             &numPrimitives, &numVertices);
		if (error) {
			setErrorCode(error);
		} else {
			rec->result = DBG_READBACK_RESULT_VERTEX_DATA;
			rec->items[0] = (ALIGNED_DATA)buffer;
			rec->items[1] = (ALIGNED_DATA)numVertices;
			rec->items[2] = (ALIGNED_DATA)numPrimitives;
		}
	} else if (target == DBG_TARGET_FRAGMENT_SHADER) {
		int numComponents = (int)rec->items[4];
		int format = (int)rec->items[5];
		int width, height;
		void *buffer;
		
		/* set debug shader code */
		error = loadDbgShader(vshader, gshader, fshader, target, 0);
		if (error) {
			setErrorCode(error);
			return;
		}
		
		/* replay recorded drawcall */
		error = setSavedGLState(target);
		if (error) {
			setErrorCode(error);
			return;
		}
		replayFunctionCalls(&G.recordedStream, 0);
		error = glError();
		if (error) {
			setErrorCode(error);
			return;
		}

		/* readback framebuffer */
		DMARK
		error = readBackRenderBuffer(numComponents, format, &width, &height, &buffer);
		DMARK
		if (error) {
			setErrorCode(error);
		} else {
			rec->result = DBG_READBACK_RESULT_FRAGMENT_DATA;
			rec->items[0] = (ALIGNED_DATA)buffer;
			rec->items[1] = (ALIGNED_DATA)width;
			rec->items[2] = (ALIGNED_DATA)height;
		}
	} else {
		dbgPrint(DBGLVL_COMPILERINFO, "\n");
		setErrorCode(DBG_ERROR_INVALID_DBG_TARGET);
	}
}

int getDbgOperation(void)
{
	pid_t pid = getpid();

	DbgRec *rec = getThreadRecord(pid);
    dbgPrint(DBGLVL_INFO, "OPERATION: %li\n", rec->operation);
	return rec->operation;
}

static int isDebuggableDrawCall(const char *name)
{
	int i = 0;
	while (glFunctions[i].fname != NULL) {
		if (!strcmp(name, glFunctions[i].fname)) {
			return glFunctions[i].isDebuggableDrawCall;
		}
		i++;
	}
	return 0;
}

static int isShaderSwitch(const char *name)
{
	int i = 0;
	while (glFunctions[i].fname != NULL) {
		if (!strcmp(name, glFunctions[i].fname)) {
			return glFunctions[i].isShaderSwitch;
		}
		i++;
	}
	return 0;
}

int keepExecuting(const char *calledName)
{
	pid_t pid = getpid();

	DbgRec *rec = getThreadRecord(pid);
	if (rec->operation == DBG_STOP_EXECUTION) {
		return 0;
	} else if (rec->operation == DBG_EXECUTE) {
		switch (rec->items[0]) {
			case DBG_EXECUTE_RUN:
				return 1;
			case DBG_JUMP_TO_SHADER_SWITCH:
				return !isShaderSwitch(calledName);
			case DBG_JUMP_TO_DRAW_CALL:
				/* TODO:  allow also jumps to non-debuggable draw calls */
				return !isDebuggableDrawCall(calledName);
			case DBG_JUMP_TO_USER_DEFINED:
				return strcmp(rec->fname, calledName);
			default:
				break;
		}
		setErrorCode(DBG_ERROR_INVALID_OPERATION);
	}
	return 0;
}

int checkGLErrorInExecution(void)
{
	pid_t pid = getpid();
	DbgRec *rec = getThreadRecord(pid);
	return rec->items[1];
	return 1;
}

void setExecuting(void)
{
	pid_t pid = getpid();

	DbgRec *rec = getThreadRecord(pid);
	rec->result = DBG_EXECUTE_IN_PROGRESS;
}

void executeDefaultDbgOperation(int op)
{
	switch (op) {
		/* DBG_CALL_FUNCTION, DBG_RECORD_CALL, and DBG_CALL_ORIGFUNCTION handled
		 * directly in functionHooks.inc 
		 */
		case DBG_ALLOC_MEM:
			allocMem();
			break;
		case DBG_FREE_MEM:
			freeMem();
			break;
		case DBG_READ_RENDER_BUFFER:
			if (G.errorCheckAllowed) {
				readRenderBuffer();
			} else {
				setErrorCode(DBG_ERROR_READBACK_NOT_ALLOWED);
			}
			break;
		case DBG_CLEAR_RENDER_BUFFER:
			if (G.errorCheckAllowed) {
				clearRenderBuffer();
			} else {
				setErrorCode(DBG_ERROR_OPERATION_NOT_ALLOWED);
			}
			break;
		case DBG_SET_DBG_TARGET:
			setDbgOutputTarget();
			break;
		case DBG_RESTORE_RENDER_TARGET:
			restoreOutputTarget();
			break;
		case DBG_START_RECORDING:
			startRecording();
			break;
		case DBG_REPLAY:
			/* should be obsolete: we use a invalid debug target to avoid
			 * interference with debug state 
			*/
			replayRecording(DBG_TARGET_FRAGMENT_SHADER+1);
			break;
		case DBG_END_REPLAY:
			endReplay();
			break;
		case DBG_STORE_ACTIVE_SHADER:
			storeActiveShader();
			break;
		case DBG_RESTORE_ACTIVE_SHADER:
			restoreActiveShader();
			break;
		case DBG_SET_DBG_SHADER:
			setDbgShader();
			break;
		case DBG_GET_SHADER_CODE:
			getShaderCode();
			break;
		case DBG_SHADER_STEP:
			shaderStep();
			break;
		case DBG_SAVE_AND_INTERRUPT_QUERIES:
			interruptAndSaveQueries();
			break;
		case DBG_RESTART_QUERIES:
			restartQueries();
			break;
		default:
			dbgPrint(DBGLVL_INFO, "HMM, UNKNOWN DEBUG OPERATION %i\n", op);
			break;
	}
}

static void dbgFunctionNOP(void)
{
	setErrorCode(DBG_ERROR_NO_SUCH_DBG_FUNC);
}


void (*getDbgFunction(void))(void)
{
	pid_t pid = getpid();

	DbgRec *rec = getThreadRecord(pid);
	int i;
	
	for (i = 0; i < g.numDbgFunctions; i++) {
		if (!strcmp(g.dbgFunctions[i].fname, rec->fname)) {
			dbgPrint(DBGLVL_INFO, "found special detour for %s\n", rec->fname);
			return g.dbgFunctions[i].function;
		}
	}
	return dbgFunctionNOP;
}

void (*getOrigFunc(const char *fname))(void)
{
	/* glXGetProcAddress and  glXGetProcAddressARB are special cases: we have to
	 * call our version not the original ones 
	 */
	if (!strcmp(fname, "glXGetProcAddress") ||
	    !strcmp(fname, "glXGetProcAddressARB")) {
		return (void (*)(void))glXGetProcAddressHook;
	} else {
		void *result = hash_find(&g.origFunctions, (void*)fname);

		if (!result) {
#ifdef USE_DLSYM_HARDCODED_LIB			
			void *origFunc = g.origdlsym(g.libgl, fname);
#else
			void *origFunc = g.origdlsym(RTLD_NEXT, fname);
#endif
			if (!origFunc) {
				origFunc = G.origGlXGetProcAddress((const GLubyte *)fname);
				if (!origFunc) {
					dbgPrint(DBGLVL_ERROR, "Error: Cannot resolve %s\n", fname);
					exit(1); /* TODO: proper error handling */
				}
			}
			hash_insert(&g.origFunctions, (void*)fname, origFunc);
			result = origFunc;
		}

		/* FIXME: Is there a better place for this ??? */
		if (!strcmp(fname, "glBegin")) {
			G.errorCheckAllowed = 0;
		} else if (!strcmp(fname, "glEnd")) {
			G.errorCheckAllowed = 1;
		}
		dbgPrint(DBGLVL_INFO, "ORIG_GL: %s (%p)\n", fname, result);
		return (void (*)(void))result;
	}
}

/* work-around for external debug functions */
/* TODO: do we need debug functions at all? */
void (*DEBUGLIB_EXTERNAL_getOrigFunc(const char *fname))(void)
{
	return getOrigFunc(fname);
}


int checkGLExtensionSupported(const char *extension)
{
    static const char *extString = NULL;
    const char *start;
	
	 if (!extString) {
		 extString = (char *)ORIG_GL(glGetString)(GL_EXTENSIONS);
		 dbgPrint(DBGLVL_INFO, "EXTENSION STRING: %s\n", extString);
	 }

	 /* Extension names do not contain spaces. */
	 if (!extension || !*extension || strchr(extension, ' ')) {
		 return 0;
	 } 

	 /* check support, take care of substrings! */
	 start = extString;
	 while(1) {
		 const char *s = strstr(start, extension);
		 if (!s) {
			 dbgPrint(DBGLVL_INFO, "not found: %s\n", extension);
			 return 0;
		 }
		 s += strlen(extension);
		 if (*s  == ' ' || *s == '\0') {
			 dbgPrint(DBGLVL_INFO, "found: %s\n", extension);
			 return 1;
		 }
		 start = strchr(s, ' ');
		 if (!start) {
			 dbgPrint(DBGLVL_INFO, "not found: %s\n", extension);
			 return 0;
		 }
		 start++;
	 }
	 dbgPrint(DBGLVL_INFO, "not found: %s\n", extension);
	 return 0;
}

int checkGLVersionSupported(int majorVersion, int minorVersion)
{
	static int major = 0;
	static int minor = 0;
		
	dbgPrint(DBGLVL_INFO, "GL version %i.%i: ", majorVersion, minorVersion);
	if (major == 0) {
		const char *versionString = (char*)ORIG_GL(glGetString)(GL_VERSION);
		const char *rendererString = (char*)ORIG_GL(glGetString)(GL_RENDERER);
		const char *vendorString = (char*)ORIG_GL(glGetString)(GL_VENDOR);
		char  *dot = NULL;
		major = (int)strtol(versionString, &dot, 10);
		minor = (int)strtol(++dot, NULL, 10);
		dbgPrint(DBGLVL_INFO, "GL VENDOR: %s\n", rendererString);
		dbgPrint(DBGLVL_INFO, "GL RENDERER: %s\n", rendererString);
		dbgPrint(DBGLVL_INFO, "GL VERSION: %s\n", versionString);
		checkGLExtensionSupported(NULL);
	}
	if (majorVersion < major ||
	    (majorVersion == major && minorVersion <= minor)) {
		return 1;
	}
	dbgPrint(DBGLVL_INFO, "required GL version supported: NO\n");
	return 0;
}

TFBVersion getTFBVersion()
{
	if (checkGLExtensionSupported("GL_NV_transform_feedback")) {
		return TFBVersion_NV;
	} else if (checkGLExtensionSupported("GL_EXT_transform_feedback")) {
		return TFBVersion_EXT;
	} else {
		return TFBVersion_None;
	}
}


#ifdef RTLD_DEEPBIND
void *dlsym(void *handle, const char *symbol)
{
	char *s;
	void *sym;
	
	if (!g.origdlsym) {
		void *origDlsymHandle;
		
		s = getenv("GLSL_DEBUGGER_LIBDLSYM");
		if (!s) {
			dbgPrint(DBGLVL_ERROR, "Strange, GLSL_DEBUGGER_LIBDLSYM is not set??\n");
			exit(1);
		}
        
	    if (! (origDlsymHandle = dlopen(s, RTLD_LAZY | RTLD_DEEPBIND))) {
    	    dbgPrint(DBGLVL_ERROR, "getting origDlsymHandle failed %s: %s\n",
			         s, dlerror());
    	}
		dlclose(origDlsymHandle);
		s = getenv("GLSL_DEBUGGER_DLSYM");
		if (s) {
			g.origdlsym = (void *(*)(void *, const char *))(intptr_t)strtoll(s, NULL, 16);
		} else {
			dbgPrint(DBGLVL_ERROR, "Strange, GLSL_DEBUGGER_DLSYM is not set??\n");
			exit(1);
		}
		unsetenv("GLSL_DEBUGGER_DLSYM");
	}
	
	if (g.initialized) {
		sym = (void*)glXGetProcAddressHook((GLubyte *)symbol);
		if (sym) {
			return sym;
		}
	}
	
	return g.origdlsym(handle, symbol);
}
#endif

