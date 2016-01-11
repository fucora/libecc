//
//  object.h
//  libecc
//
//  Created by Bouilland Aurélien on 04/07/2015.
//  Copyright (c) 2015 Libeccio. All rights reserved.
//

#ifndef io_libecc_object_h
#define io_libecc_object_h

#include "namespace_io_libecc.h"

#include "value.h"
#include "key.h"
#include "pool.h"

#include "interface.h"


enum Object(Flags)
{
	Object(mark) = 1 << 0,
	Object(sealed) = 1 << 1,
};

extern struct Object * Object(prototype);
extern struct Function * Object(constructor);


Interface(Object,
	
	(void, setup ,(void))
	(void, teardown ,(void))
	
	(struct Object *, create ,(struct Object * prototype))
	(struct Object *, createSized ,(struct Object * prototype, uint32_t size))
	(struct Object *, createTyped ,(const struct Text *type))
	(struct Object *, initialize ,(struct Object * restrict, struct Object * restrict prototype))
	(struct Object *, initializeSized ,(struct Object * restrict, struct Object * restrict prototype, uint32_t size))
	(struct Object *, finalize ,(struct Object *))
	(struct Object *, copy ,(const struct Object * original))
	(void, destroy ,(struct Object *))
	
	(struct Value, get ,(struct Object *, struct Key))
	(struct Value *, getOwnMember ,(struct Object *, struct Key))
	(struct Value *, getMember ,(struct Object *, struct Key))
	(struct Value *, getOwnProperty ,(struct Object *, struct Value))
	(struct Value *, getProperty ,(struct Object *, struct Value))
	(struct Value *, setProperty ,(struct Object *, struct Value, struct Value))
	(struct Value *, add ,(struct Object *, struct Key, struct Value, enum Value(Flags)))
	(int, delete ,(struct Object *, struct Key))
	(int, deleteProperty ,(struct Object *, struct Value))
	(void, packValue ,(struct Object *))
	(void, stripMap ,(struct Object *))
	
	(void, resizeElement ,(struct Object *, uint32_t size))
	(struct Value *, addElementAtIndex ,(struct Object *, uint32_t index, struct Value, enum Value(Flags)))
	
	(void, dumpTo ,(struct Object *, FILE *file))
	,
	{
		struct Object *prototype;
		
		const struct Text *type;
		
		struct {
			struct {
				struct Value value;
			} data;
		} *element;
		
		union {
			uint16_t slot[16];
			struct {
				struct Value value;
				struct Key key;
			} data;
		} *hashmap;
		
		uint32_t elementCount;
		uint32_t elementCapacity;
		uint16_t hashmapCount;
		uint16_t hashmapCapacity;
		
		uint8_t flags;
	}
)

#ifndef io_libecc_lexer_h
#include "ecc.h"
#include "string.h"
#include "array.h"
#endif

#endif
