# SentTCP
发送TCP数据报

主要技术：WinPcap

简介：Winpcap是一个由多组件（动态链接库+驱动程序）和相关SDK组成的"库"。它可以提供监听底层数据包、发送底层数据包的功能，但要注意，Winpcap不能提供底层包过滤等功能，所以不能用它做防火墙。
实现了TCP连接的三次握手和四次挥手

Visual Stdio 201x 环境配置参考：https://blog.csdn.net/kxcfzyk/article/details/20129867


关于Windows下底层数据包发送参考：
https://www.cnblogs.com/shenck/p/4075141.html
