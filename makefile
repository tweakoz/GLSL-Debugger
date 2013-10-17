scrdir := glsl_debugger/scripts
gendir := glsl_debugger/gen
incdir := glsl_debugger/inc
ipodir := $(gendir)/enumerants_runtime
enurdir := $(gendir)/enumerants_runtime
enucdir := $(gendir)/enumerants_common

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
	rm -rf $(enurdir)/*
	rm -rf $(enucdir)/*
	rm -f glsl_compiler/src/glslang/MachineIndependent/Gen*.cpp
	make gen

install:
	scons -f root.sconstruct --site-dir ./ork.build/site_scons install

gen:
	mkdir -p $(gendir)/gl_interpose
	mkdir -p $(enurdir)
	mkdir -p $(enucdir)
	perl -I $(scrdir) $(scrdir)/genFunctionHooks.pl > $(ipodir)/functionHooks.inc
	perl -I $(scrdir) $(scrdir)/genFunctionPointerTypes.pl > $(ipodir)/functionPointerTypes.inc
	perl -I $(scrdir) $(scrdir)/genGetProcAddressHook.pl > $(ipodir)/getProcAddressHook.inc
	perl -I $(scrdir) $(scrdir)/genFunctionList.pl > $(enucdir)/functionList.c
	perl -I $(scrdir) $(scrdir)/genReplayFunc.pl > $(enurdir)/replayFunction.c
	perl -I $(scrdir) $(scrdir)/genEnumerants.pl > $(enucdir)/enumerants.h $(incdir)/GL/gl.h $(incdir)/GL/glext.h
	perl -I $(scrdir) $(scrdir)/genGLXEnumerants.pl > $(enucdir)/glxenumerants.h $(incdir)/GL/glx.h $(incdir)/GL/glxext.h
	sh glsl_compiler/src/glslang/MachineIndependent/postProcess.sh
	
