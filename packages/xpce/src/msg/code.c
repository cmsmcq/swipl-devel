/*  $Id$

    Part of XPCE
    Designed and implemented by Anjo Anjewierden and Jan Wielemaker
    E-mail: jan@swi.psy.uva.nl

    Copyright (C) 1992 University of Amsterdam. All rights reserved.
*/

#define INLINE_UTILITIES 1
#include <h/kernel.h>

status
initialiseCode(Code c)
{ return initialiseProgramObject(c);
}

		/********************************
		*           FORWARDING		*
		********************************/

static status
forwardVarsCodev(Code c, int argc, Assignment *argv)
{ status rval;
  int errors = 0;
  int i;

  withLocalVars({ for(i=0; i<argc; i++, argv++)
		  { Any value;

		    if ( (value = expandCodeArgument(argv[0]->value)) )
		    { assignVar(argv[0]->var, value, NAME_local);
		      if ( argv[0]->var == RECEIVER && isObject(value) )
			assignVar(RECEIVER_CLASS, classOfObject(value),
				  NAME_local);
		    } else
		    { errors++;
		      break;
		    }
		  }

		  rval = (errors ? FAIL : executeCode(c));
		});

  return rval;
}



/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
		      FORWARDING WITH PUSH OF @RECEIVER

The test of `if ( RECEIVER->value != receiver )' is dubious: we should
check whether the message actually is send to @receiver
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

status
forwardReceiverCodev(Code c, Any receiver, int argc, const Any argv[])
{ if ( RECEIVER->value != receiver )
  { Any receiver_save = RECEIVER->value;
    Any receiver_class_save = RECEIVER_CLASS->value;
    status rval;

    RECEIVER->value = receiver;
    RECEIVER_CLASS->value = classOfObject(receiver);
    rval = forwardCodev(c, argc, argv);
    RECEIVER_CLASS->value = receiver_class_save;
    RECEIVER->value = receiver_save;

    return rval;
  } else
    return forwardCodev(c, argc, argv);
}


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
			  VECTOR BASED FORWARDING
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static status
forwardVectorCodev(Code c, int argc, const Any argv[])
{ Vector v;
  int shift;
  int args;

  if ( argc == 0 )
    goto usage;
  if ( argc >= 2 && isInteger(argv[argc-1]) )
  { v = argv[argc-2];
    shift = valInt(argv[argc-1]);
    args = argc-2;
  } else
  { v = argv[argc-1];
    shift = 0;
    args = argc-1;
  }

  if ( !instanceOfObject(v, ClassVector) )
    goto usage;
  else
  { int argn = args+valInt(v->size)-shift;
    ArgVector(av, args+valInt(v->size)-shift);
    int i, n;

    for(i=0; i<args; i++)
      av[i] = argv[i];
    for(n=shift; n<=valInt(v->size); n++)
      av[i++] = v->elements[n];

    return forwardCodev(c, argn, av);
  }

usage:
  return errorPce(c, NAME_badVectorUsage);
}


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
			  ARGLIST CODE INVOKATION
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

status
forwardCode(Code c, ...)
{ va_list args;
  Any argv[VA_PCE_MAX_ARGS];
  int argc;

  va_start(args, c);
  for(argc=0; (argv[argc] = va_arg(args, Any)) != NULL; argc++)
    assert(argc <= VA_PCE_MAX_ARGS);
  va_end(args);
  
  return forwardCodev(c, argc, argv);
}


status
forwardReceiverCode(Code c, Any rec, ...)
{ va_list args;
  Any argv[VA_PCE_MAX_ARGS];
  int argc;

  va_start(args, rec);
  for(argc=0; (argv[argc] = va_arg(args, Any)) != NULL; argc++)
    assert(argc <= VA_PCE_MAX_ARGS);
  va_end(args);
  
  return forwardReceiverCodev(c, rec, argc, argv);
}


static status
ExecuteCode(Code c)
{ Class cl = classOfObject(c);

  FixGetFunctionClass(cl, NAME_Execute);
  if ( cl->get_function )
    return (*cl->get_function)(c) ? SUCCEED : FAIL;

  return errorPce(c, NAME_cannotExecute);
}

static Any
getExecuteCode(Code c)
{ errorPce(c, NAME_noFunction);

  fail;
}


#ifndef O_RUNTIME
static void
traceCode(Code c)
{ Any f;
  int arity;

  f = get(c, NAME_functor, 0);
  writef("%s", f);
  arity = valInt(get(c, NAME_Arity, 0));
  if ( arity > 0 )
  { int i;

    writef("(");
    for(i=1; i<=arity; i++)
    { Any arg = get(c, NAME_Arg, toInt(i), 0);

      if ( i == 1 )
	writef("%O", arg);
      else
	writef(", %O", arg);
    }
    writef(")");
  }
}
#endif /*O_RUNTIME*/


		/********************************
		*         CLASS CODE_VECTOR	*
		********************************/

Vector
createCodeVectorv(int argc, const Any argv[])
{ Vector v = alloc(sizeof(struct vector));
  int n;

  initHeaderObj(v, ClassCodeVector);
  v->offset      = ZERO;
  v->size        = toInt(argc);
  v->elements    = alloc(argc * sizeof(Any));

  for(n=0; n < argc; n++)
  { v->elements[n] = argv[n];
    if ( isObject(argv[n]) && !isProtectedObj(argv[n]) )
      addRefObj(argv[n]);
  }

  clearCreatingObj(v);

  return v;
}


static status
unlinkCodeVector(Vector v)
{ if ( v->elements != NULL )
  { int size = valInt(v->size);
    int n;
    Any *argv = v->elements;

    for(n=0; n<size; n++)
    { if ( isObject(argv[n]) && !isProtectedObj(argv[n]) )
	delRefObj(argv[n]);
    }

    unalloc(valInt(v->size)*sizeof(Any), v->elements);
    v->elements = NULL;
  }

  succeed;
}


void
doneCodeVector(Vector v)
{ if ( isVirginObj(v) )
  { unlinkCodeVector(v);
    unalloc(sizeof(struct vector), v);
  }
}


		 /*******************************
		 *	 CLASS DECLARATION	*
		 *******************************/

/* Type declarations */

static char *T_element[] =
        { "index=int", "value=any|function" };
static char *T_fill[] =
        { "value=any|function", "from=[int]", "to=[int]" };

/* Instance Variables */

#define var_codeVector NULL
/*
static vardecl var_codeVector[] =
{ 
};
*/

/* Send Methods */

static senddecl send_codeVector[] =
{ SM(NAME_append, 1, "value=any|function ...", appendVector,
     DEFAULT, NULL),
  SM(NAME_element, 2, T_element, elementVector,
     DEFAULT, NULL),
  SM(NAME_fill, 3, T_fill, fillVector,
     DEFAULT, NULL),
  SM(NAME_initialise, 1, "element=any|function ...", initialiseVectorv,
     DEFAULT, NULL),
  SM(NAME_unlink, 0, NULL, unlinkCodeVector,
     DEFAULT, NULL)
};

/* Get Methods */

#define get_codeVector NULL
/*
static getdecl get_codeVector[] =
{ 
};
*/

/* Resources */

#define rc_codeVector NULL
/*
static resourcedecl rc_codeVector[] =
{ 
};
*/

/* Class Declaration */

ClassDecl(codeVector_decls,
          var_codeVector, send_codeVector, get_codeVector, rc_codeVector,
          ARGC_INHERIT, NULL,
          "$Rev$");

status
makeClassCodeVector(Class class)
{ declareClass(class, &codeVector_decls);

  assign(class, un_answer, OFF);
  assign(class, summary, CtoString("Argument vector"));

  succeed;
}


		 /*******************************
		 *	 CLASS DECLARATION	*
		 *******************************/

/* Type declarations */


/* Instance Variables */

#define var_code NULL
/*
static vardecl var_code[] =
{ 
};
*/

/* Send Methods */

static senddecl send_code[] =
{ SM(NAME_execute, 0, NULL, executeCode,
     NAME_execute, "Execute code"),
  SM(NAME_forward, 1, "any ...", forwardCodev,
     NAME_execute, "Push @arg1, ... and execute"),
  SM(NAME_forwardVars, 1, "assign ...", forwardVarsCodev,
     NAME_execute, "Push vars and execute"),
  SM(NAME_forwardVector, 1, "any ...", forwardVectorCodev,
     NAME_execute, "Push @arg1, ... from a vector and execute"),
  SM(NAME_Execute, 0, NULL, ExecuteCode,
     NAME_internal, "Execute the code object (redefined)")
};

/* Get Methods */

static getdecl get_code[] =
{ GM(NAME_Execute, 0, "unchecked", NULL, getExecuteCode,
     NAME_internal, "Execute the function object (error)")
};

/* Resources */

#define rc_code NULL
/*
static resourcedecl rc_code[] =
{ 
};
*/

/* Class Declaration */

ClassDecl(code_decls,
          var_code, send_code, get_code, rc_code,
          0, NULL,
          "$Rev$");


status
makeClassCode(Class class)
{ declareClass(class, &code_decls);

  setTraceFunctionClass(class, traceCode);
  cloneStyleClass(class, NAME_none);
  assign(class, un_answer, OFF);

  succeed;
}

