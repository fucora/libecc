//
//  regexp.c
//  libecc
//
//  Copyright (c) 2019 Aurélien Bouilland
//  Licensed under MIT license, see LICENSE.txt file in project root
//

#define Implementation
#include "regexp.h"

#include "../pool.h"
#include "../ecc.h"
#include "../lexer.h"

#define DUMP_REGEXP 0

enum Opcode {
	opOver = 0,
	opNLookahead = 1,
	opLookahead = 2,
	opStart,
	opEnd,
	opBoundary,
	
	opSplit,
	opReference,
	opRedo,
	opSave,
	opAny,
	opOneOf,
	opNeitherOf,
	opInRange,
	opDigit,
	opSpace,
	opWord,
	opBytes,
	opJump,
	opMatch,
};

struct RegExp(Node) {
	char *bytes;
	int16_t offset;
	uint8_t opcode;
	uint8_t depth;
};

struct Parse {
	const char *c;
	const char *end;
	uint16_t count;
	int disallowQuantifier;
};

enum Flags {
	infiniteLoop = 1 << 1,
};

// MARK: - Private

struct Object * RegExp(prototype) = NULL;
struct Function * RegExp(constructor) = NULL;

static
void mark (struct Object *object)
{
	struct RegExp *self = (struct RegExp *)object;
	
	Pool.markValue(Value.chars(self->pattern));
	Pool.markValue(Value.chars(self->source));
}

static
void finalize (struct Object *object)
{
	struct RegExp *self = (struct RegExp *)object;
	struct RegExp(Node) *n = self->program;
	while (n->opcode != opOver)
	{
		if (n->bytes)
			free(n->bytes), n->bytes = NULL;
		
		++n;
	}
	
	free(self->program), self->program = NULL;
	
	--self->pattern->referenceCount;
	--self->source->referenceCount;
}

const struct Object(Type) RegExp(type) = {
	.text = &Text(regexpType),
	.mark = mark,
	.finalize = finalize,
};

#if DUMP_REGEXP
static
void printNode (struct RegExp(Node) *n)
{
	switch (n->opcode)
	{
		case opOver: fprintf(stderr, "over "); break;
		case opNLookahead: fprintf(stderr, "!lookahead "); break;
		case opLookahead: fprintf(stderr, "lookahead "); break;
		case opStart: fprintf(stderr, "start "); break;
		case opEnd: fprintf(stderr, "end "); break;
		case opBoundary: fprintf(stderr, "boundary "); break;
		
		case opSplit: fprintf(stderr, "split "); break;
		case opReference: fprintf(stderr, "reference "); break;
		case opRedo: fprintf(stderr, "redo "); break;
		case opSave: fprintf(stderr, "save "); break;
		case opAny: fprintf(stderr, "any "); break;
		case opOneOf: fprintf(stderr, "one of "); break;
		case opNeitherOf: fprintf(stderr, "neither of "); break;
		case opInRange: fprintf(stderr, "in range "); break;
		case opDigit: fprintf(stderr, "digit "); break;
		case opSpace: fprintf(stderr, "space "); break;
		case opWord: fprintf(stderr, "word "); break;
		case opBytes: fprintf(stderr, "bytes "); break;
		case opJump: fprintf(stderr, "jump "); break;
		case opMatch: fprintf(stderr, "match "); break;
	}
	fprintf(stderr, "%d", n->offset);
	if (n->bytes)
	{
		if (n->opcode == opRedo)
		{
			char *c = n->bytes + 1;
			fprintf(stderr, " {%u-%u}", n->bytes[0], n->bytes[1]);
			if (*(c + 1))
				fprintf(stderr, " clear:");
			
			while (*(++c))
				fprintf(stderr, "%u,", *c);
		}
		else if (n->opcode != opRedo)
			fprintf(stderr, " `%s`", n->bytes);
	}
	putc('\n', stderr);
}
#endif

//MARK: parsing

static
struct RegExp(Node) * node (enum Opcode opcode, long offset, const char *bytes)
{
	struct RegExp(Node) *n = calloc(2, sizeof(*n));
	
	if (offset && bytes)
	{
		n[0].bytes = calloc(offset + 1, 1);
		memcpy(n[0].bytes, bytes, offset);
	}
	n[0].offset = offset;
	n[0].opcode = opcode;
	
	return n;
}

static
void toss (struct RegExp(Node) *node)
{
	struct RegExp(Node) *n = node;
	
	if (!node)
		return;
	
	while (n->opcode != opOver)
	{
		free(n->bytes), n->bytes = NULL;
		++n;
	}
	
	free(node), node = NULL;
}

static
uint16_t nlen (struct RegExp(Node) *n)
{
	uint16_t len = 0;
	if (n)
		while (n[++len].opcode != opOver);
	
	return len;
}

static
struct RegExp(Node) *join (struct RegExp(Node) *a, struct RegExp(Node) *b)
{
	uint16_t lena = 0, lenb = 0;
	
	if (!a)
		return b;
	else if (!b)
		return a;
	
	while (a[++lena].opcode != opOver);
	while (b[++lenb].opcode != opOver);
	
	if (lenb == 1 && a[lena - 1].opcode == opBytes && b->opcode == opBytes)
	{
		struct RegExp(Node) *c = a + lena - 1;
		
		c->bytes = realloc(c->bytes, c->offset + b->offset + 1);
		memcpy(c->bytes + c->offset, b->bytes, b->offset + 1);
		c->offset += b->offset;
		free(b->bytes), b->bytes = NULL;
	}
	else
	{
		a = realloc(a, sizeof(*a) * (lena + lenb + 1));
		memcpy(a + lena, b, sizeof(*a) * (lenb + 1));
	}
	free(b), b = NULL;
	return a;
}

static
int accept(struct Parse *p, char c)
{
	if (*p->c == c)
	{
		++p->c;
		return 1;
	}
	return 0;
}

static struct RegExp(Node) * disjunction (struct Parse *p, struct Error **error);

static
enum Opcode escape (struct Parse *p, int16_t *offset, char buffer[5])
{
	*offset = 1;
	buffer[0] = *(p->c++);
	
	switch (buffer[0])
	{
		case 'D':
			*offset = 0;
		case 'd':
			return opDigit;
			
		case 'S':
			*offset = 0;
		case 's':
			return opSpace;
			
		case 'W':
			*offset = 0;
		case 'w':
			return opWord;
			
		case 'b': buffer[0] = '\b'; break;
		case 'f': buffer[0] = '\f'; break;
		case 'n': buffer[0] = '\n'; break;
		case 'r': buffer[0] = '\r'; break;
		case 't': buffer[0] = '\t'; break;
		case 'v': buffer[0] = '\v'; break;
		case 'c':
			if (tolower(*p->c) >= 'a' && tolower(*p->c) <= 'z')
				buffer[0] = *(p->c++) % 32;
			
			break;
			
		case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7':
			buffer[0] -= '0';
			if (*p->c >= '0' && *p->c <= '7')
			{
				buffer[0] = buffer[0] * 8 + *(p->c++) - '0';
				if (*p->c >= '0' && *p->c <= '7')
				{
					buffer[0] = buffer[0] * 8 + *(p->c++) - '0';
					if (*p->c >= '0' && *p->c <= '7')
					{
						if ((int)buffer[0] * 8 + *p->c - '0' <= 0xFF)
							buffer[0] = buffer[0] * 8 + *(p->c++) - '0';
					}
				}
				
				if (buffer[0])
					*offset = Chars.writeCodepoint(buffer, ((uint8_t *)buffer)[0]);
			}
			break;
			
		case 'x':
			if (isxdigit(p->c[0]) && isxdigit(p->c[1]))
			{
				*offset = Chars.writeCodepoint(buffer, Lexer.uint8Hex(p->c[0], p->c[1]));
				p->c += 2;
			}
			break;
			
		case 'u':
			if (isxdigit(p->c[0]) && isxdigit(p->c[1]) && isxdigit(p->c[2]) && isxdigit(p->c[3]))
			{
				*offset = Chars.writeCodepoint(buffer, Lexer.uint16Hex(p->c[0], p->c[1], p->c[2], p->c[3]));
				p->c += 4;
			}
			break;
			
		default:
			break;
	}
	
	return opBytes;
}

static
struct RegExp(Node) * term (struct Parse *p, struct Error **error)
{
	struct RegExp(Node) *n;
	struct Text(Char) c;
	
	p->disallowQuantifier = 0;
	
	if (p->c >= p->end - 1)
		return NULL;
	else if (accept(p, '^'))
	{
		p->disallowQuantifier = 1;
		return node(opStart, 0, NULL);
	}
	else if (accept(p, '$'))
	{
		p->disallowQuantifier = 1;
		return node(opEnd, 0, NULL);
	}
	else if (accept(p, '\\'))
	{
		switch (*p->c)
		{
			case 'b':
				++p->c;
				p->disallowQuantifier = 1;
				return node(opBoundary, 1, NULL);
				
			case 'B':
				++p->c;
				p->disallowQuantifier = 1;
				return node(opBoundary, 0, NULL);
				
			case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
			{
				int c = *(p->c++) - '0';
				while (isdigit(*(p->c)))
					c = c * 10 + *(p->c++) - '0';
				
				return node(opReference, c, NULL);
			}
				
			default:
			{
				enum Opcode opcode;
				int16_t offset;
				char buffer[5];
				
				opcode = escape(p, &offset, buffer);
				if (opcode == opBytes)
					return node(opBytes, offset, buffer);
				else
					return node(opcode, offset, NULL);
			}
		}
	}
	else if (accept(p, '('))
	{
		unsigned char count = 0;
		char modifier = '\0';
		
		if (accept(p, '?'))
		{
			if (*p->c == '=' || *p->c == '!' || *p->c == ':')
				modifier = *(p->c++);
		}
		else
		{
			count = ++p->count;
			if ((int)count * 2 + 1 > 0xff)
			{
				*error = Error.syntaxError(Text.make(p->c, 1), Chars.create("too many captures"));
				return NULL;
			}
		}
		
		n = disjunction(p, error);
		if (!accept(p, ')'))
		{
			*error = Error.syntaxError(Text.make(p->c, 1), Chars.create("expect ')'"));
			return NULL;
		}
		
		switch (modifier) {
			case '\0': return join(node(opSave, count * 2, NULL), join(n, node(opSave, count * 2 + 1, NULL)));
			case '=': p->disallowQuantifier = 1; return join(node(opLookahead, nlen(n) + 1, NULL), n);
			case '!': p->disallowQuantifier = 1; return join(node(opNLookahead, nlen(n) + 1, NULL), n);
			case ':': return n;
		}
	}
	else if (accept(p, '.'))
		return node(opAny, 0, NULL);
	else if (accept(p, '['))
	{
		enum Opcode opcode;
		int16_t offset;
		
		int not = accept(p, '^'), length = 0, lastLength, range = -1;
		char buffer[255];
		n = NULL;
		
		while (*p->c != ']' || range >= 0)
		{
			lastLength = length;
			
			if (accept(p, '\\'))
			{
				opcode = escape(p, &offset, buffer + length);
				if (opcode == opBytes)
					length += offset;
				else
				{
					if (not)
						n = join(node(opNLookahead, 3, NULL), join(node(opcode, offset, NULL), join(node(opMatch, 0, NULL), n)));
					else
						n = join(node(opSplit, 3, NULL), join(node(opcode, offset, NULL), join(node(opJump, nlen(n)+2, NULL), n)));
				}
			}
			else
			{
				opcode = opBytes;
				buffer[length++] = *(p->c++);
			}
			
			if (range >= 0)
			{
				if (opcode == opBytes)
				{
					if (not)
						n = join(node(opNLookahead, 3, NULL),
								 join(node(opInRange, length - range, buffer + range),
									  join(node(opMatch, 0, NULL),
										   n)));
					else
						n = join(node(opSplit, 3, NULL),
								 join(node(opInRange, length - range, buffer + range),
									  join(node(opJump, nlen(n)+2, NULL),
										   n)));
					
					length = range;
				}
				range = -1;
			}
			
			if (opcode == opBytes && *p->c == '-')
			{
				buffer[length++] = *(p->c++);
				range = lastLength;
			}
			
			if (p->c >= p->end || length >= sizeof(buffer))
			{
				*error = Error.syntaxError(Text.make(p->c - 1, 1), Chars.create("expect ']'"));
				return NULL;
			}
		}
		
		buffer[length] = '\0';
		accept(p, ']');
		return join(n, node(not? opNeitherOf: opOneOf, length, buffer));
	}
	else if (*p->c && strchr("*+?)}|", *p->c))
		return NULL;
	
	c = Text.character(Text.make(p->c, p->end - p->c));
	n = node(opBytes, c.units, p->c);
	p->c += c.units;
	return n;
}

static
struct RegExp(Node) * alternative (struct Parse *p, struct Error **error)
{
	struct RegExp(Node) *n = NULL, *t = NULL;
	
	for (;;)
	{
		if (!(t = term(p, error)))
			break;
		
		if (!p->disallowQuantifier)
		{
			int noop = 0, lazy = 0;
			
			uint8_t quantifier =
				accept(p, '?')? '?':
				accept(p, '*')? '*':
				accept(p, '+')? '+':
				accept(p, '{')? '{':
				'\0',
				min = 1,
				max = 1;
			
			switch (quantifier)
			{
				case '?':
					min = 0, max = 1;
					break;
					
				case '*':
					min = 0, max = 0;
					break;
					
				case '+':
					min = 1, max = 0;
					break;
					
				case '{':
				{
					
					if (isdigit(*p->c))
					{
						min = *(p->c++) - '0';
						while (isdigit(*p->c))
							min = min * 10 + *(p->c++) - '0';
					}
					else
					{
						*error = Error.syntaxError(Text.make(p->c, 1), Chars.create("expect number"));
						goto error;
					}
					
					if (accept(p, ','))
					{
						if (isdigit(*p->c))
						{
							max = *(p->c++) - '0';
							while (isdigit(*p->c))
								max = max * 10 + *(p->c++) - '0';
							
							if (!max)
								noop = 1;
						}
						else
							max = 0;
					}
					else if (!min)
						noop = 1;
					else
						max = min;
					
					if (!accept(p, '}'))
					{
						*error = Error.syntaxError(Text.make(p->c, 1), Chars.create("expect '}'"));
						goto error;
					}
					break;
				}
			}
			
			lazy = accept(p, '?');
			if (noop)
			{
				toss(t);
				continue;
			}
			
			if (max != 1)
			{
				struct RegExp(Node) *redo;
				uint16_t index, count = nlen(t), length = 2;
				char buffer[count + 2];
				
				for (index = 0; index < count; ++index)
					if (t[index].opcode == opSave)
						buffer[length++] = (uint8_t)t[index].offset;
				
				buffer[0] = min;
				buffer[1] = max;
				buffer[length] = '\0';
				
				if (lazy)
					redo = join(node(opRedo, 2, NULL), node(opJump, -nlen(t) - 1, NULL));
				else
					redo = node(opRedo, -nlen(t), NULL);
				
				redo->bytes = malloc(length + 1);
				memcpy(redo->bytes, buffer, length + 1);
				
				t = join(t, redo);
			}
			
			if (min == 0)
			{
				if (lazy)
					t = join(node(opSplit, 2, NULL), join(node(opJump, nlen(t) + 1, NULL), t));
				else
					t = join(node(opSplit, nlen(t) + 1, NULL), t);
			}
		}
		n = join(n, t);
	}
	return n;
	
error:
	toss(t);
	return n;
}

static
struct RegExp(Node) * disjunction (struct Parse *p, struct Error **error)
{
	struct RegExp(Node) *n = alternative(p, error), *d;
	if (accept(p, '|'))
	{
		uint16_t len;
		d = disjunction(p, error);
		n = join(n, node(opJump, nlen(d) + 1, NULL));
		len = nlen(n);
		n = join(n, d);
		n = join(node(opSplit, len + 1, NULL), n);
	}
	return n;
}

static
struct RegExp(Node) * pattern (struct Parse *p, struct Error **error)
{
	assert(*p->c == '/');
	assert(*(p->end - 1) == '/');
	
	++p->c;
	return join(disjunction(p, error), node(opMatch, 0, NULL));
}


//MARK: matching

static
int match (struct RegExp(State) * const s, struct RegExp(Node) *n, struct Text text);

static
void clear (struct RegExp(State) * const s, const char *c, uint8_t *bytes)
{
	uint8_t index;
	
	if (!bytes)
		return;
	
	while (*bytes)
	{
		index = *bytes++;
		s->index[index] = index % 2? 0: c;
	}
}

static
int forkMatch (struct RegExp(State) * const s, struct RegExp(Node) *n, struct Text text, int16_t offset)
{
	int result;
	
	if (n->depth == 0xff)
		return 0; //!\\ too many recursion
	else
		++n->depth;
	
	result = match(s, n + offset, text);
	--n->depth;
	return result;
}


int match (struct RegExp(State) * const s, struct RegExp(Node) *n, struct Text text)
{
	goto start;
next:
	++n;
start:
	;
	
	switch((enum Opcode)n->opcode)
	{
		case opNLookahead:
		case opLookahead:
			if (forkMatch(s, n, text, 1) == (n->opcode - 1))
				goto jump;
			
			return 0;
			
		case opStart:
			if (text.bytes != s->start)
				return 0;
			
			goto next;
			
		case opEnd:
			if (text.bytes != s->end)
				return 0;
			
			goto next;
			
		case opBoundary:
			if (text.bytes == s->start || text.bytes == s->end)
			{
				if (Text.isWord(Text.character(text)) != n->offset)
					return 0;
			}
			else
			{
				struct Text prev = text;
				if ((Text.isWord(Text.prevCharacter(&prev))
					!=
					Text.isWord(Text.character(text))) != n->offset
					)
					return 0;
			}
			goto next;
			
		case opSplit:
			if (text.bytes == n->bytes)
			{
				s->flags |= infiniteLoop;
				return 0;
			}
			else
				n->bytes = (char *)text.bytes;
			
			if (forkMatch(s, n, text, 1))
				return 1;
			
			goto jump;
			
		case opReference:
		{
			uint16_t len;
			
			if (text.bytes == n->bytes)
			{
				s->flags |= infiniteLoop;
				return 0;
			}
			else
				n->bytes = (char *)text.bytes;
			
			len = s->capture[n->offset * 2 + 1]? s->capture[n->offset * 2 + 1] - s->capture[n->offset * 2]: 0;
			
			if (len)
			{
				if (text.length < len || memcmp(text.bytes, s->capture[n->offset * 2], len))
					return 0;
				
				Text.advance(&text, len);
			}
			goto next;
		}
			
		case opRedo:
			if (n->bytes[1] && n->depth >= n->bytes[1])
				return 0;
			
			s->flags &= ~infiniteLoop;
			
			if (forkMatch(s, n, text, n->offset))
			{
				clear(s, text.bytes, (uint8_t *)n->bytes + 2);
				return 1;
			}
			
			if (n->depth + 1 < n->bytes[0])
				return 0;
			
			if (s->flags & infiniteLoop)
				clear(s, text.bytes, (uint8_t *)n->bytes + 2);
			
			goto next;
			
		case opSave:
			s->capture[n->offset] = text.bytes;
			if (forkMatch(s, n, text, 1)) {
				if (s->capture[n->offset] < text.bytes && text.bytes > s->index[n->offset]) {
					s->capture[n->offset] = text.bytes;
				}
				return 1;
			}
			s->capture[n->offset] = NULL;
			return 0;
			
		case opDigit:
			if (text.length < 1 || Text.isDigit(Text.nextCharacter(&text)) != n->offset)
				return 0;
			
			goto next;
			
		case opSpace:
			if (text.length < 1 || Text.isSpace(Text.nextCharacter(&text)) != n->offset)
				return 0;
			
			goto next;
			
		case opWord:
			if (text.length < 1 || Text.isWord(Text.nextCharacter(&text)) != n->offset)
				return 0;
			
			goto next;
			
		case opBytes:
			if (text.length < n->offset || memcmp(n->bytes, text.bytes, n->offset))
				return 0;
			
			Text.advance(&text, n->offset);
			goto next;
			
		case opOneOf:
		{
			char buffer[5];
			struct Text(Char) c;
			
			c = Text.character(text);
			memcpy(buffer, text.bytes, c.units);
			buffer[c.units] = '\0';
			
			if (n->bytes && strstr(n->bytes, buffer))
			{
				Text.nextCharacter(&text);
				goto next;
			}
			return 0;
		}
			
		case opNeitherOf:
		{
			char buffer[5];
			struct Text(Char) c;
			
			c = Text.character(text);
			memcpy(buffer, text.bytes, c.units);
			buffer[c.units] = '\0';
			
			if (n->bytes && strstr(n->bytes, buffer))
				return 0;
			
			Text.nextCharacter(&text);
			goto next;
		}
			
		case opInRange:
		{
			struct Text range = Text.make(n->bytes, n->offset);
			struct Text(Char) from, to, c;
			
			from = Text.nextCharacter(&range);
			Text.advance(&range, 1);
			to = Text.nextCharacter(&range);
			
			c = Text.character(text);
			if ((c.codepoint < from.codepoint || c.codepoint > to.codepoint))
				return 0;
			
			Text.nextCharacter(&text);
			goto next;
		}
			
		case opAny:
			if (text.length < 1 || !Text.isLineFeed(Text.nextCharacter(&text)))
				goto next;
			
			return 0;
			
		case opJump:
			goto jump;
			
		case opMatch:
			s->capture[1] = text.bytes;
			return 1;
			
		case opOver:
			break;
	}
	abort();
	
jump:
	n += n->offset;
	goto start;
}


// MARK: - Static Members

static struct Value constructor (struct Context * const context)
{
	struct Error *error = NULL;
	struct Value pattern, flags;
	struct Chars *chars;
	struct RegExp *regexp;
	
	Context.assertParameterCount(context, 2);
	
	pattern = Context.argument(context, 0);
	flags = Context.argument(context, 1);
	
	if (pattern.type == Value(regexpType) && flags.type == Value(undefinedType))
	{
		if (context->construct)
			chars = pattern.data.regexp->pattern;
		else
			return pattern;
	}
	else
	{
		Chars.beginAppend(&chars);
		Chars.append(&chars, "/");
		
		if (pattern.type == Value(regexpType))
			Chars.appendValue(&chars, context, Value.chars(pattern.data.regexp->source));
		else
			Chars.appendValue(&chars, context, pattern);
		
		Chars.append(&chars, "/");
		if (flags.type != Value(undefinedType))
			Chars.appendValue(&chars, context, flags);
		
		Chars.endAppend(&chars);
	}
	
	regexp = create(chars, &error);
	if (error)
	{
		context->ecc->ofLine = Text.make(chars->bytes, chars->length);
		Context.setText(context, &Text(nativeCode));
		Context.throw(context, Value.error(error));
	}
	return Value.regexp(regexp);
}

static struct Value toString (struct Context * const context)
{
	struct RegExp *self = context->this.data.regexp;
	
	Context.assertParameterCount(context, 0);
	Context.assertThisType(context, Value(regexpType));
	
	if (self->program[0].opcode == opMatch)
		return Value.text(&Text(emptyRegExp));
	else
		return Value.chars(self->pattern);
}

static struct Value exec (struct Context * const context)
{
	struct RegExp *self = context->this.data.regexp;
	struct Value value, lastIndex;
	
	Context.assertParameterCount(context, 1);
	Context.assertThisType(context, Value(regexpType));
	
	value = Value.toString(context, Context.argument(context, 0));
	{
		uint16_t length = Value.stringLength(value);
		const char *bytes = Value.stringBytes(value);
		const char *capture[2 + self->count * 2];
		const char *index[2 + self->count * 2];
		struct RegExp(State) state = { bytes, bytes + length, capture, index };
		struct Chars *element;
		
		if (self->global)
		{
			lastIndex = Value.toInteger(context, Object.getMember(&self->object, context, Key(lastIndex)));
			if ((uint32_t)lastIndex.data.integer > length)
				lastIndex.data.integer = length;
			
			state.start += (uint32_t)lastIndex.data.integer;
		}
		
		if (matchWithState(self, &state))
		{
			struct Object *array = Array.createSized(self->count);
			int32_t index, count;
			
			for (index = 0, count = self->count; index < count; ++index)
			{
				if (capture[index * 2])
				{
					element = Chars.createWithBytes(capture[index * 2 + 1] - capture[index * 2], capture[index * 2]);
#warning TODO: referenceCount
					++element->referenceCount;
					array->element[index].value = Pool.retainedValue(Value.chars(element));
				}
				else
					array->element[index].value = Value(undefined);
			}
			
			if (self->global)
				Object.putMember(&self->object, context, Key(lastIndex), Value.integer((int32_t)(capture[1] - bytes)));
			
			Object.addMember(array, Key(index), Value.integer((int32_t)(capture[0] - bytes)), 0);
			Object.addMember(array, Key(input), Pool.retainedValue(value), 0);
			
			return Value.object(array);
		}
	}
	return Value(null);
}

static struct Value test (struct Context * const context)
{
	struct RegExp *self = context->this.data.regexp;
	struct Value value, lastIndex;
	
	Context.assertParameterCount(context, 1);
	Context.assertThisType(context, Value(regexpType));
	
	value = Value.toString(context, Context.argument(context, 0));
	{
		uint16_t length = Value.stringLength(value);
		const char *bytes = Value.stringBytes(value);
		const char *capture[2 + self->count * 2];
		const char *index[2 + self->count * 2];
		struct RegExp(State) state = { bytes, bytes + length, capture, index };
		
		if (self->global)
		{
			lastIndex = Value.toInteger(context, Object.getMember(&self->object, context, Key(lastIndex)));
			if ((uint32_t)lastIndex.data.integer > length)
				lastIndex.data.integer = length;
			
			state.start += (uint32_t)lastIndex.data.integer;
		}
		
		value = Value.truth(matchWithState(self, &state));
		
		if (self->global)
			Object.putMember(&self->object, context, Key(lastIndex), Value.integer((int32_t)(capture[1] - bytes)));
		
		return value;
	}
}

// MARK: - Methods

void setup ()
{
	const enum Value(Flags) h = Value(hidden);
	
	Function.setupBuiltinObject(
		&RegExp(constructor), constructor, 2,
		&RegExp(prototype), Value.regexp(create(Chars.create("//"), NULL)),
		&RegExp(type));
	
	Function.addToObject(RegExp(prototype), "toString", toString, 0, h);
	Function.addToObject(RegExp(prototype), "exec", exec, 1, h);
	Function.addToObject(RegExp(prototype), "test", test, 1, h);
}

void teardown (void)
{
	RegExp(prototype) = NULL;
	RegExp(constructor) = NULL;
}

struct RegExp * create (struct Chars *s, struct Error **error)
{
	struct Parse p = { 0 };
	
	struct RegExp *self = malloc(sizeof(*self));
	*self = RegExp.identity;
	Pool.addObject(&self->object);
	
	Object.initialize(&self->object, RegExp(prototype));
	
	p.c = s->bytes;
	p.end = s->bytes + s->length;
	while (p.end > p.c && *(p.end - 1) != '/')
		p.end--;
	
#if DUMP_REGEXP
	fprintf(stderr, "\n%.*s\n", s->length, s->bytes);
#endif
	
	self->pattern = s;
	self->program = pattern(&p, error);
	self->count = p.count + 1;
	self->source = Chars.createWithBytes(p.c - self->pattern->bytes - 1, self->pattern->bytes + 1);
	
	++self->pattern->referenceCount;
	++self->source->referenceCount;
	
	if (*p.c == '/')
		for (;;)
		{
			switch (*(++p.c)) {
				case 'g':
					if (self->global == 1)
						*error = Error.syntaxError(Text.make(p.c, 1), Chars.create("invalid flags"));
					
					self->global = 1;
					continue;
					
				case 'i':
					if (self->ignoreCase == 1)
						*error = Error.syntaxError(Text.make(p.c, 1), Chars.create("invalid flags"));
					
					self->ignoreCase = 1;
					continue;
					
				case 'm':
					if (self->multiline == 1)
						*error = Error.syntaxError(Text.make(p.c, 1), Chars.create("invalid flags"));
					
					self->multiline = 1;
					continue;
			}
			break;
		}
	else if (!*error)
		*error = Error.syntaxError(Text.make(p.c, 1), Chars.create("invalid character '%c'", isgraph(*p.c)? *p.c: '?'));
	
	Object.addMember(&self->object, Key(source), Value.chars(self->source), Value(readonly) | Value(hidden) | Value(sealed));
	Object.addMember(&self->object, Key(global), Value.truth(self->global), Value(readonly) | Value(hidden) | Value(sealed));
	Object.addMember(&self->object, Key(ignoreCase), Value.truth(self->ignoreCase), Value(readonly) | Value(hidden) | Value(sealed));
	Object.addMember(&self->object, Key(multiline), Value.truth(self->multiline), Value(readonly) | Value(hidden) | Value(sealed));
	Object.addMember(&self->object, Key(lastIndex), Value.integer(0), Value(hidden) | Value(sealed));
	
	return self;
}

int matchWithState (struct RegExp *self, struct RegExp(State) *state)
{
	int result = 0;
	uint16_t index, count;
	struct Text text = Text.make(state->start, state->end - state->start);
	
#if DUMP_REGEXP
	struct RegExp(Node) *n = self->program;
	while (n->opcode != opOver)
		printNode(n++);
#endif
	
	while (!result && text.length)
	{
		memset(state->capture, 0, sizeof(*state->capture) * (2 + self->count * 2));
		memset(state->index, 0, sizeof(*state->index) * (2 + self->count * 2));
		state->capture[0] = state->index[0] = text.bytes;
		result = match(state, self->program, text);
		Text.nextCharacter(&text);
	}
	
	/* XXX: cleanup */
	
	for (index = 0, count = nlen(self->program); index < count; ++index)
	{
		if (self->program[index].opcode == opSplit || self->program[index].opcode == opReference)
			self->program[index].bytes = NULL;
		
		self->program[index].depth = 0;
	}
	
	return result;
}
