/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *  filter.h - Functions in this file act as an abstraction layer between
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

#define FILTERGET_FAIL 0
#define FILTERGET_SUCCESS 1

typedef int (*add_iov_ptr)(void *, const void *, int);

int is_filtering_enabled(void);
void *get_flib(char *);
int addto_msglist(void *, void ***, int *, int, void *, char *, int);
void free_msglist(void *, void ***, int);
int add_filtered_msg(void *, void *, void ***, int, void *, char *, int, add_iov_ptr, int, char **);
void deserialize_item_data(char *, int, void **);
int set_function_pointers(void);
int set_multi_get_opt_vars(char *);
int set_multi_get_opt_flags(char *);
