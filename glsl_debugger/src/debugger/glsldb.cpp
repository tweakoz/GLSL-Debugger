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

#include <stdio.h>
#include <stdlib.h>
#include <list>
#include <string>
#include <unistd.h>
#include <signal.h>
#include <execinfo.h>

#include <string.h>
#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/wait.h>

#include <QtGui/QApplication>
#include <QtCore/QStringList>
#include <QtCore/QDir>
#include "mainWindow.qt.h"
#include "notify.h"
#include "build-config.h"
#include <ork/environment.h>

extern "C" {
  #include "GL/gl.h"
  #include "GL/glext.h"
  #include "debuglib.h"
  #include <enumerants_common/glenumerants.h>
  #include <glsldebug_utils/p2pcopy.h>
}

#  define DIRSEP '/'

void handler(int UNUSED sig)
{
	void *buf[MAX_BACKTRACE_DEPTH];
	int size = backtrace(buf, MAX_BACKTRACE_DEPTH);
	fprintf(stderr, "**************** SEGMENTATION FAULT - BEGIN BACKTRACE ****************\n");
	backtrace_symbols_fd(buf, size, STDERR_FILENO);
	fprintf(stderr, "**************** SEGMENTATION FAULT - END BACKTRACE   ****************\n");
	if(size == MAX_BACKTRACE_DEPTH)
		fprintf(stderr, "Warning: backtrace might have been truncated");
	exit(EXIT_FAILURE);
}

void setNotifyLevel(int l)
{
	severity_t t;
	switch (l) {
    	case 0:
			t = LV_FATAL;
			break;
		case 1:
			t = LV_ERROR;
			break;
		case 2:
			t = LV_WARN;
			break;
		case 3:
			t = LV_INFO;
			break;
		case 4:
			t = LV_DEBUG;
			break;
		case 5:
			t = LV_TRACE;
			break;
		default:
			t = LV_INFO;
	}
	UTILS_NOTIFY_LEVEL(&t);
}
QStringList parseArguments(int argc, char** argv)
{
	int opt = getopt(argc, argv, "+hv:f");
	bool abort = false;
	while(opt != -1) {
		switch(opt) {
			case 'h':
				std::cout << "Usage: " << argv[0] << " [options] debuggee [debugee_options]" << std::endl;
				std::cout << "  -h      : this help message" << std::endl; 
				std::cout << "  -v value: log level from 0 (FATAL) to 5 (LV_TRACE) " << std::endl; 
				std::cout << "  -f value: log to file \"value\"" << std::endl;
				exit(EXIT_SUCCESS);
			case 'v':
				setNotifyLevel(atoi(optarg));
				break;
			default:
				std::cout << "def" << std::endl;
				abort = true;
		}
		if(abort)
			break;
		opt = getopt(argc, argv, "+hv:f");
	}
	int i = optind;
	QStringList al;
	while(i < argc)
		al.push_back(argv[i++]);
	return al;
}

ork::Environment genviron;

int main(int argc, char **argv, char** argp)
{
	genviron.init_from_envp(argp);

	std::string glsldb_dir;

	if( false == genviron.get( "GLSLDB_DIR", glsldb_dir) )
	{
		printf( "GLSLDB_DIR not set!\n" );
		return -1;
	}

	///////////////////////////////////////
	// set QT asset search path
	///////////////////////////////////////

	std::string asset_dir = glsldb_dir+"/assets";

	printf( "asset_dir<%s>\n", asset_dir.c_str() );

	QDir::setSearchPaths("assets", QStringList(asset_dir.c_str())) ;

	QStringList al = parseArguments(argc, argv);

	///////////////////////////////////////
	// activate backtracing if log level is high enough
	///////////////////////////////////////

	if(UTILS_NOTIFY_LEVEL(0) > LV_INFO)
		signal(SIGSEGV, handler);

	///////////////////////////////////////

    QApplication app(argc, argv);

	QCoreApplication::setOrganizationName("VIS");
	QCoreApplication::setOrganizationDomain("vis.uni-stuttgart.de");
	QCoreApplication::setApplicationName("glsldevil");

	// we need both for now...
	UTILS_NOTIFY_STARTUP();
	startLogging("glsldevil");
	setMaxDebugOutputLevel(DBGLVL_DEBUG);

	UT_NOTIFY(LV_INFO, "Application startup.");

    MainWindow mainWin(argv[0], al);

    mainWin.show();

    int returnValue = app.exec();

	UTILS_NOTIFY_SHUTDOWN();
	quitLogging();
	return returnValue;
}

