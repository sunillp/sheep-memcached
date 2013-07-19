/*
 *  sheepfilter_variable_countlim.c - a filter library taking input params.
 *
 *  Copyright (c) 2013 Yahoo Inc., All Rights Reserved.
 *
 *  Use and distribution licensed under the BSD license.  See
 *  the LICENSE file for full text.
 *
 *  Authors:
 *      Sunil Patil <sunillp@yahoo-inc.com>
 */

#include "sheepfilter.h"

static int serialize(View *msg, void **obuf, int *olen, void *(*alloc_func)(int))
{
    char *buf = 0;
    unsigned len = 0;

    *obuf = NULL;
    *olen = 0;
    len = view__get_packed_size(msg);
    if (len == 0) {
        fprintf(stderr, "serialize: incoming message length is 0\n");
        return 1;
    }
    buf = alloc_func(len);
    if (buf == NULL) {
        fprintf(stderr, "serialize: error packing incoming message\n");
        return 1;
    }
    view__pack(msg, (void *) buf);
    *obuf = buf;
    *olen = len;
        
    return 0;
}

static int cmpviews(const void *p1, const void *p2)
{
    ArticleReadRecord *arr1 = * (ArticleReadRecord **) p1;
    ArticleReadRecord *arr2 = * (ArticleReadRecord **) p2;

    return (arr1->timestamp - arr2->timestamp) * -1;
}

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
void *deserialize(uint8_t *buf, int len) 
{
    View *msg = NULL;

    if (buf == NULL || len == 0) {
        fprintf(stderr, "deserialize: error in input\n");
        return NULL;
    }
    msg = view__unpack(NULL, len, buf);
    if (msg == NULL) {
        fprintf(stderr, "deserialize: error unpacking incoming message\n");
        return NULL;
    }
    
    return (void *) msg;
}

/*
 * free_msg()
 *
 * Input:
 *      msg : Pointer to buffer (containing deserialized data) to be freed.
 *            This pointer must have been returned by function "deserialize"
 *            i.e. the deserialized data buffer must have been allocated earlier
 *            by function "deserialize".
 */
void free_msg(void *msg)
{
    if (msg != NULL)
        view__free_unpacked((View *) msg, NULL);
    return;
}

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
void readfilter(void **msglist, int len, void **buf, int *buflen, void *(*alloc_func)(int), int numParam, char **params)
{
    View **viewlist = (View **) msglist;
    ArticleReadRecord **arr = NULL;
    int i = 0, k = 0;
    int total_entries = 0;
    int ret = 0;
    int count_lim = 0;

    if (len == 0 || msglist == NULL)
        return;
    if (numParam != 1 || params == NULL)
        return;
    count_lim = atoi(params[0]);
    for (i = 0; i < len; i++)
        total_entries += viewlist[i]->n_entries;
    arr = (ArticleReadRecord **) malloc(sizeof (ArticleReadRecord *) * total_entries);
    if (arr == NULL) {
        fprintf(stderr, "readfilter: error allocating arr\n");
        return;
    }
    for (i = 0, k = 0; i < len; i++) {
        memcpy(arr + k, viewlist[i]->entries, sizeof (viewlist[i]->entries[0]) * viewlist[i]->n_entries);
        k += viewlist[i]->n_entries;
    }
    assert(k == total_entries);
    qsort(arr, k, sizeof (ArticleReadRecord *), cmpviews);

    k = k > count_lim? count_lim: k;
    View msg = VIEW__INIT;
    msg.maxsize = k;
    msg.has_maxsize = 1;
    msg.has_oldestentry = 0;
    msg.n_entries = k;
    msg.entries = arr;
    ret = serialize(&msg, buf, buflen, alloc_func);
    if (ret == 1) {
        fprintf(stderr, "readfilter: error serializing\n");
    }
    free(arr);
    
    return;
}
