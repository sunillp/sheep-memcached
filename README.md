sheep-memcached
===============
SHEEP Enhancements in Memcached:

(Note: SHEEP is a paper published by a research team from Yahoo! Labs, Barcelona)

SHEEP is a distributed in-memory key-value store just like Memcached and Redis, optimized for multi-get queries, which are widely used in social networking systems. In multi-get queries, data associated with all the friends of a user is read and processed on the client. So a single “multi-get” query is issued to read data of all friends instead of sending a “get” query for each friend. This way if multiple friends data resides on a data storage server, then all this data (of multiple friends) can be retrieved in a single response. This saves sending multiple requests to a server and receiving/processing multiple responses from a server. If client is single threaded this also avoids RTT (round trip time) associated with multiple requests.

SHEEP improves performance of multi-get queries by employing two methods:

1.  Doing “data aggregation on server”. Normally in multi-get queries, huge amount of data is carried over from storage server to client and then this data is processed on client. Often a very small subset of the total-read data is returned as output (filtered data) to the user. In SHEEP, in addition to “data filtering on client” we also perform “data filtering on server”. Due to this very small amount of filtered data (only) is carried over from storage server to client. This drastically reduces network load.
2.	Data colocation. Sheep tries to store data associated with a user and its friends on the same storage server, so that only one server (or very few storage servers) is contacted for serving the request. In memcached/redis data is distributed randomly across multiple storage servers and in the worst case all storage servers are contacted for serving the request. Data colocation reduces number of network packets sent out and received. It also complements “Data aggregation on server” as lot of data (almost all) get filtered on server and very few data flows over network.

SHEEP advantages:

1.	Client consolidation/Cost Reduction (with same amount of hardware we can get higher performance or with less amount of hardware we can get same performance).
2.	Sustained high performance at peak loads. SHEEP is virtually unaffected by peak loads as it ensures that very less data flow over network from storage server to client thereby avoiding network saturation.

We have made enhancements proposed in SHEEP in memcached. This is done because of following reason:
-	Memcached is stable, popular, tried & tested and used in many mission critical applications. It supports almost all required features like persistence, replication, data re-placement policy, concurrency control (CAS), efficient use of memory etc.
-	Putting all these features of memcached in SHEEP would take lot of effort and time. So it was decided instead to put SHEEP enhancements in memcached.

How is this different or better than Redis + Lua:

“Data aggregation on server” that we have implemented in memcached is similar to executing Lua script on server in Redis. Our approach has following advantages over Redis:
-	Redis being single-threaded does not scale up well with number of cores. We often need to run multiple instances of Redis (one on each server core). So each server core ends up doing “data aggregation” independently for the same query and each sends filtered data separately to client though all run on the same server.
-	As opposed to this Memcached being multi-threaded scales up well with number of cores. Each server performs “data aggregation” just once for a given query and sends very small set of filtered data to client.
-	Besides in Memcached there is one more option that is to keep data in de-serialized form, which saves lot of CPU cycles that are generally spent in de-serializing data in query processing critical path. (Though here memcached uses quite a lot of additional memory)


More details about "changes in memcached", "overall design", "filter library design", "performance improvement", and "graph partitioning" can be found in the document "Sheep Enhancements in Memcached.docx" available in the repository.


BUILDING / TESTING:
===================

Here we have provided three things:
===================================

1. Modified memcached-1.4.13 with changes for doing data filtering at server for multi-get queries
   - BUILD as usual, "./configure" followed by "make" 

2. A sample filter library "libfilter" whose filtering functions are called for filtering multi-get queries at server. This library basically takes all records as input, sorts them and returns top 10 records as output.
   - BUILD - look at README file under libfilter dir. for buidling - This will produce two libraries,
     "libfilter.so" and "libfilter_variable_countlim.so"

3. A multi-threaded memcached client for reading and writing under "memcached_client" dir. This client is used to measure performance of memcached with/without our changes as mentioned below:
   - BUILD - first "cd cthreadpool" - perform the steps given in README under memcached_client/cthreadpool/README
           - "cd .." - perform the steps given in README under memcached_client/README - This will produce two binaries
              - memcached_client_write - writes data of users into memcached server
              - memcached_client - issues multiple async. multi-get queries in a multi-threaded way. Does same processing as done by filtering library for each multi-get query i.e. reads all records, sorts them and returns top 10 records as oupput. Uses time taken to complete/process multi-get queries to measure performance i.e. throughput and latency. Can issue multi-get queries for varying number of users, friends per user, records per user, threads, and servers.


To run/test:
============

Assuming we use single client (c1) and single server (s1):

Note: in these experiments (for option -x 1 and -x 2) we have changed "bget" to return only the filtered data for multi-get queries (with single key) and not all data of all keys that it does in normal cases. We have provided a new command "fget" which specifically does this and also takes "filter-library-name" and "parameters to filtering function" as input. For this we need to modify "libmemcached" (C client for memcacched) to support "fget" command. We are in process of doing this. Once that happens, "bget" will function as it is in normal cases for multi-get queries and "fget" will provide the new functionality for multi-get queries. For more info on "fget" command, see the document "Sheep Enhancements in Memcached.docx" available in the repository.

1. Testing performance of regular memcached (without our changes):
   - Start memcached server on (s1) as shown below.
     # ./memcached -M -m 4000
   - On client (c1), store server information in a file called "host-port.txt" in form "<server-name> <port-number>" as shown below:
     # cat host-port.txt
       s1 11211
     (Note: more servers can be added, one line per each server)
   - On client (c1) 
     # ./memcached_client_write 10000 10 host-port.txt
       numUsers: 10000, recPerUser: 10, hostPortFile: host-port.txt, #servers: 1
       s1 11211
     (This writes data for 10000 users/keys in memcached server. Each user's data/value has 10 records. "host-port.txt" contains server info. like "s1 11211")
     (Note: issue "./memcached_client_write" to see usage of this command and to try different parameters/values)
     Then issue following command to see the throughput:
     # ./memcached_client 10000 100 10 4 10000 host-port.txt
       numUsers: 10000, friendsPerUser: 100, recPerUser: 10, numThreads: 4, numReads: 10000, hostPortFile: host-port.txt, #servers: 1
       s1 11211
       < -- perf. result -- >
     (Above command issues async. multi-get queries for 10000 users, each user has 100 friends (i.e. each multi-get query requests data of 100 keys), there are 10 records per user/friend (means 1000 records are read/processed per multi-get query). 4 threads are used by client to issue multi-get queries and process the response asynchronously. 10000 multi-get asyn. multi-threaded queries are issued. "host-port.txt" contains server info. like "s1 11211")
     (Note: issue "./memcached_client" to see usage of this command and to try different parameters/values)
    
2. Testing performance of memcached with our changes (doing data filtering on server): First we pass 1 to option "-x", this means in this case, data is not kept in deserialized form on memcached server. Data is deserilized at the time of query processing.
   - On server (s1)
     # ./memcached -M -m 4000 -x 1 -y `pwd`  
     (Note: store filter library "libfilter.so" in the same directory on memcached server where "memcached" binary is stored and from where "memcached" is launched)         
   - On client (c1)
     # ./memcached_client_write 10000 10 host-port.txt
     # ./memcached_client 10000 100 10 4 10000 host-port.txt
       < -- perf. result -- > 

3. Testing performance of memcached with our changes (doing data filtering on server): Here we pass 2 to option "-x", this means in this case, data is kept in deserialized form on memcached server. Data is not deserilized at the time of query processing and this saves lot of CPU cycles that are spent in this process in critical query processing path. However in this case memcached server uses quite a lot of additional memory though performs very well)
   - On server (s1)
     # ./memcached -M -m 4000 -x 2 -y `pwd`  
     (Note: store filter library "libfilter.so" in the same directory on memcached server where "memcached" binary is stored and from where "memcached" is launched)         
   - On client (c1)
     # ./memcached_client_write 10000 10 host-port.txt
     # ./memcached_client 10000 100 10 4 10000 host-port.txt
       < -- perf. result -- > 
