/*
redis-trib.rbʹ��:
��װ
yum install ruby
yum install rubygems
gem install redis
redis-trib.rb���:http://blog.csdn.net/huwei2003/article/details/50973967  redis cluster������redis-trib.rb���


ע��:
1.��Ǩ�Ʋ�λ�ڼ䣬���͵�ԭ��Ⱥ�ò�λ��set key value����move��Ȼ��Ҫ��Ŀ�Ľڵ���set key value�����߿ͻ�����Ӧ�ð�����Ǩ�Ʋ۵�KV���͵�Ŀ�Ľڵ�
2.�����λn������A -> B�ڵ㣬�����Ѿ���A�ڵ�n��λ��key1Ǩ�Ƶ�B����������Ǩ������key,�����A��get key1��
  ��A����߿ͻ���(error) ASK 2 B-IP:B-PORT����ʾȥB��ȡ����ʱ���������B����asking��Ȼ����get

(error) ASK 2 10.2.4.5:7001 ˵��2��λ����Ǩ�Ƶ�10.2.4.5:7001�ڵ㣬�����set  get  2��λ��key��Ϣ��������asking ,Ȼ����get   set
ע�������key��������Ǩ�ƵĲ۵�ʱ����Ҫ�ȷ���asking   �м�  �м�     get  set  hset hmset�����й��ڸ�����Ǩ�Ʋ۵�key����Ҫ�ȷ���asking


��127.0.0.1:7001�ڵ������4��λǨ�Ƶ�127.0.0.1:7006��������ִ�й���:��ͨ���м乤�߻�ȡ������ЩKV��Ȼ��һ��һ����֪ͨredisȥ����Ǩ��
���Բο�http://blog.csdn.net/gqtcgq/article/details/51757755
1����Ǩ��ڵ㷢�͡� CLUSTER  SETSLOT  <slot>  IMPORTING  <node>������
2����Ǩ���ڵ㷢�͡� CLUSTER  SETSLOT  <slot>  MIGRATING  <node>������
3����Ǩ���ڵ㷢�͡�CLUSTER  GETKEYSINSLOT  <slot>  <count>������
4����Ǩ���ڵ㷢�͡�MIGRATE <target_host> <target_port> <key> <target_database> <timeout> replace������
5�������нڵ㷢�͡�CLUSTER  SETSLOT  <slot>  NODE  <nodeid>������



һ  Ǩ��ǰ�ڵ���Ϣ
127.0.0.1:7001> DBSIZE
(integer) 50217
127.0.0.1:7001> cluster nodes
f4a5287807d38083970f4b330fa78a1fe139c3ba 10.2.4.5:7000 slave a3638eccb50fb018f51b5fa8a2d70cb09c38dc4d 0 1481094817241 3 connected
a3638eccb50fb018f51b5fa8a2d70cb09c38dc4d 10.2.4.5:7005 master - 0 1481094822252 3 connected 10923-16383
b88af084e00308e7ab219f212617d121c92434e6 10.2.4.5:7002 slave 6f4e14557ff3ea0111ef802438bb612043f18480 0 1481094820248 5 connected
86ede7388b40eec6c2203155717f684afc016544 10.2.4.5:7003 master - 0 1481094821251 2 connected 5462-10922
6f4e14557ff3ea0111ef802438bb612043f18480 10.2.4.5:7001 myself,master - 0 0 5 connected 4-5461
4f3012ab3fcaf52d21d453219f6575cdf06d2ca6 10.2.4.5:7006 master - 0 1481094818244 6 connected 0-3
f1566757b261fd4305882f607df6586c3b944f52 10.2.4.5:7004 slave 86ede7388b40eec6c2203155717f684afc016544 0 1481094819248 4 connected

[root@s10-2-4-5 redis-3.0.6]# redis-cli -c -p 7006
127.0.0.1:7006> DBSIZE
(integer) 42
127.0.0.1:7006> quit   


��  Ǩ�ƹ���
����1: ֪ͨԴ�ڵ����ð�4��λǨ�Ƶ�127.0.0.1:7006(4f3012ab3fcaf52d21d453219f6575cdf06d2ca6)��׼��
[root@s10-2-4-5 redis-3.0.6]# redis-cli -c -p 7001
127.0.0.1:7001> CLUSTER SETSLOT 4 migrating 4f3012ab3fcaf52d21d453219f6575cdf06d2ca6
OK
127.0.0.1:7001> quit

����2:֪ͨĿ��127.0.0.1:7006�ڵ����ý���127.0.0.1:7001�ڵ���4��λǨ�Ƶ����ڵ��׼��
[root@s10-2-4-5 redis-3.0.6]# redis-cli -c -p 7006
127.0.0.1:7006> CLUSTER SETSLOT 4 importing 6f4e14557ff3ea0111ef802438bb612043f18480
OK
127.0.0.1:7006> quit


����3: ͨ��redis-cli�ͻ��˻�ȡ��ԭ��Ⱥ�ڵ���4��λ�е�KV��Ȼ��һ��һ����֪ͨԭ��Ⱥ�ڵ��KVһ��һ��Ǩ�Ƶ�Ŀ�Ľڵ�
[root@s10-2-4-5 redis-3.0.6]# redis-cli -c -p 7001
127.0.0.1:7001> CLUSTER GETKEYSINSLOT 4 20    ��ͨ���м乤�߻�ȡԴ�ڵ�����ЩKV��Ȼ��һ��һ��֪ͨԴ�ڵ�Ǩ����ЩKV
1) "2xxxxxxxxxxxxxxxxxxxxxxx146073"
2) "2xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx149173"
3) "7xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx8565"
4) "7xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx38515"
127.0.0.1:7001> MIGRATE 127.0.0.1 7006 2xxxxxxxxxxxxxxxxxxxxxxx146073 0 10 replace
OK
127.0.0.1:7001> MIGRATE 127.0.0.1 7006 2xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx149173 0 10 replace
OK
127.0.0.1:7001> MIGRATE 127.0.0.1 7006 7xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx8565 0 10 replace
OK
127.0.0.1:7001> MIGRATE 127.0.0.1 7006 7xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx38515 0 10 replace
OK
127.0.0.1:7001> CLUSTER GETKEYSINSLOT 4 20
(empty list or set)
127.0.0.1:7001> cluster nodes
f4a5287807d38083970f4b330fa78a1fe139c3ba 10.2.4.5:7000 slave a3638eccb50fb018f51b5fa8a2d70cb09c38dc4d 0 1481095441547 3 connected
a3638eccb50fb018f51b5fa8a2d70cb09c38dc4d 10.2.4.5:7005 master - 0 1481095438539 3 connected 10923-16383
b88af084e00308e7ab219f212617d121c92434e6 10.2.4.5:7002 slave 6f4e14557ff3ea0111ef802438bb612043f18480 0 1481095435534 5 connected
86ede7388b40eec6c2203155717f684afc016544 10.2.4.5:7003 master - 0 1481095440545 2 connected 5462-10922
//Դ�ڵ�������ʾ4��λ����Ǩ�Ƹ�7006�ڵ������
6f4e14557ff3ea0111ef802438bb612043f18480 10.2.4.5:7001 myself,master - 0 0 5 connected 4-5461 [4->-4f3012ab3fcaf52d21d453219f6575cdf06d2ca6]
4f3012ab3fcaf52d21d453219f6575cdf06d2ca6 10.2.4.5:7006 master - 0 1481095437537 6 connected 0-3
f1566757b261fd4305882f607df6586c3b944f52 10.2.4.5:7004 slave 86ede7388b40eec6c2203155717f684afc016544 0 1481095439541 4 connected
127.0.0.1:7001> quit
[root@s10-2-4-5 redis-3.0.6]# redis-cli -c -p 7006
127.0.0.1:7006> cluster nodes
f1566757b261fd4305882f607df6586c3b944f52 10.2.4.5:7004 slave 86ede7388b40eec6c2203155717f684afc016544 0 1481095452552 2 connected
b88af084e00308e7ab219f212617d121c92434e6 10.2.4.5:7002 slave 6f4e14557ff3ea0111ef802438bb612043f18480 0 1481095450549 5 connected
86ede7388b40eec6c2203155717f684afc016544 10.2.4.5:7003 master - 0 1481095454556 2 connected 5462-10922
//Դ�ڵ�������ʾ4��λ���ڴ�7006�ڵ�Ǩ�Ƶ���7001������
4f3012ab3fcaf52d21d453219f6575cdf06d2ca6 10.2.4.5:7006 myself,master - 0 0 6 connected 0-3 [4-<-6f4e14557ff3ea0111ef802438bb612043f18480]
f4a5287807d38083970f4b330fa78a1fe139c3ba 10.2.4.5:7000 slave a3638eccb50fb018f51b5fa8a2d70cb09c38dc4d 0 1481095455558 3 connected
a3638eccb50fb018f51b5fa8a2d70cb09c38dc4d 10.2.4.5:7005 master - 0 1481095457561 3 connected 10923-16383
6f4e14557ff3ea0111ef802438bb612043f18480 10.2.4.5:7001 master - 0 1481095456559 5 connected 4-5461


����4: ����3Ǩ��������ɺ�֪ͨԴ�ڵ�����Ǩ����ϣ���λmigrating_slots_to=NULL�������ִ�и����Դ�ڵ�Ͳ�֪��Ǩ����ɣ�cluster nodes���ǻῴ������Ǩ�ƹ����У���ӡ[4-<-6f4e14557ff3ea0111ef802438bb612043f18480]
[root@s10-2-4-5 redis-3.0.6]# redis-cli -c -p 7001
127.0.0.1:7001> CLUSTER SETSLOT 4 node 4f3012ab3fcaf52d21d453219f6575cdf06d2ca6
OK
127.0.0.1:7001> cluster nodes
f4a5287807d38083970f4b330fa78a1fe139c3ba 10.2.4.5:7000 slave a3638eccb50fb018f51b5fa8a2d70cb09c38dc4d 0 1481095512698 3 connected
a3638eccb50fb018f51b5fa8a2d70cb09c38dc4d 10.2.4.5:7005 master - 0 1481095508689 3 connected 10923-16383
b88af084e00308e7ab219f212617d121c92434e6 10.2.4.5:7002 slave 6f4e14557ff3ea0111ef802438bb612043f18480 0 1481095511194 5 connected
86ede7388b40eec6c2203155717f684afc016544 10.2.4.5:7003 master - 0 1481095511695 2 connected 5462-10922
6f4e14557ff3ea0111ef802438bb612043f18480 10.2.4.5:7001 myself,master - 0 0 5 connected 5-5461
//�����￴���Ѿ�û��[4-<-6f4e14557ff3ea0111ef802438bb612043f18480]
4f3012ab3fcaf52d21d453219f6575cdf06d2ca6 10.2.4.5:7006 master - 0 1481095510693 6 connected 0-4
f1566757b261fd4305882f607df6586c3b944f52 10.2.4.5:7004 slave 86ede7388b40eec6c2203155717f684afc016544 0 1481095509690 4 connected
127.0.0.1:7001> quit


����5: ����3Ǩ��������ɺ�֪ͨĿ�Ľڵ�Ǩ�����,��λimporting_slots_from=NULL,ͬʱ�����м�Ⱥ�ڵ㷢�͸�����
127.0.0.1:7006> CLUSTER SETSLOT 4 node 4f3012ab3fcaf52d21d453219f6575cdf06d2ca6
OK
127.0.0.1:7006> cluster nodes
f1566757b261fd4305882f607df6586c3b944f52 10.2.4.5:7004 slave 86ede7388b40eec6c2203155717f684afc016544 0 1481095495136 2 connected
b88af084e00308e7ab219f212617d121c92434e6 10.2.4.5:7002 slave 6f4e14557ff3ea0111ef802438bb612043f18480 0 1481095492632 5 connected
86ede7388b40eec6c2203155717f684afc016544 10.2.4.5:7003 master - 0 1481095494634 2 connected 5462-10922
//�����￴���Ѿ�û��[4->-4f3012ab3fcaf52d21d453219f6575cdf06d2ca6]
4f3012ab3fcaf52d21d453219f6575cdf06d2ca6 10.2.4.5:7006 myself,master - 0 0 6 connected 0-4
f4a5287807d38083970f4b330fa78a1fe139c3ba 10.2.4.5:7000 slave a3638eccb50fb018f51b5fa8a2d70cb09c38dc4d 0 1481095491628 3 connected
a3638eccb50fb018f51b5fa8a2d70cb09c38dc4d 10.2.4.5:7005 master - 0 1481095493634 3 connected 10923-16383
6f4e14557ff3ea0111ef802438bb612043f18480 10.2.4.5:7001 master - 0 1481095495638 5 connected 5-5461
127.0.0.1:7006> quit


����6:�鿴�����ڵ��Ƿ��Ѿ���ȡ������ȷ�Ľڵ��λ��Ϣ����dbsize�仯
[root@s10-2-4-5 redis-3.0.6]# redis-cli -c -p 7002
127.0.0.1:7002> cluster nodes
4f3012ab3fcaf52d21d453219f6575cdf06d2ca6 10.2.4.5:7006 master - 0 1481095518275 6 connected 0-4
6f4e14557ff3ea0111ef802438bb612043f18480 10.2.4.5:7001 master - 0 1481095522282 5 connected 5-5461
f4a5287807d38083970f4b330fa78a1fe139c3ba 10.2.4.5:7000 slave a3638eccb50fb018f51b5fa8a2d70cb09c38dc4d 0 1481095516271 3 connected
a3638eccb50fb018f51b5fa8a2d70cb09c38dc4d 10.2.4.5:7005 master - 0 1481095520278 3 connected 10923-16383
86ede7388b40eec6c2203155717f684afc016544 10.2.4.5:7003 master - 0 1481095519276 2 connected 5462-10922
b88af084e00308e7ab219f212617d121c92434e6 10.2.4.5:7002 myself,slave 6f4e14557ff3ea0111ef802438bb612043f18480 0 0 1 connected
f1566757b261fd4305882f607df6586c3b944f52 10.2.4.5:7004 slave 86ede7388b40eec6c2203155717f684afc016544 0 1481095521281 4 connected
127.0.0.1:7002> quit
[root@s10-2-4-5 redis-3.0.6]# 

[root@s10-2-4-5 redis-3.0.6]# redis-cli -c -p 7001
127.0.0.1:7001> DBSIZE
(integer) 50213       ��50217��Ϊ��50213����ΪǨ����4����7006�ڵ�

[root@s10-2-4-5 redis-3.0.6]# redis-cli -c -p 7006
127.0.0.1:7006> DBSIZE
(integer) 46
127.0.0.1:7006> quit   ��42��Ϊ��46����ΪǨ����4����7001�ڵ�
*/
