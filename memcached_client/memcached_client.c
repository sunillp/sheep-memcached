/*
 *  memcached_client.c - a memcached client for testing multi-get performance
 *                       of memcached server. This is based on "libmemcached"
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
#include "cthreadpool/threadpool.h"

static void readPerf(void);
void getFriendsLatestActivity_post_processing(void *);
struct threadpool *pool = NULL;

memcached_st *memc = NULL;
size_t **keylen_arr = NULL;
char ***key_arr = NULL;
int *idtab = NULL;

int numUsers = 0;
int friendsPerUser = 0;
int numThreads = 0;
int numReads = 0;
int recPerUser = 0;
int keylenArrSize = 10000; // This number of unique READ queries containing no common
                           // friends (no key overlap) will be issued. This is to avoid
                           // possible optimzation performed by memcached client where it
                           // tries to merge multiple queries and issues READ only for
                           // unique keys (finds duplicate/overlapping keys and does not
                           // READ them multiple times)
char *hostPortFile = NULL;
int enableThreading = 0;

struct serverinfo {
    char name[50];
    int port;
} serversinfo[500];

void printUsage(void)
{
    printf("WRONG USAGE: memcached_client <#users - Max=1000000> <#friends-per-user - Max=100> <#records-per-user> <#threads> <#Reads - (1-200000)> <hostname-port-filename>\n");
    printf("Ex: memcached_client 1000000 100 10 4 50000 host-port.txt\n");
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
    char user[256];
    FILE *fp = NULL;

    if (argc != 7)
        printUsage();
    numUsers = atoi(argv[1]);
    friendsPerUser = atoi(argv[2]);
    recPerUser = atoi(argv[3]);
    numThreads = atoi(argv[4]);
    numReads = atoi(argv[5]);
    hostPortFile = strdup(argv[6]);
    fp = fopen(hostPortFile, "r");
    if (fp == NULL) {
        printf("Error: Could not open file: %s\n", hostPortFile);
        exit(1);
    }
    while (!feof(fp)) {
        fscanf(fp, "%s %d", serversinfo[i].name, &serversinfo[i].port);
        i++;
    }
    if (i-- <= 0 || numUsers > 1000000 || friendsPerUser > 100 || numReads > 200000)
        printUsage();
    printf("numUsers: %d, friendsPerUser: %d, recPerUser: %d, numThreads: %d, numReads: %d, hostPortFile: %s, #servers: %d\n",
            numUsers, friendsPerUser, recPerUser, numThreads, numReads, hostPortFile, i);
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

    key_arr = (char ***) malloc(sizeof (char **) * keylenArrSize);
    keylen_arr = (size_t **) malloc(sizeof (size_t *) * keylenArrSize);
    idtab = (int *) malloc(sizeof (int) * keylenArrSize);
    if (key_arr == NULL || keylen_arr == NULL || idtab == NULL) {
        printf("Error allocating key_arr or keylen_arr or idtab\n");
        exit(1);
    }
    for (i = 0; i < keylenArrSize; i++) {
        key_arr[i] = (char **) malloc(sizeof (char *) * friendsPerUser); 
        keylen_arr[i] = (size_t *) malloc(sizeof (size_t) * friendsPerUser);
        idtab[i] = i + 1;
        if (key_arr[i] == NULL || keylen_arr[i] == NULL) {
            printf("Error allocating key_arr[%d] or keylen_arr[%d]\n", i, i);
            exit(1);
        }
        for (j = 0; j < friendsPerUser; j++) {
            sprintf(user, "u%d", (i * friendsPerUser + j) % numUsers + 1);
            keylen_arr[i][j] = strlen(user);
        }
        for (j = 0; j < friendsPerUser; j++) {
            sprintf(user, "u%d", (i * friendsPerUser + j) % numUsers + 1);
            key_arr[i][j] = strdup(user);
            if (key_arr[i][j] == NULL) {
                printf("Error allocating key_arr[%d][%d]\n", i, j);
                exit(1);
            }
        }
    }

    printf("\nCalling readPerf with enableThreading = 1:\n");
    enableThreading = 1;
    readPerf();

    return 0;
}

static int cmpviews(const void *p1, const void *p2)
{
    ArticleReadRecord *arr1 = * (ArticleReadRecord **) p1;
    ArticleReadRecord *arr2 = * (ArticleReadRecord **) p2;

    return (arr1->timestamp - arr2->timestamp) * -1;
}

void readfilter(void **msglist, int len, void **buf, int *buflen, int s1, int s2)
{
    View **viewlist = (View **) msglist;
    ArticleReadRecord **arr = NULL;
    int i = 0, k = 0;
    int total_entries = 0;

    if (len == 0 || msglist == NULL)
        return;
    for (i = 0; i < len; i++)
        total_entries += viewlist[i]->n_entries;
    arr = (ArticleReadRecord **) malloc(sizeof (ArticleReadRecord *) * total_entries);
    if (arr == NULL) {}
    for (i = 0, k = 0; i < len; i++) {
        memcpy(arr + k, viewlist[i]->entries, sizeof (viewlist[i]->entries[0]) * viewlist[i]->n_entries);
        k += viewlist[i]->n_entries;
    }
    assert(k == total_entries);
    qsort(arr, k, sizeof (ArticleReadRecord *), cmpviews);
    k = k > recPerUser ? recPerUser : k;
    free(arr);

    return;
}

void getFriendsLatestActivity(void *userId)
{
    int *idp = (int *) userId;
    int id = *idp;
    memcached_return rc;
    memcached_st *clone = memcached_clone(NULL, memc);

    assert(clone);
    rc = memcached_mget(clone, (const char **) key_arr[id % keylenArrSize], keylen_arr[id % keylenArrSize], friendsPerUser);
    if (rc != MEMCACHED_SUCCESS)
        fprintf(stderr,"getFriendsLatestActivity error: %s\n", memcached_strerror(clone, rc));
    assert(rc == MEMCACHED_SUCCESS);
    if (enableThreading == 2) {
        threadpool_add_task(pool, getFriendsLatestActivity_post_processing, (void *) clone, 1);
        return;
    }
    getFriendsLatestActivity_post_processing((void *) clone);
  
    return;
}

void getFriendsLatestActivity_post_processing(void *ptr)
{   
    View **vlist = NULL;
    memcached_result_st *result;
    memcached_st *clone = (memcached_st *) ptr;
    memcached_return rc;
    int j = 0;
    int i = 0;

    vlist = (View **) malloc (sizeof (View *) * friendsPerUser);
    if (vlist == NULL) {
        printf("Error allocating vlist\n");
        exit(1);
    }
    while ((result = memcached_fetch_result(clone, NULL, &rc))) {
        assert(result);
        assert(rc == MEMCACHED_SUCCESS);
        View *msg = view__unpack(NULL, memcached_result_length(result), (void *) memcached_result_value(result));
        memcached_result_free(result);
        if (msg->n_entries != recPerUser && msg->n_entries != 10) {
            printf("Error: getFriendsLatestActivity_post_processing: msg->n_entries = %d, recPerUser = %d\n",
                        (int) msg->n_entries, recPerUser);
            exit(1);
        }
        vlist[j] = msg;
        j++;
    }
    // if (j != 1 && j != friendsPerUser) { // True for x0 for colocated & random data. True for x1/x2 for colocated data but not for random data (j=#servers contacted).
    //     printf("Error: getFriendsLatestActivity_post_processing: j = %d\n", j);
    //     exit(1);
    // }
    readfilter((void **) vlist, j, NULL, NULL, 0, 0);
    for (i = 0; i < j; i++)
        view__free_unpacked((View *) vlist[i], NULL);
    memcached_free(clone);
    free(vlist);

    return;
}

static void doRead(int loopCount, int startid)
{
    double eTd = 0;
    long estimatedTime;
    struct timeval tv1, tv2;
    double QueryRate = 0;
    int i = 0;
    int id = startid;

    if (enableThreading > 0 && (pool = threadpool_init(numThreads)) == NULL) {
        printf("Error! Failed to create a thread pool struct.\n");
        exit(EXIT_FAILURE);
    }
    gettimeofday(&tv1, NULL);
    while (i < loopCount) {
        id = (id % numUsers);
        id++;
        if (enableThreading == 0 || enableThreading == 2)
            getFriendsLatestActivity(&idtab[id % keylenArrSize]);
        else if (enableThreading == 1)
            threadpool_add_task(pool, getFriendsLatestActivity, &idtab[id % keylenArrSize], 1);
        i++;
    }
    if (enableThreading > 0) threadpool_free(pool, 1);
    gettimeofday(&tv2, NULL);
    estimatedTime = (tv2.tv_sec - tv1.tv_sec) * 1000000 + tv2.tv_usec - tv1.tv_usec;
    eTd = estimatedTime / 1000000.0;
    QueryRate = loopCount / eTd;
    printf("%6d\t\t%.4f\t%.4f\n", loopCount, eTd, QueryRate);
}

static void readPerf(void)
{
    printf("READ Performance:\n");
    printf("Number of queries serviced per second\n");
    printf("Number-of-Reads   Avg   QueryRate\n");
    
    if (numReads >= 1) doRead(1, 1);
    if (numReads >= 1) doRead(1, 2);
    if (numReads >= 2) doRead(2, 3);
    if (numReads >= 10) doRead(10, 10);
    if (numReads >= 100) doRead(100, 100);
    if (numReads >= 1000) doRead(1000, 1000);
    if (numReads >= 4000) doRead(4000, 4000);
    if (numReads >= 5000) doRead(5000, 9000);
    if (numReads >= 6000) doRead(6000, 15000);
    if (numReads >= 7000) doRead(7000, 22000);
    if (numReads >= 8000) doRead(8000, 30000);
    if (numReads >= 9000) doRead(9000, 39000);
    if (numReads >= 10000) doRead(10000, 49000);
    if (numReads >= 20000) doRead(20000, 60000);
    if (numReads >= 30000) doRead(30000, 90000);
    if (numReads >= 40000) doRead(40000, 130000);
    if (numReads >= 50000) doRead(50000, 180000);
    if (numReads >= 100000) doRead(100000, 240000);
    if (numReads >= 150000) doRead(150000, 350000);
    if (numReads >= 200000) doRead(200000, 500000);

    return;
}
