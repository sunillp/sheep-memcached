/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *  filter.c - Functions in this file act as an abstraction layer between
 *             memcached server and user-defined filter-library.
 *             These functions are called from memcached server and internally
 *             these functions call user-implemented filtering functions.
 *             Here "dlopen" and "dlsym" framework is used to interact with
 *             user-defined functions in user-defined filter library.
 *
 *             These changes enable doing data filtering on memcached server.
 *             This improves performance of multi-get operations ex: reading 
 *             data of all friends of a user from server to client over
 *             network, processing this data to find say latest 10 updates
 *             across all friends on client and reurning this filtered data
 *             which is a small subset of total data read from server to user.
 *             Mutli-get operations are very common in social networking
 *             applications. Since these changes help in doing fitering on
 *             server, very less data is sent from server to client over
 *             network. This drastically minimizes network load and avoids
 *             situations like network saturation and reduced performance at
 *             peak load.
 *
 *  Copyright (c) 2013 Yahoo Inc., All Rights Reserved.
 *
 *  Use and distribution licensed under the BSD license.  See
 *  the LICENSE file for full text.
 *
 *  Authors:
 *      Sunil Patil <sunillp@yahoo-inc.com>
 */

#include "filter.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include "memcached.h"

enum multi_get_opt {
    NO_FILTERING = 1,                   /* No data filtering at server */
    MEMCACHED_FILTERING = 2,            /* Data filtering at server enable (no deserialized data) */
    MEMCACHED_DATAOPT_FILTERING = 4,    /* Data filtering at server enable (with deserialized data) */
};

#define INITIAL_MSGLIST_SIZE        100
#define FILTER_FLAGS                6  /* (bits=110) */
#define DATA_DESERIALIZATON_FLAGS   4  /* (bits=100) */
#define FILTERING_ENABLED()            (multi_get_opt_flags & FILTER_FLAGS)
#define DATA_DESERIALIZATON_ENABLED()  (multi_get_opt_flags & DATA_DESERIALIZATON_FLAGS)

int multi_get_opt_flags = NO_FILTERING;
char *multi_get_opt_filterlibpath = NULL;
char *multi_get_opt_filterlib = NULL;

typedef unsigned char uint8_t;

union deserialize_u {
    void *(*func)(uint8_t *, int);
    void *funcp;
};

union free_msg_u {
    void (*func)(void *);
    void *funcp;
};

union readfilter_u {
    void (*func)(void **, int, void **, int *, void *(*)(int), int, char **);
    void *funcp;
};

struct fliblist_s {
    char *flibname;
    union deserialize_u deserialize;
    union free_msg_u free_msg;
    union readfilter_u readfilter;
    struct fliblist_s *next;
};

struct fliblist_s *head = NULL;
struct fliblist_s *firstlib = NULL;

static struct fliblist_s *load_library(char *);

int is_filtering_enabled(void)
{
    return FILTERING_ENABLED();
}

void *get_flib(char *flibname)
{
    struct fliblist_s *flib = head;

    if (strcmp(flibname, "default") == 0)
        return head;
    while (flib != NULL) {
        if (strcmp(flib->flibname, flibname) == 0)
            return flib;
        flib = flib->next;
    }
    if (flib == NULL) {
        flib = load_library(flibname);
    }
    
    return (void *) flib;
}

static void *alloc_func(int len)
{
    char *buf = NULL;
    int tmp_nsuffix = 40;
    char *tmp_dataend = "\r\n";
    buf = calloc(tmp_nsuffix + len + strlen(tmp_dataend), sizeof(char));
    if (buf != NULL)
        return (void *) &buf[40];
    return (void *) buf;
}

static void get_filtered_msg(void *flib, void **msglist, int i, char *suffix, void **msg, int *mstart, int *mlen, int numParam, char **params)
{
    *msg = NULL;
    *mstart = 0;
    *mlen = 0;

    if (!FILTERING_ENABLED() || i <= 0)
        return;

    int tflags = 0;
    sscanf(suffix, "%d", &tflags);

    int msglen = 0;
    int tmp_nsuffix = 40;
    char *tmp_dataend = "\r\n";
    ((struct fliblist_s *) flib)->readfilter.func(msglist, i, msg, &msglen, alloc_func, numParam, params);
    if (*msg == NULL || msglen == 0)
        return;
    *msg = (void *) ((char *) *msg - tmp_nsuffix);

    char tbuf[40];
    int tbufsize = 0;
    tbufsize = (uint8_t) snprintf(tbuf, tmp_nsuffix, " %d %d\r\n", tflags, msglen);
    memcpy((char *) *msg + tmp_nsuffix - tbufsize, tbuf, tbufsize);
    memcpy((char *) *msg + tmp_nsuffix + msglen, tmp_dataend, strlen(tmp_dataend));
    *mstart = tmp_nsuffix - tbufsize;
    *mlen = tbufsize + msglen + strlen(tmp_dataend);

    return;
}

static void free_msg_multi(void *flib, void **msglist, int len)
{
    int j = 0;
    if (msglist == NULL)
        return;

    for (j = 0; j < len; j++)
        ((struct fliblist_s *) flib)->free_msg.func(msglist[j]);
    return;
}

void free_msglist(void *flib, void ***msglist, int i)
{
    if (*msglist != NULL) {
        if (!DATA_DESERIALIZATON_ENABLED() && i > 0) {
            free_msg_multi(flib, *msglist, i);
        }
        free(*msglist);
        *msglist = NULL;
    }

    return;
}

int addto_msglist(void *flib, void ***msglist, int *msglistlen, int i, void *it_msg, char *it_data, int it_nbytes)
{
    void **tmsglist = NULL;
    void **newmsglist = NULL;

    if (!FILTERING_ENABLED())
        return 1;

    if (*msglist == NULL) {
        *msglistlen = INITIAL_MSGLIST_SIZE;
        *msglist = (void **) malloc(sizeof (void *) * (*msglistlen));
        if (*msglist == NULL)
            return 1;
    }
    if (i >= *msglistlen) {
        *msglistlen = *msglistlen * 2;
        newmsglist = (void **) realloc(*msglist, sizeof (void *) * (*msglistlen));
        if (newmsglist == NULL) {
            free_msglist(flib, msglist, i);
            return 1;
        }
        *msglist = newmsglist;
    }
    tmsglist = *msglist;
    if (DATA_DESERIALIZATON_ENABLED()) {
        // Deserialized data is available in "it_msg"
        tmsglist[i] = it_msg;
        if (tmsglist[i] == NULL)
            return 1;
    } else {
        // Need to do filtering on serialized data by first deserializing it.
        tmsglist[i] = ((struct fliblist_s *) flib)->deserialize.func((uint8_t *) it_data, it_nbytes - 2); // Last 2 bytes contain "\r\n", ignore that
        if (tmsglist[i] == NULL)
            return 1;
    }

    return 0;
}

void deserialize_item_data(char *it_data, int it_nbytes, void **it_msg)
{
    if (DATA_DESERIALIZATON_ENABLED()) {
        if (*it_msg) {
            firstlib->free_msg.func(*it_msg);
            *it_msg = NULL;
        }
        *it_msg = firstlib->deserialize.func((uint8_t *) it_data, it_nbytes - 2); // Last 2 bytes contain "\r\n", ignore that
        if (*it_msg == NULL) {
            // nothing to be done
        }
    }

    return;
}

static struct fliblist_s *load_library(char *flibname)
{
    struct fliblist_s *flib = NULL;

    void *dlhandle = dlopen(flibname, RTLD_NOW);
    if (dlhandle == NULL) {
        fprintf(stderr, "FilterLib Error: Could not open lib %s\n", flibname);
        return NULL;
    }
    void *deserialize = dlsym(dlhandle, "deserialize");
    if (deserialize == NULL) {
        fprintf(stderr, "FilterLib Error: Could not find function deserialize in lib %s\n", flibname);
        return NULL;
    }
    void *free_msg = dlsym(dlhandle, "free_msg");
    if (free_msg == NULL) {
        fprintf(stderr, "FilterLib Error: Could not find function free_msg in lib %s\n", flibname);
        return NULL;
    }
    void *readfilter = dlsym(dlhandle, "readfilter");
    if (readfilter == NULL) {
        fprintf(stderr, "FilterLib Error: Could not find function readfilter in lib %s\n", flibname);
        return NULL;
    }

    flib = (struct fliblist_s *) malloc(sizeof (struct fliblist_s) * 1);
    if (flib == NULL) {
        return NULL;
    }
    flib->flibname = strdup(flibname);
    if (flib->flibname == NULL)
        return NULL;
    flib->deserialize.funcp = deserialize;
    flib->free_msg.funcp = free_msg;
    flib->readfilter.funcp = readfilter;
    flib->next = head;
    if (head == NULL) {
        firstlib = flib;
    }
    head = flib;

    return flib;
}

int set_function_pointers(void)
{
    if (FILTERING_ENABLED() && multi_get_opt_filterlibpath == NULL) {
        // -x option was specified i.e. filtering was enabled but -y option i.e. filterlibpath was not specified.
        fprintf(stderr, "-x <num> -y <filterlib-path>: -x and -y options must be used together\n");
        return 1;
    }
    if (FILTERING_ENABLED()) {
        // Filtering is enabled.
        if (NULL == load_library(multi_get_opt_filterlib)) {
            return 1;
        }
    }

    return 0;
}

int set_multi_get_opt_vars(char *optarg)
{
    if (multi_get_opt_flags == NO_FILTERING) {
        fprintf(stderr, "-x <num> -y <filterlib-path>: -x and -y options must be used together\n");
        return 1;
    }
    multi_get_opt_filterlibpath = strdup(optarg);
    if (multi_get_opt_filterlibpath == NULL) {
        fprintf(stderr, "multi_get_opt_filterlibpath allocation failed\n");
        return 1;
    }

    char path[2048];
    memset(path, 0, sizeof (path));
    strcpy(path, multi_get_opt_filterlibpath);
    strcat(path, "/libfilter.so");
    FILE *fp = NULL;
    if ((fp = fopen(path, "r")) == NULL) {
        fprintf(stderr, "-x <num> -y <filterlib-path>: Library not found %s\n", path);
        return 1;
    }
    fclose(fp);
    multi_get_opt_filterlib = strdup(path);
    if (multi_get_opt_filterlib == NULL) {
        fprintf(stderr, "multi_get_opt_filterlib allocation failed\n");
        return 1;
    }

    return 0;
}

int set_multi_get_opt_flags(char *optarg)
{
    switch (atoi(optarg)) {
        case 1:
            multi_get_opt_flags = MEMCACHED_FILTERING;
            break;
        case 2:
            multi_get_opt_flags = MEMCACHED_DATAOPT_FILTERING;
            break;
        default:
            fprintf(stderr, "-x <num> -y <filterlib-path>: <num> must be 1 or 2\n");
            return 1;
    }

    return 0;
}

int add_filtered_msg(void *flib, void *tc, void ***msglist, int i, void *tit, char *keys_arr, int keys_len, add_iov_ptr add_iov, int numParam, char **params)
{
    void *msg = NULL;
    int mstart = 0;
    int mlen = 0;
    int ret = 1;
    conn *c = (conn *) tc;
    item *it = (item *) tit;

    if (c != NULL && *msglist != NULL && it != NULL && i > 0) {
        get_filtered_msg(flib, *msglist, i, ITEM_suffix(it), &msg, &mstart, &mlen, numParam, params);
        if (msg != NULL && mstart != 0 && mlen != 0) {
            c->filteredmsg = msg;
            if (add_iov(c, "VALUE ", 6) != 0 ||
                add_iov(c, keys_arr, keys_len) != 0 ||
                add_iov(c, (char *) msg + mstart, mlen) != 0)
                    ret = 2;
                else
                    ret = 0;
        }
    }
    free_msglist(flib, msglist, i);

    return ret;
}
