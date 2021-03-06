//
//  json.c
//  libecc
//
//  Copyright (c) 2019 Aurélien Bouilland
//  Licensed under MIT license, see LICENSE.txt file in project root
//

#define Implementation
#include "json.h"

#include "../ecc.h"
#include "../parser.h"
#include "../oplist.h"

// MARK: - Private

struct Parse {
	struct Text text;
	const char *start;
	int line;
	
	// reviver
	
	struct Context context;
	struct Function * function;
	struct Object * arguments;
	const struct Op * ops;
};

struct Stringify {
	struct Chars(Append) chars;
	char spaces[11];
	int level;
	struct Object *filter;
	
	// replacer
	
	struct Context context;
	struct Function * function;
	struct Object * arguments;
	const struct Op * ops;
};

const struct Object(Type) JSON(type) = {
	.text = &Text(jsonType),
};

static
struct Value error (struct Parse *parse, int length, struct Chars *chars)
{
	return Value.error(Error.syntaxError(Text.make(parse->text.bytes + (length < 0? length: 0), abs(length)), chars));
}

static
struct Text errorOfLine (struct Parse *parse)
{
	struct Text text = { 0 };
	text.bytes = parse->start;
	while (parse->text.length)
	{
		struct Text(Char) c = Text.character(parse->text);
		if (c.codepoint == '\r' || c.codepoint == '\n')
			break;
		
		Text.advance(&parse->text, 1);
	}
	text.length = (int32_t)(parse->text.bytes - parse->start);
	return text;
}

static
struct Text(Char) nextc (struct Parse *parse)
{
	struct Text(Char) c = { 0 };
	while (parse->text.length)
	{
		c = Text.nextCharacter(&parse->text);
		
		if (c.codepoint == '\r' && Text.character(parse->text).codepoint == '\n')
			Text.advance(&parse->text, 1);
		
		if (c.codepoint == '\r' || c.codepoint == '\n')
		{
			parse->start = parse->text.bytes;
			++parse->line;
		}
		
		if (!isspace(c.codepoint))
			break;
	}
	return c;
}

static
struct Text string (struct Parse *parse)
{
	struct Text text = { parse->text.bytes };
	struct Text(Char) c;
	
	do
	{
		c = Text.nextCharacter(&parse->text);
		if (c.codepoint == '\\')
			Text.advance(&parse->text, 1);
	}
	while (c.codepoint != '\"' && parse->text.length);
	
	text.length = (int32_t)(parse->text.bytes - text.bytes - 1);
	return text;
}

static struct Value object (struct Parse *parse);
static struct Value array (struct Parse *parse);

static
struct Value literal (struct Parse *parse)
{
	struct Text(Char) c = nextc(parse);
	
	switch (c.codepoint)
	{
		case 't':
			if (!memcmp(parse->text.bytes, "rue", 3))
			{
				Text.advance(&parse->text, 3);
				return Value(true);
			}
			break;
			
		case 'f':
			if (!memcmp(parse->text.bytes, "alse", 4))
			{
				Text.advance(&parse->text, 4);
				return Value(false);
			}
			break;
			
		case 'n':
			if (!memcmp(parse->text.bytes, "ull", 3))
			{
				Text.advance(&parse->text, 3);
				return Value(null);
			}
			break;
			
		case '-':
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
		{
			struct Text text = Text.make(parse->text.bytes - 1, 1);
			
			if (c.codepoint == '-')
				c = Text.nextCharacter(&parse->text);
			
			if (c.codepoint != '0')
				while (parse->text.length)
				{
					c = Text.nextCharacter(&parse->text);
					if (!isdigit(c.codepoint))
						break;
				}
			else
				c = Text.nextCharacter(&parse->text);
			
			if (c.codepoint == '.')
			{
				while (parse->text.length)
				{
					c = Text.nextCharacter(&parse->text);
					if (!isdigit(c.codepoint))
						break;
				}
			}
			
			if (c.codepoint == 'e' || c.codepoint == 'E')
			{
				c = Text.nextCharacter(&parse->text);
				if (c.codepoint == '+' || c.codepoint == '-')
					c = Text.nextCharacter(&parse->text);
				
				while (parse->text.length)
				{
					c = Text.nextCharacter(&parse->text);
					if (!isdigit(c.codepoint))
						break;
				}
			}
			
			Text.advance(&parse->text, -c.units);
			text.length = (int32_t)(parse->text.bytes - text.bytes);
			
			return Lexer.scanBinary(text, 0);
		}
			
		case '"':
		{
			struct Text text = string(parse);
			return Value.chars(Chars.createWithBytes(text.length, text.bytes));
			break;
		}
			
		case '{':
			return object(parse);
			break;
			
		case '[':
			return array(parse);
			break;
	}
	return error(parse, -c.units, Chars.create("unexpected '%.*s'", c.units, parse->text.bytes - c.units));
}

static
struct Value object (struct Parse *parse)
{
	struct Object *object = Object.create(Object(prototype));
	struct Text(Char) c;
	struct Value value;
	struct Key key;
	
	c = nextc(parse);
	if (c.codepoint != '}')
		for (;;)
		{
			if (c.codepoint != '"')
				return error(parse, -c.units, Chars.create("expect property name"));
			
			key = Key.makeWithText(string(parse), Key(copyOnCreate));
			
			c = nextc(parse);
			if (c.codepoint != ':')
				return error(parse, -c.units, Chars.create("expect colon"));
			
			value = literal(parse);
			if (value.type == Value(errorType))
				return value;
			
			Object.addMember(object, key, value, 0);
			
			c = nextc(parse);
			
			if (c.codepoint == '}')
				break;
			else if (c.codepoint == ',')
				c = nextc(parse);
			else
				return error(parse, -c.units, Chars.create("unexpected '%.*s'", c.units, parse->text.bytes - c.units));
		}
	
	return Value.object(object);
}

static
struct Value array (struct Parse *parse)
{
	struct Object *object = Array.create();
	struct Text(Char) c;
	struct Value value;
	
	for (;;)
	{
		value = literal(parse);
		if (value.type == Value(errorType))
			return value;
		
		Object.addElement(object, object->elementCount, value, 0);
		
		c = nextc(parse);
		
		if (c.codepoint == ',')
			continue;
		
		if (c.codepoint == ']')
			break;
		
		return error(parse, -c.units, Chars.create("unexpected '%.*s'", c.units, parse->text.bytes - c.units));
	}
	return Value.object(object);
}

static
struct Value json (struct Parse *parse)
{
	struct Text(Char) c = nextc(parse);
	
	if (c.codepoint == '{')
		return object(parse);
	else if (c.codepoint == '[')
		return array(parse);
	
	return error(parse, -c.units, Chars.create("expect { or ["));
}

// MARK: - Static Members

static
struct Value revive (struct Parse *parse, struct Value this, struct Value property, struct Value value)
{
	uint16_t hashmapCount;
	
	hashmapCount = parse->context.environment->hashmapCount;
	switch (hashmapCount) {
		default:
			memcpy(parse->context.environment->hashmap + 5,
				   parse->function->environment.hashmap,
				   sizeof(*parse->context.environment->hashmap) * (hashmapCount - 5));
		case 5:
			parse->context.environment->hashmap[3 + 1].value = value;
		case 4:
			parse->context.environment->hashmap[3 + 0].value = property;
		case 3:
			break;
		case 2:
		case 1:
		case 0:
			assert(0);
			break;
	}
	
	parse->context.ops = parse->ops;
	parse->context.this = this;
	parse->arguments->element[0].value = property;
	parse->arguments->element[1].value = value;
	return parse->context.ops->native(&parse->context);
}

static
struct Value walker (struct Parse *parse, struct Value this, struct Value property, struct Value value)
{
	uint32_t index, count;
	struct Chars(Append) chars;
	
	if (Value.isObject(value))
	{
		struct Object *object = value.data.object;
		
		for (index = 0, count = object->elementCount < Object(ElementMax)? object->elementCount: Object(ElementMax); index < count; ++index)
		{
			if (object->element[index].value.check == 1)
			{
				Chars.beginAppend(&chars);
				Chars.append(&chars, "%d", index);
				object->element[index].value = walker(parse, this, Chars.endAppend(&chars), object->element[index].value);
			}
		}
		
		for (index = 2; index < object->hashmapCount; ++index)
		{
			if (object->hashmap[index].value.check == 1)
				object->hashmap[index].value = walker(parse, this, Value.key(object->hashmap[index].value.key), object->hashmap[index].value);
		}
	}
	return revive(parse, this, property, value);
}

static
struct Value jsonParse (struct Context * const context)
{
	struct Value value, reviver, result;
	struct Parse parse = {
		.context = {
			.parent = context,
			.ecc = context->ecc,
			.depth = context->depth + 1,
			.textIndex = Context(callIndex),
		},
	};
	
	value = Value.toString(context, Context.argument(context, 0));
	reviver = Context.argument(context, 1);
	
	parse.text = Text.make(Value.stringBytes(&value), Value.stringLength(&value));
	parse.start = parse.text.bytes;
	parse.line = 1;
	parse.function = reviver.type == Value(functionType)? reviver.data.function: NULL;
	parse.ops = parse.function? parse.function->oplist->ops: NULL;
	
	result = json(&parse);
	
	if (result.type != Value(errorType) && parse.text.length)
	{
		struct Text(Char) c = Text.character(parse.text);
		result = error(&parse, c.units, Chars.create("unexpected '%.*s'", c.units, parse.text.bytes));
	}
	
	if (result.type == Value(errorType))
	{
		Context.setTextIndex(context, Context(noIndex));
		context->ecc->ofLine = parse.line;
		context->ecc->ofText = errorOfLine(&parse);
		context->ecc->ofInput = "(parse)";
		Context.throw(context, result);
	}
	
	if (parse.function && parse.function->flags & Function(needHeap))
	{
		struct Object *environment = Object.copy(&parse.function->environment);
		
		parse.context.environment = environment;
		parse.arguments = Arguments.createSized(2);
		++parse.arguments->referenceCount;
		
		environment->hashmap[2].value = Value.object(parse.arguments);
		
		result = walker(&parse, result, Value.text(&Text(empty)), result);
	}
	else if (parse.function)
	{
		struct Object environment = parse.function->environment;
		struct Object arguments = Object.identity;
		union Object(Hashmap) hashmap[parse.function->environment.hashmapCapacity];
		union Object(Element) element[2];
		
		memcpy(hashmap, parse.function->environment.hashmap, sizeof(hashmap));
		parse.context.environment = &environment;
		parse.arguments = &arguments;
		
		arguments.element = element;
		arguments.elementCount = 2;
		environment.hashmap = hashmap;
		environment.hashmap[2].value = Value.object(&arguments);
		
		result = walker(&parse, result, Value.text(&Text(empty)), result);
	}
	return result;
}

static
struct Value replace (struct Stringify *stringify, struct Value this, struct Value property, struct Value value)
{
	uint16_t hashmapCount;
	
	hashmapCount = stringify->context.environment->hashmapCount;
	switch (hashmapCount) {
		default:
			memcpy(stringify->context.environment->hashmap + 5,
				   stringify->function->environment.hashmap,
				   sizeof(*stringify->context.environment->hashmap) * (hashmapCount - 5));
		case 5:
			stringify->context.environment->hashmap[3 + 1].value = value;
		case 4:
			stringify->context.environment->hashmap[3 + 0].value = property;
		case 3:
			break;
		case 2:
		case 1:
		case 0:
			assert(0);
			break;
	}
	
	stringify->context.ops = stringify->ops;
	stringify->context.this = this;
	stringify->arguments->element[0].value = property;
	stringify->arguments->element[1].value = value;
	return stringify->context.ops->native(&stringify->context);
}

static
int stringifyValue (struct Stringify *stringify, struct Value this, struct Value property, struct Value value, int isArray, int addComa)
{
	uint32_t index, count;
	
	if (stringify->function)
		value = replace(stringify, this, property, value);
	
	if (!isArray)
	{
		if (value.type == Value(undefinedType))
			return 0;
		
		if (stringify->filter)
		{
			struct Object *object = stringify->filter;
			int found = 0;
			
			for (index = 0, count = object->elementCount < Object(ElementMax)? object->elementCount: Object(ElementMax); index < count; ++index)
			{
				if (object->element[index].value.check == 1)
				{
					if (Value.isTrue(Value.equals(&stringify->context, property, object->element[index].value)))
					{
						found = 1;
						break;
					}
				}
			}
			
			if (!found)
				return 0;
		}
	}
	
	if (addComa)
		Chars.append(&stringify->chars, ",%s", strlen(stringify->spaces)? "\n": "");
	
	for (index = 0, count = stringify->level; index < count; ++index)
		Chars.append(&stringify->chars, "%s", stringify->spaces);
	
	if (!isArray)
		Chars.append(&stringify->chars, "\"%.*s\":%s", Value.stringLength(&property), Value.stringBytes(&property), strlen(stringify->spaces)? " ": "");
	
	if (value.type == Value(functionType) || value.type == Value(undefinedType))
		Chars.append(&stringify->chars, "null");
	else if (Value.isObject(value))
	{
		struct Object *object = value.data.object;
		int isArray = Value.objectIsArray(object);
		struct Chars(Append) chars;
		const struct Text *property;
		int hasValue = 0;
		
		Chars.append(&stringify->chars, "%s%s", isArray? "[": "{", strlen(stringify->spaces)? "\n": "");
		++stringify->level;
		
		for (index = 0, count = object->elementCount < Object(ElementMax)? object->elementCount: Object(ElementMax); index < count; ++index)
		{
			if (object->element[index].value.check == 1)
			{
				Chars.beginAppend(&chars);
				Chars.append(&chars, "%d", index);
				hasValue |= stringifyValue(stringify, value, Chars.endAppend(&chars), object->element[index].value, isArray, hasValue);
			}
		}
		
		if (!isArray)
		{
			for (index = 0; index < object->hashmapCount; ++index)
			{
				if (object->hashmap[index].value.check == 1)
				{
					property = Key.textOf(object->hashmap[index].value.key);
					hasValue |= stringifyValue(stringify, value, Value.text(property), object->hashmap[index].value, isArray, hasValue);
				}
			}
		}
		
		Chars.append(&stringify->chars, "%s", strlen(stringify->spaces)? "\n": "");
		
		--stringify->level;
		for (index = 0, count = stringify->level; index < count; ++index)
			Chars.append(&stringify->chars, "%s", stringify->spaces);
		
		Chars.append(&stringify->chars, "%s", isArray? "]": "}");
	}
	else
		Chars.appendValue(&stringify->chars, &stringify->context, value);
	
	return 1;
}

static
struct Value jsonStringify (struct Context * const context)
{
	struct Value value, replacer, space;
	struct Stringify stringify = {
		.context = {
			.parent = context,
			.ecc = context->ecc,
			.depth = context->depth + 1,
			.textIndex = Context(callIndex),
		},
		.spaces = { 0 },
	};
	
	value = Context.argument(context, 0);
	replacer = Context.argument(context, 1);
	space = Context.argument(context, 2);
	
	stringify.filter = replacer.type == Value(objectType) && replacer.data.object->type == &Array(type)? replacer.data.object: NULL;
	stringify.function = replacer.type == Value(functionType)? replacer.data.function: NULL;
	stringify.ops = stringify.function? stringify.function->oplist->ops: NULL;
	
	if (Value.isString(space))
		snprintf(stringify.spaces, sizeof(stringify.spaces), "%.*s", (int)Value.stringLength(&space), Value.stringBytes(&space));
	else if (Value.isNumber(space))
	{
		int i = Value.toInteger(context, space).data.integer;
		
		if (i < 0)
			i = 0;
		
		if (i > 10)
			i = 10;
		
		while (i--)
			strcat(stringify.spaces, " ");
	}
	
	Chars.beginAppend(&stringify.chars);
	
	if (stringify.function && stringify.function->flags & Function(needHeap))
	{
		struct Object *environment = Object.copy(&stringify.function->environment);
		
		stringify.context.environment = environment;
		stringify.arguments = Arguments.createSized(2);
		++stringify.arguments->referenceCount;
		
		environment->hashmap[2].value = Value.object(stringify.arguments);
		
		stringifyValue(&stringify, value, Value.text(&Text(empty)), value, 1, 0);
	}
	else if (stringify.function)
	{
		struct Object environment = stringify.function->environment;
		struct Object arguments = Object.identity;
		union Object(Hashmap) hashmap[stringify.function->environment.hashmapCapacity];
		union Object(Element) element[2];
		
		memcpy(hashmap, stringify.function->environment.hashmap, sizeof(hashmap));
		stringify.context.environment = &environment;
		stringify.arguments = &arguments;
		
		arguments.element = element;
		arguments.elementCount = 2;
		environment.hashmap = hashmap;
		environment.hashmap[2].value = Value.object(&arguments);
		
		stringifyValue(&stringify, value, Value.text(&Text(empty)), value, 1, 0);
	}
	else
		stringifyValue(&stringify, value, Value.text(&Text(empty)), value, 1, 0);
	
	return Chars.endAppend(&stringify.chars);
}

// MARK: - Public

struct Object * JSON(object) = NULL;

void setup ()
{
	const enum Value(Flags) h = Value(hidden);
	
	JSON(object) = Object.createTyped(&JSON(type));
	
	Function.addToObject(JSON(object), "parse", jsonParse, -1, h);
	Function.addToObject(JSON(object), "stringify", jsonStringify, -1, h);
}

void teardown (void)
{
	JSON(object) = NULL;
}
