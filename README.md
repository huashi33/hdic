# hdic
my remote dic
str------
del tag1
get tag1 > tag1 huashi
set tag1 val1

hash------
hadd tag1 name huashi age 30 addr nanjing
del tag1 
del tag1 name age
get tag1 > tag1 name huashi age 30 addr nanjing
get tag1 name addr > tag1 name huashi addr nanjing
set tag1 age 31
set tag1 age 31 addr wanzhou

hkeys tag1 > name age addr
// hlen tag1 > 3
// hexists tag1 name > 0




