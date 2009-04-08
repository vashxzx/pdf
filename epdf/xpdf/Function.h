//========================================================================
//
// Function.h
//
// Copyright 2001-2002 Glyph & Cog, LLC
//
//========================================================================

#ifndef FUNCTION_H
#define FUNCTION_H

#ifdef __GNUC__
#pragma interface
#endif

#include "../aconf.h"
#include "../goo/gtypes.h"
#include "Object.h"

class GString;
class Dict;
class Stream;
struct PSObject;
class PSStack;

//------------------------------------------------------------------------
// Function
//------------------------------------------------------------------------

#define funcMaxInputs  8
#define funcMaxOutputs 8

class Function {
public:

  Function();

  virtual ~Function();

  // Construct a function.  Returns NULL if unsuccessful.
  static Function *parse(Object *funcObj);

  // Initialize the entries common to all function types.
  GBool init(Dict *dict);

  virtual Function *copy() = 0;

  // Return size of input and output tuples.
  int getInputSize() { return m; }
  int getOutputSize() { return n; }

  // Transform an input tuple into an output tuple.
  virtual void transform(fouble *in, fouble *out) = 0;

  virtual GBool isOk() = 0;

protected:

  int m, n;			// size of input and output tuples
  fouble			// min and max values for function domain
    domain[funcMaxInputs][2];
  fouble			// min and max values for function range
    range[funcMaxOutputs][2];
  GBool hasRange;		// set if range is defined
};

//------------------------------------------------------------------------
// IdentityFunction
//------------------------------------------------------------------------

class IdentityFunction: public Function {
public:

  IdentityFunction();
  virtual ~IdentityFunction();
  virtual Function *copy() { return new IdentityFunction(); }
  virtual void transform(fouble *in, fouble *out);
  virtual GBool isOk() { return gTrue; }

private:
};

//------------------------------------------------------------------------
// SampledFunction
//------------------------------------------------------------------------

class SampledFunction: public Function {
public:

  SampledFunction(Object *funcObj, Dict *dict);
  virtual ~SampledFunction();
  virtual Function *copy() { return new SampledFunction(this); }
  virtual void transform(fouble *in, fouble *out);
  virtual GBool isOk() { return ok; }

private:

  SampledFunction(SampledFunction *func);

  int				// number of samples for each domain element
    sampleSize[funcMaxInputs];
  fouble			// min and max values for domain encoder
    encode[funcMaxInputs][2];
  fouble			// min and max values for range decoder
    decode[funcMaxOutputs][2];
  fouble *samples;		// the samples
  GBool ok;
};

//------------------------------------------------------------------------
// ExponentialFunction
//------------------------------------------------------------------------

class ExponentialFunction: public Function {
public:

  ExponentialFunction(Object *funcObj, Dict *dict);
  virtual ~ExponentialFunction();
  virtual Function *copy() { return new ExponentialFunction(this); }
  virtual void transform(fouble *in, fouble *out);
  virtual GBool isOk() { return ok; }

private:

  ExponentialFunction(ExponentialFunction *func);

  fouble c0[funcMaxOutputs];
  fouble c1[funcMaxOutputs];
  fouble e;
  GBool ok;
};

//------------------------------------------------------------------------
// StitchingFunction
//------------------------------------------------------------------------

class StitchingFunction: public Function {
public:

  StitchingFunction(Object *funcObj, Dict *dict);
  virtual ~StitchingFunction();
  virtual Function *copy() { return new StitchingFunction(this); }
  virtual void transform(fouble *in, fouble *out);
  virtual GBool isOk() { return ok; }

private:

  StitchingFunction(StitchingFunction *func);

  int k;
  Function **funcs;
  fouble *bounds;
  fouble *encode;
  GBool ok;
};

//------------------------------------------------------------------------
// PostScriptFunction
//------------------------------------------------------------------------

class PostScriptFunction: public Function {
public:

  PostScriptFunction(Object *funcObj, Dict *dict);
  virtual ~PostScriptFunction();
  virtual Function *copy() { return new PostScriptFunction(this); }
  virtual void transform(fouble *in, fouble *out);
  virtual GBool isOk() { return ok; }

private:

  PostScriptFunction(PostScriptFunction *func);
  GBool parseCode(Stream *str, int *codePtr);
  GString *getToken(Stream *str);
  void resizeCode(int newSize);
  void exec(PSStack *stack, int codePtr);

  PSObject *code;
  int codeSize;
  GBool ok;
};

#endif
