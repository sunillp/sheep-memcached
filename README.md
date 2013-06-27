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
-	Redis being single-threaded does not scale up well with number of cores. We often need to run multiple instances of Redis on each server core. So each server core ends up doing “data aggregation” independently for the same query and each sends filtered data separately to client though all run on the same server.
-	As opposed to this Memcached being multi-threaded scales up well with number of cores. Each server performs “data aggregation” just once for a given query and sends very small set of filtered data to client.
-	Besides in Memcached there is one more option that is to keep data in de-serialized form, which saves lot of CPU cycles that are generally spent in de-serializing data in query processing critical path. (Though here memcached uses quite a lot of additional memory)
