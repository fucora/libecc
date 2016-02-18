//
//  string.c
//  libecc
//
//  Created by Bouilland Aurélien on 04/07/2015.
//  Copyright (c) 2015 Libeccio. All rights reserved.
//

#include "string.h"

// MARK: - Private

struct Object * String(prototype) = NULL;
struct Function * String(constructor) = NULL;

const struct Object(Type) String(type) = {
	.text = &Text(stringType),
};

static inline int32_t positionIndex (const char *chars, int32_t length, int32_t position, int enableReverse)
{
	int32_t byte;
	if (position >= 0)
	{
		byte = 0;
		while (position--)
			while (byte < length && (chars[++byte] & 0xc0) == 0x80);
	}
	else if (enableReverse)
	{
		byte = length;
		position = -position;
		while (position--)
			while (byte && (chars[--byte] & 0xc0) == 0x80);
	}
	else
		byte = 0;
	
	return byte;
}

static inline int32_t indexPosition (const char *chars, int32_t length, int32_t index)
{
	int32_t byte = 0, position = 0;
	while (index--)
	{
		if (byte < length)
		{
			++position;
			while ((chars[++byte] & 0xc0) == 0x80);
		}
	}
	return position;
}

static struct Value toString (struct Native(Context) * const context)
{
	Native.assertParameterCount(context, 0);
	
	if (context->this.type != Value(stringType))
		Ecc.jmpEnv(context->ecc, Value.error(Error.typeError(Native.textSeek(context, Native(thisIndex)), "not a string")));
	
	return Value.chars(context->this.data.string->value);
}

static struct Value valueOf (struct Native(Context) * const context)
{
	Native.assertParameterCount(context, 0);
	
	if (context->this.type != Value(stringType))
		Ecc.jmpEnv(context->ecc, Value.error(Error.typeError(Native.textSeek(context, Native(thisIndex)), "not a string")));
	
	return Value.chars(context->this.data.string->value);
}

static struct Value charAt (struct Native(Context) * const context)
{
	int32_t position, index, length;
	const char *chars;
	
	Native.assertParameterCount(context, 1);
	
	if (!Value.isString(context->this))
		context->this = Value.toString(context->this);
	
	chars = Value.stringBytes(context->this);
	length = Value.stringLength(context->this);
	
	position = Value.toInteger(Native.argument(context, 0)).data.integer;
	index = positionIndex(chars, length, position, 0);
	length = positionIndex(chars, length, position + 1, 0) - index;
	
	if (length <= 0)
		return Value.text(&Text(empty));
	else
	{
		struct Chars *result = Chars.createSized(length);
		memcpy(result->bytes, chars + index, length);
		return Value.chars(result);
	}
}

static struct Value charCodeAt (struct Native(Context) * const context)
{
	int32_t position, index, length;
	const char *chars;
	
	Native.assertParameterCount(context, 1);
	
	if (!Value.isString(context->this))
		context->this = Value.toString(context->this);
	
	chars = Value.stringBytes(context->this);
	length = Value.stringLength(context->this);
	
	position = Value.toInteger(Native.argument(context, 0)).data.integer;
	index = positionIndex(chars, length, position, 0);
	length = positionIndex(chars, length, position + 1, 0) - index;
	
	if (length <= 0)
		return Value.binary(NAN);
	else
	{
		struct Text text = { chars + index, length };
		return Value.binary(Text.nextCodepoint(&text));
	}
}

static struct Value concat (struct Native(Context) * const context)
{
	struct Chars *result;
	int32_t length = 0, offset = 0, index, count;
	
	Native.assertVariableParameter(context);
	
	count = Native.variableArgumentCount(context);
	
	length += Value.toLength(context->this);
	for (index = 0; index < count; ++index)
		length += Value.toLength(Native.variableArgument(context, index));
	
	result = Chars.createSized(length);
	
	offset += Value.toBytes(context->this, result->bytes + offset);
	for (index = 0; index < count; ++index)
		offset += Value.toBytes(Native.variableArgument(context, index), result->bytes + offset);
	
	return Value.chars(result);
}

static struct Value indexOf (struct Native(Context) * const context)
{
	struct Value search;
	int32_t position, index, offset, length, searchLength, argumentCount;
	const char *chars, *searchChars;
	
	Native.assertVariableParameter(context);
	
	argumentCount = Native.variableArgumentCount(context);
	
	context->this = Value.toString(context->this);
	chars = Value.stringBytes(context->this);
	length = Value.stringLength(context->this);
	
	search = argumentCount >= 1? Value.toString(Native.variableArgument(context, 0)): Value.text(&Text(undefined));
	searchChars = Value.stringBytes(search);
	searchLength = Value.stringLength(search);
	
	length -= searchLength;
	position = argumentCount >= 2? Value.toInteger(Native.variableArgument(context, 1)).data.integer: 0;
	index = positionIndex(chars, length, position, 0);
	
	for (; index <= length; ++index)
	{
		if ((chars[index] & 0xc0) == 0x80)
			continue;
		
		if (chars[index] == searchChars[0])
		{
			offset = 0;
			do
			{
				if (offset >= searchLength - 1)
					return Value.integer(position);
				
				++offset;
			}
			while (chars[index + offset] == searchChars[offset]);
		}
		
		++position;
	}
	
	return Value.integer(-1);
}

static struct Value lastIndexOf (struct Native(Context) * const context)
{
	struct Value search;
	int32_t position, index, offset, length, searchLength, argumentCount;
	const char *chars, *searchChars;
	
	Native.assertVariableParameter(context);
	
	argumentCount = Native.variableArgumentCount(context);
	
	context->this = Value.toString(context->this);
	chars = Value.stringBytes(context->this);
	length = Value.stringLength(context->this);
	
	search = argumentCount >= 1? Value.toString(Native.variableArgument(context, 0)): Value.text(&Text(undefined));
	searchChars = Value.stringBytes(search);
	searchLength = Value.stringLength(search) - 1;
	
	if (argumentCount < 2 || Native.variableArgument(context, 1).type == Value(undefinedType))
		position = indexPosition(chars, length, length);
	else
		position = Value.toInteger(Native.variableArgument(context, 1)).data.integer;
	
	position -= indexPosition(searchChars, searchLength, searchLength);
	index = positionIndex(chars, length, position, 0);
	
	for (; index >= 0; --index)
	{
		if ((chars[index] & 0xc0) == 0x80)
			continue;
		
		if (chars[index] == searchChars[0])
		{
			offset = 0;
			do
			{
				if (offset >= searchLength - 1)
					return Value.integer(position);
				
				++offset;
			}
			while (chars[index + offset] == searchChars[offset]);
		}
		
		--position;
	}
	
	return Value.integer(-1);
}

static struct Value slice (struct Native(Context) * const context)
{
	struct Value from, to;
	int32_t start, end, length;
	const char *chars;
	
	Native.assertParameterCount(context, 2);
	
	if (!Value.isString(context->this))
		context->this = Value.toString(context->this);
	
	chars = Value.stringBytes(context->this);
	length = Value.stringLength(context->this);
	
	from = Native.argument(context, 0);
	if (from.type == Value(undefinedType))
		start = 0;
	else
		start = positionIndex(chars, length, Value.toInteger(from).data.integer, 1);
	
	to = Native.argument(context, 1);
	if (to.type == Value(undefinedType))
		end = length;
	else
		end = positionIndex(chars, length, Value.toInteger(to).data.integer, 1);
	
	length = end - start;
	
	if (length <= 0)
		return Value.text(&Text(empty));
	else
	{
		struct Chars *result = Chars.createSized(length);
		memcpy(result->bytes, chars + start, length);
		return Value.chars(result);
	}
}

static struct Value substring (struct Native(Context) * const context)
{
	struct Value from, to;
	int32_t start, end, length;
	const char *chars;
	
	Native.assertParameterCount(context, 2);
	
	if (!Value.isString(context->this))
		context->this = Value.toString(context->this);
	
	chars = Value.stringBytes(context->this);
	length = Value.stringLength(context->this);
	
	from = Native.argument(context, 0);
	if (from.type == Value(undefinedType))
		start = 0;
	else
		start = positionIndex(chars, length, Value.toInteger(from).data.integer, 0);
	
	to = Native.argument(context, 1);
	if (to.type == Value(undefinedType))
		end = length;
	else
		end = positionIndex(chars, length, Value.toInteger(to).data.integer, 0);
	
	if (start > end)
	{
		int32_t temp = start;
		start = end;
		end = temp;
	}
	
	length = end - start;
	
	if (length <= 0)
		return Value.text(&Text(empty));
	else
	{
		struct Chars *result = Chars.createSized(length);
		memcpy(result->bytes, chars + start, length);
		return Value.chars(result);
	}
}

static struct Value stringConstructor (struct Native(Context) * const context)
{
	struct Value value;
	
	Native.assertParameterCount(context, 1);
	
	value = Native.argument(context, 0);
	if (value.type == Value(undefinedType))
		value = Value.text(&Text(empty));
	else
		value = Value.toString(value);
	
	if (context->construct)
		return Value.string(String.create(Chars.createWithBytes(Value.stringLength(value), Value.stringBytes(value))));
	else
		return value;
}

static struct Value fromCharCode (struct Native(Context) * const context)
{
	int32_t c, index, count, length = 0;
	struct Chars *chars;
	char *b;
	
	Native.assertVariableParameter(context);
	
	for (index = 0, count = Native.variableArgumentCount(context); index < count; ++index)
	{
		c = Value.toInteger(Native.variableArgument(context, index)).data.integer;
		if (c < 0x80) length += 1;
		else if (c < 0x800) length += 2;
		else if (c <= 0xffff) length += 3;
		else length += 1;
	}
	
	chars = Chars.createSized(length);
	b = chars->bytes;
	
	for (index = 0, count = Native.variableArgumentCount(context); index < count; ++index)
	{
		c = Value.toInteger(Native.variableArgument(context, index)).data.integer;
		if (c < 0x80) *b++ = c;
		else if (c < 0x800) *b++ = 192 + c / 64, *b++ = 128 + c % 64;
		else if (c <= 0xffff) *b++ = 224 + c / 4096, *b++ = 128 + c / 64 % 64, *b++ = 128 + c % 64;
		else *b++ = '\0';
	}
	
	while (chars->length && !chars->bytes[chars->length - 1])
		--chars->length;
	
	return Value.chars(chars);
}

// MARK: - Static Members

// MARK: - Methods

void setup ()
{
	const enum Value(Flags) flags = Value(hidden);
	
	Function.setupBuiltinObject(&String(constructor), stringConstructor, 1, &String(prototype), Value.string(create(Chars.createSized(0))), &String(type));
	
	Function.addToObject(String(prototype), "toString", toString, 0, flags);
	Function.addToObject(String(prototype), "valueOf", valueOf, 0, flags);
	Function.addToObject(String(prototype), "charAt", charAt, 1, flags);
	Function.addToObject(String(prototype), "charCodeAt", charCodeAt, 1, flags);
	Function.addToObject(String(prototype), "concat", concat, -1, flags);
	Function.addToObject(String(prototype), "indexOf", indexOf, -1, flags);
	Function.addToObject(String(prototype), "lastIndexOf", lastIndexOf, -1, flags);
	Function.addToObject(String(prototype), "slice", slice, 2, flags);
	Function.addToObject(String(prototype), "substring", substring, 2, flags);
	
	Function.addToObject(&String(constructor)->object, "fromCharCode", fromCharCode, -1, flags);
}

void teardown (void)
{
	String(prototype) = NULL;
	String(constructor) = NULL;
}

struct String * create (struct Chars *chars)
{
	struct String *self = malloc(sizeof(*self));
	*self = String.identity;
	Pool.addObject(&self->object);
	Object.initialize(&self->object, String(prototype));
	Object.add(&self->object, Key(length), Value.integer(indexPosition(chars->bytes, chars->length, chars->length)), Value(readonly));
	
	self->value = chars;
	
	return self;
}
