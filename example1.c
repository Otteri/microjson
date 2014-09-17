/* example1.c - first example in the programming guide
 *
 * This file is Copyright (c) 2010 by the GPSD project
 * BSD terms apply: see the file COPYING in the distribution root for details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "microjson.h"

static const char *simple = "{\"flag1\":true,\"flag2\":false,\"count\":42}";

static bool flag1, flag2;
static int count;

static const struct json_attr_t json_attrs[] = {
    {"count",   t_integer, .addr.integer = &count},
    {"flag1",   t_boolean, .addr.boolean = &flag1,},
    {"flag2",   t_boolean, .addr.boolean = &flag2,},
    {NULL},
};

int main(int argc, char *argv[])
{
    int status = 0;

    puts(simple);
    json_read_object(simple, json_attrs, NULL);
    printf("count = %d, flag1 = %d, flag2 = %d\n",
	   count, flag1, flag2);
}

/* end */
