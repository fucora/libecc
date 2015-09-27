//
//  ecc.c
//  libecc
//
//  Copyright (c) 2019 Aurélien Bouilland
//  Licensed under MIT license, see LICENSE.txt file in project root
//

#include "ecc.h"

// MARK: - Private

static int instanceCount = 0;

static struct Value eval (const struct Op ** const ops, struct Ecc * const ecc)
{
	Op.assertParameterCount(ecc, 1);
	
	struct Value value = Value.toString(Op.argument(ecc, 0));
	struct Input *input = Input.createFromBytes(Value.stringChars(value), Value.stringLength(value), "(eval)");
	
	struct Object *context = ecc->context;
	struct Value this = ecc->this;
	ecc->context = &ecc->global->context;
	ecc->this = Value.object(&ecc->global->context);
	evalInput(ecc, input);
	ecc->context = context;
	ecc->this = this;
	
	return Value.undefined();
}

static struct Value parseInt (const struct Op ** const ops, struct Ecc * const ecc)
{
	Op.assertParameterCount(ecc, 1);
	
	struct Value value = Value.toString(Op.argument(ecc, 0));
	int32_t radix = Value.toInteger(Op.argument(ecc, 1)).data.integer;
	
	struct Text text = Text.make(Value.stringChars(value), Value.stringLength(value));
	ecc->result = Lexer.parseInteger(text, radix);
	
	return Value.undefined();
}

static struct Value parseFloat (const struct Op ** const ops, struct Ecc * const ecc)
{
	Op.assertParameterCount(ecc, 1);
	
	struct Value value = Value.toString(Op.argument(ecc, 0));
	
	struct Text text = Text.make(Value.stringChars(value), Value.stringLength(value));
	ecc->result = Lexer.parseBinary(text);
	
	return Value.undefined();
}

static struct Value isFinite (const struct Op ** const ops, struct Ecc * const ecc)
{
	Op.assertParameterCount(ecc, 1);
	
	struct Value value = Value.toBinary(Op.argument(ecc, 0));
	ecc->result = Value.boolean(!isnan(value.data.binary) && !isinf(value.data.binary));
	
	return Value.undefined();
}

static struct Value isNaN (const struct Op ** const ops, struct Ecc * const ecc)
{
	Op.assertParameterCount(ecc, 1);
	
	struct Value value = Value.toBinary(Op.argument(ecc, 0));
	ecc->result = Value.boolean(isnan(value.data.binary));
	
	return Value.undefined();
}

// MARK: - Static Members

static struct Input * findInput (Instance ecc, struct Text text)
{
	for (uint_fast16_t i = 0; i < ecc->inputCount; ++i)
		if (text.location >= ecc->inputs[i]->bytes && text.location <= ecc->inputs[i]->bytes + ecc->inputs[i]->length)
			return ecc->inputs[i];
	
	return NULL;
}

static void addInput(Instance self, struct Input *input)
{
	self->inputs = realloc(self->inputs, sizeof(*self->inputs) * (self->inputCount + 1));
	self->inputs[self->inputCount++] = input;
}

// MARK: - Methods

Instance create (void)
{
	if (!instanceCount++)
	{
		Env.setup();
		Pool.setup();
		Identifier.setup();
		
		Object.setupPrototype();
		Function.setup();
		Object.setup();
		Error.setup();
		Array.setup();
		Date.setup();
	}
	
	Instance self = malloc(sizeof(*self));
	*self = Module.identity;
	
	self->global = Function.create(NULL);
	
	Function.addValue(self->global, "NaN", Value.binary(NAN), 0);
	Function.addValue(self->global, "Infinity", Value.binary(INFINITY), 0);
	Function.addValue(self->global, "undefined", Value.undefined(), 0);
	Function.addNative(self->global, "eval", eval, 1, 0);
	Function.addNative(self->global, "parseInt", parseInt, 2, 0);
	Function.addNative(self->global, "parseFloat", parseFloat, 1, 0);
	Function.addNative(self->global, "isNaN", isNaN, 1, 0);
	Function.addNative(self->global, "isFinite", isFinite, 1, 0);
	
	Function.addValue(self->global, "Object", Value.function(Object.constructor()), 0);
	Function.addValue(self->global, "Array", Value.function(Array.constructor()), 0);
	Function.addValue(self->global, "Function", Value.function(Function.constructor()), 0);
	
	Function.addValue(self->global, "Error", Value.object(Error.prototype()), 0);
	Function.addValue(self->global, "RangeError", Value.object(Error.rangePrototype()), 0);
	Function.addValue(self->global, "ReferenceError", Value.object(Error.referencePrototype()), 0);
	Function.addValue(self->global, "SyntaxError", Value.object(Error.syntaxPrototype()), 0);
	Function.addValue(self->global, "TypeError", Value.object(Error.typePrototype()), 0);
	Function.addValue(self->global, "URIError", Value.object(Error.uriPrototype()), 0);
	
	Function.addValue(self->global, "Array", Value.object(Array.prototype()), 0);
	Function.addValue(self->global, "Date", Value.object(Date.prototype()), 0);
	
	self->context = &self->global->context;
	
	return self;
}

void destroy (Instance self)
{
	assert(self);
	
	while (self->inputCount--)
		Input.destroy(self->inputs[self->inputCount]), self->inputs[self->inputCount] = NULL;
	
	free(self->inputs), self->inputs = NULL;
	free(self->envList), self->envList = NULL;
	free(self), self = NULL;
	
	if (!--instanceCount)
	{
		Function.teardown();
		Object.teardown();
//		Error.teardown();
//		Array.teardown();
//		Date.teardown();
		
		Identifier.teardown();
		Env.teardown();
		
		Pool.teardown();
	}
}

void addNative (Instance self, const char *name, const Native native, int argumentCount, enum Object(Flags) flags)
{
	assert(self);
	
	Function.addNative(self->global, name, native, argumentCount, flags);
}

void addValue (Instance self, const char *name, struct Value value, enum Object(Flags) flags)
{
	assert(self);
	
	Function.addValue(self->global, name, value, flags);
}

int evalInput (Instance self, struct Input *input)
{
	assert(self);
	
	if (!input)
		return EXIT_FAILURE;
	
	addInput(self, input);
	
	int result = EXIT_SUCCESS;
	
	struct Object *parentContext = self->context;
	
	struct Lexer *lexer = Lexer.createWithInput(input);
	struct Parser *parser = Parser.createWithLexer(lexer);
	struct Function *function = Parser.parseWithContext(parser, parentContext);
	const struct Op *ops = function->oplist->ops;
	
	Parser.destroy(parser), parser = NULL;
	
	self->context = &function->context;
	self->result = Value.undefined();
	
//	fprintf(stderr, "source:\n%.*s\n", input->length, input->bytes);
//	OpList.dumpTo(function->oplist, stderr);
	
	if (!self->envCount)
	{
		if (!setjmp(*pushEnv(self)))
			ops->native(&ops, self);
		else
		{
			result = EXIT_FAILURE;
			
			struct Value value = self->result;
			struct Value name = Value.undefined();
			struct Value message;
			
			if (Value.isObject(value))
			{
				name = Value.toString(Object.get(value.data.object, Identifier.name()));
				message = Value.toString(Object.get(value.data.object, Identifier.message()));
			}
			else
				message = Value.toString(value);
			
			if (name.type == Value(undefined))
				name = Value.text(Text.errorName());
			
			putc('\n', stderr);
			Env.printError(Value.stringLength(name), Value.stringChars(name), "%.*s" , Value.stringLength(message), Value.stringChars(message));
			printTextInput(self, value.type == Value(error)? value.data.error->text: ops->text);
		}
		
		popEnv(self);
	}
	else
		ops->native(&ops, self);
	
	self->context = parentContext;
	
	return result;
}

jmp_buf * pushEnv(Instance self)
{
	if (self->envCount >= self->envCapacity)
	{
		self->envCapacity = self->envCapacity? self->envCapacity * 2: 8;
		self->envList = realloc(self->envList, sizeof(*self->envList) * self->envCapacity);
	}
	
	self->envList[self->envCount].context = self->context;
	self->envList[self->envCount].this = self->this;
	
	return &self->envList[self->envCount++].buf;
}

void popEnv(Instance self)
{
	assert(self->envCount);
	
	--self->envCount;
	self->context = self->envList[self->envCount].context;
	self->this = self->envList[self->envCount].this;
}

void jmpEnv (Instance self, struct Value value)
{
	assert(self);
	
	self->result = value;
	
	longjmp(self->envList[self->envCount - 1].buf, 1);
}

void printTextInput (Instance self, struct Text text)
{
	assert(self);
	
	struct Input *input = findInput(self, text);
	if (input)
		Input.printText(input, text);
	else
	{
		Env.printDim("(unknown input)\n");
		fprintf(stderr, "%.*s\n\n", text.length, text.location);
	}
}

void garbageCollect(Instance self)
{
	Pool.markAll();
	Pool.unmarkValue(Value.function(self->global));
	Pool.unmarkValue(self->this);
	Pool.collectMarked();
}
