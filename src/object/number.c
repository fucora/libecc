//
//  number.c
//  libecc
//
//  Created by Bouilland Aurélien on 17/10/2015.
//  Copyright (c) 2015 Libeccio. All rights reserved.
//

#include "number.h"

// MARK: - Private

struct Object * Number(prototype) = NULL;
struct Function * Number(constructor) = NULL;

static struct Value toExponential (const struct Op ** const ops, struct Ecc * const ecc)
{
	struct Value value;
	
	Op.assertParameterCount(ecc, 1);
	
	if (ecc->this.type != Value(number))
		Ecc.jmpEnv(ecc, Value.error(Error.typeError((*ops)->text, "is not an number")));
	
	value = Op.argument(ecc, 0);
	if (value.type != Value(undefined))
	{
		int32_t precision = Value.toInteger(value).data.integer;
		
		if (precision < 0 || precision > 48)
			Ecc.jmpEnv(ecc, Value.error(Error.rangeError((*ops)->text, "precision %d out of range", precision)));
		
		ecc->result = Value.chars(Chars.create("%.*e", precision, ecc->this.data.number->value));
	}
	else
		ecc->result = Value.chars(Chars.create("%e", ecc->this.data.number->value));
	
	return Value.undefined();
}

static struct Value toString (const struct Op ** const ops, struct Ecc * const ecc)
{
	struct Value value;
	int32_t radix = 10;
	
	Op.assertParameterCount(ecc, 1);
	
	if (ecc->this.type != Value(number))
		Ecc.jmpEnv(ecc, Value.error(Error.typeError((*ops)->text, "is not an number")));
	
	value = Op.argument(ecc, 0);
	if (value.type != Value(undefined))
	{
		radix = Value.toInteger(value).data.integer;
		if (radix < 2 || radix > 36)
			Ecc.jmpEnv(ecc, Value.error(Error.rangeError((*ops)->text, "radix must be an integer at least 2 and no greater than 36")));
		
		if (radix != 10 && (ecc->this.data.number->value < LONG_MIN || ecc->this.data.number->value > LONG_MAX))
			Env.printWarning("%g.toString(%d) out of bounds; only long int are supported by radices other than 10", ecc->this.data.number->value, radix);
	}
	
	ecc->result = Value.binaryToString(ecc->this.data.number->value, NULL, radix);
	
	return Value.undefined();
}

static struct Value valueOf (const struct Op ** const ops, struct Ecc * const ecc)
{
	Op.assertParameterCount(ecc, 0);
	if (ecc->this.type != Value(number))
		Ecc.jmpEnv(ecc, Value.error(Error.typeError((*ops)->text, "is not an number")));
	
	ecc->result = Value.binary(ecc->this.data.number->value);
	return Value.undefined();
}

static struct Value constructorFunction (const struct Op ** const ops, struct Ecc * const ecc)
{
	struct Value value;
	
	Op.assertParameterCount(ecc, 1);
	
	value = Value.toBinary(Op.argument(ecc, 0));
	if (ecc->construct)
		ecc->result = Value.number(Number.create(value.data.binary));
	else
		ecc->result = value;
	
	return Value.undefined();
}

// MARK: - Static Members

// MARK: - Methods

void setup ()
{
	const enum Object(Flags) flags = Object(writable) | Object(configurable);
	
	Number(prototype) = Object.create(Object(prototype));
	Function.addToObject(Number(prototype), "toString", toString, 1, flags);
	Function.addToObject(Number(prototype), "valueOf", valueOf, 0, flags);
	Function.addToObject(Number(prototype), "toExponential", toExponential, 1, flags);
	
	Number(constructor) = Function.createWithNative(NULL, constructorFunction, 1);
	Object.add(&Number(constructor)->object, Key.makeWithCString("MAX_VALUE"), Value.binary(DBL_MAX), flags);
	Object.add(&Number(constructor)->object, Key.makeWithCString("MIN_VALUE"), Value.binary(DBL_MIN), flags);
	Object.add(&Number(constructor)->object, Key.makeWithCString("NaN"), Value.binary(NAN), flags);
	Object.add(&Number(constructor)->object, Key.makeWithCString("NEGATIVE_INFINITY"), Value.binary(-INFINITY), flags);
	Object.add(&Number(constructor)->object, Key.makeWithCString("POSITIVE_INFINITY"), Value.binary(INFINITY), flags);
	
	Object.add(Number(prototype), Key(constructor), Value.function(Number(constructor)), 0);
	Object.add(&Number(constructor)->object, Key(prototype), Value.object(Number(prototype)), 0);
}

void teardown (void)
{
	Number(prototype) = NULL;
	Number(constructor) = NULL;
}

struct Number * create (double binary)
{
	struct Number *self = malloc(sizeof(*self));
	*self = Number.identity;
	Pool.addObject(&self->object);
	Object.initialize(&self->object, Number(prototype));
	
	self->object.type = &Text(numberType);
	self->value = binary;
	
	return self;
}
