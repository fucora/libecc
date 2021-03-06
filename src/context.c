//
//  context.c
//  libecc
//
//  Copyright (c) 2019 Aurélien Bouilland
//  Licensed under MIT license, see LICENSE.txt file in project root
//

#define Implementation
#include "context.h"

#include "ecc.h"
#include "op.h"

// MARK: - Private

// MARK: - Static Members

// MARK: - Methods

void rangeError (struct Context * const self, struct Chars *chars)
{
	throw(self, Value.error(Error.rangeError(textSeek(self), chars)));
}

void referenceError (struct Context * const self, struct Chars *chars)
{
	throw(self, Value.error(Error.referenceError(textSeek(self), chars)));
}

void syntaxError (struct Context * const self, struct Chars *chars)
{
	throw(self, Value.error(Error.syntaxError(textSeek(self), chars)));
}

void typeError (struct Context * const self, struct Chars *chars)
{
	throw(self, Value.error(Error.typeError(textSeek(self), chars)));
}

void uriError (struct Context * const self, struct Chars *chars)
{
	throw(self, Value.error(Error.uriError(textSeek(self), chars)));
}

void throw (struct Context * const self, struct Value value)
{
	if (value.type == Value(errorType))
		self->ecc->text = value.data.error->text;
	
	if (self->ecc->printLastThrow && self->ecc->envCount == 1)
	{
		struct Value name, message;
		name = Value(undefined);
		
		if (value.type == Value(errorType))
		{
			name = Value.toString(self, Object.getMember(self, value.data.object, Key(name)));
			message = Value.toString(self, Object.getMember(self, value.data.object, Key(message)));
		}
		else
			message = Value.toString(self, value);
		
		if (name.type == Value(undefinedType))
			name = Value.text(&Text(errorName));
		
		Env.newline();
		Env.printError(Value.stringLength(&name), Value.stringBytes(&name), "%.*s" , Value.stringLength(&message), Value.stringBytes(&message));
		printBacktrace(self);
		Ecc.printTextInput(self->ecc, self->ecc->text, 1);
	}
	
	Ecc.jmpEnv(self->ecc, value);
}

struct Value callFunction (struct Context * const self, struct Function *function, struct Value this, int argumentCount, ... )
{
	struct Value result;
	va_list ap;
	int offset = 0;
	
	if (argumentCount & Context(asAccessor)) {
		offset = Context(accessorOffset);
	}
	
	va_start(ap, argumentCount);
	result = Op.callFunctionVA(self, offset, function, this, argumentCount & Context(countMask), ap);
	va_end(ap);
	
	return result;
}

int argumentCount (struct Context * const self)
{
	if (self->environment->hashmap[2].value.type == Value(objectType))
		return self->environment->hashmap[2].value.data.object->elementCount;
	else
		return self->environment->hashmapCount - 3;
}

struct Value argument (struct Context * const self, int argumentIndex)
{
	self->textIndex = argumentIndex + 4;
	
	if (self->environment->hashmap[2].value.type == Value(objectType))
	{
		if (argumentIndex < self->environment->hashmap[2].value.data.object->elementCount)
			return self->environment->hashmap[2].value.data.object->element[argumentIndex].value;
	}
	else if (argumentIndex < self->environment->hashmapCount - 3)
		return self->environment->hashmap[argumentIndex + 3].value;
	
	return Value(none);
}

void replaceArgument (struct Context * const self, int argumentIndex, struct Value value)
{
	if (self->environment->hashmap[2].value.type == Value(objectType))
	{
		if (argumentIndex < self->environment->hashmap[2].value.data.object->elementCount)
			self->environment->hashmap[2].value.data.object->element[argumentIndex].value = value;
	}
	else if (argumentIndex < self->environment->hashmapCount - 3)
		self->environment->hashmap[argumentIndex + 3].value = value;
}

struct Value this (struct Context * const self)
{
	self->textIndex = Context(thisIndex);
	return self->this;
}

void assertThisType (struct Context * const self, enum Value(Type) type)
{
	if (self->this.type != type)
	{
		setTextIndex(self, Context(thisIndex));
		typeError(self, Chars.create("'this' is not a %s", Value.typeName(type)));
	}
}

void assertThisMask (struct Context * const self, enum Value(Mask) mask)
{
	if (!(self->this.type & mask))
	{
		setTextIndex(self, Context(thisIndex));
		typeError(self, Chars.create("'this' is not a %s", Value.maskName(mask)));
	}
}

void assertThisCoerciblePrimitive (struct Context * const self)
{
	if (self->this.type == Value(undefinedType) || self->this.type == Value(nullType))
	{
		setTextIndex(self, Context(thisIndex));
		typeError(self, Chars.create("'this' cannot be null or undefined"));
	}
}

void setText (struct Context * const self, const struct Text *text)
{
	self->textIndex = Context(savedIndex);
	self->text = text;
}

void setTexts (struct Context * const self, const struct Text *text, const struct Text *textAlt)
{
	self->textIndex = Context(savedIndex);
	self->text = text;
	self->textAlt = textAlt;
}

void setTextIndex (struct Context * const self, enum Context(Index) index)
{
	self->textIndex = index;
}

void setTextIndexArgument (struct Context * const self, int argument)
{
	self->textIndex = argument + 4;
}

struct Text textSeek (struct Context * const self)
{
	const char *bytes;
	struct Context seek = *self;
	uint32_t breakArray = 0, argumentCount = 0;
	struct Text callText;
	enum Context(Index) index;
	int isAccessor = 0;
	
	assert(self);
	assert(self->ecc);
	
	index = self->textIndex;
	
	if (index == Context(savedIndex))
		return *self->text;
	
	if (index == Context(savedIndexAlt))
		return *self->textAlt;
	
	while (seek.ops->text.bytes == Text(nativeCode).bytes)
	{
		if (!seek.parent)
			return seek.ops->text;
		
		isAccessor = seek.argumentOffset == Context(accessorOffset);
		
		if (seek.argumentOffset > 0 && index >= Context(thisIndex))
		{
			++index;
			++argumentCount;
			breakArray <<= 1;
			
			if (seek.argumentOffset == Context(applyOffset))
				breakArray |= 2;
		}
		seek = *seek.parent;
	}
	
	if (seek.ops->native == Op.noop)
		--seek.ops;
	
	if (isAccessor)
	{
		if (index > Context(thisIndex))
			Context.rewindStatement(&seek);
	}
	else if (index > Context(noIndex))
	{
		while (seek.ops->text.bytes != seek.textCall->bytes
			|| seek.ops->text.length != seek.textCall->length
			)
			--seek.ops;
		
		argumentCount += seek.ops->value.data.integer;
		callText = seek.ops->text;
		
		// func
		if (index-- > Context(callIndex))
			++seek.ops;
		
		// this
		if (index-- > Context(callIndex) && (seek.ops + 1)->text.bytes <= seek.ops->text.bytes)
			++seek.ops;
		
		// arguments
		while (index-- > Context(callIndex))
		{
			if (!argumentCount--)
				return Text.make(callText.bytes + callText.length - 1, 0);
			
			bytes = seek.ops->text.bytes + seek.ops->text.length;
			while (bytes > seek.ops->text.bytes && seek.ops->text.bytes)
				++seek.ops;
			
			if (breakArray & 0x1 && seek.ops->native == Op.array)
				++seek.ops;
			
			breakArray >>= 1;
		}
	}
	
	return seek.ops->text;
}

void rewindStatement(struct Context * const context)
{
	while (!(context->ops->text.flags & Text(breakFlag)))
		--context->ops;
}

void printBacktrace (struct Context * const context)
{
	int depth = context->depth, count, skip;
	struct Context frame;
	
	if (depth > 12)
	{
		Env.printColor(0, Env(bold), "...");
		fprintf(stderr, " (%d more)\n", depth - 12);
		depth = 12;
	}
	
	while (depth > 0)
	{
		count = depth--;
		frame = *context;
		skip = 0;
		
		while (count--)
		{
			--skip;
			
			if (frame.argumentOffset == Context(callOffset) || frame.argumentOffset == Context(applyOffset))
				skip = 2;
			else if (frame.textIndex > Context(noIndex) && frame.ops->text.bytes == Text(nativeCode).bytes)
				skip = 1;
			
			frame = *frame.parent;
		}
		
		if (skip <= 0 && frame.ops->text.bytes != Text(nativeCode).bytes)
		{
			Context.rewindStatement(&frame);
			if (frame.ops->text.length)
				Ecc.printTextInput(frame.ecc, frame.ops->text, 0);
		}
	}
}

struct Object *environmentRoot (struct Context * const context)
{
	struct Object *environment = context->strictMode? context->environment: &context->ecc->global->environment;
	
	if (context->strictMode)
		while (environment->prototype && environment->prototype != &context->ecc->global->environment)
			environment = environment->prototype;
	
	return environment;
}
