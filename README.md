# reading
redis阅读理解，带详细注释，原注释版使用source insight，现在使用understand



说明
===================================  
本份代码从https://github.com/huangz1990/redis-3.0-annotated clone下来，然后自己添加自己的理解，再次基础上增加函数调用流程注释。
参考数据<redis涉及实现>  

阅读工具source insight,如果中文乱码，按照source insight configure目录中说明操作  
本代码解决了huangz1990原始代码source insight中文乱码问题  



阅读进度：
===================================  
本代码对redis源码主要功能进行了详细注释，并加上了自己的理解。redis源码基本通读完毕，并注释添加了相关函数的调用流程。    
cluster集群cluster、redis节点扩容、数据迁移等功能重新梳理分析    


问题及改造点： 
===================================  

-----------------------------------    
问题及改造点:  
	全量同步网卡容易打满  
	
	40ms时延问题   
	
	数据过期清除策略不合适  
	
	一master多slave，新的slave通过投票机制表为master后，其他slave需要和这个新master进行全量同步，效率低下  
	
	热点数据需要统计，应对qps节点访问不均  
	
	大value统计，大value容易拖累整个业务时延  
	
	增量同步过程受积压缓冲区影响，主从实时同步收到client->buffer影响，该机制本身就有缺陷。
	
	短连接情况下QPS 4000左右redis CPU就达到快100%，通过oprofile发现在释放连接的时候有client链表查找操作。
	
	不同业务使用同一个redis，无法区分每个业务的QPS,命中率等  
	
	SLAVE重启需要全量同步，是否可以在重启前先记录下ID和偏移量
	
	单台物理机多redis实例，如故避免多实例同时触发bgsave  
	
	主备模式，主复制积压缓冲区满造成主备反复整体同步。  
	
	多业务使用同一个redis集群，如何按照业务区分统计，例如不同业务的命中率，访问qps等  
	
	主备整体同步过程中，网络抖动,造成整体同步过程中的客户端set、get等KV数据积压在客户端缓冲区中，如果同步时间过长，则容易造成该buf满，进而主会端口slave连接，从而引发反复的同步过程，会引起反复整体同步  
	
	内部阻塞可疑点没有探针跟踪，内部阻塞引起集群部稳定无法确定问题，不知道阻塞在那个环节  
	
	qps比较高的情况下，由于单线程的原因，时延会慢慢增大。  
	
	慢日志只能记录一条命令redis内部处理的时间，但是并不报文数据包读取和发送的时间，对客户端来说，不能完全反应出时延情况。
	
	aof会占用磁盘空间很大，当磁盘空间满了后，flushAppendOnlyFile会失败，这时候无法恢复，只能加硬盘修复。  
	
    数据量大的集群迁移太慢，优化,现有迁移过程是通过工具获取某个槽位的KV，然后一条一条的通知redis进行迁移，慢如牛。可以避开中间工具，直接进行批量数据迁移。  
	
	hash结构存储的HGETALL  HDEL，如果hash上存储的kv对太多，容易造成redis阻塞，进一步引起集群节点反复掉线，集群抖动进一步引起整体同步.是否应该起一个线程单独做这个事情，或者像scan机制那样异步逐条清理  
	
	LPUSH是一直往链表追加，在集群跨机房同步的时候需要特别小心，集群数据迁移容易引起目的集群内存飙涨。  
	
	redis定时清理策略是定时随机循环取20个key来做判断，如果一次循环中至少有5个key过期，则继续循环，直到阻塞25ms时间到退出，然后过2ms继续清理。在实际业务使用中可能业务数据都是匹配设置的过期时间，导致批量失效，进而触发这个过程中的redis访问阻塞，业务时延剧增。也就是27ms时间片中有25ms在做过期清理，2ms在做正常业务处理  
	
	redis集群跨机房同步工具，异常情况考虑。例如redis-migrate-tool第一次进行源目的整体同步的时候，存储耗大量内存情况，最坏情况该工具会用掉整个原集群内存容量，如果原集群内存几百G，工具所在集群会炸掉,OOM, 如果源集群和目的集群长期失去联系，会造成redis-migrate-tool中KV积压严重，进一步触发OOM，集群状态变化无法自动感知等    
	
	slave重启会触发全量同步，可以优化为增量同步
	
	主从KV实时同步和主从增量同步本身机制分别依赖于客户端buffer和积压缓冲区buffer，很容易造成全量同步，网卡瞬间打满，这种设计机制就有问题，过度依赖buff，可以参考mysql的binlog机制进行优化修改。  
    
	echo 'vm.overcommit_memory=1' >> /etc/sysctl.conf   
	
 
其他需要改造的地方:  
	改造点1:  
	在应答客户端请求数据的时候，全是epoll采用epool write事件触发，这样不太好，每次发送数据前通过epoll_ctl来触发epoll write事件，即使发送一个"+OK"字符串，也是这个流程。  
	  
	改造方法: 开始不把socket加入epoll，需要向socket写数据的时候，直接调用write或者send发送数据。如果返回EAGAIN，把socket加入epoll，在epoll的驱动下写数据，全部数据发送完毕后，再移出epoll。  
	这种方式的优点是：数据不多的时候可以避免epoll的事件处理，提高效率。 这个机制和nginx发送机制一致。  


	改造点2:  
		主服务器同步rdb文件给从服务器的时候是采用直接读取文件，然后通过网络发送出去，首先需要把文件内容从内核读取到应用层，在通过网络应用程序从应用层到内核网络协议栈，这样有点浪费CPU资源和内存。  
  
		改造方法:通过sendfile或者aio方式发送，避免多次内核与应用层交互，提高性能。或者在全量同步的时候，不做RDB重写，而是直接把内存中KV安装RDB格式组包直接发送到slave  
	
	改造点4：  
		主备同步每次都要写磁盘，然后从磁盘读，效率低下。  
		
		改造方法：直接把内存中的key-value对由主传到备，不用落地。  
	
	改造点5：  
		如果主备之间网络不好，例如rdb文件落地成功，主读取rdb文件，然后通过网络向备传输，如果传输一般网络断开，当网络重新恢复后，备有重新走之前的流程，主又要写rdb文件到磁盘，然后重新读磁盘往备传送。如此反反复复，主会不停额写磁盘，读磁盘，传输，永远传输不完。  
	
	权限控制能力不强，很容易被识破并攻击，就像wifi万能钥匙一样，很容易破密。需要挑战认证过程，保证安全。  
	

	
=================================== 
运维方面：  
	1. 多实例部署的时候，最好保证master slave在不同的物理机上，保证一个物理机掉电等故障，能正常提供服务  
	2. 同一物理机多实例部署的时候，最好每个实例的redis-server放在不同路径下面，当对redis做二次开发的时候，初期验证阶段可以只替换部分实例，这样对业务影响面较小  
