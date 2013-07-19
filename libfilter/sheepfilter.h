/*
 *  sheepfilter.h - a filter library.
 *
 *  Copyright (c) 2013 Yahoo Inc., All Rights Reserved.
 *
 *  Use and distribution licensed under the BSD license.  See
 *  the LICENSE file for full text.
 *
 *  Authors:
 *      Sunil Patil <sunillp@yahoo-inc.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "sheep.pb-c.h"

/*
 * Functions called by memcached server.
 */

/*
 * deserialize()
 *
 * Input:
 *      buf : Buffer containing data to be deserialized
 *      len : Length of buf
 *
 * Return Value:
 *      - Pointer to deserialized data buffer in case of success
 *      - NULL in case of error 
 *
 * Note: "Deserialized data buffer" (return value, void *) should be
 *       freed by caller after use by calling function "free_msg".
 */
void *deserialize(uint8_t *buf, int len);

/*
 * free_msg()
 *
 * Input:
 *      msg : Pointer to buffer (containing deserialized data) to be freed.
 *            This pointer must have been returned by function "deserialize"
 *            i.e. the deserialized data buffer must have been allocated earlier
 *            by function "deserialize".
 */
void free_msg(void *msg);

/*
 * readfilter()
 * 
 * Input:
 *      msglist    : Array of "Deserialized data/buffer" to be filtered.
 *      len        : Length of array msglist
 *      alloc_func : Function used to allocate "buf" (buffer containing
 *                   filtered ouptut data in serialized form). This function
 *                   is supplied by caller and is similar to "malloc".
 *
 * Output:
 *      buf        : Buffer containing filtered ouptut data in serialized form
 *      buflen     : Length of buf
 *
 * Note: Caller should supply function "alloc_func" similar to "malloc" and should
 *       free output buffer "buf"
 */
void readfilter(void **msglist, int len, void **buf, int *buflen, void *(*alloc_func)(int), int numParam, char **params);
