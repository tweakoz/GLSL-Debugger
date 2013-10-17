#!/bin/bash
cd $GLSLCC_DIR/src/glslang/MachineIndependent
flex -o Gen_glslang.cpp glslang.l 
dos2unix glslang.y
bison -t -v -d glslang.y
echo "#include \"../../../glsl_debugger/inc/glsldebug_utils/dbgprint.h\"" > Gen_glslang_tab.cpp
perl -ne "s/(fprintf|YYFPRINTF)\s*\([^,]*\s*/dbgPrint\(DBGLVL_COMPILERINFO/;print;" glslang.tab.c >> Gen_glslang_tab.cpp
rm glslang.tab.c
mv glslang.tab.h glslang_tab.h
cd $GLSLCC_DIR/../