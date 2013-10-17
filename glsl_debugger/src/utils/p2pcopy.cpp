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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#ifdef __APPLE__
	#include "osx_ptrace_defs.h"
#endif /* __APPLE __ */

#include <sys/ptrace.h>

#define _GNU_SOURCE
#include <stdio.h>

#include <glsldebug_utils/dbgprint.h>
#include <glsldebug_utils/p2pcopy.h>


extern "C" {

typedef intptr_t ALIGNED_DATA;

void cpyFromProcess(pid_t pid, void *dst, void *src, size_t size)
{
	ALIGNED_DATA start, *buffer;
	size_t count;
	int i;

	/* Round starting address down to word boundary */
	start = (ALIGNED_DATA)src & -(ALIGNED_DATA)sizeof(ALIGNED_DATA);

	/* number of words to copy */
	count = ((((ALIGNED_DATA)src + size) - start) + sizeof(ALIGNED_DATA) - 1)/sizeof(ALIGNED_DATA);
	  
	buffer = (ALIGNED_DATA*)malloc(count*sizeof(ALIGNED_DATA));
	if (!buffer) {
		dbgPrint(DBGLVL_ERROR, "cpyFromProcess: Could not allocate buffer\n");
		exit(1);
	}

	/* read data from other process, word by word :-( */
	for (i = 0; i < count; i++, start += sizeof(ALIGNED_DATA)) {
		buffer[i] = ptrace(PTRACE_PEEKTEXT, pid, (void*)start, 0);
	}

	/* Copy appropriate bytes out of the buffer.  */
	memcpy (dst, (char*)buffer + ((ALIGNED_DATA)src & (sizeof(ALIGNED_DATA) - 1)), size);
	
	free(buffer);
}

void cpyToProcess(pid_t pid, void *dst, void *src, size_t size)
{
	ALIGNED_DATA start, *buffer;
	size_t count;
	int i;
    
	/* Round starting address down to word boundary */
	start = (ALIGNED_DATA)dst & -(ALIGNED_DATA)sizeof(ALIGNED_DATA);
	
	/* number of words to copy */
	count = ((((ALIGNED_DATA)dst + size) - start) + sizeof(ALIGNED_DATA) - 1)/sizeof(ALIGNED_DATA);
		
	buffer = (ALIGNED_DATA*)malloc(count*sizeof(ALIGNED_DATA));
	if (!buffer) {
		dbgPrint(DBGLVL_ERROR, "cpyFromProcess: Could not allocate buffer\n");
		exit(1);
	}
	
	/* fill extra bytes at start and end of buffer with existing data */
	buffer[0] = ptrace(PTRACE_PEEKTEXT, pid, (void*)start, 0);
	if (count > 1) {
		buffer[count - 1] = ptrace(PTRACE_PEEKTEXT, pid,
	                               (void*)(start + (count - 1)*sizeof(ALIGNED_DATA)), 0);
    }

	/* copy data */
	memcpy ((char*)buffer + ((ALIGNED_DATA)dst & (sizeof(ALIGNED_DATA) - 1)), src, size);

	/* write buffer */
	for (i = 0; i < count; i++) {
		ptrace(PTRACE_POKETEXT, pid, (void*)start, buffer[i]);
		start += sizeof(ALIGNED_DATA);
	}
	free(buffer);
}

}
