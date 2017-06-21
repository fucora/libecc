//
//  lexer.c
//  libecc
//
//  Copyright (c) 2019 Aurélien Bouilland
//  Licensed under MIT license, see LICENSE.txt file in project root
//

#define Implementation
#include "lexer.h"

// MARK: - Private

// MARK: - Static Members

static inline
void addLine(struct Lexer *self, uint32_t offset)
{
	if (self->input->lineCount + 1 >= self->input->lineCapacity)
	{
		self->input->lineCapacity *= 2;
		self->input->lines = realloc(self->input->lines, sizeof(*self->input->lines) * self->input->lineCapacity);
	}
	self->input->lines[++self->input->lineCount] = offset;
}

static inline
unsigned char previewChar(struct Lexer *self)
{
	if (self->offset < self->input->length)
		return self->input->bytes[self->offset];
	else
		return 0;
}

static inline
unsigned char nextChar(struct Lexer *self)
{
	if (self->offset < self->input->length)
	{
		unsigned char c = self->input->bytes[self->offset++];
		
		if ((c == '\r' && previewChar(self) != '\n') || c == '\n')
		{
			self->didLineBreak = 1;
			addLine(self, self->offset);
		}
		
		++self->text.length;
		return c;
	}
	else
		return 0;
}

static inline
int acceptChar(struct Lexer *self, char c)
{
	if (previewChar(self) == c)
	{
		nextChar(self);
		return 1;
	}
	else
		return 0;
}

static inline
char eof(struct Lexer *self)
{
	return self->offset >= self->input->length;
}

static inline
enum Lexer(Token) syntaxError(struct Lexer *self, struct Chars *message)
{
	struct Error *error = Error.syntaxError(self->text, message);
	self->value = Value.error(error);
	return Lexer(errorToken);
}

// MARK: - Methods

struct Lexer * createWithInput(struct Input *input)
{
	struct Lexer *self = malloc(sizeof(*self));
	*self = Lexer.identity;
	
	assert(input);
	self->input = input;
	
	return self;
}

void destroy (struct Lexer *self)
{
	assert(self);
	
	self->input = NULL;
	free(self), self = NULL;
}

enum Lexer(Token) nextToken (struct Lexer *self)
{
	unsigned char c;
	assert(self);
	
	self->value = Value(undefined);
	self->didLineBreak = 0;
	
	retry:
	self->text.bytes = self->input->bytes + self->offset;
	self->text.length = 0;
	
	while (( c = nextChar(self) ))
	{
		switch (c)
		{
			case '\n':
			case '\r':
			case '\t':
			case '\v':
			case '\f':
			case ' ':
				goto retry;
			
			case '/':
			{
				if (acceptChar(self, '*'))
				{
					while (!eof(self))
						if (nextChar(self) == '*' && acceptChar(self, '/'))
							goto retry;
					
					return syntaxError(self, Chars.create("unterminated comment"));
				}
				else if (previewChar(self) == '/')
				{
					while (( c = nextChar(self) ))
						if (c == '\r' || c == '\n')
							goto retry;
					
					return 0;
				}
				else if (self->allowRegex)
				{
					while (!eof(self))
					{
						int c = self->input->bytes[self->offset++];
						++self->text.length;
						
						if (c == '\\')
						{
							++self->offset;
							++self->text.length;
						}
						else if (c == '/')
						{
							while (isalpha(previewChar(self)))
								nextChar(self);
							
							return Lexer(regexpToken);
						}
						else if (c == '\r' || c == '\n')
							break;
					}
					return syntaxError(self, Chars.create("unterminated regexp literal"));
				}
				else if (acceptChar(self, '='))
					return Lexer(divideAssignToken);
				else
					return '/';
			}
			
			case '\'':
			case '"':
			{
				char end = c;
				int haveEscape = 0;
				int didLineBreak = self->didLineBreak;
				
				while (( c = nextChar(self) ))
				{
					if (c == '\\')
					{
						haveEscape = 1;
						nextChar(self);
						self->didLineBreak = didLineBreak;
					}
					else if (c == end)
					{
						const char *bytes = self->text.bytes++;
						uint32_t length = self->text.length -= 2;
						
						if (haveEscape)
						{
							struct Chars *chars;
							uint32_t index;
							
							++bytes;
							--length;
							
							Chars.beginAppendSized(&chars, length);
							
							for (index = 0; index <= length; ++index)
								if (bytes[index] == '\\' && bytes[++index] != '\\')
								{
									switch (bytes[index])
									{
										case '0': Chars.appendCodepoint(&chars, '\0'); break;
										case 'b': Chars.appendCodepoint(&chars, '\b'); break;
										case 'f': Chars.appendCodepoint(&chars, '\f'); break;
										case 'n': Chars.appendCodepoint(&chars, '\n'); break;
										case 'r': Chars.appendCodepoint(&chars, '\r'); break;
										case 't': Chars.appendCodepoint(&chars, '\t'); break;
										case 'v': Chars.appendCodepoint(&chars, '\v'); break;
										case 'x':
											if (isxdigit(bytes[index + 1]) && isxdigit(bytes[index + 2]))
											{
												Chars.appendCodepoint(&chars, uint8Hex(bytes[index + 1], bytes[index + 2]));
												index += 2;
												break;
											}
											self->text = Text.make(self->text.bytes + index - 1, 4);
											return syntaxError(self, Chars.create("malformed hexadecimal character escape sequence"));
										
										case 'u':
											if (isxdigit(bytes[index + 1]) && isxdigit(bytes[index + 2]) && isxdigit(bytes[index + 3]) && isxdigit(bytes[index + 4]))
											{
												Chars.appendCodepoint(&chars, uint16Hex(bytes[index+ 1], bytes[index + 2], bytes[index + 3], bytes[index + 4]));
												index += 4;
												break;
											}
											self->text = Text.make(self->text.bytes + index - 1, 6);
											return syntaxError(self, Chars.create("malformed Unicode character escape sequence"));
										
										default:
											Chars.append(&chars, "%c", bytes[index]);
									}
								}
								else
									Chars.append(&chars, "%c", bytes[index]);
							
							++chars->referenceCount;
							self->value = Value.chars(Chars.endAppend(&chars));
							
							Input.attachValue(self->input, self->value);
							
							return Lexer(escapedStringToken);
						}
						
						return Lexer(stringToken);
					}
					else if (c == '\r' || c == '\n')
						break;
				}
				
				return syntaxError(self, Chars.create("unterminated string literal"));
			}
			
			case '.':
				if (!isdigit(previewChar(self)))
					return c;
				
				/*vvv*/
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
				int binary = 0;
				
				if (c == '0' && (acceptChar(self, 'x') || acceptChar(self, 'X')))
				{
					while (( c = previewChar(self) ))
						if (isxdigit(c))
							nextChar(self);
						else
							break;
					
					if (self->text.length <= 2)
						return syntaxError(self, Chars.create("missing hexadecimal digits after '0x'"));
				}
				else
				{
					while (isdigit(previewChar(self)))
						nextChar(self);
					
					if (c == '.' || acceptChar(self, '.'))
						binary = 1;
					
					while (isdigit(previewChar(self)))
						nextChar(self);
					
					if (acceptChar(self, 'e') || acceptChar(self, 'E'))
					{
						binary = 1;
						
						if (!acceptChar(self, '+'))
							acceptChar(self, '-');
						
						if (!isdigit(previewChar(self)))
							return syntaxError(self, Chars.create("missing exponent"));
						
						while (isdigit(previewChar(self)))
							nextChar(self);
					}
				}
				
				if (isalpha(previewChar(self)))
				{
					self->text.bytes += self->text.length;
					self->text.length = 1;
					return syntaxError(self, Chars.create("identifier starts immediately after numeric literal"));
				}
				
				if (binary)
				{
					self->value = parseBinary(self->text, 0);
					return Lexer(binaryToken);
				}
				else
				{
					self->value = parseInteger(self->text, 0, 0);
					
					if (self->value.type == Value(integerType))
						return Lexer(integerToken);
					else
						return Lexer(binaryToken);
				}
			}
			
			case '}':
			case ')':
			case ']':
			case '{':
			case '(':
			case '[':
			case ';':
			case ',':
			case '~':
			case '?':
			case ':':
				return c;
			
			case '^':
				if (acceptChar(self, '='))
					return Lexer(xorAssignToken);
				else
					return c;
			
			case '%':
				if (acceptChar(self, '='))
					return Lexer(moduloAssignToken);
				else
					return c;
			
			case '*':
				if (acceptChar(self, '='))
					return Lexer(multiplyAssignToken);
				else
					return c;
			
			case '=':
				if (acceptChar(self, '='))
				{
					if (acceptChar(self, '='))
						return Lexer(identicalToken);
					else
						return Lexer(equalToken);
				}
				else
					return c;
			
			case '!':
				if (acceptChar(self, '='))
				{
					if (acceptChar(self, '='))
						return Lexer(notIdenticalToken);
					else
						return Lexer(notEqualToken);
				}
				else
					return c;
			
			case '+':
				if (acceptChar(self, '+'))
					return Lexer(incrementToken);
				else if (acceptChar(self, '='))
					return Lexer(addAssignToken);
				else
					return c;
			
			case '-':
				if (acceptChar(self, '-'))
					return Lexer(decrementToken);
				else if (acceptChar(self, '='))
					return Lexer(minusAssignToken);
				else
					return c;
			
			case '&':
				if (acceptChar(self, '&'))
					return Lexer(logicalAndToken);
				else if (acceptChar(self, '='))
					return Lexer(andAssignToken);
				else
					return c;
			
			case '|':
				if (acceptChar(self, '|'))
					return Lexer(logicalOrToken);
				else if (acceptChar(self, '='))
					return Lexer(orAssignToken);
				else
					return c;
			
			case '<':
				if (acceptChar(self, '<'))
				{
					if (acceptChar(self, '='))
						return Lexer(leftShiftAssignToken);
					else
						return Lexer(leftShiftToken);
				}
				else if (acceptChar(self, '='))
					return Lexer(lessOrEqualToken);
				else
					return c;
			
			case '>':
				if (acceptChar(self, '>'))
				{
					if (acceptChar(self, '>'))
					{
						if (acceptChar(self, '='))
							return Lexer(unsignedRightShiftAssignToken);
						else
							return Lexer(unsignedRightShiftToken);
					}
					else if (acceptChar(self, '='))
						return Lexer(rightShiftAssignToken);
					else
						return Lexer(rightShiftToken);
				}
				else if (acceptChar(self, '='))
					return Lexer(moreOrEqualToken);
				else
					return c;
			
			default:
			{
				if (isalpha(c) || c == '$' || c == '_')
				{
					static const struct {
						const char *name;
						size_t length;
						enum Lexer(Token) token;
					} keywords[] = {
						#define _(X) { #X, sizeof(#X) - 1, Lexer(X##Token) },
						_(break)
						_(case)
						_(catch)
						_(continue)
						_(debugger)
						_(default)
						_(delete)
						_(do)
						_(else)
						_(finally)
						_(for)
						_(function)
						_(if)
						_(in)
						_(instanceof)
						_(new)
						_(return)
						_(switch)
						_(typeof)
						_(throw)
						_(try)
						_(var)
						_(void)
						_(while)
						_(with)
						
						_(void)
						_(typeof)
						
						_(null)
						_(true)
						_(false)
						_(this)
						#undef _
					};
					
					static const struct {
						const char *name;
						size_t length;
					} reservedKeywords[] = {
						#define _(X) { #X, sizeof(#X) - 1 },
						_(class)
						_(enum)
						_(extends)
						_(super)
						_(const)
						_(export)
						_(import)
						_(implements)
						_(let)
						_(private)
						_(public)
						_(interface)
						_(package)
						_(protected)
						_(static)
						_(yield)
						#undef _
					};
					
					int k;
					
					while (isalnum(previewChar(self)) || previewChar(self) == '$' || previewChar(self) == '_')
						nextChar(self);
					
					if (!self->disallowKeyword)
					{
						for (k = 0; k < sizeof(keywords) / sizeof(*keywords); ++k)
							if (self->text.length == keywords[k].length && memcmp(self->text.bytes, keywords[k].name, keywords[k].length) == 0)
								return keywords[k].token;
						
						for (k = 0; k < sizeof(reservedKeywords) / sizeof(*reservedKeywords); ++k)
							if (self->text.length == reservedKeywords[k].length && memcmp(self->text.bytes, reservedKeywords[k].name, reservedKeywords[k].length) == 0)
								return syntaxError(self, Chars.create("'%s' is a reserved identifier", reservedKeywords[k]));
					}
					
					self->value = Value.key(Key.makeWithText(self->text, 0));
					return Lexer(identifierToken);
				}
				else
					if (isprint(c))
						return syntaxError(self, Chars.create("invalid character '%c'", c));
					else
						return syntaxError(self, Chars.create("invalid character '\\%d'", c & 0xff));
			}
		}
	}
	
	addLine(self, self->offset);
	return Lexer(noToken);
}

const char * tokenChars (enum Lexer(Token) token, char buffer[4])
{
	int index;
	
	struct {
		const char *name;
		const enum Lexer(Token) token;
	} static const tokenList[] = {
		#define _(X, S, V) { sizeof(S "") > sizeof("")? S "": #X, Lexer(X ## Token), },
		io_libecc_lexer_Tokens
		#undef _
	};
	
	if (token > Lexer(noToken) && token < Lexer(errorToken))
	{
		buffer[0] = '\'';
		buffer[1] = token;
		buffer[2] = '\'';
		buffer[3] = '\0';
		return buffer;
	}
	
	for (index = 0; index < sizeof(tokenList); ++index)
		if (tokenList[index].token == token)
			return tokenList[index].name;
	
	assert(0);
	return "unknow";
}

struct Value parseBinary (struct Text text, int strict)
{
	double binary;
	char buffer[text.length + 1];
	char *end;
	memcpy(buffer, text.bytes, text.length);
	buffer[text.length] = '\0';
	
	binary = strtod(buffer, &end);
	if (*end && (!isspace(*end) || strict))
		return Value.binary(NAN);
	
	return Value.binary(binary);
}

static double strtolHexFallback (struct Text text)
{
	double binary = 0;
	int sign = 1;
	int offset = 0;
	int c;
	
	if (text.bytes[offset] == '-')
		sign = -1, ++offset;
	
	if (text.length - offset > 1 && text.bytes[offset] == '0' && tolower(text.bytes[offset + 1]) == 'x')
	{
		offset += 2;
		
		while (text.length - offset >= 1)
		{
			c = text.bytes[offset++];
			
			binary *= 16;
			
			if (isdigit(c))
				binary += c - '0';
			else if (isxdigit(c))
				binary += tolower(c) - ('a' - 10);
			else
				return NAN;
		}
	}
	
	return binary * sign;
}

struct Value parseInteger (struct Text text, int base, int strict)
{
	long integer;
	char buffer[text.length + 1];
	char *end;
	
	if (!text.length)
		return Value.binary(NAN);
	
	memcpy(buffer, text.bytes, text.length);
	buffer[text.length] = '\0';
	
	errno = 0;
	integer = strtol(buffer, &end, base);
	
	if (*end && (!isspace(*end) || strict))
		return Value.binary(NAN);
	else if (errno == ERANGE)
	{
		if (!base || base == 10)
		{
			double binary = strtod(buffer, NULL);
			
			if (!binary && !base)
				binary = strtolHexFallback(text);
			
			return Value.binary(binary);
		}
		
		Env.printWarning("parseInt('%.*s', %d) out of bounds; only long int are supported by radices other than 10", text.length, text.bytes, base);
		return Value.binary(NAN);
	}
	else if (integer < INT32_MIN || integer > INT32_MAX)
		return Value.binary(integer);
	else
		return Value.integer((int32_t)integer);
}

uint32_t parseElement (struct Text text)
{
	struct Value value;
	uint16_t index;
	
	if (!text.length)
		return UINT32_MAX;
	
	for (index = 0; index < text.length; ++index)
		if (!isdigit(text.bytes[index]))
			return UINT32_MAX;
	
	value = parseInteger(text, 0, 0);
	
	if (value.type == Value(integerType))
		return value.data.integer;
	if (value.type == Value(binaryType) && value.data.binary >= 0 && value.data.binary < UINT32_MAX && value.data.binary == (uint32_t)value.data.binary)
		return value.data.binary;
	else
		return UINT32_MAX;
}

static inline int8_t hexhigit(int c)
{
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	else if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	else
		return c - '0';
}

uint8_t uint8Hex(char a, char b)
{
	return hexhigit(a) << 4 | hexhigit(b);
}

uint16_t uint16Hex(char a, char b, char c, char d)
{
	return hexhigit(a) << 12 | hexhigit(b) << 8 | hexhigit(c) << 4 | hexhigit(d);
}
