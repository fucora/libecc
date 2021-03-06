//
//  function.h
//  libecc
//
//  Copyright (c) 2019 Aurélien Bouilland
//  Licensed under MIT license, see LICENSE.txt file in project root
//

#ifndef io_libecc_function_h
#ifdef Implementation
#undef Implementation
#include __FILE__
#include "../implementation.h"
#else
#include "../interface.h"
#define io_libecc_function_h

	#include "global.h"
	#include "../context.h"
	#include "../native.h"
	#include "../chars.h"

	enum Function(Flags) {
		Function(needHeap)      = 1 << 1,
		Function(needArguments) = 1 << 2,
		Function(useBoundThis)  = 1 << 3,
		Function(strictMode)    = 1 << 4,
	};

	extern struct Object * Function(prototype);
	extern struct Function * Function(constructor);
	extern const struct Object(Type) Function(type);

#endif


Interface(Function,
	
	(void, setup ,(void))
	(void, teardown ,(void))
	
	(struct Function *, create ,(struct Object *environment))
	(struct Function *, createSized ,(struct Object *environment, uint32_t size))
	(struct Function *, createWithNative ,(const Native(Function) native, int parameterCount))
	(struct Function *, copy ,(struct Function * original))
	(void, destroy ,(struct Function *))
	
	(void, addMember ,(struct Function *, const char *name, struct Value value, enum Value(Flags)))
	(void, addValue ,(struct Function *, const char *name, struct Value value, enum Value(Flags)))
	(struct Function *, addMethod ,(struct Function *, const char *name, const Native(Function) native, int argumentCount, enum Value(Flags)))
	(struct Function *, addFunction ,(struct Function *, const char *name, const Native(Function) native, int argumentCount, enum Value(Flags)))
	(struct Function *, addToObject ,(struct Object *object, const char *name, const Native(Function) native, int parameterCount, enum Value(Flags)))
	
	(void, linkPrototype ,(struct Function *, struct Value prototype, enum Value(Flags)))
	(void, setupBuiltinObject ,(struct Function **, const Native(Function), int parameterCount, struct Object **, struct Value prototype, const struct Object(Type) *type))
	
	(struct Value, accessor ,(const Native(Function) getter, const Native(Function) setter))
	,
	{
		struct Object object;
		struct Object environment;
		struct Object *refObject;
		struct OpList *oplist;
		struct Function *pair;
		struct Value boundThis;
		struct Text text;
		const char *name;
		int parameterCount;
		enum Function(Flags) flags;
	}
)

#endif
