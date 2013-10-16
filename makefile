scrdir := glsl_debugger/scripts
gendir := glsl_debugger/gen
incdir := glsl_debugger/inc
ipodir := $(gendir)/gl_interpose
enudir := $(gendir)/glenumerants

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
	rm -rf $(gendir)/gl_interpose/*
	rm -rf $(gendir)/glenumerants/*

install:
	scons -f root.sconstruct --site-dir ./ork.build/site_scons install

gen:
	mkdir -p $(gendir)/gl_interpose
	mkdir -p $(gendir)/glenumerants
	perl -I $(scrdir) $(scrdir)/genFunctionHooks.pl > $(ipodir)/functionHooks.inc
	perl -I $(scrdir) $(scrdir)/genFunctionPointerTypes.pl > $(ipodir)/functionPointerTypes.inc
	perl -I $(scrdir) $(scrdir)/genGetProcAddressHook.pl > $(ipodir)/getProcAddressHook.inc
	perl -I $(scrdir) $(scrdir)/genFunctionList.pl > $(ipodir)/functionList.c
	perl -I $(scrdir) $(scrdir)/genReplayFunc.pl > $(ipodir)/replayFunction.c
	perl -I $(scrdir) $(scrdir)/genEnumerants.pl > $(enudir)/enumerants.h $(incdir)/GL/gl.h $(incdir)/GL/glext.h
	perl -I $(scrdir) $(scrdir)/genGLXEnumerants.pl > $(enudir)/glxenumerants.h $(incdir)/GL/glx.h $(incdir)/GL/glxext.h
	
