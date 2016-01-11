//
//  function.h
//  libecc
//
//  Created by Bouilland Aurélien on 15/07/2015.
//  Copyright (c) 2015 Libeccio. All rights reserved.
//

#ifndef io_libecc_function_h
#define io_libecc_function_h

#include "namespace_io_libecc.h"

#include "object.h"
#include "native.h"

#include "interface.h"


enum Function(Flags) {
	Function(needHeap)      = 1 << 1,
	Function(needArguments) = 1 << 2,
	Function(isGetter)      = 1 << 3,
	Function(isSetter)      = 1 << 4,
	Function(isAccessor)    = Function(isGetter) | Function(isSetter),
};

extern struct Object * Function(prototype);
extern struct Function * Function(constructor);


Interface(Function,
	
	(void, setup ,(void))
	(void, teardown ,(void))
	
	(struct Function *, create ,(struct Object *context))
	(struct Function *, createSized ,(struct Object *context, uint32_t size))
	(struct Function *, createWithNative ,(const Native native, int parameterCount))
	(struct Function *, createWithNativeAccessor ,(const Native getter, const Native setter))
	(struct Function *, copy ,(struct Function * original))
	(void, destroy ,(struct Function *))
	
	(void, addValue ,(struct Function *, const char *name, struct Value value, enum Value(Flags)))
	(struct Function *, addNative ,(struct Function *, const char *name, const Native native, int argumentCount, enum Value(Flags)))
	(struct Function *, addToObject ,(struct Object *object, const char *name, const Native native, int parameterCount, enum Value(Flags)))
	
	(void, linkPrototype ,(struct Function *, struct Object *prototype))
	
	(uint16_t, toBufferLength ,(struct Function *))
	(uint16_t, toBuffer ,(struct Function *, char *buffer, uint16_t length))
	,
	{
		struct Object object;
		struct Object context;
		struct OpList *oplist;
		struct Function *pair;
		struct Text text;
		const char *name;
		int parameterCount;
		enum Function(Flags) flags;
	}
)

#endif
