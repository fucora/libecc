//
//  parser.c
//  libecc
//
//  Copyright (c) 2019 Aurélien Bouilland
//  Licensed under MIT license, see LICENSE.txt file in project root
//

#include "parser.h"

// MARK: - Private

static void changeFunction (struct Op *op, Function function)
{
	*op = Op.make(function, op->value, op->text);
}


// MARK: - Static Members

static struct OpList * new (Instance self);
static struct OpList * assignment (Instance self, int noIn);
static struct OpList * expression (Instance self, int noIn);
static struct OpList * statement (Instance self);
static struct OpList * function (Instance self, int declaration);
static struct OpList * sourceElements (Instance self, enum Lexer(Token) endToken);


// MARK: Token

static inline enum Lexer(Token) previewToken (Instance self)
{
	return self->previewToken;
}

static void error (Instance self, struct Error *error)
{
	if (!self->error)
	{
		self->error = error;
		self->previewToken = Lexer(errorToken);
	}
	else
		Error.destroy(error);
}

static inline enum Lexer(Token) nextToken (Instance self)
{
	if (self->previewToken != Lexer(errorToken))
	{
		self->previewToken = Lexer.nextToken(self->lexer);
		
		if (self->previewToken == Lexer(errorToken))
			self->error = self->lexer->value.data.error;
	}
	return self->previewToken;
}

static inline int acceptToken (Instance self, enum Lexer(Token) token)
{
	if (previewToken(self) != token)
		return 0;
	
	nextToken(self);
	return 1;
}

static inline int expectToken (Instance self, enum Lexer(Token) token)
{
	if (previewToken(self) != token)
	{
		if (token && token < Lexer(errorToken))
			error(self, Error.syntaxError(self->lexer->text, "expected '%c', got %s", token, Lexer.tokenChars(previewToken(self))));
		else
			error(self, Error.syntaxError(self->lexer->text, "expected %s, got %s", Lexer.tokenChars(token), Lexer.tokenChars(previewToken(self))));
		
		return 0;
	}
	
	nextToken(self);
	return 1;
}


// MARK: Depth

static void pushDepth (Instance self, struct Identifier identifier, char depth)
{
	self->depths = realloc(self->depths, (self->depthCount + 1) * sizeof(*self->depths));
	self->depths[self->depthCount].identifier = identifier;
	self->depths[self->depthCount].depth = depth;
	++self->depthCount;
}

static void popDepth (Instance self)
{
	--self->depthCount;
}


// MARK: Expression

static struct OpList * expressionRef (Instance self, struct OpList *oplist, const char *name)
{
	if (oplist->ops[0].function == Op.getLocal && oplist->opCount == 1)
		changeFunction(oplist->ops, Op.getLocalRef);
	else if (oplist->ops[oplist->opCount - 1].function == Op.getMember)
		changeFunction(oplist->ops, Op.getMemberRef);
	else if (oplist->ops[0].function == Op.getProperty)
		changeFunction(oplist->ops, Op.getPropertyRef);
	else
		error(self, Error.referenceError(OpList.text(oplist), "%s", name));
	
	return oplist;
}

static void semicolon (Instance self)
{
	if (previewToken(self) == ';')
	{
		nextToken(self);
		return;
	}
	else if (self->lexer->didLineBreak || previewToken(self) == '}' || previewToken(self) == Lexer(noToken))
		return;
	
	error(self, Error.syntaxError(self->lexer->text, "missing ; before statement"));
}

static struct Op identifier (Instance self)
{
	struct Value value = self->lexer->value;
	struct Text text = self->lexer->text;
	if (!expectToken(self, Lexer(identifierToken)))
		return (struct Op){ 0 };
	
	return Op.make(Op.value, value, text);
}

static struct OpList * array (Instance self)
{
	struct OpList *oplist = NULL;
	uint32_t count = 0;
	do
	{
		while (previewToken(self) == ',')
		{
			++count;
			oplist = OpList.append(oplist, Op.make(Op.value, Value.undefined(), self->lexer->text));
			nextToken(self);
		}
		
		if (previewToken(self) == ']')
			break;
		
		++count;
		oplist = OpList.join(oplist, assignment(self, 0));
	}
	while (acceptToken(self, ','));
	return OpList.unshift(Op.make(Op.array, Value.integer(count), OpList.text(oplist)), oplist);
}

static struct OpList * propertyAssignment (Instance self)
{
	struct OpList *oplist = NULL;
	
	if (previewToken(self) == Lexer(integerToken))
		oplist = OpList.create(Op.value, self->lexer->value, self->lexer->text);
	else if (previewToken(self) == Lexer(binaryToken))
	{
		Input.printTextInput(self->lexer->input, self->lexer->text);
		Env.printWarning("Using floating-point as property name polute identifier's pool");
		oplist = OpList.create(Op.value, Value.identifier(Identifier.makeWithText(self->lexer->text, 0)), self->lexer->text);
	}
	else if (previewToken(self) == Lexer(stringToken))
		oplist = OpList.create(Op.value, Value.identifier(Identifier.makeWithText(self->lexer->text, 0)), self->lexer->text);
	else if (previewToken(self) == Lexer(identifierToken))
		oplist = OpList.create(Op.value, self->lexer->value, self->lexer->text);
	else
		expectToken(self, Lexer(identifierToken));
	
	nextToken(self);
	expectToken(self, ':');
	
	return OpList.join(oplist, assignment(self, 0));
}

static struct OpList * object (Instance self)
{
	struct OpList *oplist = NULL;
	uint32_t count = 0;
	do
	{
		if (previewToken(self) == '}')
			break;
		
		++count;
		oplist = OpList.join(oplist, propertyAssignment(self));
	}
	while (acceptToken(self, ','));
	return OpList.unshift(Op.make(Op.object, Value.integer(count), OpList.text(oplist)), oplist);
}

static struct OpList * primary (Instance self)
{
	struct OpList *oplist = NULL;
	
	if (previewToken(self) == Lexer(identifierToken))
	{
		oplist = OpList.create(Op.getLocal, self->lexer->value, self->lexer->text);
		
		if (Identifier.isEqual(self->lexer->value.data.identifier, Identifier.arguments()))
		{
			self->closure->needArguments = 1;
			self->closure->needHeap = 1;
		}
	}
	else if (previewToken(self) == Lexer(stringToken))
		oplist = OpList.create(Op.text, Value.undefined(), self->lexer->text);
	else if (previewToken(self) == Lexer(regexToken)
		|| previewToken(self) == Lexer(integerToken)
		|| previewToken(self) == Lexer(binaryToken)
		)
		oplist = OpList.create(Op.value, self->lexer->value, self->lexer->text);
	else if (previewToken(self) == Lexer(thisToken))
		oplist = OpList.create(Op.this, Value.undefined(), self->lexer->text);
	else if (previewToken(self) == Lexer(nullToken))
		oplist = OpList.create(Op.value, Value.null(), self->lexer->text);
	else if (previewToken(self) == Lexer(trueToken))
		oplist = OpList.create(Op.value, Value.boolean(1), self->lexer->text);
	else if (previewToken(self) == Lexer(falseToken))
		oplist = OpList.create(Op.value, Value.boolean(0), self->lexer->text);
	else if (acceptToken(self, '{'))
	{
		oplist = object(self);
		expectToken(self, '}');
		return oplist;
	}
	else if (acceptToken(self, '['))
	{
		oplist = array(self);
		expectToken(self, ']');
		return oplist;
	}
	else if (acceptToken(self, '('))
	{
		oplist = expression(self, 0);
		expectToken(self, ')');
		return oplist;
	}
	else
		return NULL;
	
	nextToken(self);
	return oplist;
}

static struct OpList * arguments (Instance self, int *count)
{
	*count = 0;
	struct OpList *oplist = NULL;
	if (previewToken(self) != ')')
		do
		{
			++*count;
			oplist = OpList.join(oplist, assignment(self, 0));
		} while (acceptToken(self, ','));
	
	return oplist;
}

static struct OpList * member (Instance self)
{
	struct OpList *oplist = new(self);
	while (1)
	{
		if (acceptToken(self, '.'))
		{
			struct Value value = self->lexer->value;
			struct Text text = self->lexer->text;
			if (!expectToken(self, Lexer(identifierToken)))
				return oplist;
			
			oplist = OpList.unshift(Op.make(Op.getMember, value, Text.join(OpList.text(oplist), text)), oplist);
		}
		else if (acceptToken(self, '['))
		{
			oplist = OpList.join(oplist, expression(self, 0));
			if (!expectToken(self, ']'))
				return oplist;
			
			oplist = OpList.unshift(Op.make(Op.getProperty, Value.undefined(), OpList.text(oplist)), oplist);
		}
		else
			break;
	}
	return oplist;
}

static struct OpList * new (Instance self)
{
	struct OpList *oplist = NULL;
	
	if (acceptToken(self, Lexer(newToken)))
	{
		int count = 0;
		oplist = member(self);
		if (acceptToken(self, '('))
		{
			oplist = OpList.join(oplist, arguments(self, &count));
			expectToken(self, ')');
		}
		return OpList.unshift(Op.make(Op.construct, Value.integer(count), OpList.text(oplist)), oplist);
	}
	else if (previewToken(self) == Lexer(functionToken))
		return function(self, 0);
	else
		return primary(self);
}

static struct OpList * leftHandSide (Instance self)
{
	struct Text text;
	struct OpList *oplist = new(self);
	while (1)
	{
		if (acceptToken(self, '.'))
		{
			struct Value value = self->lexer->value;
			text = Text.join(OpList.text(oplist), self->lexer->text);
			if (!expectToken(self, Lexer(identifierToken)))
				return oplist;
			
			oplist = OpList.unshift(Op.make(Op.getMember, value, text), oplist);
		}
		else if (acceptToken(self, '['))
		{
			oplist = OpList.join(oplist, expression(self, 0));
			text = Text.join(OpList.text(oplist), self->lexer->text);
			if (!expectToken(self, ']'))
				return oplist;
			
			oplist = OpList.unshift(Op.make(Op.getProperty, Value.undefined(), text), oplist);
		}
		else if (acceptToken(self, '('))
		{
			int count = 0;
			oplist = OpList.join(oplist, arguments(self, &count));
			oplist = OpList.unshift(Op.make(Op.call, Value.integer(count), OpList.text(oplist)), oplist);
			if (!expectToken(self, ')'))
				return oplist;
		}
		else
			break;
	}
	return oplist;
}

static struct OpList * postfix (Instance self)
{
	struct OpList *oplist = leftHandSide(self);
	
	if (!self->lexer->didLineBreak && acceptToken(self, Lexer(incrementToken)))
		oplist = OpList.unshift(Op.make(Op.postIncrementRef, Value.undefined(), self->lexer->text), expressionRef(self, oplist, "invalid increment operand"));
	if (!self->lexer->didLineBreak && acceptToken(self, Lexer(decrementToken)))
		oplist = OpList.unshift(Op.make(Op.postDecrementRef, Value.undefined(), self->lexer->text), expressionRef(self, oplist, "invalid decrement operand"));
	
	return oplist;
}

static struct OpList * unary (Instance self)
{
	if (acceptToken(self, Lexer(deleteToken)))
	{
		struct OpList *oplist = unary(self);
		
		if (oplist->ops[0].function == Op.getMember)
			changeFunction(oplist->ops, Op.deleteMember);
		else if (oplist->ops[0].function == Op.getProperty)
			changeFunction(oplist->ops, Op.deleteProperty);
		else
			error(self, Error.referenceError(OpList.text(oplist), "invalid delete operand"));
		
		return oplist;
	}
	else if (acceptToken(self, Lexer(voidToken)))
		return OpList.unshift(Op.make(Op.exchange, Value.undefined(), self->lexer->text), unary(self));
	else if (acceptToken(self, Lexer(typeofToken)))
		return OpList.unshift(Op.make(Op.typeOf, Value.undefined(), self->lexer->text), unary(self));
	else if (acceptToken(self, Lexer(incrementToken)))
		return OpList.unshift(Op.make(Op.incrementRef, Value.undefined(), self->lexer->text), expressionRef(self, unary(self), "invalid increment operand"));
	else if (acceptToken(self, Lexer(decrementToken)))
		return OpList.unshift(Op.make(Op.decrementRef, Value.undefined(), self->lexer->text), expressionRef(self, unary(self), "invalid decrement operand"));
	else if (acceptToken(self, '+'))
		return OpList.unshift(Op.make(Op.positive, Value.undefined(), self->lexer->text), unary(self));
	else if (acceptToken(self, '-'))
		return OpList.unshift(Op.make(Op.negative, Value.undefined(), self->lexer->text), unary(self));
	else if (acceptToken(self, '~'))
		return OpList.unshift(Op.make(Op.invert, Value.undefined(), self->lexer->text), unary(self));
	else if (acceptToken(self, '!'))
		return OpList.unshift(Op.make(Op.not, Value.undefined(), self->lexer->text), unary(self));
	else
		return postfix(self);
}

static struct OpList * multiplicative (Instance self)
{
	struct OpList *oplist = unary(self);
	while (1)
	{
		struct Text text = self->lexer->text;
		
		if (acceptToken(self, '*'))
			oplist = OpList.unshift(Op.make(Op.multiply, Value.undefined(), text), OpList.join(oplist, unary(self)));
		else if (acceptToken(self, '/'))
			oplist = OpList.unshift(Op.make(Op.divide, Value.undefined(), text), OpList.join(oplist, unary(self)));
		else if (acceptToken(self, '%'))
			oplist = OpList.unshift(Op.make(Op.modulo, Value.undefined(), text), OpList.join(oplist, unary(self)));
		else
			return oplist;
	}
}

static struct OpList * additive (Instance self)
{
	struct OpList *oplist = multiplicative(self);
	while (1)
	{
		struct Text text = self->lexer->text;
		
		if (acceptToken(self, '+'))
			oplist = OpList.unshift(Op.make(Op.add, Value.undefined(), text), OpList.join(oplist, multiplicative(self)));
		else if (acceptToken(self, '-'))
			oplist = OpList.unshift(Op.make(Op.minus, Value.undefined(), text), OpList.join(oplist, multiplicative(self)));
		else
			return oplist;
	}
}

static struct OpList * shift (Instance self)
{
	struct OpList *oplist = additive(self);
	while (1)
	{
		struct Text text = self->lexer->text;
		
		if (acceptToken(self, Lexer(leftShiftToken)))
			oplist = OpList.unshift(Op.make(Op.leftShift, Value.undefined(), text), additive(self));
		else if (acceptToken(self, Lexer(rightShiftToken)))
			oplist = OpList.unshift(Op.make(Op.rightShift, Value.undefined(), text), additive(self));
		else if (acceptToken(self, Lexer(unsignedRightShiftToken)))
			oplist = OpList.unshift(Op.make(Op.unsignedRightShift, Value.undefined(), text), additive(self));
		else
			return oplist;
	}
}

static struct OpList * relational (Instance self, int noIn)
{
	struct OpList *oplist = shift(self);
	while (1)
	{
		struct Op op;
		struct Text text = self->lexer->text;
		
		if (acceptToken(self, '<'))
			op = Op.make(Op.less, Value.undefined(), text);
		else if (acceptToken(self, '>'))
			op = Op.make(Op.more, Value.undefined(), text);
		else if (acceptToken(self, Lexer(lessOrEqualToken)))
			op = Op.make(Op.lessOrEqual, Value.undefined(), text);
		else if (acceptToken(self, Lexer(moreOrEqualToken)))
			op = Op.make(Op.moreOrEqual, Value.undefined(), text);
		else if (acceptToken(self, Lexer(instanceofToken)))
			op = Op.make(Op.instanceOf, Value.undefined(), text);
		else if (!noIn && acceptToken(self, Lexer(inToken)))
			op = Op.make(Op.in, Value.undefined(), text);
		else
			return oplist;
		
		oplist = OpList.unshift(op, OpList.join(oplist, shift(self)));
	}
}

static struct OpList * equality (Instance self, int noIn)
{
	struct OpList *oplist = relational(self, noIn);
	while(1)
	{
		struct Op op;
		struct Text text = self->lexer->text;
		
		if (acceptToken(self, Lexer(equalToken)))
			op = Op.make(Op.equal, Value.undefined(), text);
		else if (acceptToken(self, Lexer(notEqualToken)))
			op = Op.make(Op.notEqual, Value.undefined(), text);
		else if (acceptToken(self, Lexer(identicalToken)))
			op = Op.make(Op.identical, Value.undefined(), text);
		else if (acceptToken(self, Lexer(notIdenticalToken)))
			op = Op.make(Op.notIdentical, Value.undefined(), text);
		else
			return oplist;
		
		oplist = OpList.unshift(op, OpList.join(oplist, relational(self, noIn)));
	}
}

static struct OpList * bitwiseAnd (Instance self, int noIn)
{
	struct OpList *oplist = equality(self, noIn);
	while (acceptToken(self, '&'))
		oplist = OpList.unshift(Op.make(Op.bitwiseAnd, Value.undefined(), self->lexer->text), OpList.join(oplist, equality(self, noIn)));
	
	return oplist;
}

static struct OpList * bitwiseXor (Instance self, int noIn)
{
	struct OpList *oplist = bitwiseAnd(self, noIn);
	while (acceptToken(self, '^'))
		oplist = OpList.unshift(Op.make(Op.bitwiseXor, Value.undefined(), self->lexer->text), OpList.join(oplist, bitwiseAnd(self, noIn)));
	
	return oplist;
}

static struct OpList * bitwiseOr (Instance self, int noIn)
{
	struct OpList *oplist = bitwiseXor(self, noIn);
	while (acceptToken(self, '|'))
		oplist = OpList.unshift(Op.make(Op.bitwiseOr, Value.undefined(), self->lexer->text), OpList.join(oplist, bitwiseXor(self, noIn)));
	
	return oplist;
}

static struct OpList * logicalAnd (Instance self, int noIn)
{
	struct OpList *oplist = bitwiseOr(self, noIn), *nextOp = NULL;
	while (acceptToken(self, Lexer(logicalAndToken)))
	{
		nextOp = bitwiseOr(self, noIn);
		oplist = OpList.unshift(Op.make(Op.logicalAnd, Value.integer(nextOp->opCount), self->lexer->text), OpList.join(oplist, nextOp));
	}
	
	return oplist;
}

static struct OpList * logicalOr (Instance self, int noIn)
{
	struct OpList *oplist = logicalAnd(self, noIn), *nextOp = NULL;
	while (acceptToken(self, Lexer(logicalOrToken)))
	{
		nextOp = logicalAnd(self, noIn);
		oplist = OpList.unshift(Op.make(Op.logicalOr, Value.integer(nextOp->opCount), self->lexer->text), OpList.join(oplist, nextOp));
	}
	
	return oplist;
}

static struct OpList * conditional (Instance self, int noIn)
{
	struct OpList *oplist = logicalOr(self, noIn);
	if (acceptToken(self, '?'))
	{
		struct OpList *trueOps = assignment(self, 0);
		if (!expectToken(self, ':'))
			return oplist;
		
		struct OpList *falseOps = assignment(self, noIn);
		
		trueOps = OpList.append(trueOps, Op.make(Op.jump, Value.integer(falseOps->opCount), OpList.text(trueOps)));
		oplist = OpList.unshift(Op.make(Op.jumpIfNot, Value.integer(trueOps->opCount), OpList.text(oplist)), oplist);
		oplist = OpList.join(oplist, trueOps);
		oplist = OpList.join(oplist, falseOps);
		
		return oplist;
	}
	return oplist;
}

static struct OpList * assignment (Instance self, int noIn)
{
	struct OpList *oplist = conditional(self, noIn);
	struct Text text = self->lexer->text;
	Function function = NULL;
	
	if (acceptToken(self, '='))
	{
		if (oplist->ops[0].function == Op.getLocal && oplist->opCount == 1)
			changeFunction(oplist->ops, Op.setLocal);
		else if (oplist->ops[0].function == Op.getMember)
			changeFunction(oplist->ops, Op.setMember);
		else if (oplist->ops[0].function == Op.getProperty)
			changeFunction(oplist->ops, Op.setProperty);
		else
			error(self, Error.referenceError(OpList.text(oplist), "invalid assignment left-hand side"));
		
		return OpList.join(oplist, assignment(self, noIn));
	}
	else if (acceptToken(self, Lexer(multiplyAssignToken)))
		function = Op.multiplyAssignRef;
	else if (acceptToken(self, Lexer(divideAssignToken)))
		function = Op.divideAssignRef;
	else if (acceptToken(self, Lexer(moduloAssignToken)))
		function = Op.moduloAssignRef;
	else if (acceptToken(self, Lexer(addAssignToken)))
		function = Op.addAssignRef;
	else if (acceptToken(self, Lexer(minusAssignToken)))
		function = Op.minusAssignRef;
	else if (acceptToken(self, Lexer(leftShiftAssignToken)))
		function = Op.leftShiftAssignRef;
	else if (acceptToken(self, Lexer(rightShiftAssignToken)))
		function = Op.rightShiftAssignRef;
	else if (acceptToken(self, Lexer(unsignedRightShiftAssignToken)))
		function = Op.unsignedRightShiftAssignRef;
	else if (acceptToken(self, Lexer(andAssignToken)))
		function = Op.bitAndAssignRef;
	else if (acceptToken(self, Lexer(xorAssignToken)))
		function = Op.bitXorAssignRef;
	else if (acceptToken(self, Lexer(orAssignToken)))
		function = Op.bitOrAssignRef;
	else
		return oplist;
	
	return OpList.join(OpList.unshift(Op.make(function, Value.undefined(), text), expressionRef(self, oplist, "invalid assignment left-hand side")), assignment(self, noIn));
}

static struct OpList * expression (Instance self, int noIn)
{
	struct OpList *oplist = assignment(self, noIn);
	while (acceptToken(self, ','))
		oplist = OpList.join(oplist, OpList.unshift(Op.make(Op.discard, Value.undefined(), self->lexer->text), assignment(self, noIn)));
	
	return oplist;
}


// MARK: Statements

static struct OpList * statementList (Instance self)
{
	struct OpList *oplist = NULL, *statementOps = NULL;
	
	while (( statementOps = statement(self) ))
		oplist = OpList.join(oplist, statementOps);
	
	return oplist;
}

static struct OpList * block (Instance self)
{
	struct OpList *oplist = NULL;
	expectToken(self, '{');
	if (previewToken(self) == '}')
		oplist = OpList.create(Op.next, Value.undefined(), self->lexer->text);
	else
		oplist = statementList(self);
	
	expectToken(self, '}');
	return oplist;
}

static struct OpList * variableDeclaration (Instance self, int noIn)
{
	struct Value value = self->lexer->value;
	struct Text text = self->lexer->text;
	if (!expectToken(self, Lexer(identifierToken)))
		return NULL;
	
	Object.add(&self->closure->context, value.data.identifier, Value.undefined(), Object(writable) | Object(configurable));
	
	if (acceptToken(self, '='))
		return OpList.join(OpList.create(Op.setLocal, value, text), assignment(self, noIn));
	else
		return OpList.append(OpList.create(Op.setLocal, value, text), Op.make(Op.value, Value.undefined(), text));
	
	return NULL;
}

static struct OpList * variableDeclarationList (Instance self, int noIn)
{
	struct OpList *oplist = NULL;
	do
		oplist = OpList.join(oplist, OpList.unshift(Op.make(Op.discard, Value.undefined(), self->lexer->text), variableDeclaration(self, noIn)));
	while (acceptToken(self, ','));
	return oplist;
}

static struct OpList * ifStatement (Instance self)
{
	struct OpList *oplist = NULL, *trueOps = NULL, *falseOps = NULL;
	expectToken(self, '(');
	oplist = expression(self, 0);
	expectToken(self, ')');
	trueOps = statement(self);
	if (acceptToken(self, Lexer(elseToken)))
	{
		falseOps = statement(self);
		trueOps = OpList.append(trueOps, Op.make(Op.jump, Value.integer(falseOps->opCount), OpList.text(trueOps)));
	}
	oplist = OpList.unshift(Op.make(Op.jumpIfNot, Value.integer(trueOps->opCount), OpList.text(oplist)), oplist);
	oplist = OpList.join(oplist, trueOps);
	oplist = OpList.join(oplist, falseOps);
	return oplist;
}

static struct OpList * doStatement (Instance self)
{
	pushDepth(self, Identifier.none(), 2);
	struct OpList *oplist = statement(self);
	popDepth(self);
	
	expectToken(self, Lexer(whileToken));
	expectToken(self, '(');
	struct OpList *condition = expression(self, 0);
	expectToken(self, ')');
	semicolon(self);
	return OpList.createLoop(NULL, condition, NULL, oplist, 1);
}

static struct OpList * whileStatement (Instance self)
{
	expectToken(self, '(');
	struct OpList *condition = expression(self, 0);
	expectToken(self, ')');
	
	pushDepth(self, Identifier.none(), 2);
	struct OpList *oplist = statement(self);
	popDepth(self);
	
	return OpList.createLoop(NULL, condition, NULL, oplist, 0);
}

static struct OpList * forStatement (Instance self)
{
	int noIn = 0;
	struct OpList *oplist = NULL, *condition = NULL, *increment = NULL, *body = NULL;
	
	expectToken(self, '(');
	
	if (acceptToken(self, Lexer(varToken)))
		oplist = variableDeclarationList(self, 1);
	else if (previewToken(self) != ';')
	{
		oplist = leftHandSide(self);
		if (!oplist)
		{
			noIn = 1;
			oplist = expression(self, 1);
		}
		
		if (oplist)
			oplist = OpList.unshift(Op.make(Op.discard, Value.undefined(), OpList.text(oplist)), oplist);
	}
	
	if (!noIn && acceptToken(self, Lexer(inToken)))
	{
		if (oplist->ops[0].function == Op.setLocal && oplist->opCount == 2)
			oplist = OpList.join(OpList.create(Op.iterateInRef, Value.undefined(), self->lexer->text), OpList.create(Op.getLocalRef, oplist->ops[0].value, oplist->ops[0].text));
		else if (oplist->ops[0].function == Op.getLocal && oplist->opCount == 1)
			oplist = OpList.unshift(Op.make(Op.iterateInRef, Value.undefined(), self->lexer->text), expressionRef(self, oplist, "invalid for/in left-hand side"));
		else
			error(self, Error.referenceError(OpList.text(oplist), "invalid for/in left-hand side"));
		
		oplist = OpList.join(oplist, expression(self, 0));
		expectToken(self, ')');
		
		pushDepth(self, Identifier.none(), 2);
		body = statement(self);
		popDepth(self);
		
		return OpList.join(OpList.append(oplist, Op.make(Op.value, Value.integer(body->opCount), self->lexer->text)), body);
	}
	else
	{
		expectToken(self, ';');
		if (previewToken(self) != ';')
			condition = expression(self, 0);
		
		expectToken(self, ';');
		if (previewToken(self) != ')')
			increment = expression(self, 0);
		
		expectToken(self, ')');
		
		pushDepth(self, Identifier.none(), 2);
		body = statement(self);
		popDepth(self);
		
		return OpList.createLoop(oplist, condition, increment, body, 0);
	}
}

static struct OpList * continueStatement (Instance self, struct Text text)
{
	struct OpList *oplist = NULL;
	struct Identifier label = Identifier.none();
	struct Text labelText = self->lexer->text;
	if (!self->lexer->didLineBreak && previewToken(self) == Lexer(identifierToken))
	{
		label = self->lexer->value.data.identifier;
		nextToken(self);
	}
	semicolon(self);
	
	uint16_t depth = self->depthCount, breaker = 0;
	if (!depth)
		error(self, Error.syntaxError(text, "continue must be inside loop"));
	
	int lastestDepth = 0;
	while (depth--)
	{
		breaker += self->depths[depth].depth;
		if (self->depths[depth].depth)
			lastestDepth = self->depths[depth].depth;
		
		if (lastestDepth == 2)
			if (Identifier.isEqual(label, Identifier.none()) || Identifier.isEqual(label, self->depths[depth].identifier))
				return OpList.create(Op.value, Value.breaker(breaker - 1), self->lexer->text);
	}
	error(self, Error.syntaxError(labelText, "label not found"));
	return oplist;
}

static struct OpList * breakStatement (Instance self, struct Text text)
{
	struct OpList *oplist = NULL;
	struct Identifier label = Identifier.none();
	struct Text labelText = self->lexer->text;
	if (!self->lexer->didLineBreak && previewToken(self) == Lexer(identifierToken))
	{
		label = self->lexer->value.data.identifier;
		nextToken(self);
	}
	semicolon(self);
	
	uint16_t depth = self->depthCount, breaker = 0;
	if (!depth)
		error(self, Error.syntaxError(text, "break must be inside loop or switch"));
	
	while (depth--)
	{
		breaker += self->depths[depth].depth;
		if (Identifier.isEqual(label, Identifier.none()) || Identifier.isEqual(label, self->depths[depth].identifier))
			return OpList.create(Op.value, Value.breaker(breaker), self->lexer->text);
	}
	error(self, Error.syntaxError(labelText, "label not found"));
	return oplist;
}

static struct OpList * returnStatement (Instance self, struct Text text)
{
	struct OpList *oplist = NULL;
	if (!self->lexer->didLineBreak && previewToken(self) != ';' && previewToken(self) != '}' && previewToken(self) != Lexer(noToken))
		oplist = expression(self, 0);
	else
		oplist = OpList.create(Op.value, Value.undefined(), self->lexer->text);
	
	semicolon(self);
	oplist = OpList.unshift(Op.make(Op.result, Value.undefined(), text), oplist);
	return oplist;
}

static struct OpList * switchStatement (Instance self)
{
	struct OpList *oplist = NULL, *conditionOps = NULL, *defaultOps = NULL;
	struct Text text;
	uint32_t conditionCount = 0;
	
	expectToken(self, '(');
	conditionOps = expression(self, 0);
	expectToken(self, ')');
	expectToken(self, '{');
	pushDepth(self, Identifier.none(), 1);
	
	while (previewToken(self) != '}' && previewToken(self) != Lexer(errorToken) && previewToken(self) != Lexer(noToken))
	{
		text = self->lexer->text;
		
		if (acceptToken(self, Lexer(caseToken)))
		{
			conditionOps = OpList.join(conditionOps, expression(self, 0));
			conditionOps = OpList.append(conditionOps, Op.make(Op.value, Value.integer(oplist? oplist->opCount: 0), text));
			++conditionCount;
			expectToken(self, ':');
			oplist = OpList.join(oplist, statementList(self));
		}
		else if (acceptToken(self, Lexer(defaultToken)))
		{
			if (!defaultOps)
			{
				defaultOps = OpList.create(Op.jump, Value.integer(oplist? oplist->opCount: 0), text);
				expectToken(self, ':');
				oplist = OpList.join(oplist, statementList(self));
			}
			else
				error(self, Error.syntaxError(text, "more than one switch default"));
		}
		else
			error(self, Error.syntaxError(text, "invalid switch statement"));
	}
	
	if (!defaultOps)
		defaultOps = OpList.create(Op.jump, Value.integer(oplist? oplist->opCount: 0), text);
	
	conditionOps = OpList.unshift(Op.make(Op.switchOp, Value.integer(conditionOps? conditionOps->opCount: 0), OpList.text(conditionOps)), conditionOps);
	conditionOps = OpList.join(conditionOps, defaultOps);
	oplist = OpList.join(conditionOps, oplist);
	
	popDepth(self);
	expectToken(self, '}');
	return oplist;
}

static struct OpList * statement (Instance self)
{
	struct OpList *oplist = NULL;
	struct Text text = self->lexer->text;
	
	if (previewToken(self) == '{')
		return block(self);
	else if (acceptToken(self, Lexer(varToken)))
	{
		oplist = variableDeclarationList(self, 0);
		semicolon(self);
		return oplist;
	}
	else if (acceptToken(self, ';'))
		return OpList.create(Op.next, Value.undefined(), text);
	else if (acceptToken(self, Lexer(ifToken)))
		return ifStatement(self);
	else if (acceptToken(self, Lexer(doToken)))
		return doStatement(self);
	else if (acceptToken(self, Lexer(whileToken)))
		return whileStatement(self);
	else if (acceptToken(self, Lexer(forToken)))
		return forStatement(self);
	else if (acceptToken(self, Lexer(continueToken)))
		return continueStatement(self, text);
	else if (acceptToken(self, Lexer(breakToken)))
		return breakStatement(self, text);
	else if (acceptToken(self, Lexer(returnToken)))
		return returnStatement(self, text);
	else if (acceptToken(self, Lexer(withToken)))
	{
		error(self, Error.syntaxError(text, "strict mode code may not contain 'with' statements"));
		return oplist;
	}
	else if (acceptToken(self, Lexer(switchToken)))
		return switchStatement(self);
	else if (acceptToken(self, Lexer(throwToken)))
	{
		if (!self->lexer->didLineBreak)
			oplist = expression(self, 0);
		
		if (!oplist)
			error(self, Error.syntaxError(text, "throw statement is missing an expression"));
		
		semicolon(self);
		return OpList.unshift(Op.make(Op.throw, Value.undefined(), Text.join(text, OpList.text(oplist))), oplist);
	}
	else if (acceptToken(self, Lexer(tryToken)))
	{
		error(self, Error.syntaxError(text, "TODO: try/catch/finally"));
		return NULL;
	}
	else if (acceptToken(self, Lexer(debuggerToken)))
	{
		semicolon(self);
		return OpList.create(Op.debug, Value.undefined(), text);
	}
	else
	{
		oplist = expression(self, 0);
		if (!oplist)
			return NULL;
//			error(self, Error.syntaxError(self->lexer->text, "expected statement, got %s", Lexer.tokenChars(previewToken(self))));
		
		if (oplist->ops[0].function == Op.getLocal && oplist->opCount == 1 && acceptToken(self, ':'))
		{
			if (previewToken(self) == Lexer(doToken)
				|| previewToken(self) == Lexer(whileToken)
				|| previewToken(self) == Lexer(forToken)
				|| previewToken(self) == Lexer(switchToken)
				)
			{
				pushDepth(self, oplist->ops[0].value.data.identifier, 0);
				free(oplist), oplist = NULL;
				oplist = statement(self);
				popDepth(self);
				return oplist;
			}
			else
				return statement(self);
		}
		
		semicolon(self);
		return OpList.unshift(Op.make(Op.discard, Value.undefined(), OpList.text(oplist)), oplist);
	}
}


// MARK: Function

static struct OpList * parameters (Instance self, int *count)
{
	*count = 0;
	struct Op op;
	if (previewToken(self) != ')')
		do
		{
			++*count;
			op = identifier(self);
			Object.add(&self->closure->context, op.value.data.identifier, Value.undefined(), Object(writable) | Object(configurable));
		} while (acceptToken(self, ','));
	
	return NULL;
}

static struct OpList * function (Instance self, int isDeclaration)
{
	struct Text text = self->lexer->text;
	expectToken(self, Lexer(functionToken));
	
	struct OpList *oplist = NULL;
	int count = 0;
	
	struct Op identifierOp = { 0 };
	
	if (previewToken(self) == Lexer(identifierToken))
		identifierOp = identifier(self);
	else if (isDeclaration)
		error(self, Error.syntaxError(self->lexer->text, "function statement requires a name"));
	
	struct Closure *parentClosure = self->closure;
	struct Closure *closure = Closure.create(NULL);
	Object.add(&closure->context, Identifier.arguments(), Value.undefined(), Object(writable) | Object(configurable));
	
	self->closure = closure;
	expectToken(self, '(');
	oplist = OpList.join(oplist, parameters(self, &count));
	expectToken(self, ')');
	expectToken(self, '{');
	oplist = OpList.appendNoop(OpList.join(oplist, sourceElements(self, '}')));
	text.length = self->lexer->text.location - text.location + 1;
	expectToken(self, '}');
	self->closure = parentClosure;
	
	closure->oplist = oplist;
	closure->text = text;
	closure->parameterCount = count;
	parentClosure->needHeap = 1;
	
	if (isDeclaration)
		Object.add(&parentClosure->context, identifierOp.value.data.identifier, Value.undefined(), Object(writable) | Object(configurable));
	else if (identifierOp.value.type != Value(undefined))
		Object.add(&closure->context, identifierOp.value.data.identifier, Value.closure(closure), Object(writable) | Object(configurable));
	
	if (isDeclaration)
		return OpList.append(OpList.create(Op.setLocal, identifierOp.value, text), Op.make(Op.closure, Value.closure(closure), text));
	else
		return OpList.create(Op.closure, Value.closure(closure), self->lexer->text);
}


// MARK: Source

static struct OpList * sourceElements (Instance self, enum Lexer(Token) endToken)
{
	struct OpList *oplist = NULL, *init = NULL;
	
	while (previewToken(self) != endToken && previewToken(self) != Lexer(errorToken) && previewToken(self) != Lexer(noToken))
		if (previewToken(self) == Lexer(functionToken))
			init = OpList.join(init, OpList.unshift(Op.make(Op.discard, Value.undefined(), self->lexer->text), function(self, 1)));
		else
			oplist = OpList.join(oplist, statement(self));
	
	oplist = OpList.join(init, oplist);
	OpList.optimizeWithContext(oplist, &self->closure->context);
	return oplist;
}


// MARK: - Methods

Instance createWithLexer (struct Lexer *lexer)
{
	Instance self = malloc(sizeof(*self));
	assert(self);
	*self = Module.identity;
	
	self->lexer = lexer;
	
	return self;
}

void destroy (Instance self)
{
	assert(self);
	
	Lexer.destroy(self->lexer), self->lexer = NULL;
	free(self->depths), self->depths = NULL;
	free(self), self = NULL;
}

struct Closure * parseWithContext (Instance const self, struct Object *context)
{
	assert(self);
	
	struct Closure *closure = Closure.create(context);
	nextToken(self);
	
	self->closure = closure;
	struct OpList *oplist = OpList.appendNoop(sourceElements(self, Lexer(noToken)));
	self->closure = NULL;
	
	if (self->error)
	{
		OpList.destroy(oplist), oplist = NULL;
		
		struct Op errorOps[] = {
			{ Op.throw, Value.undefined(), self->error->text },
			{ Op.value, Value.error(self->error) },
		};
		oplist = malloc(sizeof(*oplist) + sizeof(errorOps));
		oplist->opCount = sizeof(errorOps) / sizeof(*errorOps);
		memcpy(oplist->ops, errorOps, sizeof(errorOps));
	}
	
//	OpList.dumpTo(oplist, stderr);
	
//	struct Op *oplist = NULL;
//	
//	enum Lexer(Token) token;
//	while (( token = Lexer.nextToken(self->lexer) ))
//	{
////		fprintf(stderr, "<%02x> %.*s  ", token, lexer->range.length, lexer->range.location);
////		if (lexer->value.type != Value(Undefined))
////			Value.dumpTo(&lexer->value, stderr);
////		
////		fprintf(stderr, "\n");
//		
//		if (token == Lexer(errorToken))
//		{
//			self->error = lexer->value.data.error;
//			self->error->range = lexer->range;
//			break;
//		}
//	}
//	
//	if (self->error)
//	{
//		#warning TODO: cleanup oplist here
//		struct Op errorOps[] = {
//			{ Op.throw, },
//			{ Op.value, Value.error(self->error), self->error->range },
//		};
//		oplist = malloc(sizeof(errorOps));
//		memcpy(oplist, errorOps, sizeof(errorOps));
//	}
//	else if (!oplist)
//	{
	
//	struct Op noopOps[] = {
//		{ Op.noop, },
//	};
//	struct Op *oplist = malloc(sizeof(noopOps));
//	memcpy(oplist, noopOps, sizeof(noopOps));
//	return oplist;
	
	closure->oplist = oplist;
	return closure;
}
