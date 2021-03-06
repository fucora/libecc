//
//  string.h
//  libecc
//
//  Copyright (c) 2019 Aurélien Bouilland
//  Licensed under MIT license, see LICENSE.txt file in project root
//

#ifndef io_libecc_string_h
#ifdef Implementation
#undef Implementation
#include __FILE__
#include "../implementation.h"
#else
#include "../interface.h"
#define io_libecc_string_h

	#include "global.h"

	extern struct Object * String(prototype);
	extern struct Function * String(constructor);
	extern const struct Object(Type) String(type);

#endif


Interface(String,
	
	(void, setup ,(void))
	(void, teardown ,(void))
	
	(struct String *, create ,(struct Chars *))
	(struct Value, valueAtIndex ,(struct String *, int32_t index))
	
	(struct Text, textAtIndex ,(const char *chars, int32_t length, int32_t index, int enableReverse))
	(int32_t, unitIndex ,(const char *chars, int32_t max, int32_t unit))
	,
	{
		struct Object object;
		struct Chars *value;
	}
)

#endif
