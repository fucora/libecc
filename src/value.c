//
//  value.c
//  libecc
//
//  Copyright (c) 2019 Aurélien Bouilland
//  Licensed under MIT license, see LICENSE.txt file in project root
//

#define Implementation
#include "value.h"

#include "ecc.h"
#include "op.h"
#include "lexer.h"

// MARK: - Private

#define valueMake(T) { .type = Value(T), .check = 1 }

const struct Value Value(none) = {{ 0 }};
const struct Value Value(undefined) = valueMake(undefinedType);
const struct Value Value(true) = valueMake(trueType);
const struct Value Value(false) = valueMake(falseType);
const struct Value Value(null) = valueMake(nullType);

// MARK: - Static Members

// MARK: - Methods


// make

struct Value truth (int truth)
{
	return (struct Value){
		.type = truth? Value(trueType): Value(falseType),
		.check = 1,
	};
}

struct Value integer (int32_t integer)
{
	return (struct Value){
		.data = { .integer = integer },
		.type = Value(integerType),
		.check = 1,
	};
}

struct Value binary (double binary)
{
	return (struct Value){
		.data = { .binary = binary },
		.type = Value(binaryType),
		.check = 1,
	};
}

struct Value buffer (const char b[7], uint8_t units)
{
	struct Value value = {
		.type = Value(bufferType),
		.check = 1,
	};
	memcpy(value.data.buffer, b, units);
	value.data.buffer[7] = units;
	return value;
}

struct Value key (struct Key key)
{
	return (struct Value){
		.data = { .key = key },
		.type = Value(keyType),
		.check = 0,
	};
}

struct Value text (const struct Text *text)
{
	return (struct Value){
		.data = { .text = text },
		.type = Value(textType),
		.check = 1,
	};
}

struct Value chars (struct Chars *chars)
{
	assert(chars);
	
	return (struct Value){
		.data = { .chars = chars },
		.type = Value(charsType),
		.check = 1,
	};
}

struct Value object (struct Object *object)
{
	assert(object);
	
	return (struct Value){
		.data = { .object = object },
		.type = Value(objectType),
		.check = 1,
	};
}

struct Value error (struct Error *error)
{
	assert(error);
	
	return (struct Value){
		.data = { .error = error },
		.type = Value(errorType),
		.check = 1,
	};
}

struct Value string (struct String *string)
{
	assert(string);
	
	return (struct Value){
		.data = { .string = string },
		.type = Value(stringType),
		.check = 1,
	};
}

struct Value regexp (struct RegExp *regexp)
{
	assert(regexp);
	
	return (struct Value){
		.data = { .regexp = regexp },
		.type = Value(regexpType),
		.check = 1,
	};
}

struct Value number (struct Number *number)
{
	assert(number);
	
	return (struct Value){
		.data = { .number = number },
		.type = Value(numberType),
		.check = 1,
	};
}

struct Value boolean (struct Boolean *boolean)
{
	assert(boolean);
	
	return (struct Value){
		.data = { .boolean = boolean },
		.type = Value(booleanType),
		.check = 1,
	};
}

struct Value date (struct Date *date)
{
	assert(date);
	
	return (struct Value){
		.data = { .date = date },
		.type = Value(dateType),
		.check = 1,
	};
}

struct Value function (struct Function *function)
{
	assert(function);
	
	return (struct Value){
		.data = { .function = function },
		.type = Value(functionType),
		.check = 1,
	};
}

struct Value host (struct Object *object)
{
	assert(object);
	
	return (struct Value){
		.data = { .object = object },
		.type = Value(hostType),
		.check = 1,
	};
}

struct Value reference (struct Value *reference)
{
	assert(reference);
	
	return (struct Value){
		.data = { .reference = reference },
		.type = Value(referenceType),
		.check = 0,
	};
}

// check

int isPrimitive (struct Value value)
{
	return !(value.type & Value(objectMask));
}

int isBoolean (struct Value value)
{
	return value.type & Value(booleanMask);
}

int isNumber (struct Value value)
{
	return value.type & Value(numberMask);
}

int isString (struct Value value)
{
	return value.type & Value(stringMask);
}

int isObject (struct Value value)
{
	return value.type & Value(objectMask);
}

int isDynamic (struct Value value)
{
	return value.type & Value(dynamicMask);
}

int isTrue (struct Value value)
{
	if (value.type <= Value(undefinedType))
		return 0;
	if (value.type >= Value(trueType))
		return 1;
	else if (value.type == Value(integerType))
		return value.data.integer != 0;
	else if (value.type == Value(binaryType))
		return !isnan(value.data.binary) && value.data.binary != 0;
	else if (isString(value))
		return stringLength(&value) > 0;
	
	Ecc.fatal("Invalid Value(Type) : %u", value.type);
}


// convert

struct Value toPrimitive (struct Context * const context, struct Value value, enum Value(hintPrimitive) hint)
{
	struct Object *object;
	struct Key aKey;
	struct Key bKey;
	struct Value aFunction;
	struct Value bFunction;
	struct Value result;
	struct Text text;
	
	if (value.type < Value(objectType))
		return value;
	
	if (!context)
		Ecc.fatal("cannot use toPrimitive outside context");
	
	object = value.data.object;
	hint = hint? hint: value.type == Value(dateType)? Value(hintString): Value(hintNumber);
	aKey = hint > 0? Key(toString): Key(valueOf);
	bKey = hint > 0? Key(valueOf): Key(toString);
	
	aFunction = Object.getMember(context, object, aKey);
	if (aFunction.type == Value(functionType))
	{
		struct Value result = Context.callFunction(context, aFunction.data.function, value, 0 | Context(asAccessor));
		if (isPrimitive(result))
			return result;
	}
	
	bFunction = Object.getMember(context, object, bKey);
	if (bFunction.type == Value(functionType))
	{
		result = Context.callFunction(context, bFunction.data.function, value, 0 | Context(asAccessor));
		if (isPrimitive(result))
			return result;
	}
	
	text = Context.textSeek(context);
	if (context->textIndex != Context(callIndex) && text.length)
		Context.typeError(context, Chars.create("cannot convert '%.*s' to primitive", text.length, text.bytes));
	else
		Context.typeError(context, Chars.create("cannot convert value to primitive"));
}

struct Value toBinary (struct Context * const context, struct Value value)
{
	switch ((enum Value(Type))value.type)
	{
		case Value(binaryType):
			return value;
		
		case Value(integerType):
			return binary(value.data.integer);
		
		case Value(numberType):
			return binary(value.data.number->value);
		
		case Value(nullType):
		case Value(falseType):
			return binary(0);
		
		case Value(trueType):
			return binary(1);
		
		case Value(booleanType):
			return binary(value.data.boolean->truth? 1: 0);
		
		case Value(undefinedType):
			return binary(NAN);
		
		case Value(textType):
			if (value.data.text == &Text(zero))
				return binary(0);
			else if (value.data.text == &Text(one))
				return binary(1);
			else if (value.data.text == &Text(nan))
				return binary(NAN);
			else if (value.data.text == &Text(infinity))
				return binary(INFINITY);
			else if (value.data.text == &Text(negativeInfinity))
				return binary(-INFINITY);
			
			/*vvv*/
			
		case Value(keyType):
		case Value(charsType):
		case Value(stringType):
		case Value(bufferType):
			return Lexer.scanBinary(textOf(&value), context && context->ecc->sloppyMode? Lexer(scanSloppy): 0);
			
		case Value(objectType):
		case Value(errorType):
		case Value(dateType):
		case Value(functionType):
		case Value(regexpType):
		case Value(hostType):
			return toBinary(context, toPrimitive(context, value, Value(hintNumber)));
		
		case Value(referenceType):
			break;
	}
	Ecc.fatal("Invalid Value(Type) : %u", value.type);
}

struct Value toInteger (struct Context * const context, struct Value value)
{
	const double modulus = (double)UINT32_MAX + 1;
	double binary = toBinary(context, value).data.binary;
	
	if (!binary || !isfinite(binary))
		return integer(0);
	
	binary = fmod(binary, modulus);
	if (binary >= 0)
		binary = floor(binary);
	else
		binary = ceil(binary) + modulus;
	
	if (binary > INT32_MAX)
		return integer(binary - modulus);
	
	return integer(binary);
}

struct Value binaryToString (double binary, int base)
{
	struct Chars(Append) chars;
	
	if (binary == 0)
		return text(&Text(zero));
	else if (binary == 1)
		return text(&Text(one));
	else if (isnan(binary))
		return text(&Text(nan));
	else if (isinf(binary))
	{
		if (binary < 0)
			return text(&Text(negativeInfinity));
		else
			return text(&Text(infinity));
	}
	
	Chars.beginAppend(&chars);
	Chars.appendBinary(&chars, binary, base);
	return Chars.endAppend(&chars);
}

struct Value toString (struct Context * const context, struct Value value)
{
	switch ((enum Value(Type))value.type)
	{
		case Value(textType):
		case Value(charsType):
		case Value(bufferType):
			return value;
			
		case Value(keyType):
			return text(Key.textOf(value.data.key));
		
		case Value(stringType):
			return chars(value.data.string->value);
		
		case Value(nullType):
			return text(&Text(null));
		
		case Value(undefinedType):
			return text(&Text(undefined));
		
		case Value(falseType):
			return text(&Text(false));
		
		case Value(trueType):
			return text(&Text(true));
		
		case Value(booleanType):
			return value.data.boolean->truth? text(&Text(true)): text(&Text(false));
		
		case Value(integerType):
			return binaryToString(value.data.integer, 10);
		
		case Value(numberType):
			value.data.binary = value.data.number->value;
			/*vvv*/
		
		case Value(binaryType):
			return binaryToString(value.data.binary, 10);
		
		case Value(objectType):
		case Value(dateType):
		case Value(functionType):
		case Value(errorType):
		case Value(regexpType):
		case Value(hostType):
			return toString(context, toPrimitive(context, value, Value(hintString)));
		
		case Value(referenceType):
			break;
	}
	Ecc.fatal("Invalid Value(Type) : %u", value.type);
}

int32_t stringLength (const struct Value *value)
{
	switch (value->type)
	{
		case Value(charsType):
			return value->data.chars->length;
			
		case Value(textType):
			return value->data.text->length;
			
		case Value(stringType):
			return value->data.string->value->length;
			
		case Value(bufferType):
			return value->data.buffer[7];
			
		default:
			return 0;
	}
}

const char * stringBytes (const struct Value *value)
{
	switch (value->type)
	{
		case Value(charsType):
			return value->data.chars->bytes;
			
		case Value(textType):
			return value->data.text->bytes;
			
		case Value(stringType):
			return value->data.string->value->bytes;
			
		case Value(bufferType):
			return value->data.buffer;
			
		default:
			return NULL;
	}
}

struct Text textOf (const struct Value *value)
{
	switch (value->type)
	{
		case Value(charsType):
			return Text.make(value->data.chars->bytes, value->data.chars->length);
			
		case Value(textType):
			return *value->data.text;
			
		case Value(stringType):
			return Text.make(value->data.string->value->bytes, value->data.string->value->length);
			
		case Value(keyType):
			return *Key.textOf(value->data.key);
			
		case Value(bufferType):
			return Text.make(value->data.buffer, value->data.buffer[7]);
			
		default:
			return Text(empty);
	}
}

struct Value toObject (struct Context * const context, struct Value value)
{
	if (value.type >= Value(objectType))
		return value;
	
	switch ((enum Value(Type))value.type)
	{
		case Value(binaryType):
			return number(Number.create(value.data.binary));
		
		case Value(integerType):
			return number(Number.create(value.data.integer));
		
		case Value(textType):
		case Value(charsType):
		case Value(bufferType):
			return string(String.create(Chars.createWithBytes(stringLength(&value), stringBytes(&value))));
			
		case Value(falseType):
		case Value(trueType):
			return boolean(Boolean.create(value.type == Value(trueType)));
		
		case Value(nullType):
			goto error;
		
		case Value(undefinedType):
			goto error;
		
		case Value(keyType):
		case Value(referenceType):
		case Value(functionType):
		case Value(objectType):
		case Value(errorType):
		case Value(stringType):
		case Value(numberType):
		case Value(dateType):
		case Value(booleanType):
		case Value(regexpType):
		case Value(hostType):
			break;
	}
	Ecc.fatal("Invalid Value(Type) : %u", value.type);
	
error:
	{
		struct Text text = Context.textSeek(context);
		
		if (context->textIndex != Context(callIndex) && text.length)
			Context.typeError(context, Chars.create("cannot convert '%.*s' to object", text.length, text.bytes));
		else
			Context.typeError(context, Chars.create("cannot convert %s to object", typeName(value.type)));
	}
}

struct Value objectValue (struct Object *object)
{
	if (!object)
		return Value(undefined);
	else if (object->type == &Function(type))
		return Value.function((struct Function *)object);
	else if (object->type == &String(type))
		return Value.string((struct String *)object);
	else if (object->type == &Boolean(type))
		return Value.boolean((struct Boolean *)object);
	else if (object->type == &Number(type))
		return Value.number((struct Number *)object);
	else if (object->type == &Date(type))
		return Value.date((struct Date *)object);
	else if (object->type == &RegExp(type))
		return Value.regexp((struct RegExp *)object);
	else if (object->type == &Error(type))
		return Value.error((struct Error *)object);
	else if (object->type == &Object(type)
			|| object->type == &Array(type)
			|| object->type == &Arguments(type)
			|| object->type == &Math(type)
			)
		return Value.object((struct Object *)object);
	else
		return Value.host(object);
}

int objectIsArray (struct Object *object)
{
	return object->type == &Array(type) || object->type == &Arguments(type);
}

struct Value toType (struct Value value)
{
	switch ((enum Value(Type))value.type)
	{
		case Value(trueType):
		case Value(falseType):
			return text(&Text(boolean));
		
		case Value(undefinedType):
			return text(&Text(undefined));
		
		case Value(integerType):
		case Value(binaryType):
			return text(&Text(number));
		
		case Value(keyType):
		case Value(textType):
		case Value(charsType):
		case Value(bufferType):
			return text(&Text(string));
			
		case Value(nullType):
		case Value(objectType):
		case Value(stringType):
		case Value(numberType):
		case Value(booleanType):
		case Value(errorType):
		case Value(dateType):
		case Value(regexpType):
		case Value(hostType):
			return text(&Text(object));
		
		case Value(functionType):
			return text(&Text(function));
		
		case Value(referenceType):
			break;
	}
	Ecc.fatal("Invalid Value(Type) : %u", value.type);
}

struct Value equals (struct Context * const context, struct Value a, struct Value b)
{
	if (isObject(a) && isObject(b))
		return truth(a.data.object == b.data.object);
	else if (((isString(a) || isNumber(a)) && isObject(b))
			 || (isObject(a) && (isString(b) || isNumber(b)))
			 )
	{
		a = toPrimitive(context, a, Value(hintAuto));
		Context.setTextIndex(context, Context(savedIndexAlt));
		b = toPrimitive(context, b, Value(hintAuto));
		
		return equals(context, a, b);
	}
	else if (isNumber(a) && isNumber(b))
		return truth(toBinary(context, a).data.binary == toBinary(context, b).data.binary);
	else if (isString(a) && isString(b))
	{
		int32_t aLength = stringLength(&a);
		int32_t bLength = stringLength(&b);
		if (aLength != bLength)
			return Value(false);
		
		return truth(!memcmp(stringBytes(&a), stringBytes(&b), aLength));
	}
	else if (a.type == b.type)
		return Value(true);
	else if (a.type == Value(nullType) && b.type == Value(undefinedType))
		return Value(true);
	else if (a.type == Value(undefinedType) && b.type == Value(nullType))
		return Value(true);
	else if (isNumber(a) && isString(b))
		return equals(context, a, toBinary(context, b));
	else if (isString(a) && isNumber(b))
		return equals(context, toBinary(context, a), b);
	else if (isBoolean(a))
		return equals(context, toBinary(context, a), b);
	else if (isBoolean(b))
		return equals(context, a, toBinary(context, b));
	
	return Value(false);
}

struct Value same (struct Context * const context, struct Value a, struct Value b)
{
	if (isObject(a) || isObject(b))
		return truth(isObject(a) && isObject(b) && a.data.object == b.data.object);
	else if (isNumber(a) && isNumber(b))
		return truth(toBinary(context, a).data.binary == toBinary(context, b).data.binary);
	else if (isString(a) && isString(b))
	{
		int32_t aLength = stringLength(&a);
		int32_t bLength = stringLength(&b);
		if (aLength != bLength)
			return Value(false);
		
		return truth(!memcmp(stringBytes(&a), stringBytes(&b), aLength));
	}
	else if (a.type == b.type)
		return Value(true);
	
	return Value(false);
}

struct Value add (struct Context * const context, struct Value a, struct Value b)
{
	if (!isNumber(a) || !isNumber(b))
	{
		a = toPrimitive(context, a, Value(hintAuto));
		Context.setTextIndex(context, Context(savedIndexAlt));
		b = toPrimitive(context, b, Value(hintAuto));
		
		if (isString(a) || isString(b))
		{
			struct Chars(Append) chars;
			
			Chars.beginAppend(&chars);
			Chars.appendValue(&chars, context, a);
			Chars.appendValue(&chars, context, b);
			return Chars.endAppend(&chars);
		}
	}
	return binary(toBinary(context, a).data.binary + toBinary(context, b).data.binary);
}

struct Value subtract (struct Context * const context, struct Value a, struct Value b)
{
	return binary(toBinary(context, a).data.binary - toBinary(context, b).data.binary);
}

static
struct Value compare (struct Context * const context, struct Value a, struct Value b)
{
	a = toPrimitive(context, a, Value(hintNumber));
	Context.setTextIndex(context, Context(savedIndexAlt));
	b = toPrimitive(context, b, Value(hintNumber));
	
	if (isString(a) && isString(b))
	{
		int32_t aLength = stringLength(&a);
		int32_t bLength = stringLength(&b);
		
		if (aLength < bLength && !memcmp(stringBytes(&a), stringBytes(&b), aLength))
			return Value(true);
		
		if (aLength > bLength && !memcmp(stringBytes(&a), stringBytes(&b), bLength))
			return Value(false);
		
		return truth(memcmp(stringBytes(&a), stringBytes(&b), aLength) < 0);
	}
	else
	{
		a = toBinary(context, a);
		b = toBinary(context, b);
		
		if (isnan(a.data.binary) || isnan(b.data.binary))
			return Value(undefined);
		
		return truth(a.data.binary < b.data.binary);
	}
}

struct Value less (struct Context * const context, struct Value a, struct Value b)
{
	a = compare(context, a, b);
	if (a.type == Value(undefinedType))
		return Value(false);
	else
		return a;
}

struct Value more (struct Context * const context, struct Value a, struct Value b)
{
	a = compare(context, b, a);
	if (a.type == Value(undefinedType))
		return Value(false);
	else
		return a;
}

struct Value lessOrEqual (struct Context * const context, struct Value a, struct Value b)
{
	a = compare(context, b, a);
	if (a.type == Value(undefinedType) || a.type == Value(trueType))
		return Value(false);
	else
		return Value(true);
}

struct Value moreOrEqual (struct Context * const context, struct Value a, struct Value b)
{
	a = compare(context, a, b);
	if (a.type == Value(undefinedType) || a.type == Value(trueType))
		return Value(false);
	else
		return Value(true);
}

const char * typeName (enum Value(Type) type)
{
	switch (type)
	{
		case Value(nullType):
			return "null";
		
		case Value(undefinedType):
			return "undefined";
		
		case Value(falseType):
		case Value(trueType):
			return "boolean";
		
		case Value(integerType):
		case Value(binaryType):
			return "number";
		
		case Value(keyType):
		case Value(textType):
		case Value(charsType):
		case Value(bufferType):
			return "string";
			
		case Value(objectType):
		case Value(hostType):
			return "object";
		
		case Value(errorType):
			return "error";
		
		case Value(functionType):
			return "function";
		
		case Value(dateType):
			return "date";
		
		case Value(numberType):
			return "number";
		
		case Value(stringType):
			return "string";
		
		case Value(booleanType):
			return "boolean";
		
		case Value(regexpType):
			return "regexp";
			
		case Value(referenceType):
			break;
	}
	Ecc.fatal("Invalid Value(Type) : %u", type);
}

const char * maskName (enum Value(Mask) mask)
{
	switch (mask)
	{
		case Value(numberMask):
			return "number";
		
		case Value(stringMask):
			return "string";
		
		case Value(booleanMask):
			return "boolean";
		
		case Value(objectMask):
			return "object";
		
		case Value(dynamicMask):
			return "dynamic";
	}
	Ecc.fatal("Invalid Value(Mask) : %u", mask);
}

void dumpTo (struct Value value, FILE *file)
{
	switch ((enum Value(Type))value.type)
	{
		case Value(nullType):
			fputs("null", file);
			return;
		
		case Value(undefinedType):
			fputs("undefined", file);
			return;
		
		case Value(falseType):
			fputs("false", file);
			return;
		
		case Value(trueType):
			fputs("true", file);
			return;
		
		case Value(booleanType):
			fputs(value.data.boolean->truth? "true": "false", file);
			return;
		
		case Value(integerType):
			fprintf(file, "%d", (int)value.data.integer);
			return;
		
		case Value(numberType):
			value.data.binary = value.data.number->value;
			/*vvv*/
			
		case Value(binaryType):
			fprintf(file, "%g", value.data.binary);
			return;
		
		case Value(keyType):
		case Value(textType):
		case Value(charsType):
		case Value(stringType):
		case Value(bufferType):
		{
			const struct Text text = textOf(&value);
			putc('\'', file);
			fwrite(text.bytes, sizeof(char), text.length, file);
			putc('\'', file);
			return;
		}
		
		case Value(objectType):
		case Value(dateType):
		case Value(errorType):
		case Value(regexpType):
		case Value(hostType):
			Object.dumpTo(value.data.object, file);
			return;
		
		case Value(functionType):
			fwrite(value.data.function->text.bytes, sizeof(char), value.data.function->text.length, file);
			return;
		
		case Value(referenceType):
			fputs("-> ", file);
			dumpTo(*value.data.reference, file);
			return;
	}
}
