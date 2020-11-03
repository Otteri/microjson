/* Structures for JSON parsing using only fixed-extent memory
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * SPDX-License-Identifier: BSD-2-clause
 */
#pragma once

#include <stdbool.h>
#include <stdio.h>
#include <ctype.h>
#ifdef TIME_ENABLE
#include <time.h>
#endif /* TIME_ENABLE */

typedef enum {t_integer, t_uinteger, t_real,
	      t_string, t_boolean, t_character,
	      t_time,
	      t_object, t_structobject, t_array,
	      t_check, t_ignore,
	      t_short, t_ushort}
    json_type;

struct json_enum_t {
    char	*name;
    int		value;
};

struct json_array_t {
    json_type element_type;
    union arr_tag {
	struct {
	    const struct json_attr_t *subtype;
	    char *base;
	    size_t stride;
	} objects;
	struct {
	    char **ptrs;
	    char *store;
	    int storelen;
	} strings;
	struct {
	    int *store;
	} integers;
	struct {
	    unsigned int *store;
	} uintegers;
	struct {
	    short *store;
	} shorts;
	struct {
	    unsigned short *store;
	} ushorts;
	struct double_tag {
	    double *store;

#if defined(__cplusplus)
        double_tag(double *const array): store(array) {}
#endif
	} reals;
	struct {
	    bool *store;
	} booleans;

#if defined(__cplusplus)
    arr_tag(double *const array) : reals(array) {}
#endif
    } arr;
    int *count, maxlen;

#if defined(__cplusplus)
    json_array_t( double *const array, int *const len, int const maxlen ) : element_type(t_real), arr(array), count(len), maxlen(maxlen) {}
#endif
};

struct json_attr_t {
    char *attribute;
    json_type type;
    union addr_tag {
        int *integer;
        unsigned int *uinteger;
        short *shortint;
        unsigned short *ushortint;
        double *real;
        char *string;
        bool *boolean;
        char *character;
        const struct json_attr_t *attrs;
        const struct json_array_t array;
        size_t offset;

#if defined(__cplusplus)
        addr_tag() {}
        addr_tag(int* in)            : integer(in) {}
        addr_tag(unsigned int* in)   : uinteger(in) {}
        addr_tag(short* in)          : shortint(in) {}
        addr_tag(unsigned short* in) : ushortint(in) {}
        addr_tag(double* in)         : real(in) {}
        addr_tag(bool* in)           : boolean(in) {}
        addr_tag(char* in)           : character(in) {}
        // String is handled separately below; cannot overload here.
        addr_tag(double *const in, int *const len, int const maxlen) : array(in, len, maxlen) {}
#endif
    } addr;
    union dflt_tag {
        int integer;
        unsigned int uinteger;
        short shortint;
        unsigned short ushortint;
        double real;
        bool boolean;
        char character;
        char *check;
        #if defined(__cplusplus)
        dflt_tag() {}
        dflt_tag(int in)            : integer(in) {}
        dflt_tag(unsigned int in)   : uinteger(in) {}
        dflt_tag(short in)          : shortint(in) {}
        dflt_tag(unsigned short in) : ushortint(in) {}
        dflt_tag(double in)         : real(in) {}
        dflt_tag(bool* in)          : boolean(in) {}   
        dflt_tag(char in)           : character(in) {}
        // No default for str, because len must be given explicitly!
        #endif
    } dflt;
    size_t len;
    const struct json_enum_t *map;
    bool nodefault;

    #if defined(__cplusplus)
    // With default
    json_attr_t(const char* name, json_type type, int* addr, int dflt)
        : attribute(const_cast<char*>(name)), type(type), addr(addr), dflt(dflt) {}
    json_attr_t(const char* name, json_type type, unsigned int* addr, uint dflt)
        : attribute(const_cast<char*>(name)), type(type), addr(addr), dflt(dflt) {}
    json_attr_t(const char* name, json_type type, short* addr, short dflt)
        : attribute(const_cast<char*>(name)), type(type), addr(addr), dflt(dflt) {}
    json_attr_t(const char* name, json_type type, unsigned short* addr, unsigned short dflt)
        : attribute(const_cast<char*>(name)), type(type), addr(addr), dflt(dflt) {}
    json_attr_t(const char* name, json_type type, double* addr, double dflt)
        : attribute(const_cast<char*>(name)), type(type), addr(addr), dflt(dflt) {}
    json_attr_t(const char* name, json_type type, bool* addr, bool dflt)
        : attribute(const_cast<char*>(name)), type(type), addr(addr), dflt(dflt) {}
    json_attr_t(const char* name, json_type type, char* addr, char dflt)
        : attribute(const_cast<char*>(name)), type(type), addr(addr), dflt(dflt) {}

    // Arrays with default value
    json_attr_t(const char *const name, double *const addr, int *const len, int const maxlen, double const dflt)
    : attribute(const_cast<char*>(name)), type(t_array), addr(addr,len,maxlen), dflt(dflt) {}

    // Without default value
    json_attr_t(nullptr_t) : attribute(nullptr) {}
    json_attr_t(const char* name, json_type type, int* addr)           : json_attr_t(name, type, addr, 0) {}
    json_attr_t(const char* name, json_type type, unsigned int* addr)  : json_attr_t(name, type, addr, 0u) {}
    json_attr_t(const char* name, json_type type, short* addr)         : json_attr_t(name, type, addr, 0) {}
    json_attr_t(const char* name, json_type type, unsigned short* addr): json_attr_t(name, type, addr, 0u) {}
    json_attr_t(const char* name, json_type type, double* addr)        : json_attr_t(name, type, addr, 0.0) {}
    json_attr_t(const char* name, json_type type, bool* addr)          : json_attr_t(name, type, addr, false) {}

    // Arrays without default value
    json_attr_t(const char *const name, double *const addr, int *const len, int const maxlen)  : json_attr_t(name, addr, len, maxlen, 0.0) {}

    // String & character constructor
    // Assignments, because addr_tag() cannot construct char and str automatically due to same input type
    // It is not possible to utilize len argument, because it is given after dflt -> we should always have dflt.
    json_attr_t(const char* name, json_type type, char* addr, size_t len=1u)
        : attribute(const_cast<char*>(name)), type(type)
    {
        if (type == t_string) {
            this->len = len;
            this->addr.string = addr;
        }
        else if (type == t_character) {
            this->len = 1u;
            this->addr.character = addr;
        };
    }

    json_attr_t(const char* name, json_type type, size_t len=1u) {
        if (type == t_ignore) {
            (void)name; // Silence warning: unused parameter
            this->len = len;
            return;
        }
        else {
            puts("Invalid json_attr_t arguments. Missing storage field!");
            exit(EXIT_FAILURE);
        }
    }
    #endif
};

#define JSON_ATTR_MAX	31	/* max chars in JSON attribute name */
#define JSON_VAL_MAX	512	/* max chars in JSON value part */

#ifdef __cplusplus
extern "C" {
#endif
int json_read_object(const char *, const struct json_attr_t *,
		     const char **);
int json_read_array(const char *, const struct json_array_t *,
		    const char **);
const char *json_error_string(int);

#ifdef TIME_ENABLE
extern time_t timegm(struct tm *tm);
#endif /* TIME_ENABLE */
    
void json_enable_debug(int, FILE *);
#ifdef __cplusplus
}
#endif

#define JSON_ERR_OBSTART	1	/* non-WS when expecting object start */
#define JSON_ERR_ATTRSTART	2	/* non-WS when expecting attrib start */
#define JSON_ERR_BADATTR	3	/* unknown attribute name */
#define JSON_ERR_ATTRLEN	4	/* attribute name too long */
#define JSON_ERR_NOARRAY	5	/* saw [ when not expecting array */
#define JSON_ERR_NOBRAK 	6	/* array element specified, but no [ */
#define JSON_ERR_STRLONG	7	/* string value too long */
#define JSON_ERR_TOKLONG	8	/* token value too long */
#define JSON_ERR_BADTRAIL	9	/* garbage while expecting comma or } or ] */
#define JSON_ERR_ARRAYSTART	10	/* didn't find expected array start */
#define JSON_ERR_OBJARR 	11	/* error while parsing object array */
#define JSON_ERR_SUBTOOLONG	12	/* too many array elements */
#define JSON_ERR_BADSUBTRAIL	13	/* garbage while expecting array comma */
#define JSON_ERR_SUBTYPE	14	/* unsupported array element type */
#define JSON_ERR_BADSTRING	15	/* error while string parsing */
#define JSON_ERR_CHECKFAIL	16	/* check attribute not matched */
#define JSON_ERR_NOPARSTR	17	/* can't support strings in parallel arrays */
#define JSON_ERR_BADENUM	18	/* invalid enumerated value */
#define JSON_ERR_QNONSTRING	19	/* saw quoted value when expecting nonstring */
#define JSON_ERR_NONQSTRING	19	/* didn't see quoted value when expecting string */
#define JSON_ERR_MISC		20	/* other data conversion error */
#define JSON_ERR_BADNUM		21	/* error while parsing a numerical argument */
#define JSON_ERR_NULLPTR	22	/* unexpected null value or attribute pointer */
#define JSON_ERR_NOCURLY	23	/* object element specified, but no { */

/*
 * Use the following macros to declare template initializers for structobject
 * arrays.  Writing the equivalents out by hand is error-prone.
 *
 * STRUCTOBJECT takes a structure name s, and a fieldname f in s.
 *
 * STRUCTARRAY takes the name of a structure array, a pointer to a an
 * initializer defining the subobject type, and the address of an integer to
 * store the length in.
 */
#define STRUCTOBJECT(s, f)	.addr.offset = offsetof(s, f)
#define STRUCTARRAY(a, e, n) \
	.addr.array.element_type = t_structobject, \
	.addr.array.arr.objects.subtype = e, \
	.addr.array.arr.objects.base = (char*)a, \
	.addr.array.arr.objects.stride = sizeof(a[0]), \
	.addr.array.count = n, \
	.addr.array.maxlen = (int)(sizeof(a)/sizeof(a[0]))

/* json.h ends here */
