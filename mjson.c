/****************************************************************************

NAME
   mjson.c - parse JSON into fixed-extent data structures

DESCRIPTION
   This module parses a large subset of JSON (JavaScript Object
Notation).  Unlike more general JSON parsers, it doesn't use malloc(3)
and doesn't support polymorphism; you need to give it a set of
template structures describing the expected shape of the incoming
JSON, and it will error out if that shape is not matched.  When the
parse succeeds, attribute values will be extracted into static
locations specified in the template structures.

   The "shape" of a JSON object in the type signature of its
attributes (and attribute values, and so on recursively down through
all nestings of objects and arrays).  This parser is indifferent to
the order of attributes at any level, but you have to tell it in
advance what the type of each attribute value will be and where the
parsed value will be stored. The template structures may supply
default values to be used when an expected attribute is omitted.

   The preceding paragraph told one fib.  A single attribute may
actually have a span of multiple specifications with different
syntactically distinguishable types (e.g. string vs. real vs. integer
vs. boolean, but not signed integer vs. unsigned integer).  The parser
will match the right spec against the actual data.

   The dialect this parses has some limitations.  First, it cannot
recognize the JSON "null" value. Second, all elements of an array must
be of the same type. Third, some types (t_time, t_character) may not
be array elements (this restriction could be lifted)

   There are separate entry points for beginning a parse of either
JSON object or a JSON array. JSON "float" quantities are actually
stored as doubles.

   This parser processes object arrays in one of two different ways,
defending on whether the array subtype is declared as object or
structobject.

   Object arrays take one base address per object subfield, and are
mapped into parallel C arrays (one per subfield).  Strings are not
supported in this kind of array, as they don't have a "natural" size
to use as an offset multiplier.

   Structobjects arrays are a way to parse a list of objects to a set
of modifications to a corresponding array of C structs.  The trick is
that the array object initialization has to specify both the C struct
array's base address and the stride length (the size of the C struct).
If you initialize the offset fields with the correct offsetof calls,
everything will work. Strings are supported but all string storage
has to be inline in the struct.

PERMISSIONS
   This file is Copyright (c) 2014 by Eric S. Raymond
   BSD terms apply: see the file COPYING in the distribution root for details.

***************************************************************************/

/* The strptime prototype is not provided unless explicitly requested.
 * See the POSIX spec for more info:
 * http://pubs.opengroup.org/onlinepubs/9699919799/functions/V2_chap02.html#tag_15_02_01_02 */
#define _XOPEN_SOURCE 600

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>

#include "mjson.h"

#ifdef DEBUG_ENABLE
static int debuglevel = 0;
static FILE *debugfp;

void json_enable_debug(int level, FILE * fp)
/* control the level and destination of debug trace messages */
{
    debuglevel = level;
    debugfp = fp;
}

static void json_trace(int errlevel, const char *fmt, ...)
/* assemble command in printf(3) style */
{
    if (errlevel <= debuglevel) {
	char buf[BUFSIZ];
	va_list ap;

	(void)strncpy(buf, "json: ", BUFSIZ-1);
	buf[BUFSIZ-1] = '\0';
	va_start(ap, fmt);
	(void)vsnprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), fmt,
			ap);
	va_end(ap);

	(void)fputs(buf, debugfp);
    }
}

# define json_debug_trace(args) (void) json_trace args
#else
# define json_debug_trace(args) /*@i1@*/do { } while (0)
#endif /* DEBUG_ENABLE */

/*
 * Berkeley implementation of strtod(), inlined to avoid locale problems
 * with the decimal point and stripped down to an atof()-equivalent.
 */

/*@-shiftimplementation +charint@*/
static double safe_atof(const char *string)
/* Takes a decimal ASCII floating-point number, optionally
 * preceded by white space.  Must have form "-I.FE-X",
 * where I is the integer part of the mantissa, F is
 * the fractional part of the mantissa, and X is the
 * exponent.  Either of the signs may be "+", "-", or
 * omitted.  Either I or F may be omitted, or both.
 * The decimal point isn't necessary unless F is
 * present.  The "E" may actually be an "e".  E and X
 * may both be omitted (but not just one).
 */
{
    static int maxExponent = 511;   /* Largest possible base 10 exponent.  Any
				     * exponent larger than this will already
				     * produce underflow or overflow, so there's
				     * no need to worry about additional digits.
				     */
    static double powersOf10[] = {  /* Table giving binary powers of 10.  Entry */
	10.,			/* is 10^2^i.  Used to convert decimal */
	100.,			/* exponents into floating-point numbers. */
	1.0e4,
	1.0e8,
	1.0e16,
	1.0e32,
	1.0e64,
	1.0e128,
	1.0e256
    };

    bool sign, expSign = false;
    double fraction, dblExp, *d;
    register const char *p;
    register int c;
    int exp = 0;		/* Exponent read from "EX" field. */
    int fracExp = 0;		/* Exponent that derives from the fractional
				 * part.  Under normal circumstatnces, it is
				 * the negative of the number of digits in F.
				 * However, if I is very long, the last digits
				 * of I get dropped (otherwise a long I with a
				 * large negative exponent could cause an
				 * unnecessary overflow on I alone).  In this
				 * case, fracExp is incremented one for each
				 * dropped digit. */
    int mantSize;		/* Number of digits in mantissa. */
    int decPt;			/* Number of mantissa digits BEFORE decimal
				 * point. */
    const char *pExp;		/* Temporarily holds location of exponent
				 * in string. */

    /*
     * Strip off leading blanks and check for a sign.
     */

    p = string;
    while (isspace((unsigned char) *p)) {
	p += 1;
    }
    if (*p == '-') {
	sign = true;
	p += 1;
    } else {
	if (*p == '+') {
	    p += 1;
	}
	sign = false;
    }

    /*
     * Count the number of digits in the mantissa (including the decimal
     * point), and also locate the decimal point.
     */

    decPt = -1;
    for (mantSize = 0; ; mantSize += 1)
    {
	c = *p;
	if (!isdigit(c)) {
	    if ((c != '.') || (decPt >= 0)) {
		break;
	    }
	    decPt = mantSize;
	}
	p += 1;
    }

    /*
     * Now suck up the digits in the mantissa.  Use two integers to
     * collect 9 digits each (this is faster than using floating-point).
     * If the mantissa has more than 18 digits, ignore the extras, since
     * they can't affect the value anyway.
     */

    pExp  = p;
    p -= mantSize;
    if (decPt < 0) {
	decPt = mantSize;
    } else {
	mantSize -= 1;			/* One of the digits was the point. */
    }
    if (mantSize > 18) {
	fracExp = decPt - 18;
	mantSize = 18;
    } else {
	fracExp = decPt - mantSize;
    }
    if (mantSize == 0) {
	fraction = 0.0;
	//p = string;
	goto done;
    } else {
	int frac1, frac2;
	frac1 = 0;
	for ( ; mantSize > 9; mantSize -= 1)
	{
	    c = *p;
	    p += 1;
	    if (c == '.') {
		c = *p;
		p += 1;
	    }
	    frac1 = 10*frac1 + (c - '0');
	}
	frac2 = 0;
	for (; mantSize > 0; mantSize -= 1)
	{
	    c = *p;
	    p += 1;
	    if (c == '.') {
		c = *p;
		p += 1;
	    }
	    frac2 = 10*frac2 + (c - '0');
	}
	fraction = (1.0e9 * frac1) + frac2;
    }

    /*
     * Skim off the exponent.
     */

    p = pExp;
    if ((*p == 'E') || (*p == 'e')) {
	p += 1;
	if (*p == '-') {
	    expSign = true;
	    p += 1;
	} else {
	    if (*p == '+') {
		p += 1;
	    }
	    expSign = false;
	}
	while (isdigit((unsigned char) *p)) {
	    exp = exp * 10 + (*p - '0');
	    p += 1;
	}
    }
    if (expSign) {
	exp = fracExp - exp;
    } else {
	exp = fracExp + exp;
    }

    /*
     * Generate a floating-point number that represents the exponent.
     * Do this by processing the exponent one bit at a time to combine
     * many powers of 2 of 10. Then combine the exponent with the
     * fraction.
     */

    if (exp < 0) {
	expSign = true;
	exp = -exp;
    } else {
	expSign = false;
    }
    if (exp > maxExponent) {
	exp = maxExponent;
	errno = ERANGE;
    }
    dblExp = 1.0;
    for (d = powersOf10; exp != 0; exp >>= 1, d += 1) {
	if (exp & 01) {
	    dblExp *= *d;
	}
    }
    if (expSign) {
	fraction /= dblExp;
    } else {
	fraction *= dblExp;
    }

done:
    if (sign) {
	return -fraction;
    }
    return fraction;
}
/*@+shiftimplementation -charint@*/

#define MONTHSPERYEAR	12	/* months per calendar year */

static time_t mkutctime(register struct tm * t)
/* struct tm to seconds since Unix epoch */
{
    register int year;
    register time_t result;
    static const int cumdays[MONTHSPERYEAR] =
	{ 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };

    /*@ +matchanyintegral @*/
    year = 1900 + t->tm_year + t->tm_mon / MONTHSPERYEAR;
    result = (year - 1970) * 365 + cumdays[t->tm_mon % MONTHSPERYEAR];
    result += (year - 1968) / 4;
    result -= (year - 1900) / 100;
    result += (year - 1600) / 400;
    if ((year % 4) == 0 && ((year % 100) != 0 || (year % 400) == 0) &&
	(t->tm_mon % MONTHSPERYEAR) < 2)
	result--;
    result += t->tm_mday - 1;
    result *= 24;
    result += t->tm_hour;
    result *= 60;
    result += t->tm_min;
    result *= 60;
    result += t->tm_sec;
    /*@ -matchanyintegral @*/
    return (result);
}

static double iso8601_to_unix(/*@in@*/const char *isotime)
/* ISO8601 UTC to Unix UTC */
{
    char *dp = NULL;
    double usec;
    struct tm tm;

    /*@i1@*/ dp = strptime(isotime, "%Y-%m-%dT%H:%M:%S", &tm);
    if (dp != NULL && *dp == '.')
	usec = strtod(dp, NULL);
    else
	usec = 0;
    return mkutctime(&tm) + usec;
}

/*@-immediatetrans -dependenttrans -usereleased -compdef@*/
static /*@null@*/ char *json_target_address(const struct json_attr_t *cursor,
					     /*@null@*/
					     const struct json_array_t
					     *parent, int offset)
{
    char *targetaddr = NULL;
    if (parent == NULL || parent->element_type != t_structobject) {
	/* ordinary case - use the address in the cursor structure */
	switch (cursor->type) {
	case t_ignore:
	    targetaddr = NULL;
	    break;
	case t_integer:
	    targetaddr = (char *)&cursor->addr.integer[offset];
	    break;
	case t_uinteger:
	    targetaddr = (char *)&cursor->addr.uinteger[offset];
	    break;
	case t_time:
	case t_real:
	    targetaddr = (char *)&cursor->addr.real[offset];
	    break;
	case t_string:
	    targetaddr = cursor->addr.string;
	    break;
	case t_boolean:
	    targetaddr = (char *)&cursor->addr.boolean[offset];
	    break;
	case t_character:
	    targetaddr = (char *)&cursor->addr.character[offset];
	    break;
	default:
	    targetaddr = NULL;
	    break;
	}
    } else
	/* tricky case - hacking a member in an array of structures */
	targetaddr =
	    parent->arr.objects.base + (offset * parent->arr.objects.stride) +
	    cursor->addr.offset;
    json_debug_trace((1, "Target address for %s (offset %d) is %p\n",
		      cursor->attribute, offset, targetaddr));
    return targetaddr;
}

/*@-immediatetrans -dependenttrans +usereleased +compdef@*/

static int json_internal_read_object(const char *cp,
				     const struct json_attr_t *attrs,
				     /*@null@*/
				     const struct json_array_t *parent,
				     int offset,
				     /*@null@*/ const char **end)
{
    /*@ -nullstate -nullderef -mustfreefresh -nullpass -usedef @*/
    enum
    { init, await_attr, in_attr, await_value, in_val_string,
	in_escape, in_val_token, post_val, post_array
    } state = 0;
#ifdef DEBUG_ENABLE
    char *statenames[] = {
	"init", "await_attr", "in_attr", "await_value", "in_val_string",
	"in_escape", "in_val_token", "post_val", "post_array",
    };
#endif /* DEBUG_ENABLE */
    char attrbuf[JSON_ATTR_MAX + 1], *pattr = NULL;
    char valbuf[JSON_VAL_MAX + 1], *pval = NULL;
    bool value_quoted = false;
    char uescape[5];		/* enough space for 4 hex digits and a NUL */
    const struct json_attr_t *cursor;
    int substatus, n, maxlen = 0;
    unsigned int u;
    const struct json_enum_t *mp;
    char *lptr;

#ifdef S_SPLINT_S
    /* prevents gripes about buffers not being completely defined */
    memset(valbuf, '\0', sizeof(valbuf));
    memset(attrbuf, '\0', sizeof(attrbuf));
#endif /* S_SPLINT_S */

    if (end != NULL)
	*end = NULL;		/* give it a well-defined value on parse failure */

    /* stuff fields with defaults in case they're omitted in the JSON input */
    for (cursor = attrs; cursor->attribute != NULL; cursor++)
	if (!cursor->nodefault) {
	    lptr = json_target_address(cursor, parent, offset);
	    if (lptr != NULL)
		switch (cursor->type) {
		case t_integer:
		    memcpy(lptr, &cursor->dflt.integer, sizeof(int));
		    break;
		case t_uinteger:
		    memcpy(lptr, &cursor->dflt.uinteger, sizeof(unsigned int));
		    break;
		case t_time:
		case t_real:
		    memcpy(lptr, &cursor->dflt.real, sizeof(double));
		    break;
		case t_string:
		    if (parent != NULL
			&& parent->element_type != t_structobject
			&& offset > 0)
			return JSON_ERR_NOPARSTR;
		    lptr[0] = '\0';
		    break;
		case t_boolean:
		    memcpy(lptr, &cursor->dflt.boolean, sizeof(bool));
		    break;
		case t_character:
		    lptr[0] = cursor->dflt.character;
		    break;
		case t_object:	/* silences a compiler warning */
		case t_structobject:
		case t_array:
		case t_check:
		case t_ignore:
		    break;
		}
	}

    json_debug_trace((1, "JSON parse of '%s' begins.\n", cp));

    /* parse input JSON */
    for (; *cp != '\0'; cp++) {
	json_debug_trace((2, "State %-14s, looking at '%c' (%p)\n",
			  statenames[state], *cp, cp));
	switch (state) {
	case init:
	    if (isspace((unsigned char) *cp))
		continue;
	    else if (*cp == '{')
		state = await_attr;
	    else {
		json_debug_trace((1,
				  "Non-WS when expecting object start.\n"));
		return JSON_ERR_OBSTART;
	    }
	    break;
	case await_attr:
	    if (isspace((unsigned char) *cp))
		continue;
	    else if (*cp == '"') {
		state = in_attr;
		pattr = attrbuf;
	    } else if (*cp == '}')
		break;
	    else {
		json_debug_trace((1, "Non-WS when expecting attribute.\n"));
		return JSON_ERR_ATTRSTART;
	    }
	    break;
	case in_attr:
	    if (pattr == NULL)
		return JSON_ERR_NULLPTR;
	    if (*cp == '"') {
		*pattr++ = '\0';
		json_debug_trace((1, "Collected attribute name %s\n",
				  attrbuf));
		for (cursor = attrs; cursor->attribute != NULL; cursor++) {
		    json_debug_trace((2, "Checking against %s\n",
				      cursor->attribute));
		    if (strcmp(cursor->attribute, attrbuf) == 0)
			break;
		}
		if (cursor->attribute == NULL) {
		    json_debug_trace((1,
				      "Unknown attribute name '%s' (attributes begin with '%s').\n",
				      attrbuf, attrs->attribute));
		    return JSON_ERR_BADATTR;
		}
		state = await_value;
		if (cursor->type == t_string)
		    maxlen = (int)cursor->len - 1;
		else if (cursor->type == t_check)
		    maxlen = (int)strlen(cursor->dflt.check);
		else if (cursor->type == t_time || cursor->type == t_ignore)
		    maxlen = JSON_VAL_MAX;
		else if (cursor->map != NULL)
		    maxlen = (int)sizeof(valbuf) - 1;
		pval = valbuf;
	    } else if (pattr >= attrbuf + JSON_ATTR_MAX - 1) {
		json_debug_trace((1, "Attribute name too long.\n"));
		return JSON_ERR_ATTRLEN;
	    } else
		*pattr++ = *cp;
	    break;
	case await_value:
	    if (isspace((unsigned char) *cp) || *cp == ':')
		continue;
	    else if (*cp == '[') {
		if (cursor->type != t_array) {
		    json_debug_trace((1,
				      "Saw [ when not expecting array.\n"));
		    return JSON_ERR_NOARRAY;
		}
		substatus = json_read_array(cp, &cursor->addr.array, &cp);
		if (substatus != 0)
		    return substatus;
		state = post_array;
	    } else if (cursor->type == t_array) {
		json_debug_trace((1,
				  "Array element was specified, but no [.\n"));
		return JSON_ERR_NOBRAK;
	    } else if (*cp == '"') {
		value_quoted = true;
		state = in_val_string;
		pval = valbuf;
	    } else {
		value_quoted = false;
		state = in_val_token;
		pval = valbuf;
		*pval++ = *cp;
	    }
	    break;
	case in_val_string:
	    if (pval == NULL)
		return JSON_ERR_NULLPTR;
	    if (*cp == '\\')
		state = in_escape;
	    else if (*cp == '"') {
		*pval++ = '\0';
		json_debug_trace((1, "Collected string value %s\n", valbuf));
		state = post_val;
	    } else if (pval > valbuf + JSON_VAL_MAX - 1
		       || pval > valbuf + maxlen) {
		json_debug_trace((1, "String value too long.\n"));
		return JSON_ERR_STRLONG;	/*  */
	    } else
		*pval++ = *cp;
	    break;
	case in_escape:
	    if (pval == NULL)
		return JSON_ERR_NULLPTR;
	    switch (*cp) {
	    case 'b':
		*pval++ = '\b';
		break;
	    case 'f':
		*pval++ = '\f';
		break;
	    case 'n':
		*pval++ = '\n';
		break;
	    case 'r':
		*pval++ = '\r';
		break;
	    case 't':
		*pval++ = '\t';
		break;
	    case 'u':
		for (n = 0; n < 4 && cp[n] != '\0'; n++)
		    uescape[n] = *cp++;
		--cp;
		(void)sscanf(uescape, "%04x", &u);
		*pval++ = (char)u;	/* will truncate values above 0xff */
		break;
	    default:		/* handles double quote and solidus */
		*pval++ = *cp;
		break;
	    }
	    state = in_val_string;
	    break;
	case in_val_token:
	    if (pval == NULL)
		return JSON_ERR_NULLPTR;
	    if (isspace((unsigned char) *cp) || *cp == ',' || *cp == '}') {
		*pval = '\0';
		json_debug_trace((1, "Collected token value %s.\n", valbuf));
		state = post_val;
		if (*cp == '}' || *cp == ',')
		    --cp;
	    } else if (pval > valbuf + JSON_VAL_MAX - 1) {
		json_debug_trace((1, "Token value too long.\n"));
		return JSON_ERR_TOKLONG;
	    } else
		*pval++ = *cp;
	    break;
	case post_val:
	    /*
	     * We know that cursor points at the first spec matching
	     * the current attribute.  We don't know that it's *the*
	     * correct spec; our dialect allows there to be any number
	     * of adjacent ones with the same attrname but different
	     * types.  Here's where we try to seek forward for a
	     * matching type/attr pair if we're not looking at one.
	     */
	    for (;;) {
		int seeking = cursor->type;
		if (value_quoted && (cursor->type == t_string || cursor->type == t_time))
		    break;
		if ((strcmp(valbuf, "true")==0 || strcmp(valbuf, "false")==0)
			&& seeking == t_boolean)
		    break;
		if (isdigit((unsigned char) valbuf[0])) {
		    bool decimal = strchr(valbuf, '.') != NULL;
		    if (decimal && seeking == t_real)
			break;
		    if (!decimal && (seeking == t_integer || seeking == t_uinteger))
			break;
		}
		if (cursor[1].attribute==NULL)	/* out of possiblities */
		    break;
		if (strcmp(cursor[1].attribute, attrbuf)!=0)
		    break;
		++cursor;
	    }
	    if (value_quoted
		&& (cursor->type != t_string && cursor->type != t_character
		    && cursor->type != t_check && cursor->type != t_time
		    && cursor->type != t_ignore && cursor->map == 0)) {
		json_debug_trace((1,
				  "Saw quoted value when expecting non-string.\n"));
		return JSON_ERR_QNONSTRING;
	    }
	    if (!value_quoted
		&& (cursor->type == t_string || cursor->type == t_check
		    || cursor->type == t_time || cursor->map != 0)) {
		json_debug_trace((1,
				  "Didn't see quoted value when expecting string.\n"));
		return JSON_ERR_NONQSTRING;
	    }
	    if (cursor->map != 0) {
		for (mp = cursor->map; mp->name != NULL; mp++)
		    if (strcmp(mp->name, valbuf) == 0) {
			goto foundit;
		    }
		json_debug_trace((1, "Invalid enumerated value string %s.\n",
				  valbuf));
		return JSON_ERR_BADENUM;
	      foundit:
		(void)snprintf(valbuf, sizeof(valbuf), "%d", mp->value);
	    }
	    lptr = json_target_address(cursor, parent, offset);
	    if (lptr != NULL)
		switch (cursor->type) {
		case t_integer:
		    {
			int tmp = atoi(valbuf);
			memcpy(lptr, &tmp, sizeof(int));
		    }
		    break;
		case t_uinteger:
		    {
			unsigned int tmp = (unsigned int)atoi(valbuf);
			memcpy(lptr, &tmp, sizeof(unsigned int));
		    }
		    break;
		case t_time:
		    {
			double tmp = iso8601_to_unix(valbuf);
			memcpy(lptr, &tmp, sizeof(double));
		    }
		    break;
		case t_real:
		    {
			double tmp = safe_atof(valbuf);
			memcpy(lptr, &tmp, sizeof(double));
		    }
		    break;
		case t_string:
		    if (parent != NULL
			&& parent->element_type != t_structobject
			&& offset > 0)
			return JSON_ERR_NOPARSTR;
		    (void)strncpy(lptr, valbuf, cursor->len);
		    valbuf[sizeof(valbuf)-1] = '\0';
		    break;
		case t_boolean:
		    {
			bool tmp = (strcmp(valbuf, "true") == 0);
			memcpy(lptr, &tmp, sizeof(bool));
		    }
		    break;
		case t_character:
		    if (strlen(valbuf) > 1)
			return JSON_ERR_STRLONG;
		    else
			lptr[0] = valbuf[0];
		    break;
		case t_ignore:	/* silences a compiler warning */
		case t_object:	/* silences a compiler warning */
		case t_structobject:
		case t_array:
		    break;
		case t_check:
		    if (strcmp(cursor->dflt.check, valbuf) != 0) {
			json_debug_trace((1,
					  "Required attribute value %s not present.\n",
					  cursor->dflt.check));
			return JSON_ERR_CHECKFAIL;
		    }
		    break;
		}
	    /*@fallthrough@*/
	case post_array:
	    if (isspace((unsigned char) *cp))
		continue;
	    else if (*cp == ',')
		state = await_attr;
	    else if (*cp == '}') {
		++cp;
		goto good_parse;
	    } else {
		json_debug_trace((1, "Garbage while expecting comma or }\n"));
		return JSON_ERR_BADTRAIL;
	    }
	    break;
	}
    }

  good_parse:
    /* in case there's another object following, consume trailing WS */
    while (isspace((unsigned char) *cp))
	++cp;
    if (end != NULL)
	*end = cp;
    json_debug_trace((1, "JSON parse ends.\n"));
    return 0;
    /*@ +nullstate +nullderef +mustfreefresh +nullpass +usedef @*/
}

int json_read_array(const char *cp, const struct json_array_t *arr,
		    const char **end)
{
    /*@-nullstate -onlytrans@*/
    int substatus, offset, arrcount;
    char *tp;

    if (end != NULL)
	*end = NULL;		/* give it a well-defined value on parse failure */

    json_debug_trace((1, "Entered json_read_array()\n"));

    while (isspace((unsigned char) *cp))
	cp++;
    if (*cp != '[') {
	json_debug_trace((1, "Didn't find expected array start\n"));
	return JSON_ERR_ARRAYSTART;
    } else
	cp++;

    tp = arr->arr.strings.store;
    arrcount = 0;

    /* Check for empty array */
    while (isspace((unsigned char) *cp))
	cp++;
    if (*cp == ']')
	goto breakout;

    for (offset = 0; offset < arr->maxlen; offset++) {
	char *ep = NULL;
	json_debug_trace((1, "Looking at %s\n", cp));
	switch (arr->element_type) {
	case t_string:
	    if (isspace((unsigned char) *cp))
		cp++;
	    if (*cp != '"')
		return JSON_ERR_BADSTRING;
	    else
		++cp;
	    arr->arr.strings.ptrs[offset] = tp;
	    for (; tp - arr->arr.strings.store < arr->arr.strings.storelen;
		 tp++)
		if (*cp == '"') {
		    ++cp;
		    *tp++ = '\0';
		    goto stringend;
		} else if (*cp == '\0') {
		    json_debug_trace((1,
				      "Bad string syntax in string list.\n"));
		    return JSON_ERR_BADSTRING;
		} else {
		    *tp = *cp++;
		}
	    json_debug_trace((1, "Bad string syntax in string list.\n"));
	    return JSON_ERR_BADSTRING;
	  stringend:
	    break;
	case t_object:
	case t_structobject:
	    substatus =
		json_internal_read_object(cp, arr->arr.objects.subtype, arr,
					  offset, &cp);
	    if (substatus != 0)
		return substatus;
	    break;
	case t_integer:
	    arr->arr.integers.store[offset] = (int)strtol(cp, &ep, 0);
	    if (ep == cp)
		return JSON_ERR_BADNUM;
	    else
		cp = ep;
	    break;
	case t_uinteger:
	    arr->arr.uintegers.store[offset] = (unsigned int)strtoul(cp, &ep, 0);
	    if (ep == cp)
		return JSON_ERR_BADNUM;
	    else
		cp = ep;
	    break;
	case t_real:
	    arr->arr.reals.store[offset] = strtod(cp, &ep);
	    if (ep == cp)
		return JSON_ERR_BADNUM;
	    else
		cp = ep;
	    break;
	case t_boolean:
	    if (strncmp(cp, "true", 4) == 0) {
		arr->arr.booleans.store[offset] = true;
		cp += 4;
	    }
	    else if (strncmp(cp, "false", 5) == 0) {
		arr->arr.booleans.store[offset] = false;
		cp += 5;
	    }
	    break;
	case t_time:
	case t_character:
	case t_array:
	case t_check:
	case t_ignore:
	    json_debug_trace((1, "Invalid array subtype.\n"));
	    return JSON_ERR_SUBTYPE;
	}
	arrcount++;
	if (isspace((unsigned char) *cp))
	    cp++;
	if (*cp == ']') {
	    json_debug_trace((1, "End of array found.\n"));
	    goto breakout;
	} else if (*cp == ',')
	    cp++;
	else {
	    json_debug_trace((1, "Bad trailing syntax on array.\n"));
	    return JSON_ERR_BADSUBTRAIL;
	}
    }
    json_debug_trace((1, "Too many elements in array.\n"));
    return JSON_ERR_SUBTOOLONG;
  breakout:
    if (arr->count != NULL)
	*(arr->count) = arrcount;
    if (end != NULL)
	*end = cp;
    /*@ -nullderef @*/
    json_debug_trace((1, "leaving json_read_array() with %d elements\n",
		      arrcount));
    /*@ +nullderef @*/
    return 0;
    /*@+nullstate +onlytrans@*/
}

int json_read_object(const char *cp, const struct json_attr_t *attrs,
		     /*@null@*/ const char **end)
{
    int st;

    json_debug_trace((1, "json_read_object() sees '%s'\n", cp));
    st = json_internal_read_object(cp, attrs, NULL, 0, end);
    return st;
}

const /*@observer@*/ char *json_error_string(int err)
{
    const char *errors[] = {
	"unknown error while parsing JSON",
	"non-whitespace when expecting object start",
	"non-whitespace when expecting attribute start",
	"unknown attribute name",
	"attribute name too long",
	"saw [ when not expecting array",
	"array element specified, but no [",
	"string value too long",
	"token value too long",
	"garbage while expecting comma or } or ]",
	"didn't find expected array start",
	"error while parsing object array",
	"too many array elements",
	"garbage while expecting array comma",
	"unsupported array element type",
	"error while string parsing",
	"check attribute not matched",
	"can't support strings in parallel arrays",
	"invalid enumerated value",
	"saw quoted value when expecting nonstring",
	"didn't see quoted value when expecting string",
	"other data conversion error",
	"unexpected null value or attribute pointer",
    };

    if (err <= 0 || err >= (int)(sizeof(errors) / sizeof(errors[0])))
	return errors[0];
    else
	return errors[err];
}

/* end */

