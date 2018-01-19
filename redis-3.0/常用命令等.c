/*
/*

? 

APPEND  key value  

Append a value to a key

? 

AUTH  password  

Authenticate to the server

? 

BGREWRITEAOF  

Asynchronously rewrite the append-only file

? 

BGSAVE  

Asynchronously save the dataset to disk

? 

BITCOUNT  key [start end]  

Count set bits in a string

? 

BITOP  operation destkey key [key ...]  

Perform bitwise operations between strings

? 

BITPOS  key bit [start] [end]  

Find first bit set or clear in a string

? 

BLPOP  key [key ...] timeout  

Remove and get the first element in a list, or block until one is available

? 

BRPOP  key [key ...] timeout  

Remove and get the last element in a list, or block until one is available

? 

BRPOPLPUSH  source destination timeout  

Pop a value from a list, push it to another list and return it; or block until one is available

? 

CLIENT KILL  [ip:port] [ID client-id] [TYPE normal|slave|pubsub] [ADDR ip:port] [SKIPME yes/no]  

Kill the connection of a client

? 

CLIENT LIST  

Get the list of client connections

? 

CLIENT GETNAME  

Get the current connection name

? 

CLIENT PAUSE  timeout  

Stop processing commands from clients for some time

? 

CLIENT SETNAME  connection-name  

Set the current connection name

? 

CLUSTER ADDSLOTS  slot [slot ...]  

Assign new hash slots to receiving node

? 

CLUSTER COUNT-FAILURE-REPORTS  node-id  

Return the number of failure reports active for a given node

? 

CLUSTER COUNTKEYSINSLOT  slot  

Return the number of local keys in the specified hash slot

? 

CLUSTER DELSLOTS  slot [slot ...]  

Set hash slots as unbound in receiving node

? 

CLUSTER FAILOVER  [FORCE|TAKEOVER]  

Forces a slave to perform a manual failover of its master.

? 

CLUSTER FORGET  node-id  

Remove a node from the nodes table

? 

CLUSTER GETKEYSINSLOT  slot count  

Return local key names in the specified hash slot

? 

CLUSTER INFO  

Provides info about Redis Cluster node state

? 

CLUSTER KEYSLOT  key  

Returns the hash slot of the specified key

? 

CLUSTER MEET  ip port  

Force a node cluster to handshake with another node

? 

CLUSTER NODES  

Get Cluster config for the node

? 

CLUSTER REPLICATE  node-id  

Reconfigure a node as a slave of the specified master node

? 

CLUSTER RESET  [HARD|SOFT]  

Reset a Redis Cluster node

? 

CLUSTER SAVECONFIG  

Forces the node to save cluster state on disk

? 

CLUSTER SET-CONFIG-EPOCH  config-epoch  

Set the configuration epoch in a new node

? 

CLUSTER SETSLOT  slot IMPORTING|MIGRATING|STABLE|NODE [node-id]  

Bind an hash slot to a specific node

? 

CLUSTER SLAVES  node-id  

List slave nodes of the specified master node

? 

CLUSTER SLOTS  

Get array of Cluster slot to node mappings

? 

COMMAND  

Get array of Redis command details

? 

COMMAND COUNT  

Get total number of Redis commands

? 

COMMAND GETKEYS  

Extract keys given a full Redis command

? 

COMMAND INFO  command-name [command-name ...]  

Get array of specific Redis command details

? 

CONFIG GET  parameter  

Get the value of a configuration parameter

? 

CONFIG REWRITE  

Rewrite the configuration file with the in memory configuration

? 

CONFIG SET  parameter value  

Set a configuration parameter to the given value

? 

CONFIG RESETSTAT  

Reset the stats returned by INFO

? 

DBSIZE  

Return the number of keys in the selected database

? 

DEBUG OBJECT  key  

Get debugging information about a key

? 

DEBUG SEGFAULT  

Make the server crash

? 

DECR  key  

Decrement the integer value of a key by one

? 

DECRBY  key decrement  

Decrement the integer value of a key by the given number

? 

DEL  key [key ...]  

Delete a key

? 

DISCARD  

Discard all commands issued after MULTI

? 

DUMP  key  

Return a serialized version of the value stored at the specified key.

? 

ECHO  message  

Echo the given string

? 

EVAL  script numkeys key [key ...] arg [arg ...]  

Execute a Lua script server side

? 

EVALSHA  sha1 numkeys key [key ...] arg [arg ...]  

Execute a Lua script server side

? 

EXEC  

Execute all commands issued after MULTI

? 

EXISTS  key [key ...]  

Determine if a key exists

? 

EXPIRE  key seconds  

Set a key's time to live in seconds

? 

EXPIREAT  key timestamp  

Set the expiration for a key as a UNIX timestamp

? 

FLUSHALL  

Remove all keys from all databases

? 

FLUSHDB  

Remove all keys from the current database

? 

GEOADD  key longitude latitude member [longitude latitude member ...]  

Add one or more geospatial items in the geospatial index represented using a sorted set

? 

GEOHASH  key member [member ...]  

Returns members of a geospatial index as standard geohash strings

? 

GEOPOS  key member [member ...]  

Returns longitude and latitude of members of a geospatial index

? 

GEODIST  key member1 member2 [unit]  

Returns the distance between two members of a geospatial index

? 

GEORADIUS  key longitude latitude radius m|km|ft|mi [WITHCOORD] [WITHDIST] [WITHHASH] [COUNT count]  

Query a sorted set representing a geospatial index to fetch members matching a given maximum distance from a point

? 

GEORADIUSBYMEMBER  key member radius m|km|ft|mi [WITHCOORD] [WITHDIST] [WITHHASH] [COUNT count]  

Query a sorted set representing a geospatial index to fetch members matching a given maximum distance from a member

? 

GET  key  

Get the value of a key

? 

GETBIT  key offset  

Returns the bit value at offset in the string value stored at key

? 

GETRANGE  key start end  

Get a substring of the string stored at a key

? 

GETSET  key value  

Set the string value of a key and return its old value

? 

HDEL  key field [field ...]  

Delete one or more hash fields

? 

HEXISTS  key field  

Determine if a hash field exists

? 

HGET  key field  

Get the value of a hash field

? 

HGETALL  key  

Get all the fields and values in a hash

? 

HINCRBY  key field increment  

Increment the integer value of a hash field by the given number

? 

HINCRBYFLOAT  key field increment  

Increment the float value of a hash field by the given amount

? 

HKEYS  key  

Get all the fields in a hash

? 

HLEN  key  

Get the number of fields in a hash

? 

HMGET  key field [field ...]  

Get the values of all the given hash fields

? 

HMSET  key field value [field value ...]  

Set multiple hash fields to multiple values

? 

HSET  key field value  

Set the string value of a hash field

? 

HSETNX  key field value  

Set the value of a hash field, only if the field does not exist

? 

HSTRLEN  key field  

Get the length of the value of a hash field

? 

HVALS  key  

Get all the values in a hash

? 

INCR  key  

Increment the integer value of a key by one

? 

INCRBY  key increment  

Increment the integer value of a key by the given amount

? 

INCRBYFLOAT  key increment  

Increment the float value of a key by the given amount

? 

INFO  [section]  

Get information and statistics about the server

? 

KEYS  pattern  

Find all keys matching the given pattern

? 

LASTSAVE  

Get the UNIX time stamp of the last successful save to disk

? 

LINDEX  key index  

Get an element from a list by its index

? 

LINSERT  key BEFORE|AFTER pivot value  

Insert an element before or after another element in a list

? 

LLEN  key  

Get the length of a list

? 

LPOP  key  

Remove and get the first element in a list

? 

LPUSH  key value [value ...]  

Prepend one or multiple values to a list

? 

LPUSHX  key value  

Prepend a value to a list, only if the list exists

? 

LRANGE  key start stop  

Get a range of elements from a list

? 

LREM  key count value  

Remove elements from a list

? 

LSET  key index value  

Set the value of an element in a list by its index

? 

LTRIM  key start stop  

Trim a list to the specified range

? 

MGET  key [key ...]  

Get the values of all the given keys

? 

MIGRATE  host port key destination-db timeout [COPY] [REPLACE]  

Atomically transfer a key from a Redis instance to another one.

? 

MONITOR  

Listen for all requests received by the server in real time

? 

MOVE  key db  

Move a key to another database

? 

MSET  key value [key value ...]  

Set multiple keys to multiple values

? 

MSETNX  key value [key value ...]  

Set multiple keys to multiple values, only if none of the keys exist

? 

MULTI  

Mark the start of a transaction block

? 

OBJECT  subcommand [arguments [arguments ...]]  

Inspect the internals of Redis objects

? 

PERSIST  key  

Remove the expiration from a key

? 

PEXPIRE  key milliseconds  

Set a key's time to live in milliseconds

? 

PEXPIREAT  key milliseconds-timestamp  

Set the expiration for a key as a UNIX timestamp specified in milliseconds

? 

PFADD  key element [element ...]  

Adds the specified elements to the specified HyperLogLog.

? 

PFCOUNT  key [key ...]  

Return the approximated cardinality of the set(s) observed by the HyperLogLog at key(s).

? 

PFMERGE  destkey sourcekey [sourcekey ...]  

Merge N different HyperLogLogs into a single one.

? 

PING  

Ping the server

? 

PSETEX  key milliseconds value  

Set the value and expiration in milliseconds of a key

? 

PSUBSCRIBE  pattern [pattern ...]  

Listen for messages published to channels matching the given patterns

? 

PUBSUB  subcommand [argument [argument ...]]  

Inspect the state of the Pub/Sub subsystem

? 

PTTL  key  

Get the time to live for a key in milliseconds

? 

PUBLISH  channel message  

Post a message to a channel

? 

PUNSUBSCRIBE  [pattern [pattern ...]]  

Stop listening for messages posted to channels matching the given patterns

? 

QUIT  

Close the connection

? 

RANDOMKEY  

Return a random key from the keyspace

? 

READONLY  

Enables read queries for a connection to a cluster slave node

? 

READWRITE  

Disables read queries for a connection to a cluster slave node

? 

RENAME  key newkey  

Rename a key

? 

RENAMENX  key newkey  

Rename a key, only if the new key does not exist

? 

RESTORE  key ttl serialized-value [REPLACE]  

Create a key using the provided serialized value, previously obtained using DUMP.

? 

ROLE  

Return the role of the instance in the context of replication

? 

RPOP  key  

Remove and get the last element in a list

? 

RPOPLPUSH  source destination  

Remove the last element in a list, prepend it to another list and return it

? 

RPUSH  key value [value ...]  

Append one or multiple values to a list

? 

RPUSHX  key value  

Append a value to a list, only if the list exists

? 

SADD  key member [member ...]  

Add one or more members to a set

? 

SAVE  

Synchronously save the dataset to disk

? 

SCARD  key  

Get the number of members in a set

? 

SCRIPT EXISTS  script [script ...]  

Check existence of scripts in the script cache.

? 

SCRIPT FLUSH  

Remove all the scripts from the script cache.

? 

SCRIPT KILL  

Kill the script currently in execution.

? 

SCRIPT LOAD  script  

Load the specified Lua script into the script cache.

? 

SDIFF  key [key ...]  

Subtract multiple sets

? 

SDIFFSTORE  destination key [key ...]  

Subtract multiple sets and store the resulting set in a key

? 

SELECT  index  

Change the selected database for the current connection

? 

SET  key value [EX seconds] [PX milliseconds] [NX|XX]  

Set the string value of a key

? 

SETBIT  key offset value  

Sets or clears the bit at offset in the string value stored at key

? 

SETEX  key seconds value  

Set the value and expiration of a key

? 

SETNX  key value  

Set the value of a key, only if the key does not exist

? 

SETRANGE  key offset value  

Overwrite part of a string at key starting at the specified offset

? 

SHUTDOWN  [NOSAVE] [SAVE]  

Synchronously save the dataset to disk and then shut down the server

? 

SINTER  key [key ...]  

Intersect multiple sets

? 

SINTERSTORE  destination key [key ...]  

Intersect multiple sets and store the resulting set in a key

? 

SISMEMBER  key member  

Determine if a given value is a member of a set

? 

SLAVEOF  host port  

Make the server a slave of another instance, or promote it as master

? 

SLOWLOG  subcommand [argument]  

Manages the Redis slow queries log

? 

SMEMBERS  key  

Get all the members in a set

? 

SMOVE  source destination member  

Move a member from one set to another

? 

SORT  key [BY pattern] [LIMIT offset count] [GET pattern [GET pattern ...]] [ASC|DESC] [ALPHA] [STORE destination]  

Sort the elements in a list, set or sorted set

? 

SPOP  key [count]  

Remove and return one or multiple random members from a set

? 

SRANDMEMBER  key [count]  

Get one or multiple random members from a set

? 

SREM  key member [member ...]  

Remove one or more members from a set

? 

STRLEN  key  

Get the length of the value stored in a key

? 

SUBSCRIBE  channel [channel ...]  

Listen for messages published to the given channels

? 

SUNION  key [key ...]  

Add multiple sets

? 

SUNIONSTORE  destination key [key ...]  

Add multiple sets and store the resulting set in a key

? 

SYNC  

Internal command used for replication

? 

TIME  

Return the current server time

? 

TTL  key  

Get the time to live for a key

? 

TYPE  key  

Determine the type stored at key

? 

UNSUBSCRIBE  [channel [channel ...]]  

Stop listening for messages posted to the given channels

? 

UNWATCH  

Forget about all watched keys

? 

WAIT  numslaves timeout  

Wait for the synchronous replication of all the write commands sent in the context of the current connection

? 

WATCH  key [key ...]  

Watch the given keys to determine execution of the MULTI/EXEC block

? 

ZADD  key [NX|XX] [CH] [INCR] score member [score member ...]  

Add one or more members to a sorted set, or update its score if it already exists

? 

ZCARD  key  

Get the number of members in a sorted set

? 

ZCOUNT  key min max  

Count the members in a sorted set with scores within the given values

? 

ZINCRBY  key increment member  

Increment the score of a member in a sorted set

? 

ZINTERSTORE  destination numkeys key [key ...] [WEIGHTS weight [weight ...]] [AGGREGATE SUM|MIN|MAX]  

Intersect multiple sorted sets and store the resulting sorted set in a new key

? 

ZLEXCOUNT  key min max  

Count the number of members in a sorted set between a given lexicographical range

? 

ZRANGE  key start stop [WITHSCORES]  

Return a range of members in a sorted set, by index

? 

ZRANGEBYLEX  key min max [LIMIT offset count]  

Return a range of members in a sorted set, by lexicographical range

? 

ZREVRANGEBYLEX  key max min [LIMIT offset count]  

Return a range of members in a sorted set, by lexicographical range, ordered from higher to lower strings.

? 

ZRANGEBYSCORE  key min max [WITHSCORES] [LIMIT offset count]  

Return a range of members in a sorted set, by score

? 

ZRANK  key member  

Determine the index of a member in a sorted set

? 

ZREM  key member [member ...]  

Remove one or more members from a sorted set

? 

ZREMRANGEBYLEX  key min max  

Remove all members in a sorted set between the given lexicographical range

? 

ZREMRANGEBYRANK  key start stop  

Remove all members in a sorted set within the given indexes

? 

ZREMRANGEBYSCORE  key min max  

Remove all members in a sorted set within the given scores

? 

ZREVRANGE  key start stop [WITHSCORES]  

Return a range of members in a sorted set, by index, with scores ordered from high to low

? 

ZREVRANGEBYSCORE  key max min [WITHSCORES] [LIMIT offset count]  

Return a range of members in a sorted set, by score, with scores ordered from high to low

? 

ZREVRANK  key member  

Determine the index of a member in a sorted set, with scores ordered from high to low

? 

ZSCORE  key member  

Get the score associated with the given member in a sorted set

? 

ZUNIONSTORE  destination numkeys key [key ...] [WEIGHTS weight [weight ...]] [AGGREGATE SUM|MIN|MAX]  

Add multiple sorted sets and store the resulting sorted set in a new key

? 

SCAN  cursor [MATCH pattern] [COUNT count]  

Incrementally iterate the keys space

? 

SSCAN  key cursor [MATCH pattern] [COUNT count]  

Incrementally iterate Set elements

? 

HSCAN  key cursor [MATCH pattern] [COUNT count]  

Incrementally iterate hash fields and associated values

? 

ZSCAN  key cursor [MATCH pattern] [COUNT count]  

Incrementally iterate sorted sets elements and associated scores





















Redis�����ȫ 


?������Ҫ���� �ַ�������
?�б�����ͼ�������
?ɢ����������򼯺�����
?���������붩������
?�������� 

3.1 �ַ���

�����ڵ�1�º͵�2������˵����Redis���ַ�������һ�����ֽ���ɵ����У����Ǻͺܶ�������������ַ���û��ʲô�����Ĳ�ͬ����C����C++�����ַ�����Ҳ��ȥ��Զ��
��Redis���棬�ַ������Դ洢����3�����͵�ֵ��

�ֽڴ���byte string����
������
��������

�û�����ͨ������һ���������ֵ���Դ洢���������߸��������ַ���ִ��������increment�������Լ���decrement��������������Ҫ��ʱ��Redis���Ὣ����ת���ɸ�������
������ȡֵ��Χ��ϵͳ�ĳ�������long integer����ȡֵ��Χ��ͬ����32λϵͳ�ϣ���������32λ�з�����������64λϵͳ�ϣ���������64λ�з���������������������ȡ
ֵ��Χ�;�������IEEE 754��׼��˫���ȸ�������double����ͬ��Redis��ȷ�������ֽڴ��������͸�������������һ�����ƣ�����ֻ�ܹ��洢�ֽڴ���������Redis����
�������ݱ��ַ�����и��������ԡ�

���ڽ���Redis������򵥵Ľṹ���ַ����������ۣ����ܻ�������ֵ�������Լ��������Լ�������λ��bit�����Ӵ���substring������������߿��ܻᾪ�ȵط��֣�Redis��
����򵥵Ľṹ��ȻҲ�����ǿ������á�

����				����������
INCR				INCR key-name�������洢��ֵ����1
DECR   				DECR key-name�������洢��ֵ��ȥ1
INCRBY   			INCRBY key-name amount�������洢��ֵ��������amount
DECRBY			    DECRBY key-name amount�������洢��ֵ��ȥ����amount
INCRBYFLOAT			INCRBYFLOAT key-name amount�������洢��ֵ���ϸ�����amount�����������Redis 2.6�����ϵİ汾����
 

���û���һ��ֵ�洢��Redis�ַ��������ʱ��������ֵ���Ա����ͣ�interpret��Ϊʮ�����������߸���������ôRedis��������һ�㣬�������û�������ַ���
ִ�и���INCR��DECR����������û���һ�������ڵļ�����һ�������˿մ��ļ�ִ�����������Լ���������ôRedis��ִ�в���ʱ�Ὣ�������ֵ������0�����������
�����Զ�һ��ֵ�޷�������Ϊ�������߸��������ַ�����ִ�����������Լ���������ôRedis�����û�����һ������


�ڶ��걾�������½�֮�󣬶��߿��ܻᷢ�ֱ���ֻ������ incr()��������Ϊ Python��Redis�����ڲ�ʹ��INCRBY������ʵ��incr()������������������ĵڶ��������ǿ�ѡ�ģ�
����û�û��Ϊ�����ѡ��������ֵ����ô��������ͻ�ʹ��Ĭ��ֵ1���ڱ�д�����ʱ��Python��Redis�ͻ��˿�֧��Redis 2.6��������������ͨ��incrbyfloat()
������ʵ��INCRBYFLOAT�������incrbyfloat()����Ҳ��������incr()�����Ŀ�ѡ�������ԡ�

���������������Լ�����֮�⣬Redis��ӵ�ж��ֽڴ�������һ�������ݽ��ж�ȡ����д��Ĳ�������Щ����Ҳ���������������߸��������������÷�����������������
�ڵ�9�½�չʾ���ʹ����Щ��������Ч�ؽ��ṹ�����ݴ����pack���洢���ַ��������档��3-2չʾ�����������ַ����Ӵ��Ͷ�����λ�����

��Redis�����Ӵ��Ͷ�����λ������

����    ����������

APPEND		APPEND key-name value����ֵvalue׷�ӵ�������key-name��ǰ�洢��ֵ��ĩβ
GETRANGE	GETRANGE key-name start end����ȡһ����ƫ����start��ƫ����end��Χ�������ַ���ɵ��Ӵ�������start��end����
SETRANGE	SETRANGE key-name offset value������startƫ������ʼ���Ӵ�����Ϊ����ֵ
GETBIT		GETBIT key-name offset�����ֽڴ������Ƕ�����λ����bit string����������λ����ƫ����Ϊoffset�Ķ�����λ��ֵ
SETBIT		SETBIT key-name offset value�����ֽڴ������Ƕ�����λ��������λ����ƫ����Ϊoffset�Ķ�����λ��ֵ����Ϊvalue
BITCOUNT	BITCOUNT key-name [start end]��ͳ�ƶ�����λ������ֵΪ1�Ķ�����λ����������������˿�ѡ��startƫ������endƫ��������ôֻ��ƫ����ָ����Χ�ڵĶ�����λ����ͳ��
BITOP		BITOP operation dest-key key-name [key-name ...]����һ������������λ��ִ�а�������AND������OR�������XOR�����ǣ�NOT�����ڵ�����һ�ְ�λ���������bitwise operation����
			��������ó��Ľ��������dest-key������
 

GETRANGE��SUBSTR Redis���ڵ�GETRANGE����������ǰ��SUBSTR������������ģ���ˣ�Python�ͻ���������Ȼ����ʹ��substr()��������ȡ�Ӵ������������ʹ�õ���2.6�����ϰ汾��Redis��
��ô��û���ʹ��getrange()��������ȡ�Ӵ���

��ʹ��SETRANGE����SETBIT������ַ�������д���ʱ������ַ�����ǰ�ĳ��Ȳ�������д���Ҫ����ôRedis���Զ���ʹ�ÿ��ֽڣ�null�������ַ�����չ������ĳ��ȣ�Ȼ���ִ��
д����߸��²�������ʹ��GETRANGE��ȡ�ַ�����ʱ�򣬳����ַ���ĩβ�����ݻᱻ��Ϊ�ǿմ�������ʹ��GETBIT��ȡ������λ����ʱ�򣬳����ַ���ĩβ�Ķ�����λ�ᱻ��Ϊ��0������

�ܶ��ֵ���ݿ�ֻ�ܽ����ݴ洢Ϊ��ͨ���ַ��������Ҳ��ṩ�κ��ַ��������������һЩ��ֵ���ݿ������û����ֽ�׷�ӵ��ַ�����ǰ����ߺ��棬����ȴû�취��Redisһ��
���ַ������Ӵ����ж�д���Ӻܶ෽����������ʹRedisֻ֧���ַ����ṹ������ֻ֧�ֱ����г����ַ����������RedisҲ�Ⱥܶ������ݿ�Ҫǿ��öࣻͨ��ʹ���Ӵ�������
������λ���������WATCH���MULTI�����EXEC��������3.7.2�ڽ�����3��������г����Ľ��ܣ����ڵ�4�¶����ǽ��и�����Ľ��⣩���û����������Լ�����ȥ������
��������Ҫ�����ݽṹ����9�½��������ʹ���ַ���ȥ�洢һ�ּ򵥵�ӳ�䣬����ӳ�������ĳЩ����½�ʡ�����ڴ档

ֻҪ��Щ��˼�������������Խ��ַ��������б���ʹ�ã������������ܹ�ִ�е��б���������࣬���õİ취��ֱ��ʹ����һ�ڽ��ܵ��б�ṹ��RedisΪ���ֽṹ�ṩ�˷ḻ���б�������

3.2 �б�

�ڵ�1���������ܹ���Redis���б������û������е�����������ߵ���Ԫ�أ���ȡ�б�Ԫ�أ��Լ�ִ�и��ֳ������б����������֮�⣬�б����������洢������Ϣ���������������»��߳�����ϵ����Ϣ��

���ڽ����б�����ɶ���ַ���ֵ��ɵ��������нṹ���н��ܣ���չʾһЩ��õ��б�������Ķ����ڿ����ö���ѧ�����ʹ����Щ�����������б���3-3չʾ������һ������õ��б����

һЩ���õ��б�����
����			����������

RPUSH
RPUSH key-name value [value ...]����һ������ֵ�����б���Ҷ�
 
LPUSH
LPUSH key-name value [value ...]����һ������ֵ�����б�����

RPOP
RPOP key-name���Ƴ��������б����Ҷ˵�Ԫ��
 
LPOP
LPOP key-name���Ƴ��������б�����˵�Ԫ��
 
LINDEX
LINDEX key-name offset�������б���ƫ����Ϊoffset��Ԫ��
 

LRANGE
LRANGE key-name start end�������б��startƫ������endƫ������Χ�ڵ�����Ԫ�أ�����ƫ����Ϊstart��ƫ����Ϊend��Ԫ��Ҳ������ڱ����ص�Ԫ��֮��
 
LTRIM
LTRIM key-name start end�����б�����޼���ֻ������startƫ������endƫ������Χ�ڵ�Ԫ�أ�����ƫ����Ϊstart��ƫ����Ϊend��Ԫ��Ҳ�ᱻ����
 

 
����ʽ���б��������Լ����б�֮���ƶ�Ԫ�ص�����
����		����������
 


BLPOP
BLPOP key-name [key-name ...] timeout���ӵ�һ���ǿ��б��е���λ������˵�Ԫ�أ�������timeout��֮���������ȴ��ɵ�����Ԫ�س���
 
BRPOP
BRPOP key-name [key-name ...] timeout���ӵ�һ���ǿ��б��е���λ�����Ҷ˵�Ԫ�أ�������timeout��֮���������ȴ��ɵ�����Ԫ�س���
 
RPOPLPUSH
RPOPLPUSH source-key dest-key����source-key�б��е���λ�����Ҷ˵�Ԫ�أ�Ȼ�����Ԫ������dest-key�б������ˣ������û��������Ԫ��
 
BRPOPLPUSH
BRPOPLPUSH source-key dest-key timeout����source-key�б��е���λ�����Ҷ˵�Ԫ�أ�Ȼ�����Ԫ������dest-key�б������ˣ������û��������Ԫ�أ����source-keyΪ�գ���ô��timeout��֮���������ȴ��ɵ�����Ԫ�س���
 
����������������͵���������������������������Ϣ���ݣ�messaging����������У�task queue�������齫�ڵ�6�¶�������������н��ܡ�

��ϰ��ͨ���б��������ڴ�ռ�� ��2.1�ں�2.5���У�����ʹ�������򼯺�����¼�û�������������Ʒ�������û������Щ��Ʒʱ��ʱ�������Ϊ��ֵ��
�Ӷ�ʹ�ó������������ɻỰ�Ĺ����л���ִ���깺�����֮�󣬽�����Ӧ�����ݷ����������ڱ���ʱ�����Ҫռ����Ӧ�Ŀռ䣬�����������������
����Ҫ�õ�ʱ����Ļ�����ô��û�б�Ҫʹ�����򼯺��������û�������������Ʒ�ˡ�Ϊ�ˣ����ڱ�֤���岻�������£���update_token()��������
ʹ�õ����򼯺��滻���б���ʾ����������ڽ���������ʱ�������ѵĻ������Ե�6.1.1����������С�

�б��һ����Ҫ�ŵ����������԰�������ַ���ֵ����ʹ���û����Խ����ݼ�����ͬһ���ط���Redis�ļ���Ҳ�ṩ�����б����Ƶ����ԣ�������ֻ�ܱ��������ͬ
��Ԫ�ء���������һ���о����������������ܱ�����ͬԪ�صļ��϶�����Щʲô��

3.3 ����

Redis�ļ���������ķ�ʽ���洢���������ͬ��Ԫ�أ��û����Կ��ٵضԼ���ִ�����Ԫ�ز������Ƴ�Ԫ�ز����Լ����һ��Ԫ���Ƿ�����ڼ����

���ڽ�����õļ���������н��ܣ�������������Ƴ������Ԫ�ش�һ�������ƶ�����һ�����ϵ�����Լ��Զ������ִ�н������㡢
��������Ͳ���������Ķ�����Ҳ�����ڶ��߸��õ���Ȿ����

һЩ���õļ�������

����			����������
 
SADD
SADD key-name item [item ...]����һ������Ԫ����ӵ��������棬�����ر����Ԫ�ص���ԭ�����������ڼ��������Ԫ������
 
SREM
SREM key-name item [item ...]���Ӽ��������Ƴ�һ������Ԫ�أ������ر��Ƴ�Ԫ�ص�����
 
SISMEMBER
SISMEMBER key-name item�����Ԫ��item�Ƿ�����ڼ���key-name ��
 
SCARD
SCARD key-name�����ؼ��ϰ�����Ԫ�ص�����

SMEMBERS
SMEMBERS key-name�����ؼ��ϰ���������Ԫ��
 
SRANDMEMBER
SRANDMEMBER key-name [count]���Ӽ�����������ط���һ������Ԫ�ء���countΪ����ʱ������ص����Ԫ�ز����ظ�����countΪ����ʱ������ص����Ԫ�ؿ��ܻ�����ظ�

SPOP
SPOP key-name��������Ƴ������е�һ��Ԫ�أ������ر��Ƴ���Ԫ��
 
SMOVE
SMOVE source-key dest-key item���������source-key����Ԫ��item����ô�Ӽ���source-key�����Ƴ�Ԫ��item������Ԫ��item��ӵ�����dest-key�У����item���ɹ��Ƴ�����ô�����1�����򷵻�0
 

������Ϻʹ��������ϵ�Redis����



����			����������
 
SDIFF
SDIFF key-name [key-name ...]��������Щ�����ڵ�һ�����ϡ��������������������е�Ԫ�أ���ѧ�ϵĲ���㣩
 
SDIFFSTORE
SDIFFSTORE dest-key key-name [key-name ...]������Щ�����ڵ�һ�����ϵ��������������������е�Ԫ�أ���ѧ�ϵĲ���㣩�洢��dest-key������
 
SINTER
SINTER key-name [key-name ...]��������Щͬʱ���������м����е�Ԫ�أ���ѧ�ϵĽ������㣩
 
SINTERSTORE
SINTERSTORE dest-key key-name [key-name ...]������Щͬʱ���������м��ϵ�Ԫ�أ���ѧ�ϵĽ������㣩�洢��dest-key������
 
SUNION
SUNION key-name [key-name ...]��������Щ���ٴ�����һ�������е�Ԫ�أ���ѧ�ϵĲ������㣩
 
SUNIONSTORE
SUNIONSTORE dest-key key-name [key-name ...]������Щ���ٴ�����һ�������е�Ԫ�أ���ѧ�ϵĲ������㣩�洢��dest-key������
 

��Щ����ֱ��ǲ������㡢��������Ͳ������3���������ϲ����ġ����ؽ�����汾�͡��洢������汾

3.4 ɢ��

��1���ᵽ����Redis��ɢ�п������û��������ֵ�Դ洢��һ��Redis�����档�ӹ�������˵��RedisΪɢ��ֵ�ṩ��һЩ���ַ���ֵ��ͬ�����ԣ�ʹ��ɢ�зǳ�������
��һЩ��ص����ݴ洢��һ�����ǿ��԰��������ݾۼ������ǹ�ϵ���ݿ��е��У������ĵ����ݿ��е��ĵ���

���ڽ�����õ�ɢ��������н��ܣ����а�����Ӻ�ɾ����ֵ�Ե������ȡ���м�ֵ�Ե�����Լ��Լ�ֵ�Ե�ֵ�������������Լ�����������Ķ���һ�ڿ�����
����ѧϰ����ν����ݴ洢��ɢ�����棬�Լ��������ĺô���ʲô����3-7չʾ��һ���ֳ��õ�ɢ�����

������Ӻ�ɾ����ֵ�Ե�ɢ�в���

����			����������
 


HMGET
HMGET key-name key [key ...]����ɢ�������ȡһ����������ֵ

HMSET
HMSET key-name key value [key value ...]��Ϊɢ�������һ������������ֵ
 
HDEL
HDEL key-name key [key ...]��ɾ��ɢ�������һ��������ֵ�ԣ����سɹ��ҵ���ɾ���ļ�ֵ������
 
HLEN
HLEN key-name������ɢ�а����ļ�ֵ������
 

Redisɢ�еĸ��߼�����



����				����������
 
HEXISTS
HEXISTS key-name key�����������Ƿ������ɢ����

HKEYS
HKEYS key-name����ȡɢ�а��������м�
 
HVALS
HVALS key-name����ȡɢ�а���������ֵ
 
HGETALL
HGETALL key-name����ȡɢ�а��������м�ֵ��
 
HINCRBY
HINCRBY key-name key increment������key�����ֵ��������increment
 
HINCRBYFLOAT
HINCRBYFLOAT key-name key increment������key�����ֵ���ϸ�����increment
 

������HGETALL���ڣ���HKEYS��HVALUESҲ�Ƿǳ����õģ����ɢ�а�����ֵ�ǳ�����ô�û�������ʹ��HKEYSȡ��ɢ�а��������м���Ȼ����ʹ��HGETһ��
��һ����ȡ������ֵ���Ӷ�������Ϊһ�λ�ȡ����������ֵ�����·�����������

HINCRBY��HINCRBYFLOAT���ܻ��ö��߻��������ڴ����ַ�����INCRBY��INCRBYFLOAT������������ӵ����ͬ�����壬���ǵĲ�ͬ����HINCRBY��HINCRBYFLOAT����
����ɢ�У��������ַ����������嵥3-8չʾ����Щ�����ʹ�÷�����

����ǰ����˵���ڶ�ɢ�н��д����ʱ�������ֵ�Ե�ֵ������ǳ��Ӵ���ô�û�������ʹ��HKEYS��ȡɢ�е����м���Ȼ��ͨ��ֻ��ȡ��Ҫ��ֵ��������Ҫ
�����������������֮�⣬�û���������ʹ��SISMEMBER���һ��Ԫ���Ƿ�����ڼ�������һ����ʹ��HEXISTS���һ�����Ƿ������ɢ�����档�����1��Ҳ�õ�
�˱��ڸոջع˹���HINCRBY����¼���±�ͶƱ�Ĵ�����

�ڽ�������һ���У�����Ҫ�˽����֮����½�����ᾭ���õ������򼯺Ͻṹ��

3.5 ���򼯺�

��ɢ�д洢�ż���ֵ֮���ӳ�����ƣ����򼯺�Ҳ�洢�ų�Ա���ֵ֮���ӳ�䣬�����ṩ�˷�ֵ��������Լ����ݷ�ֵ��С����ػ�ȡ��fetch����ɨ�裨scan����
Ա�ͷ�ֵ������������ڵ�1��ʹ�����򼯺�ʵ�ֹ����ڷ���ʱ������������б�ͻ���ͶƱ��������������б����ڵ�2��ʹ�����򼯺ϴ洢��cookie�Ĺ���ʱ�䡣

���ڽ��Բ������򼯺ϵ�������н��ܣ����а��������򼯺������Ԫ�ص������������Ԫ�ص�����Լ������򼯺Ͻ��н�������Ͳ������������Ķ����ڿ�
�Լ�����߶����򼯺ϵ���ʶ���Ӷ��������߸��õ���Ȿ���ڵ�1�¡���5�¡���6�º͵�7��չʾ�����򼯺�ʾ����


һЩ���õ����򼯺�����
����				����������

ZADD
ZADD key-name score member [score member ...]�������и�����ֵ�ĳ�Ա��ӵ����򼯺�����
 
ZREM
ZREM key-name member [member ...]�������򼯺������Ƴ������ĳ�Ա�������ر��Ƴ���Ա������
 
ZCARD
ZCARD key-name���������򼯺ϰ����ĳ�Ա����
 
ZINCRBY
ZINCRBY key-name increment member����member��Ա�ķ�ֵ����increment
 
ZCOUNT
ZCOUNT key-name min max�����ط�ֵ����min��max֮��ĳ�Ա����
 
ZRANK
ZRANK key-name member�����س�Աmember�����򼯺��е�����
 
ZSCORE
ZSCORE key-name member�����س�Աmember�ķ�ֵ

ZRANGE
ZRANGE key-name start stop [WITHSCORES]���������򼯺�����������start��stop֮��ĳ�Ա����������˿�ѡ��WITHSCORESѡ���ô����Ὣ��Ա�ķ�ֵҲһ������
 


���򼯺ϵķ�Χ�����ݻ�ȡ����ͷ�Χ������ɾ������Լ���������ͽ�������

����			����������

ZREVRANK
ZREVRANK key-name member���������򼯺����Աmember������λ�ã���Ա���շ�ֵ�Ӵ�С����
 
ZREVRANGE
ZREVRANGE key-name start stop [WITHSCORES]���������򼯺ϸ���������Χ�ڵĳ�Ա����Ա���շ�ֵ�Ӵ�С����
 
ZRANGEBYSCORE
ZRANGEBYSCORE key min max [WITHSCORES] [LIMIT offset count]���������򼯺��У���ֵ����min��max֮������г�Ա
 
ZREVRANGEBYSCORE
ZREVRANGEBYSCORE key max min [WITHSCORES] [LIMIT offset count]����ȡ���򼯺��з�ֵ����min��max֮������г�Ա�������շ�ֵ�Ӵ�С��˳������������
 
ZREMRANGEBYRANK
ZREMRANGEBYRANK key-name start stop���Ƴ����򼯺�����������start��stop֮������г�Ա
 
ZREMRANGEBYSCORE
ZREMRANGEBYSCORE key-name min max���Ƴ����򼯺��з�ֵ����min��max֮������г�Ա
 
ZINTERSTORE
ZINTERSTORE dest-key key-count key [key ...] [WEIGHTS weight [weight ...]] [AGGREGATE SUM�����������ߣ�MIN�����������ߣ�MAX]���Ը��������򼯺�ִ�������ڼ��ϵĽ�������

ZUNIONSTORE
ZUNIONSTORE dest-key key-count key [key ...] [WEIGHTS weight [weight ...]] [AGGREGATE SUM�����������ߣ�IN�����������ߣ�MAX]���Ը��������򼯺�ִ�������ڼ��ϵĲ�������
 

�����붩��

�������Ϊ�벻����������ǰ����ĸ��½�������ܹ������붩�Ķ�������ô��ɲ��ء����Ǳ���ĿǰΪֹ��һ�ν��ܷ����붩�ġ�һ����˵������
�붩�ģ��ֳ�pub/sub�����ص��Ƕ����ߣ�listener��������Ƶ����channel���������ߣ�publisher��������Ƶ�����Ͷ������ַ�����Ϣ��binary string message����
ÿ������Ϣ������������Ƶ��ʱ��Ƶ�������ж����߶����յ���Ϣ������Ҳ���԰�Ƶ�������ǵ�̨�����ж����߿���ͬʱ���������̨������������������κε�̨������Ϣ��

���ڽ��Է����붩�ĵ���ز������н��ܣ��Ķ���һ�ڿ����ö���ѧ������ʹ�÷����붩�ĵ����������˽⵽Ϊʲô������֮����½������ʹ���������ƵĽ������������Redis�ṩ�ķ����붩�ġ�

Redis�ṩ�ķ����붩������

����			����������
 

SUBSCRIBE
SUBSCRIBE channel [channel ...]�����ĸ�����һ������Ƶ��
 
UNSUBSCRIBE
UNSUBSCRIBE [channel [channel ...]]���˶�������һ������Ƶ�������ִ��ʱû�и����κ�Ƶ������ô�˶�����Ƶ��
 
PUBLISH 
PUBLISH channel message�������Ƶ��������Ϣ
 
PSUBSCRIBE
PSUBSCRIBE pattern [pattern ...]�����������ģʽ��ƥ�������Ƶ��
 
PUNSUBSCRIBE
PUNSUBSCRIBE [pattern [pattern ...]]���˶�������ģʽ�����ִ��ʱû�и����κ�ģʽ����ô�˶�����ģʽ
 

���ǵ�PUBLISH�����SUBSCRIBE������Python�ͻ��˵�ʵ�ַ�ʽ��һ���Ƚϼ򵥵���ʾ�����붩�ĵķ���������������嵥3-11����ʹ�ø����̣߳�helper thread����ִ��PUBLISH���



3.7.1 ����

Redis���������������������Ե��������һ���������Ը���ĳ�ֱȽϹ����һϵ��Ԫ�ؽ�����������С�����ִ�����������SORT������Ը����ַ������б����ϡ����򼯺ϡ�
ɢ����5�ּ�����洢�ŵ����ݣ����б������Լ����򼯺Ͻ��������������֮ǰ����ʹ�ù���ϵ���ݿ�Ļ�����ô���Խ�SORT�������SQL�������order by�Ӿ䡣��3-12չʾ��SORT����Ķ��塣

SORT����Ķ���

����			����������
 

SORT
SORT source-key [BY pattern] [LIMIT offset count] [GET pattern [GET pattern ...]] [ASC�����������ߣ�DESC] [ALPHA] [STORE dest-key]�����ݸ�����ѡ�
�������б����ϻ������򼯺Ͻ�������Ȼ�󷵻ػ��ߴ洢����Ľ��
 
ʹ��SORT�����ṩ��ѡ�����ʵ�����¹��ܣ����ݽ��������Ĭ�ϵ�����������Ԫ�أ���Ԫ�ؿ������������������򣬻��߽�Ԫ�ؿ����Ƕ������ַ������������򣨱���
�����ַ���'110'��'12'�Ľ���͸���������110��12�Ľ����һ������ʹ�ñ�����Ԫ��֮�������ֵ��ΪȨ���������������������Դ�������б����ϡ����򼯺�����������ط�����ȡֵ��


3.7.2 ������Redis����

��ʱ��Ϊ��ͬʱ�������ṹ��������Ҫ��Redis���Ͷ���������Redis�м���������������֮�临�ƻ����ƶ�Ԫ�ص������ȴû�����ֿ�����������ͬ����֮���ƶ�Ԫ�ص������Ȼ����ʹ��ZUNIONSTORE���Ԫ�ش�һ�����ϸ��Ƶ�һ�����򼯺ϣ���Ϊ�˶���ͬ���߲�ͬ���͵Ķ����ִ�в�����Redis��5������������û��ڲ�����ϣ�interruption��������¶Զ����ִ�в��������Ƿֱ���WATCH��MULTI��EXEC��UNWATCH��DISCARD��

��һ��ֻ�����������Redis�����÷�����ʹ��MULTI�����EXEC�����������뿴��ʹ��WATCH��MULTI��EXEC��UNWATCH�ȶ�������������ʲô���ӵģ������Ķ�4.4�ڣ����н�����Ϊʲô��Ҫ��ʹ��MULTI��EXEC��ͬʱʹ��WATCH��UNWATCH��

ʲô��Redis�Ļ�������

Redis�Ļ�������basic transaction����Ҫ�õ�MULTI�����EXEC����������������һ���ͻ����ڲ��������ͻ��˴�ϵ������ִ�ж������͹�ϵ���ݿ����ֿ�����ִ�еĹ����н��лع���rollback��������ͬ����Redis���棬��MULTI�����EXEC�����Χ�����������һ����һ����ִ�У�ֱ���������ִ�����Ϊֹ����һ������ִ�����֮��Redis�Żᴦ�������ͻ��˵����

Ҫ��Redis����ִ����������������Ҫִ��MULTI���Ȼ��������Щ������Ҫ����������ִ�е���������ִ��EXEC�����Redis��һ���ͻ���������յ�MULTI����ʱ��Redis�Ὣ����ͻ���֮���͵�����������뵽һ���������棬ֱ������ͻ��˷���EXEC����Ϊֹ��Ȼ��Redis�ͻ��ڲ�����ϵ�����£�һ����һ����ִ�д洢�ڶ���������������������˵��Redis������Python�ͻ�������������ˮ�ߣ�pipeline��ʵ�ֵģ������Ӷ������piepline()����������һ��������һ������������£��ͻ��˻��Զ���ʹ��MULTI��EXEC�������û�����Ķ��������⣬Ϊ�˼���Redis��ͻ���֮���ͨ����������������ִ�ж������ʱ�����ܣ�Python��Redis�ͻ��˻�洢����������Ķ�����Ȼ��������ִ��ʱһ���Եؽ�����������͸�Redis��

������PUBLISH�����SUBSCRIBE����ʱ�����һ����Ҫչʾ����ִ�н������򵥵ķ������ǽ�����ŵ��߳�����ִ�С������嵥3-13չʾ����û��ʹ�����������£�ִ�в��У�parallel�����������Ľ����

�����嵥3-13 �ڲ���ִ������ʱ��ȱ��������ܻ�����������

..\01\3-13.tif

��Ϊû��ʹ����������3���̶߳�������ִ���Լ�����֮ǰ����notrans:������ִ��������������Ȼ�����嵥����ͨ������100����ķ�ʽ���Ŵ���Ǳ�ڵ����⣬���������ȷʵ��Ҫ�ڲ�������������ŵ�����£��Լ�����ִ�������������Լ���������ô���ǾͲ��ò�������Ǳ�ڵ����⡣�����嵥3-14չʾ�����ʹ��������ִ����ͬ�Ĳ�����

�����嵥3-14 ʹ����������������Ĳ���ִ������

..\01\3-14.tif

..\01\3-14b.tif

���Կ��������������������Լ�����֮����һ���ӳ�ʱ�䣬��ͨ��ʹ�����񣬸����̶߳������ڲ��������̴߳�ϵ�����£�ִ�и��Զ�������������ס��RedisҪ�ڽ��յ�EXEC����֮�󣬲Ż�ִ����Щλ��MULTI��EXEC֮���������

ʹ�����������Ҳ�бף������4.4�ڽ����������������ۡ�

��ϰ���Ƴ��������� ����ǰ��Ĵ����嵥3-13��ʾ��MULTI��EXEC�����һ����Ҫ�������Ƴ�������������1��չʾ��articlevote()��������һ�����������Լ�һ����Ϊ�������������ֵ�bug�������ľ����������ܻ�����ڴ�й©����������bug����ܻᵼ�²���ȷ��ͶƱ������֡�����article vote()�����ľ���������bug���ֵĻ��ᶼ�ǳ��٣���Ϊ�˷�����δȻ����������취�޸�����ô����ʾ���������ú�����⾺������Ϊʲô�ᵼ���ڴ�й©����ô�����ڷ�����1�µ�post_article()������ͬʱ���Ķ�һ��6.2.5�ڡ�

��ϰ��������� ��Redis����ʹ����ˮ�ߵ���һ��Ŀ����������ܣ���ϸ����Ϣ����֮���4.4����4.6���н��ܣ�����ִ��һ��������ʱ������Redis��ͻ���֮���ͨ�������������Դ�����Ϳͻ��˵ȴ��ظ������ʱ�䡣��1�µ�get_articles()�����ڻ�ȡ����ҳ�������ʱ����Ҫ��Redis��ͻ���֮�����26��ͨ������������������ֱ��Ч�����˷�ָ�����ܷ�����취��get_articles()����������������26�ν���Ϊ2���أ� |

��ʹ��Redis�洢���ݵ�ʱ����Щ���ݽ���һ�κ̵ܶ�ʱ�������ã���Ȼ���ǿ��������ݵ���Ч�ڹ���֮���ֶ�ɾ�����õ����ݣ������õİ취��ʹ��Redis�ṩ�ļ����ڲ������Զ�ɾ���������ݡ�

3.7.3 ���Ĺ���ʱ��

��ʹ��Redis�洢���ݵ�ʱ����Щ���ݿ�����ĳ��ʱ���֮��Ͳ��������ˣ��û�����ʹ��DEL������ʽ��ɾ����Щ�������ݣ�Ҳ����ͨ��Redis�Ĺ���ʱ�䣨expiration����������һ�����ڸ�����ʱ�ޣ�timeout��֮���Զ���ɾ����������˵һ��������������ʱ�䣨time to live��������һ�����������ض�ʱ��֮����ڣ�expire����ʱ������ָ����Redis����������Ĺ���ʱ�䵽��ʱ�Զ�ɾ���ü���

��Ȼ����ʱ�����Զ������������ݷǳ����ã�����������߷�һ�±���������½ڣ��ͻᷢ�ֳ���6.2�ڡ�7.1�ں�7.2��֮�⣬����ʹ�ù���ʱ�����Ե���������࣬����Ҫ�ͱ�
��ʹ�õĽṹ�����йء��ڱ��鳣�õ�����У�ֻ�����������������ԭ�ӵ�Ϊ�����ù���ʱ�䣬���Ҷ����б����ϡ�ɢ�к����򼯺�������������container����˵������
������ֻ��Ϊ���������ù���ʱ�䣬��û�취Ϊ������ĵ���Ԫ�����ù���ʱ�䣨Ϊ�˽��������⣬�����ںü����ط���ʹ���˴洢ʱ��������򼯺���ʵ����Ե���Ԫ�صĹ��ڲ�������

���ڽ�����Щ�����ڸ���ʱ��֮����߸���ʱ��֮���Զ�ɾ�����ڼ���Redis������н��ܣ��Ķ����ڿ����ö���ѧϰ��ʹ�ù��ڲ������Զ�ɾ���������ݲ�����Redis�ڴ�ռ�õķ�����

��3-13�г���Redis�ṩ������Ϊ�����ù���ʱ�������Լ��鿴���Ĺ���ʱ������

��3-13 ���ڴ������ʱ���Redis����



����
 

ʾ��������
PERSIST
PERSIST key-name���Ƴ����Ĺ���ʱ��
 

TTL
TTL key-name���鿴������������ڻ��ж�����
 
EXPIRE
EXPIRE key-name seconds���ø�������ָ��������֮�����

EXPIREAT
EXPIREAT key-name timestamp�����������Ĺ���ʱ������Ϊ������UNIXʱ���
 

PTTL
PTTL key-name���鿴�������������ʱ�仹�ж��ٺ��룬���������Redis 2.6�����ϰ汾����
 

PEXPIRE
PEXPIRE key-name milliseconds���ü���������ָ���ĺ�����֮����ڣ����������Redis 2.6�����ϰ汾����
 

PEXPIREAT
PEXPIREAT key-name timestamp-milliseconds����һ�����뼶���ȵ�UNIXʱ�������Ϊ�������Ĺ���ʱ�䣬���������Redis 2.6�����ϰ汾����
 






http://redisdoc.com/

?Key������ ?DEL
?DUMP
?EXISTS
?EXPIRE
?EXPIREAT
?KEYS
?MIGRATE
?MOVE
?OBJECT
?PERSIST
?PEXPIRE
?PEXPIREAT
?PTTL
?RANDOMKEY
?RENAME
?RENAMENX
?RESTORE
?SORT
?TTL
?TYPE
?SCAN

 
?String���ַ����� ?APPEND
?BITCOUNT
?BITOP
?DECR
?DECRBY
?GET
?GETBIT
?GETRANGE
?GETSET
?INCR
?INCRBY
?INCRBYFLOAT
?MGET
?MSET
?MSETNX
?PSETEX
?SET
?SETBIT
?SETEX
?SETNX
?SETRANGE
?STRLEN

 
?Hash����ϣ�� ?HDEL
?HEXISTS
?HGET
?HGETALL
?HINCRBY
?HINCRBYFLOAT
?HKEYS
?HLEN
?HMGET
?HMSET
?HSET
?HSETNX
?HVALS
?HSCAN

 
?List���б� ?BLPOP
?BRPOP
?BRPOPLPUSH
?LINDEX
?LINSERT
?LLEN
?LPOP
?LPUSH
?LPUSHX
?LRANGE
?LREM
?LSET
?LTRIM
?RPOP
?RPOPLPUSH
?RPUSH
?RPUSHX

 

?Set�����ϣ� ?SADD
?SCARD
?SDIFF
?SDIFFSTORE
?SINTER
?SINTERSTORE
?SISMEMBER
?SMEMBERS
?SMOVE
?SPOP
?SRANDMEMBER
?SREM
?SUNION
?SUNIONSTORE
?SSCAN

 
?SortedSet�����򼯺ϣ� ?ZADD
?ZCARD
?ZCOUNT
?ZINCRBY
?ZRANGE
?ZRANGEBYSCORE
?ZRANK
?ZREM
?ZREMRANGEBYRANK
?ZREMRANGEBYSCORE
?ZREVRANGE
?ZREVRANGEBYSCORE
?ZREVRANK
?ZSCORE
?ZUNIONSTORE
?ZINTERSTORE
?ZSCAN
?ZRANGEBYLEX
?ZLEXCOUNT
?ZREMRANGEBYLEX

 
?HyperLogLog ?PFADD
?PFCOUNT
?PFMERGE

 
?GEO������λ�ã� ?GEOADD
?GEOPOS
?GEODIST
?GEORADIUS
?GEORADIUSBYMEMBER
?GEOHASH

 

?Pub/Sub������/���ģ� ?PSUBSCRIBE
?PUBLISH
?PUBSUB
?PUNSUBSCRIBE
?SUBSCRIBE
?UNSUBSCRIBE

 
?Transaction������ ?DISCARD
?EXEC
?MULTI
?UNWATCH
?WATCH

 
?Script���ű��� ?EVAL
?EVALSHA
?SCRIPT EXISTS
?SCRIPT FLUSH
?SCRIPT KILL
?SCRIPT LOAD

 
?Connection�����ӣ� ?AUTH
?ECHO
?PING
?QUIT
?SELECT

 

?Server���������� ?BGREWRITEAOF
?BGSAVE
?CLIENT GETNAME
?CLIENT KILL
?CLIENT LIST
?CLIENT SETNAME
?CONFIG GET
?CONFIG RESETSTAT
?CONFIG REWRITE
?CONFIG SET
?DBSIZE
?DEBUG OBJECT
?DEBUG SEGFAULT
?FLUSHALL
?FLUSHDB
?INFO
?LASTSAVE
?MONITOR
?PSYNC
?SAVE
?SHUTDOWN
?SLAVEOF
?SLOWLOG
?SYNC
?TIME












yang add
�б����ı�������� ziplist ���� linkedlist ��
����ת��

���б�������ͬʱ����������������ʱ�� �б����ʹ�� ziplist ���룺
1.�б���󱣴�������ַ���Ԫ�صĳ��ȶ�С�� 64 �ֽڣ�
2.�б���󱣴��Ԫ������С�� 512 ����

���������������������б������Ҫʹ�� linkedlist ���롣

ע��
������������������ֵ�ǿ����޸ĵģ� �����뿴�����ļ��й��� list-max-ziplist-value ѡ��� list-max-ziplist-entries ѡ���˵����

�б������ʵ��

��Ϊ�б����ֵΪ�б���� ���������б�����������������б�����������ģ� �г�������һ�����б����� �Լ���Щ�����ڲ�ͬ������б�����µ�ʵ�ַ�����


�б������ʵ��


����            ziplist �����ʵ�ַ���                  linkedlist �����ʵ�ַ���


LPUSH   ���� ziplistPush ������ ����Ԫ�����뵽ѹ���б�ı�ͷ��                  ���� listAddNodeHead ������ ����Ԫ�����뵽˫������ı�ͷ�� 
RPUSH   ���� ziplistPush ������ ����Ԫ�����뵽ѹ���б�ı�β��                  ���� listAddNodeTail ������ ����Ԫ�����뵽˫������ı�β�� 
LPOP    ���� ziplistIndex ������λѹ���б�ı�ͷ�ڵ㣬 �����û�
        ���ؽڵ��������Ԫ��֮�� ���� ziplistDelete ����ɾ����ͷ�ڵ㡣        ���� listFirst ������λ˫������ı�ͷ�ڵ㣬 �����û����ؽڵ��������Ԫ��֮�� ���� listDelNode ����ɾ����ͷ�ڵ㡣 
RPOP    ���� ziplistIndex ������λѹ���б�ı�β�ڵ㣬 �����û����ؽ�
        ���������Ԫ��֮�� ���� ziplistDelete ����ɾ����β�ڵ㡣              ���� listLast ������λ˫������ı�β�ڵ㣬 �����û����ؽڵ��������Ԫ��֮�� ���� listDelNode ����ɾ����β�ڵ㡣 
LINDEX  ���� ziplistIndex ������λѹ���б��е�ָ���ڵ㣬 Ȼ�󷵻ؽڵ�
        �������Ԫ�ء�                                                          ���� listIndex ������λ˫�������е�ָ���ڵ㣬 Ȼ�󷵻ؽڵ��������Ԫ�ء� 
LLEN    ���� ziplistLen ��������ѹ���б�ĳ��ȡ�                                ���� listLength ��������˫������ĳ��ȡ� 
LINSERT �����½ڵ㵽ѹ���б�ı�ͷ���߱�βʱ�� ʹ�� ziplistPush 
        ������ �����½ڵ㵽ѹ���б������λ��ʱ�� ʹ�� ziplistInsert ������     ���� listInsertNode ������ ���½ڵ���뵽˫�������ָ��λ�á� 
LREM    ����ѹ���б�ڵ㣬 ������ ziplistDelete ����ɾ�������˸���
        Ԫ�صĽڵ㡣                                                            ����˫������ڵ㣬 ������ listDelNode ����ɾ�������˸���Ԫ�صĽڵ㡣 
LTRIM   ���� ziplistDeleteRange ������ ɾ��ѹ���б������в���
        ָ��������Χ�ڵĽڵ㡣                                                  ����˫������ڵ㣬 ������ listDelNode ����ɾ�����������в���ָ��������Χ�ڵĽڵ㡣 
LSET    ���� ziplistDelete ������ ��ɾ��ѹ���б�ָ�������ϵ����нڵ㣬 Ȼ����� ziplistInsert ������ ��һ����������Ԫ�ص��½ڵ���뵽��ͬ�������档 ���� listIndex ������ ��λ��˫������ָ�������ϵĽڵ㣬 Ȼ��ͨ����ֵ�������½ڵ��ֵ�� 



ziplist �ǽ�Լ�ڴ浫�ٶ������� hashtable ���ٶȿ쵫���ڴ棬���߻���������Ҫʹ��


����ת��

����ϣ�������ͬʱ����������������ʱ�� ��ϣ����ʹ�� ziplist ���룺
1.��ϣ���󱣴�����м�ֵ�Եļ���ֵ���ַ������ȶ�С�� 64 �ֽڣ�
2.��ϣ���󱣴�ļ�ֵ������С�� 512 ����

�������������������Ĺ�ϣ������Ҫʹ�� hashtable ���롣

ע��
����������������ֵ�ǿ����޸ĵģ� �����뿴�����ļ��й��� hash-max-ziplist-value ѡ��� hash-max-ziplist-entries ѡ���˵����
����ʹ�� ziplist ������б������˵�� ��ʹ�� ziplist �����������������������һ�����ܱ�����ʱ�� ����ı���ת�������ͻᱻִ�У� 
ԭ��������ѹ���б�������м�ֵ�Զ��ᱻת�Ʋ����浽�ֵ����棬 ����ı���Ҳ��� ziplist ��Ϊ hashtable ��

ziplist �ǽ�Լ�ڴ浫�ٶ������� hashtable ���ٶȿ쵫���ڴ棬���߻���������Ҫʹ��

��ϣ�����ʵ��
��Ϊ��ϣ����ֵΪ��ϣ���� �������ڹ�ϣ���������������Թ�ϣ�����������ģ� һ���ֹ�ϣ����� �Լ���Щ�����ڲ�ͬ����Ĺ�ϣ�����µ�ʵ�ַ�����

��ϣ�����ʵ��
����            ziplist ����ʵ�ַ���                    hashtable �����ʵ�ַ���


HSET        ���ȵ��� ziplistPush ������ �������뵽ѹ���б�ı�β�� Ȼ���ٴε��� ziplistPush ������ ��ֵ���뵽ѹ���б�ı�β��   ���� dictAdd ������ ���½ڵ���ӵ��ֵ����档 
HGET        ���ȵ��� ziplistFind ������ ��ѹ���б��в���ָ��������Ӧ�Ľڵ㣬 Ȼ����� ziplistNext ������ ��ָ���ƶ������ڵ��Աߵ�ֵ�ڵ㣬 ��󷵻�ֵ�ڵ㡣 ���� dictFind ������ ���ֵ��в��Ҹ������� Ȼ����� dictGetVal ������ ���ظü�����Ӧ��ֵ�� 
HEXISTS     ���� ziplistFind ������ ��ѹ���б��в���ָ��������Ӧ�Ľڵ㣬 ����ҵ��Ļ�˵����ֵ�Դ��ڣ� û�ҵ��Ļ���˵����ֵ�Բ����ڡ� ���� dictFind ������ ���ֵ��в��Ҹ������� ����ҵ��Ļ�˵����ֵ�Դ��ڣ� û�ҵ��Ļ���˵����ֵ�Բ����ڡ� 
HDEL        ���� ziplistFind ������ ��ѹ���б��в���ָ��������Ӧ�Ľڵ㣬 Ȼ����Ӧ�ļ��ڵ㡢 �Լ����ڵ��Աߵ�ֵ�ڵ㶼ɾ������ ���� dictDelete ������ ��ָ��������Ӧ�ļ�ֵ�Դ��ֵ���ɾ������ 
HLEN        ���� ziplistLen ������ ȡ��ѹ���б�����ڵ���������� ������������� 2 �� �ó��Ľ������ѹ���б���ļ�ֵ�Ե������� ���� dictSize ������ �����ֵ�����ļ�ֵ�������� ����������ǹ�ϣ��������ļ�ֵ�������� 
HGETALL     ��������ѹ���б� �� ziplistGet �����������м���ֵ�����ǽڵ㣩�� ���������ֵ䣬 �� dictGetKey ���������ֵ�ļ��� �� dictGetVal ���������ֵ��ֵ�� 


���϶���ı�������� intset ���� hashtable ��
�����ת��

�����϶������ͬʱ����������������ʱ�� ����ʹ�� intset ���룺
1.���϶��󱣴������Ԫ�ض�������ֵ��
2.���϶��󱣴��Ԫ������������ 512 ����
�������������������ļ��϶�����Ҫʹ�� hashtable ���롣
ע��
�ڶ�������������ֵ�ǿ����޸ĵģ� �����뿴�����ļ��й��� set-max-intset-entries ѡ���˵����


���������ʵ��

��Ϊ���ϼ���ֵΪ���϶��� �������ڼ��ϼ��������������Լ��϶����������ģ� �Լ���Щ�����ڲ�ͬ����ļ��϶����µ�ʵ�ַ�����

���������ʵ�ַ���


����            intset �����ʵ�ַ���                                                   hashtable �����ʵ�ַ���


SADD        ���� intsetAdd ������ ��������Ԫ����ӵ������������档                  ���� dictAdd �� ����Ԫ��Ϊ���� NULL Ϊֵ�� ����ֵ����ӵ��ֵ����档 
SCARD       ���� intsetLen ������ ��������������������Ԫ�������� �����������
            ���϶�����������Ԫ��������                                              ���� dictSize ������ �����ֵ��������ļ�ֵ�������� ����������Ǽ��϶�����������Ԫ�������� 
SISMEMBER   ���� intsetFind ������ �����������в��Ҹ�����Ԫ�أ� ����ҵ���˵
            ��Ԫ�ش����ڼ��ϣ� û�ҵ���˵��Ԫ�ز������ڼ��ϡ�                       ���� dictFind ������ ���ֵ�ļ��в��Ҹ�����Ԫ�أ� ����ҵ���˵��Ԫ�ش����ڼ��ϣ� û�ҵ���˵��Ԫ�ز������ڼ��ϡ� 
SMEMBERS    ���������������ϣ� ʹ�� intsetGet �������ؼ���Ԫ�ء�                    ���������ֵ䣬 ʹ�� dictGetKey ���������ֵ�ļ���Ϊ����Ԫ�ء� 
SRANDMEMBER ���� intsetRandom ������ �������������������һ��Ԫ�ء�                 ���� dictGetRandomKey ������ ���ֵ����������һ���ֵ���� 
SPOP        ���� intsetRandom ������ ���������������ȡ��һ��Ԫ�أ� �ڽ����
            ���Ԫ�ط��ظ��ͻ���֮�� ���� intsetRemove ������ �����Ԫ��
            ������������ɾ������                                                    ���� dictGetRandomKey ������ ���ֵ������ȡ��һ���ֵ���� �ڽ��������ֵ����ֵ���ظ��ͻ���֮�� ���� dictDelete ������ ���ֵ���ɾ������ֵ������Ӧ�ļ�ֵ�ԡ� 
SREM        ���� intsetRemove ������ ������������ɾ�����и�����Ԫ�ء�               ���� dictDelete ������ ���ֵ���ɾ�����м�Ϊ����Ԫ�صļ�ֵ�ԡ� 



Ϊʲô���򼯺���Ҫͬʱʹ����Ծ����ֵ���ʵ�֣�

����������˵�� ���򼯺Ͽ��Ե���ʹ���ֵ������Ծ�������һ�����ݽṹ��ʵ�֣� �����۵���ʹ���ֵ仹����Ծ�� �������϶�
����ͬʱʹ���ֵ����Ծ�����������͡�

�ٸ����ӣ� �������ֻʹ���ֵ���ʵ�����򼯺ϣ� ��ô��Ȼ�� O(1) ���ӶȲ��ҳ�Ա�ķ�ֵ��һ���Իᱻ������ ���ǣ� ��Ϊ�ֵ���
����ķ�ʽ�����漯��Ԫ�أ� ����ÿ����ִ�з�Χ�Ͳ��� ���� ���� ZRANK �� ZRANGE ������ʱ�� ������Ҫ���ֵ䱣�������Ԫ
�ؽ������� �������������Ҫ���� O(N \log N) ʱ�临�Ӷȣ� �Լ������ O(N) �ڴ�ռ� ����ΪҪ����һ������������������Ԫ�أ���

��һ���棬 �������ֻʹ����Ծ����ʵ�����򼯺ϣ� ��ô��Ծ��ִ�з�Χ�Ͳ����������ŵ㶼�ᱻ������ ����Ϊû�����ֵ䣬 ����
���ݳ�Ա���ҷ�ֵ��һ�����ĸ��ӶȽ��� O(1) ����Ϊ O(\log N) ��
��Ϊ����ԭ�� Ϊ�������򼯺ϵĲ��Һͷ�Χ�Ͳ����������ܿ��ִ�У� Redis ѡ����ͬʱʹ���ֵ����Ծ���������ݽṹ��ʵ�����򼯺ϡ�



�����ת��

�����򼯺϶������ͬʱ����������������ʱ�� ����ʹ�� ziplist ���룺
1.���򼯺ϱ����Ԫ������С�� 128 ����
2.���򼯺ϱ��������Ԫ�س�Ա�ĳ��ȶ�С�� 64 �ֽڣ�
�������������������������򼯺϶���ʹ�� skiplist ���롣

ע��
������������������ֵ�ǿ����޸ĵģ� �����뿴�����ļ��й��� zset-max-ziplist-entries ѡ��� zset-max-ziplist-value ѡ���˵����

����ʹ�� ziplist ��������򼯺϶�����˵�� ��ʹ�� ziplist ������������������е�����һ�����ܱ�����ʱ�� ����ͻ�ִ�б���ת�������� ��ԭ��������ѹ���б���������м���Ԫ��ת�Ƶ� zset �ṹ���棬 ��������ı���� ziplist ��Ϊ skiplist ��


���򼯺������ʵ��

��Ϊ���򼯺ϼ���ֵΪ���򼯺϶��� �����������򼯺ϼ������������������򼯺϶����������ģ�

���򼯺������ʵ�ַ���


����                        ziplist �����ʵ�ַ���                              zset �����ʵ�ַ���


ZADD        ���� ziplistInsert ������ ����Ա�ͷ�ֵ��Ϊ�����ڵ�ֱ���뵽ѹ���б�  �ȵ��� zslInsert ������ ����Ԫ����ӵ���Ծ�� Ȼ����� dictAdd ������ ����Ԫ�ع������ֵ䡣 
ZCARD       ���� ziplistLen ������ ���ѹ���б�����ڵ�������� ������������� 
            2 �ó�����Ԫ�ص�������                                                  ������Ծ�����ݽṹ�� length ���ԣ� ֱ�ӷ��ؼ���Ԫ�ص������� 
ZCOUNT      ����ѹ���б� ͳ�Ʒ�ֵ�ڸ�����Χ�ڵĽڵ��������                       ������Ծ�� ͳ�Ʒ�ֵ�ڸ�����Χ�ڵĽڵ�������� 
ZRANGE      �ӱ�ͷ���β����ѹ���б� ���ظ���������Χ�ڵ�����Ԫ�ء�               �ӱ�ͷ���β������Ծ�� ���ظ���������Χ�ڵ�����Ԫ�ء� 
ZREVRANGE   �ӱ�β���ͷ����ѹ���б� ���ظ���������Χ�ڵ�����Ԫ�ء�               �ӱ�β���ͷ������Ծ�� ���ظ���������Χ�ڵ�����Ԫ�ء� 
ZRANK       �ӱ�ͷ���β����ѹ���б� ���Ҹ����ĳ�Ա�� ��;��¼��
            ���ڵ�������� ���ҵ�������Ա֮�� ;���ڵ����������
            �ó�Ա����ӦԪ�ص�������                                                �ӱ�ͷ���β������Ծ�� ���Ҹ����ĳ�Ա�� ��;��¼�����ڵ�������� ���ҵ�������Ա֮�� ;���ڵ���������Ǹó�Ա����ӦԪ�ص������� 
ZREVRANK    �ӱ�β���ͷ����ѹ���б� ���Ҹ����ĳ�Ա�� ��;��¼��
            ���ڵ�������� ���ҵ�������Ա֮�� ;���ڵ����������
            �ó�Ա����ӦԪ�ص�������                                                �ӱ�β���ͷ������Ծ�� ���Ҹ����ĳ�Ա�� ��;��¼�����ڵ�������� ���ҵ�������Ա֮�� ;���ڵ���������Ǹó�Ա����ӦԪ�ص������� 
ZREM        ����ѹ���б� ɾ�����а���������Ա�Ľڵ㣬 �Լ���ɾ��
            ��Ա�ڵ��Աߵķ�ֵ�ڵ㡣                                                ������Ծ�� ɾ�����а����˸�����Ա����Ծ��ڵ㡣 �����ֵ��н����ɾ��Ԫ�صĳ�Ա�ͷ�ֵ�Ĺ����� 
ZSCORE      ����ѹ���б� ���Ұ����˸�����Ա�Ľڵ㣬 Ȼ��ȡ����Ա
            �ڵ��Աߵķ�ֵ�ڵ㱣���Ԫ�ط�ֵ��                                      ֱ�Ӵ��ֵ���ȡ��������Ա�ķ�ֵ�� 




RDB�־û�ԭ��:
    rdb��redis�����ڴ����ݵ��������ݵ�����һ�ַ�ʽ(��һ����AOF)��Rdb����Ҫԭ�������ĳ��ʱ�����ڴ��е��������ݵĿ��ձ���һ��
�������ϡ��������ﵽʱͨ��forkһ���ӽ��̰��ڴ��е�����д��һ����ʱ�ļ�����ʵ�ֱ������ݿ��ա�����������д����ٰ������ʱ��
����ԭ�Ӻ���rename(2)������ΪĿ��rdb�ļ�������ʵ�ַ�ʽ�������fork��copy on write��



�����ڼ�������ɾ�����߶���ɾ��֮�󣬳������AOF�ļ�׷�ӣ�append��һ��DEL
�������ʽ�ؼ�¼�ü��ѱ�ɾ����
    �ٸ����ӣ�����ͻ���ʹ��GET message�����ͼ���ʹ��ڵ�message������ô
��������ִ����������������
    1)�����ݿ���ɾ��message����
    2)׷��һ��DEL message���AOF�ļ���
    3)��ִ��GET����Ŀͻ��˷��ؿջظ���






http://www.jb51.net/article/56448.htm  ��ǿ������ϸRedis���ݿ����Ž̳�
redis�ṩ�����ֳ־û��ķ�ʽ���ֱ���RDB��Redis DataBase����AOF��Append Only File����

���������ȫ�����redis���ķ���ο�:http://redisdoc.com/

��־�ļ�Ĭ��·��/var/log/redis/redis.log 

�ù��̽����source insight�鿴ע���������⣬����ͨ��source insight�鿴ע����Ϣ

��һ������һЩ�ؼ������ע�ͣ�����ͻ��˷��͹����������ַ���Ϊkey����value 5M�� redis�������������ϸ����
���߷��͸��ͻ��˵������ʽ�ַ����е�һ���ؼ��ڵ��ַ���Ϊ5M, redis������첽����ʵ�ֵġ�

�༶redis���ӷ���������һ���Ա�֤��������о�ע��


����: set �� setbitû����������ֶ�����REDIS_STRING�࣬���뷽ʽҲһ����
���set test abc,Ȼ�����ִ��setbit test 0 1���ǻ�ɹ��ġ������test�����ݱ������޸ġ���������һ�ֱ���encoding��ʽ���������֡�


���Ը���ĵط�:
�����1:
    ��Ӧ��ͻ����������ݵ�ʱ��ȫ��epoll����epool write�¼�������������̫�ã�ÿ�η�������ǰͨ��epoll_ctl������epoll write�¼�����ʹ����
һ��"+OK"�ַ�����Ҳ��������̡�
    ���췽��: ��ʼ����socket����epoll����Ҫ��socketд���ݵ�ʱ��ֱ�ӵ���write����send�������ݡ��������EAGAIN����socket����epoll����epoll��
������д���ݣ�ȫ�����ݷ�����Ϻ����Ƴ�epoll��
    ���ַ�ʽ���ŵ��ǣ����ݲ����ʱ����Ա���epoll���¼��������Ч�ʡ� ������ƺ�nginx���ͻ���һ�¡�
    �����п�����ɸ��Ż���

�����2:
    �ڶ�key-value�Խ����ϻ�ɾ���Ĺ����У����ô�expire hashͰ�����ȡһ��key-value�ڵ㣬�ж��Ƿ�ʱ����ʱ��ɾ��,���ַ���ÿ�βɼ����кܴ�
    ��ȷ���ԣ���ɿ�ת��������û��ɾ���ϻ�ʱ�䵽�Ľڵ㣬�˷�CPU��Դ��

    ���췽��:����expire hashͰ����Ӱ���expireʱ���key-value�ڵ㣬��ÿ�������table[i]Ͱ��Ӧ�����У����԰���ʱ���С��������ÿ��ɾ���ϻ���ʱ��
    ֱ��ȡ����table[i]Ͱ�еĵ�һ���ڵ�ӿ��жϳ���Ͱ���Ƿ����ϻ�ʱ�䵽�Ľڵ㣬�������Ժ�׼ȷ�Ķ�λ����Щ����table[i]Ͱ�����ϻ��ڵ㣬�������ȡ��
    ɾ���ӿڡ�

�����3:
    ��������ͬ��rdb�ļ����ӷ�������ʱ���ǲ���ֱ�Ӷ�ȡ�ļ���Ȼ��ͨ�����緢�ͳ�ȥ��������Ҫ���ļ����ݴ��ں˶�ȡ��Ӧ�ò㣬��ͨ������Ӧ�ó����Ӧ�ò�
    ���ں�����Э��ջ�������е��˷�CPU��Դ���ڴ档

    ���췽��:ͨ��sendfile����aio��ʽ���ͣ��������ں���Ӧ�ò㽻����������ܡ�
�����4:����ͬ�������鷳��ÿ�ζ�Ҫ��ش��̣�Ȼ���ڴӴ��̶�ȡ
    ���췽��:ֱ�Ӱ��ڴ��е�KEY-VALUE�԰���ָ����ʽ������������д������غʹӴ��̶�

�����5:������粻�ã������������������������سɹ������Ӵ��̶�ȡ�ɹ��������ڽ�������ͬ����Ȼ���������?���������ͬ��������������������

�����6:�����Լ�д��redis�м�����������в����пͻ��������

��Ҫ���ӻ�ȡ���ݿ�ŵ������Ϊ��β������ݿ�󣬾��������ڲ��������Ǹ����ݿ�ŵ����ݿ��ˡ�


���ֿ�������:
1.���ӷ�����֮��ͨ��ack������·̽�⣬����֮���tcp����Ĭ��tcp-keepalive=0��Ҳ���ǲ������ں��Զ�keepalive����������·��ͨ�������
  (����ֱ�Ӱ������������߰ε�����ֱ�ӹػ�)���ӷ�������ʱ���޷��Զ��л���������������߷���

�޸ķ���:����Ӧ�ò㱣�ʱ��⣬����������·����̽�ⶨʱ������������ʱ��û�н��յ��˴˵ı���ģ���ֱ�������Ͽ�TCP���ӣ��Ӷ��ñ��л�Ϊ����������߷���



nodes.conf��������һ�п��У�ʲôҲû�У���redis����������clusterLoadConfig�����⣬����һ�㲻Ҫȥ����nodes.conf�ļ���������ü�Ⱥ״̬��ϵ����Э��
�����ֱ��ɾ��nodes.conf�������nodes.conf��redis�����������Ҫ�ֶ��������ļ���

redis-trib.rb  create --replicas 1 127.0.0.1:7000 127.0.0.1:7001 127.0.0.1:7002 127.0.0.1:7003 127.0.0.1:7004 127.0.0.1:7005
redis-benchmark -h 10.23.22.240 -p 22121 -c 100 -t set -d 100 -l �Cq

redis-benchmark  -h 192.168.1.111 -p 22122 -c 100 -t set -d 100 

���ܲ���
����ʹ��redis�Դ���redis-benchmark���м򵥵����ܲ��ԣ����Խ������:

Set���ԣ�
ͨ��twemproxy���ԣ�
[root@COS1 src]# redis-benchmark -h 10.23.22.240 -p 22121 -c 100 -t set -d 100 -l �Cq
SET: 38167.94 requests per second
ֱ�ӶԺ��redis���ԣ�
[root@COS2 ~]# redis-benchmark -h 10.23.22.241 -p 6379 -c 100 -t set -d 100 -l �Cq
SET: 53191.49 requests per second
Get���ԣ�
ͨ��twemproxy���ԣ�
[root@COS1 src]# redis-benchmark -h 10.23.22.240 -p 22121 -c 100 -t get -d 100 -l -q
GET: 37453.18 requests per second
ֱ�ӶԺ��redis���ԣ�
[root@COS2 ~]# redis-benchmark -h 10.23.22.241 -p 6379 -c 100 -t get -d 100 -l -q
GET: 62111.80 requests per second
�鿴��ֵ�ֲ���
[root@COS2 ~]# redis-cli info|grep db0
db0:keys=51483,expires=0,avg_ttl=0

[root@COS3 ~]# redis-cli info|grep db0
db0:keys=48525,expires=0,avg_ttl=0


��־�Ż���������ÿдһ����־��open���ļ���д���رա���־���ʱ��Ч�ʵ��£�����һֱ����־�ļ������ر�

/*
[root@V172-16-3-44 autostartrediscluster]# redis-trib.rb  create --replicas 1 127.0.0.1:7000 127.0.0.1:7001 127.0.0.1:7002 127.0.0.1:7003 127.0.0.1:7004 127.0.0.1:7005
/usr/lib/ruby/site_ruby/1.8/rubygems/custom_require.rb:31:in `gem_original_require': no such file to load -- redis (LoadError)
        from /usr/lib/ruby/site_ruby/1.8/rubygems/custom_require.rb:31:in `require'
        from /usr/local/bin/redis-trib.rb:25
����ִ��yum install ruby  yum install redis����


https://www.kernel.org/doc/Documentation/networking/ip-sysctl.txt   linux�ں˲�������
config�����ļ��ο�:http://my.oschina.net/u/568779/blog/308129

*/
*/

