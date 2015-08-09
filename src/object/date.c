//
//  date.c
//  libecc
//
//  Created by Bouilland Aurélien on 20/07/2015.
//  Copyright (c) 2015 Libeccio. All rights reserved.
//

#include "date.h"

// MARK: - Private

static struct Text dateType = { 14, "[object Date]" };
static struct Object *datePrototype = NULL;
static struct Object *dateConstructor = NULL;

// MARK: - Static Members

// MARK: - Methods

void setup (void)
{
	datePrototype = Object.create(Object.prototype());
	datePrototype->type = &dateType;
	
//	Closure.addToObject(arrayPrototype, "toString", toString, 0);
//	Closure.addToObject(arrayPrototype, "toLocaleString", toString, 0);
//	Closure.addToObject(arrayPrototype, "valueOf", valueOf, 0);
//	Closure.addToObject(arrayPrototype, "hasOwnProperty", hasOwnProperty, 0);
//	Closure.addToObject(arrayPrototype, "isPrototypeOf", isPrototypeOf, 0);
//	Closure.addToObject(arrayPrototype, "propertyIsEnumerable", propertyIsEnumerable, 0);
}

struct Object *prototype (void)
{
	return datePrototype;
}

struct Object *constructor (void)
{
	return dateConstructor;
}
