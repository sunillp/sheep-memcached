/*
 *  memcached_client_write.c - a memcached client for writing data in 
 *                             memcached server. This is based on "libmemcached"
 *
 *  Copyright (c) 2013 Yahoo Inc., All Rights Reserved.
 *
 *  Use and distribution licensed under the BSD license.  See
 *  the LICENSE file for full text.
 *
 *  Authors:
 *      Sunil Patil <sunillp@yahoo-inc.com>
 */

#include <sys/time.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libmemcached/memcached.h>
#include "../libfilter/sheep.pb-c.h"

memcached_st *memc = NULL;
struct serverinfo {
    char name[50];
    int port;
} serversinfo[500];

void printUsage(void)
{
    printf("WRONG USAGE: memcached_client_write <#users - Max=1000000> <#records-per-user> <hostname-port-filename>\n");
    printf("Ex: memcached_client 1000000 10 host-port.txt\n");
    printf("Format of host-port.txt:\n");
    printf("<hostname-1> <port-number>\n");
    printf("...\n");
    printf("<hostname-N> <port-number>\n");
    printf("\n");
    exit(1);
}

int main(int argc, char *argv[])
{
    memcached_server_st *servers = NULL;
    memcached_return rc;
    int i = 0;
    int j = 0;

    if (argc != 4)
        printUsage();

    int numUsers = atoi(argv[1]);
    int recPerUser = atoi(argv[2]);
    char *hostPortFile = strdup(argv[3]);
    FILE *fp = fopen(hostPortFile, "r");
    if (fp == NULL) {
        printf("Error: Could not open file: %s\n", hostPortFile);
        exit(1);
    }
    while (!feof(fp)) {
        fscanf(fp, "%s %d", serversinfo[i].name, &serversinfo[i].port);
        i++;
    }
    if (i-- <= 0 || numUsers > 1000000)
        printUsage();
    printf("numUsers: %d, recPerUser: %d, hostPortFile: %s, #servers: %d\n",
            numUsers, recPerUser, hostPortFile, i);
    memc = memcached_create(NULL);
    for (j = 0; j < i; j++) {
        printf("%s %d\n", serversinfo[j].name, serversinfo[j].port);
        servers = memcached_server_list_append(servers, serversinfo[j].name, serversinfo[j].port, &rc);
    }
    rc = memcached_server_push(memc, servers);
    if (rc != MEMCACHED_SUCCESS) {
        fprintf(stderr, "Couldn't add server: %s\n", memcached_strerror(memc, rc));
        return 1;
    }
    rc = memcached_behavior_set(memc, MEMCACHED_BEHAVIOR_TCP_NODELAY, 1);
    assert(rc == MEMCACHED_SUCCESS);
    rc = memcached_behavior_set(memc, MEMCACHED_BEHAVIOR_NO_BLOCK, 1);
    assert(rc == MEMCACHED_SUCCESS);

    // Write/Set-Data in memcached server:
    int ts = 0;
    int len = 0;
    void *buf = NULL;
    char user[256];
    char article[256];

    // Allocate "ArticleReadRecord"s of size = recPerUser
    ArticleReadRecord **arr = NULL;
    arr = malloc (sizeof (ArticleReadRecord *) * recPerUser);
    for (i = 0; i < recPerUser; i++) {
        arr[i] = malloc (sizeof (ArticleReadRecord));
        article_read_record__init(arr[i]);
    }

    // Write Data
    i = 0; j = 0;
    while (j < numUsers) {
        ts = rand();
        if (i == 0) {
            sprintf(user, "u%d", j + 1);
            sprintf(article, "art-u%d-t%d", j + 1, ts);
        }
        if (i < recPerUser) {
            arr[i]->userid = user;
            arr[i]->timestamp = ts;
            arr[i]->has_timestamp = 1;
            arr[i]->articleid = article;
            i++;
            if (i != recPerUser)
                continue;
        }
        View msg = VIEW__INIT;
        msg.maxsize = recPerUser;
        msg.has_maxsize = 1;
        msg.has_oldestentry = 0;
        msg.n_entries = recPerUser;
        msg.entries = arr;
        len = view__get_packed_size(&msg);
        buf = malloc(len);
        view__pack(&msg, buf);
        rc = memcached_set(memc, user, strlen(user), buf, len, (time_t)0, (uint32_t)0);
        if (rc != MEMCACHED_SUCCESS) {
            fprintf(stderr, "Couldn't store key: %s\n", memcached_strerror(memc, rc));
            return 1;
        }
        free(buf);
        for (i = 0; i < recPerUser; i++)
            article_read_record__init(arr[i]);
        i = 0;
        j++;
    }

    return 0;
}
