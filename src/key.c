//
//  key.c
//  libecc
//
//  Copyright (c) 2019 Aurélien Bouilland
//  Licensed under MIT license, see LICENSE.txt file in project root
//

#define Implementation
#include "key.h"

// MARK: - Private

static struct Text *keyPool = NULL;
static uint16_t keyCount = 0;
static uint16_t keyCapacity = 0;

static char **charsList = NULL;
static uint16_t charsCount = 0;

struct Key Key(none) = {{{ 0 }}};

struct Key Key(internal);
struct Key Key(prototype);
struct Key Key(constructor);
struct Key Key(length);
struct Key Key(arguments);
struct Key Key(callee);
struct Key Key(name);
struct Key Key(message);
struct Key Key(toString);
struct Key Key(valueOf);
struct Key Key(eval);
struct Key Key(value);
struct Key Key(writable);
struct Key Key(enumerable);
struct Key Key(configurable);
struct Key Key(get);
struct Key Key(set);
struct Key Key(join);
struct Key Key(toISOString);
struct Key Key(input);
struct Key Key(index);
struct Key Key(lastIndex);
struct Key Key(source);
struct Key Key(global);
struct Key Key(ignoreCase);
struct Key Key(multiline);


// MARK: - Static Members

// MARK: - Methods

void setup (void)
{
	if (!keyPool)
	{
		Key(prototype) = makeWithCString("prototype");
		Key(constructor) = makeWithCString("constructor");
		Key(length) = makeWithCString("length");
		Key(arguments) = makeWithCString("arguments");
		Key(callee) = makeWithCString("callee");
		Key(name) = makeWithCString("name");
		Key(message) = makeWithCString("message");
		Key(toString) = makeWithCString("toString");
		Key(valueOf) = makeWithCString("valueOf");
		Key(eval) = makeWithCString("eval");
		Key(value) = makeWithCString("value");
		Key(writable) = makeWithCString("writable");
		Key(enumerable) = makeWithCString("enumerable");
		Key(configurable) = makeWithCString("configurable");
		Key(get) = makeWithCString("get");
		Key(set) = makeWithCString("set");
		Key(join) = makeWithCString("join");
		Key(toISOString) = makeWithCString("toISOString");
		Key(input) = makeWithCString("input");
		Key(index) = makeWithCString("index");
		Key(lastIndex) = makeWithCString("lastIndex");
		Key(source) = makeWithCString("source");
		Key(global) = makeWithCString("global");
		Key(ignoreCase) = makeWithCString("ignoreCase");
		Key(multiline) = makeWithCString("multiline");
	}
}

void teardown (void)
{
	while (charsCount)
		free(charsList[--charsCount]), charsList[charsCount] = NULL;
	
	free(charsList), charsList = NULL, charsCount = 0;
	free(keyPool), keyPool = NULL, keyCount = 0, keyCapacity = 0;
}

struct Key makeWithCString (const char *cString)
{
	return makeWithText(Text.make(cString, (uint16_t)strlen(cString)), 0);
}

struct Key makeWithText (const struct Text text, int copyOnCreate)
{
	uint16_t number = 0, index = 0;
	
	#warning TODO: use binary tree
	for (index = 0; index < keyCount; ++index)
	{
		if (text.length == keyPool[index].length && memcmp(keyPool[index].bytes, text.bytes, text.length) == 0)
		{
			number = index + 1;
			break;
		}
	}
	
	if (!number)
	{
		if (keyCount >= keyCapacity)
		{
			keyCapacity = keyCapacity? keyCapacity * 2: 1;
			keyPool = realloc(keyPool, keyCapacity * sizeof(*keyPool));
		}
		
		if (copyOnCreate)
		{
			char *chars = malloc(text.length + 1);
			memcpy(chars, text.bytes, text.length);
			chars[text.length] = '\0';
			charsList = realloc(charsList, sizeof(*charsList) * (charsCount + 1));
			charsList[charsCount++] = chars;
			keyPool[keyCount++] = Text.make(chars, text.length);
		}
		else
			keyPool[keyCount++] = text;
		
		number = keyCount;
	}
	
	return (struct Key) {{{
		number >> 12 & 0xf,
		number >> 8 & 0xf,
		number >> 4 & 0xf,
		number >> 0 & 0xf,
	}}};
}

int isEqual (struct Key self, struct Key to)
{
	return self.data.integer == to.data.integer;
}

const struct Text *textOf (struct Key key)
{
	uint16_t number = key.data.depth[0] << 12 | key.data.depth[1] << 8 | key.data.depth[2] << 4 | key.data.depth[3];
	if (number)
		return &keyPool[number - 1];
	else
		return &Text(empty);
}

void dumpTo (struct Key key, FILE *file)
{
	const struct Text *text = textOf(key);
	fprintf(file, "%.*s", text->length, text->bytes);
}
