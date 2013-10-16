#ifndef _CODE_TOOLS_
#define _CODE_TOOLS_

#include <glslang/ShaderLang.h>
#include <glslang_private/Common.h>
#include <glslang_private/intermediate.h>

#define MAIN_FUNC_SIGNATURE "main("

char* getFunctionName(const TString &in);
TIntermNode* getFunctionBySignature(const char *sig, TIntermNode* root);
bool isChildofMain(TIntermNode *node, TIntermNode *root);
int getFunctionDebugParameter(TIntermAggregate *node);
TType* getTypeDebugParameter(TIntermAggregate *node, int pnum);
bool getAtomicDebugParameter(TIntermAggregate *node, int pnum);
bool getHasSideEffectsDebugParameter(TIntermAggregate *node, int pnum);
bool isPartofMain(TIntermNode *target, TIntermNode *root);
bool isPartofNode(TIntermNode *target, TIntermNode *node);

#endif

