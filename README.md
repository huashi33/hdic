# hdic
my remote dic
str------
add name1 age1 addr1
set name1 zhangsan age1 31 addr1 nanjing
get name1 age1 addr1 > zhangsan 31 nanjing
del name1 age1




export PATH=$PATH:/home/huashi/code/bin
nngcat --req --dial ipc:///tmp/hdic.sock --data "set name zs" --ascii

hdic 指令耗时测试
  地址    : ipc:///tmp/hdic.sock
  迭代次数: 10000 (每条指令)

== 单键 (每条指令 1 个 key) ==
指令  耗时(s) 吞吐(次/s) 平均(us)    p50(us)    p99(us)    min(us)    max(us) ok/bad/err
add        2.169         4610     216.90     202.77     348.32     151.57     719.72 10000/0/0
set        2.208         4529     220.81     205.75     351.64     170.34     655.48 10000/0/0
get        2.184         4579     218.37     203.33     345.06     167.52     577.52 10000/0/0
del        2.407         4155     240.68     223.14     401.86     176.04    1076.57 10000/0/0

== 多键 (每条指令 32 个 key) ==
指令  耗时(s) 吞吐(次/s) 平均(us)    p50(us)    p99(us)    min(us)    max(us) ok/bad/err
add        2.627         3806     262.74     245.09     419.34     183.44   11150.76 10000/0/0
set        2.520         3969     251.95     235.83     400.92     191.30     874.76 10000/0/0
get        2.599         3848     259.85     240.42     426.06     186.63     867.58 10000/0/0
del        2.664         3753     266.43     244.15     435.89     176.22   14380.73 10000/0/0

[合计] 80000 次往返, 耗时 19.377 s, 平均 4129 次/秒, bad=0 err=0

