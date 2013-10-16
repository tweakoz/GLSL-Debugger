all:
	scons -f root.sconstruct --site-dir ./ork.build/site_scons

env:
	./ork.build/bin/ork.build.int_env.py

.PHONY: docs

docs: .
	rm -rf docs/html_doxygen
	doxygen docs/ork.doxygen

clean:
	scons -c -f root.sconstruct --site-dir ./ork.build/site_scons
	rm -rf stage/include/ork

install:
	scons -f root.sconstruct --site-dir ./ork.build/site_scons install

scrdir := glsl_debugger/scripts
gendir := glsl_debugger/gen
incdir := glsl_debugger/inc
ipodir := $(gendir)/gl_interpose
enudir := $(gendir)/glenumerants

gen:
	perl -I $(scrdir) $(scrdir)/genFunctionHooks.pl > $(ipodir)/functionHooks.inc
	perl -I $(scrdir) $(scrdir)/genFunctionPointerTypes.pl > $(ipodir)/functionPointerTypes.inc
	perl -I $(scrdir) $(scrdir)/genGetProcAddressHook.pl > $(ipodir)/getProcAddressHook.inc
	perl -I $(scrdir) $(scrdir)/genFunctionList.pl > $(ipodir)/functionList.c
	perl -I $(scrdir) $(scrdir)/genReplayFunc.pl > $(ipodir)/replayFunction.c
	perl -I $(scrdir) $(scrdir)/genEnumerants.pl > $(enudir)/enumerants.h $(incdir)/GL/gl.h $(incdir)/GL/glext.h
	perl -I $(scrdir) $(scrdir)/genGLXEnumerants.pl > $(enudir)/glxenumerants.h glsl_debugger/inc/GL/glx.h glsl_debugger/inc/GL/glxext.h
	
