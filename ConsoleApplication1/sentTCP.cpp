#include <stdio.h>
#include <stdlib.h>
#include <winsock2.h>
#include <iostream>
#include <pcap.h>

//(ip.src == 172.16.36.102 and ip.dst == 112.80.248.73) or (ip.src == 112.80.248.73 and ip.dst == 172.16.36.102)
//SentTCP 172.16.36.102 80 112.80.248.73 80
using namespace std;
unsigned short srcPort = 80;
unsigned short dstPort = 80;
char *lpszSrcIp = "172.16.36.102";
char *lpszDstIp = "112.80.248.73";
const byte srcMac[] = { 0xA0, 0x8c, 0xFD, 0x2B, 0xCD, 0x61 };	//主机mac
const byte dstMac[] = { 0x00, 0x1a, 0xa9, 0x0c, 0xe1, 0xa9 };	//网关mac

#pragma comment(lib, "ws2_32.lib")

/*                       IP报文格式
0            8           16                        32
+------------+------------+-------------------------+
| ver + hlen |  服务类型  |         总长度          |
+------------+------------+----+--------------------+
|           标识位        |flag|   分片偏移(13位)   |
+------------+------------+----+--------------------+
|  生存时间  | 高层协议号 |       首部校验和        |
+------------+------------+-------------------------+
|                   源 IP 地址                      |
+---------------------------------------------------+
|                  目的 IP 地址                     |
+---------------------------------------------------+
*/
struct IP_HEADER
{
	byte versionAndHeader;	//版本和长度
	byte serviceType;		//区分服务
	byte totalLen[2];		//总长度
	byte seqNumber[2];		//标识
	byte flagAndFragPart[2];//标志和片偏移
	byte ttl;				//生存时间
	byte hiProtovolType;	//协议
	byte headerCheckSum[2];	//首部校验和
	byte srcIpAddr[4];		//源IP地址
	byte dstIpAddr[4];		//目的IP地址
};

/*
					 TCP 报文
0                       16                       32
+------------------------+-------------------------+
|      源端口地址        |      目的端口地址       |
+------------------------+-------------------------+
|                      序列号                      |
+--------------------------------------------------+
|                      确认号                      |
+------+--------+--------+-------------------------+
|HLEN/4| 保留位 |控制位/6|         窗口尺寸        |
+------+--------+--------+-------------------------+
|         校验和         |         应急指针        |
+------------------------+-------------------------+
*/
struct TCP_HEADER
{
	byte srcPort[2];	//源端口
	byte dstPort[2];	//目的端口
	byte seqNumber[4];	//序号
	byte ackNumber[4];	//确认号
	byte headLen;		//首部长度
	byte contrl;		//控制位
	byte wndSize[2];	//窗口大小
	byte checkSum[2];	//校验和
	byte uragentPtr[2];	//应急指针
};

/*
			 TCP伪首部
0               16                 32
+——– + ——– + ——– + ——– +
|         Source Address         |
+——– + ——– + ——– + ——– +
|        Destination Address       |
+——– + ——– + ——– + ——– +
|   zero   |  PTCL  |  TCP Length  |
+——– + ——– + ——– + ——– +
------------------------------------
*/
struct PSDTCP_HEADER
{
	byte srcIpAddr[4];     //源IP地址
	byte dstIpAddr[4];     //目的IP地址 
	byte padding;          //填充
	byte protocol;         //协议
	byte tcpLen[2];        //TCP报文段长度
};

//以太网MAC帧头
struct ETHERNET_HEADER
{
	byte dstMacAddr[6];		//目的MAC地址
	byte srcMacAddr[6];		//源MAC地址
	byte ethernetType[2];	//上层协议类型
};

//IP地址格式化，将一个十进制网络字节序转换为点分十进制IP格式的字符串
char *FormatIpAddr(unsigned uIpAddr, char szIp[])
{
	IN_ADDR addr;
	addr.S_un.S_addr = uIpAddr;//32位长ip地址

	strcpy(szIp, inet_ntoa(addr));//以点分十进制的形式表示ip地址
	return szIp;
}

//计算首部校验和函数
unsigned short CheckSum(unsigned short packet[], int size)
{
	unsigned long cksum = 0;
	while (size > 1)
	{
		cksum += *packet++;
		size -= sizeof(USHORT);
	}
	if (size)
	{
		cksum += *(UCHAR*)packet;
	}
	cksum = (cksum >> 16) + (cksum & 0xffff);
	cksum += (cksum >> 16);

	return (USHORT)(~cksum);
}

//关闭TCP连接请求
int sayGoodbye(pcap_t *handle)
{
	//构建TCP首部
	TCP_HEADER tcpHeader;
	memset(&tcpHeader, 0, sizeof tcpHeader);
	*(unsigned short *)tcpHeader.srcPort = htons(srcPort);	//源端口，16bit
	*(unsigned short *)tcpHeader.dstPort = htons(dstPort);	//目的端口，16bit
	*(unsigned int *)tcpHeader.seqNumber = htonl(0x2D);		//序号，32bit
	*(unsigned int *)tcpHeader.ackNumber = htonl(0x00);		//确认号，32bit
	tcpHeader.headLen = 5 << 4;								//数据偏移，高4bit: 0101 0000 表示5*4=20个字节——固定长度
	tcpHeader.contrl = 1;									//控制 0000 0001，FIN置1
	*(unsigned short *)tcpHeader.wndSize = htons(0xFFFF);	//窗口值，1111 1111 1111 1111

	//构建TCP伪首部
	PSDTCP_HEADER psdHeader;
	memset(&psdHeader, 0, sizeof psdHeader);
	*(unsigned int *)psdHeader.dstIpAddr = inet_addr(lpszSrcIp);	//点分十进制转化为长整数，目的ip地址
	*(unsigned int *)psdHeader.srcIpAddr = inet_addr(lpszDstIp);	//源ip地址
	psdHeader.protocol = 0x06;										//0x06为TCP，0x11为UDP
	*(unsigned short *)psdHeader.tcpLen = htons(sizeof(TCP_HEADER));//TCP长度

	//打包成TCP报文段
	byte psdPacket[1024];
	memset(psdPacket, 0, sizeof psdPacket);
	memcpy(psdPacket, &psdHeader, sizeof psdHeader);	//伪首部
	memcpy(psdPacket + sizeof psdHeader, &tcpHeader, sizeof tcpHeader);	//TCP首部

	//计算检验和
	*(unsigned short *)tcpHeader.checkSum = CheckSum((unsigned short*)psdPacket, sizeof psdHeader + sizeof tcpHeader);

	//IP数据报头部
	IP_HEADER ipHeader;
	memset(&ipHeader, 0, sizeof ipHeader);
	unsigned char versionAndLen = 0x04;	//0000 1000
	versionAndLen <<= 4;				//1000 0000
	versionAndLen |= (sizeof ipHeader / 4); //版本 + 头长度(或运算)

	ipHeader.versionAndHeader = versionAndLen;
	*(unsigned short *)ipHeader.totalLen = htons(sizeof(IP_HEADER) + sizeof(TCP_HEADER));	//总长度=IP数据报首部长+TCP首部长

	ipHeader.ttl = 0xFF;	//生存时间 1111 1111
	ipHeader.hiProtovolType = 0x06;//协议 TCP协议

	*(unsigned int *)(ipHeader.srcIpAddr) = inet_addr(lpszSrcIp);	//点分十进制转为长整型，源IP地址
	*(unsigned int *)(ipHeader.dstIpAddr) = inet_addr(lpszDstIp);	//目的IP地址
	*(unsigned short *)(ipHeader.headerCheckSum) = CheckSum((unsigned short *)&ipHeader, sizeof ipHeader);//首部校验和

	//构建以太网MAC帧头部
	ETHERNET_HEADER ethHeader;
	memset(&ethHeader, 0, sizeof ethHeader);
	memcpy(ethHeader.dstMacAddr, dstMac, 6);	//目的mac地址
	memcpy(ethHeader.srcMacAddr, srcMac, 6);	//源mac地址
	*(unsigned short *)ethHeader.ethernetType = htons(0x0800);	//类型，0x0800表示上层使用的是ip数据报

	//打包mac帧
	byte packet[1024];
	memset(packet, 0, sizeof packet);

	memcpy(packet, &ethHeader, sizeof ethHeader);	//+mac帧头部
	memcpy(packet + sizeof ethHeader, &ipHeader, sizeof ipHeader);	//+ip数据报头部
	memcpy(packet + sizeof ethHeader + sizeof ipHeader, &tcpHeader, sizeof tcpHeader);	//+tcp报文段头部

	int size = sizeof ethHeader + sizeof ipHeader + sizeof tcpHeader;	//包总长度，字节数
	if (0 == pcap_sendpacket(handle, packet, size))	//第一次挥手，CRC不用添加，网卡驱动计算后自动添加
	{
		printf("%-16s ------FIN-----> %-16s\n", lpszSrcIp, lpszDstIp);
		return 1;
	}
	else {
		return 0;
	}

}

/*
参数pcap_pkthdr 表示捕获到的数据包基本信息,包括时间,长度等信息.
struct pcap_pkthdr {
struct timeval ts; //时间戳
bpf_u_int32 caplen; // 已捕获部分的长度
bpf_u_int32 len; // 该包的脱机长度
};
参数recvPacket表示的捕获到的数据包的内容
 */
void HandlePacketCallBack(unsigned char *param, const struct pcap_pkthdr* packet_header, const unsigned char *recvPacket)
{
	unsigned short localPort = *(unsigned short *)param;

	//提取得到mac帧
	ETHERNET_HEADER *pEthHeader = (ETHERNET_HEADER *)recvPacket;
	//过滤掉非IP数据报
	if (*((unsigned short *)(pEthHeader->ethernetType)) != htons(0x0800)) return;

	//提取得到ip数据报
	IP_HEADER *pIpHeader = (IP_HEADER *)(recvPacket + sizeof(ETHERNET_HEADER));
	//过滤掉非TCP报文段
	if (pIpHeader->hiProtovolType != 0x06) return;

	//提取得到TCP报文段
	TCP_HEADER *pTcpHeader = (TCP_HEADER *)(recvPacket + sizeof(ETHERNET_HEADER) + sizeof(IP_HEADER));
	//过滤掉非发送给源端口的报文
	if (*(unsigned short *)(pTcpHeader->dstPort) != htons(localPort)) return;

	///////////////////////得到回复TCP报文，继续操作////////////////////////////

	//构造ip数据报
	IP_HEADER ipHeader;
	memset(&ipHeader, 0, sizeof ipHeader);
	unsigned char versionAndLen = 0x04;
	versionAndLen <<= 4;
	versionAndLen |= sizeof ipHeader / 4; //版本 + 头长度

	ipHeader.versionAndHeader = versionAndLen;
	*(unsigned short *)ipHeader.totalLen = htons(sizeof(IP_HEADER) + sizeof(TCP_HEADER));

	ipHeader.ttl = 0xFF;	//生存时间
	ipHeader.hiProtovolType = 0x06;	//协议

	memcpy(ipHeader.srcIpAddr, pIpHeader->dstIpAddr, sizeof(unsigned int));	//收到ip数据报中的目的ip地址即为发送源地址
	memcpy(ipHeader.dstIpAddr, pIpHeader->srcIpAddr, sizeof(unsigned int));	//收到ip数据报中的源ip地址即为目的地址

	*(unsigned short *)(ipHeader.headerCheckSum) = CheckSum((unsigned short *)&ipHeader, sizeof ipHeader);

	////////////////////////////////////////////////////////////////////
	unsigned int ack = ntohl(*(unsigned int *)(pTcpHeader->seqNumber));	//得到回复的序号seq，+1作为下次发送的确认号
	unsigned int seq = ntohl(*(unsigned int *)(pTcpHeader->ackNumber));	//得到回复的确认号ack，作为下次发送的序号

	//构建TCP报文段
	TCP_HEADER tcpHeader;
	memset(&tcpHeader, 0, sizeof tcpHeader);
	*(unsigned short *)tcpHeader.srcPort = htons(localPort);	//源端口号
	*(unsigned short *)tcpHeader.dstPort = htons(dstPort);		//目的端口号
	*(unsigned int *)tcpHeader.seqNumber = htonl(seq);			//序号
	*(unsigned int *)tcpHeader.ackNumber = htonl(ack + 1);		//确认号
	tcpHeader.headLen = 5 << 4;									//数据偏移，高4bit: 0101 0000 表示5*4=20个字节——固定长度
	tcpHeader.contrl = 0x01 << 4;								//ACK置1
	*(unsigned short *)tcpHeader.wndSize = htons(0xFFFF);		//窗口值 1111 1111

	///////////////////////////////////////////////////////////////////

	//构建TCP伪首部
	PSDTCP_HEADER psdHeader;
	memset(&psdHeader, 0x00, sizeof psdHeader);
	psdHeader.protocol = 0x06;
	*(unsigned short *)psdHeader.tcpLen = htons(sizeof(TCP_HEADER));
	memcpy(psdHeader.dstIpAddr, ipHeader.dstIpAddr, sizeof(unsigned int));
	memcpy(psdHeader.srcIpAddr, ipHeader.srcIpAddr, sizeof(unsigned int));

	//打包TCP报文段，伪首部+首部+数据部分
	byte psdPacket[1024];
	memcpy(psdPacket, &psdHeader, sizeof psdHeader);
	memcpy(psdPacket + sizeof psdHeader, &tcpHeader, sizeof tcpHeader);

	*(unsigned short *)tcpHeader.checkSum = CheckSum((unsigned short*)psdPacket, sizeof psdHeader + sizeof tcpHeader);

	//构建以太网mac帧头部
	ETHERNET_HEADER ethHeader;
	memset(&ethHeader, 0, sizeof ethHeader);
	memcpy(ethHeader.dstMacAddr, pEthHeader->srcMacAddr, 6);
	memcpy(ethHeader.srcMacAddr, pEthHeader->dstMacAddr, 6);
	*(unsigned short *)ethHeader.ethernetType = htons(0x0800);

	//打包mac帧
	byte packet[1024];
	memset(packet, 0, sizeof packet);

	memcpy(packet, &ethHeader, sizeof ethHeader);
	memcpy(packet + sizeof ethHeader, &ipHeader, sizeof ipHeader);
	memcpy(packet + sizeof ethHeader + sizeof ipHeader, &tcpHeader, sizeof tcpHeader);

	int size = sizeof ethHeader + sizeof ipHeader + sizeof tcpHeader;

	//得到网卡控制指针
	pcap_t *handle = (pcap_t*)(param + sizeof(unsigned short));

	byte data[] = "This is my homework of network ,I am happy!";

	char srcIp[32], dstIp[32];
	byte ctrl = pTcpHeader->contrl & 0x3F;//得到回复tcp报文段的控制信息 0x3f = 0011 1111
	switch (ctrl)
	{
	case 0x01 << 1: //SYN 0000 0010
		break;
		/*case 0x01 << 4: //ack
			puts("收到ack");
			break;*/
	case ((0x01 << 4) | (0x01 << 1)): //SYN+ACK 0001 0000 | 0000 0010 = 0001 0010 第二次握手

		FormatIpAddr(*(unsigned int *)(pIpHeader->srcIpAddr), srcIp);
		FormatIpAddr(*(unsigned int *)(pIpHeader->dstIpAddr), dstIp);
		printf("%-16s <--SYN + ACK--- %-16s\n", dstIp, srcIp);

		///////////////////////////////////////////////////////////

		//第三次握手
		pcap_sendpacket(handle, packet, size);
		FormatIpAddr(*(unsigned int *)ipHeader.srcIpAddr, srcIp);
		FormatIpAddr(*(unsigned int *)ipHeader.dstIpAddr, dstIp);
		printf("%-16s ------ACK-----> %-16s\n", srcIp, dstIp);

		Sleep(10);

		pIpHeader = (IP_HEADER *)(packet + sizeof(ETHERNET_HEADER));
		*(unsigned short *)(pIpHeader->totalLen) = htons(sizeof(IP_HEADER) + sizeof(TCP_HEADER) + sizeof data);
		memset(pIpHeader->headerCheckSum, 0x00, sizeof(unsigned short));
		*(unsigned short *)(pIpHeader->headerCheckSum) = CheckSum((unsigned short *)pIpHeader, sizeof(IP_HEADER));

		pTcpHeader = (TCP_HEADER *)(packet + sizeof(ETHERNET_HEADER) + sizeof(IP_HEADER));
		pTcpHeader->contrl = 0x01 << 4;
		*(unsigned int *)(pTcpHeader->ackNumber) = htonl(ack + 1);
		*(unsigned int *)(pTcpHeader->seqNumber) = htonl(seq);
		memset(pTcpHeader->checkSum, 0x00, sizeof(unsigned short));

		memset(psdPacket, 0x00, sizeof psdPacket);
		*(unsigned short *)psdHeader.tcpLen = htons(sizeof(TCP_HEADER) + sizeof(data));

		memcpy(psdPacket, &psdHeader, sizeof psdHeader);
		memcpy(psdPacket + sizeof psdHeader, pTcpHeader, sizeof(TCP_HEADER));
		memcpy(psdPacket + sizeof psdHeader + sizeof(TCP_HEADER), data, sizeof data);

		*(unsigned short *)(pTcpHeader->checkSum) = CheckSum((unsigned short*)psdPacket, sizeof psdHeader + sizeof(TCP_HEADER) + sizeof data);

		memcpy(packet, &ethHeader, sizeof ethHeader);
		memcpy(packet + sizeof(ETHERNET_HEADER), pIpHeader, sizeof(IP_HEADER));
		memcpy(packet + sizeof(ETHERNET_HEADER) + sizeof(IP_HEADER), pTcpHeader, sizeof(TCP_HEADER));
		memcpy(packet + sizeof(ETHERNET_HEADER) + sizeof(IP_HEADER) + sizeof(TCP_HEADER), data, sizeof data);

		size += sizeof data;

		if (0 == pcap_sendpacket(handle, packet, size))	printf("\nSend OK\n\n");
		else	printf("Send failed!\n");

		sayGoodbye(handle);



		break;
	case 0x01 << 4:	//ACK 0001 0000
		if (seq == 0x2D) {
			FormatIpAddr(*(unsigned int *)(pIpHeader->srcIpAddr), srcIp);
			FormatIpAddr(*(unsigned int *)(pIpHeader->dstIpAddr), dstIp);
			printf("%-16s <-----ACK------ %-16s\n", dstIp, srcIp);	//第二次挥手
		}


		break;
	case (0x01 | (0x01 << 4)):	//FIN+ACK 0000 0001 | 0001 0000 = 0001 0001

		FormatIpAddr(*(unsigned int *)(pIpHeader->srcIpAddr), srcIp);
		FormatIpAddr(*(unsigned int *)(pIpHeader->dstIpAddr), dstIp);
		if (strcmp(srcIp, lpszDstIp) == 0)
		{
			printf("%-16s <--FIN + ACK--- %-16s\n", dstIp, srcIp);	//第三次挥手

			//第四次挥手
			pcap_sendpacket(handle, packet, size);
			FormatIpAddr(*(unsigned int *)ipHeader.srcIpAddr, srcIp);
			FormatIpAddr(*(unsigned int *)ipHeader.dstIpAddr, dstIp);
			printf("%-16s ------ACK-----> %-16s\n", srcIp, dstIp);
			exit(0);
		}

		break;

	default:
		break;
		/*
		IP_HEADER *pIpHeader = (IP_HEADER *)(recvPacket + sizeof(ETHERNET_HEADER));
		unsigned s hort ipHeaderLen = pIpHeader->versionAndHeader & 0x0F;
		ipHeaderLen *= 4;
		TCP_HEADER *pTcpHeader = (TCP_HEADER *)(recvPacket + sizeof(ETHERNET_HEADER) + ipHeaderLen);

		int tcpHeaderLen = pTcpHeader->headLen >> 0x04;
		tcpHeaderLen *= 4;
		char *str = (char *)(recvPacket + sizeof(ETHERNET_HEADER) + ipHeaderLen + tcpHeaderLen);
		puts(str);
		*/
	}
	return;
}

int main(int argc, char* argv[])
{
	//srand(time(0));
	//srcPort = rand() % 65535;//6382;源端口随机

	if (argc != 5)
	{
		printf("Useage: SendTcp soruce_ip source_port dest_ip dest_port \n");
		return false;
	}

	lpszSrcIp = argv[1];
	srcPort = (unsigned short)atoi(argv[2]);
	lpszDstIp = argv[3];
	dstPort = (unsigned short)atoi(argv[4]);

	char szError[1024];
	const char *lpszAdapterName = "\\Device\\NPF_{9BF865ED-2A3F-4141-AA4A-44CD37E68B45}";
	pcap_t *handle = pcap_open_live(lpszAdapterName, 65536, 1, 1000, szError);//获得数据包捕获描述字的函数,打开指定网卡
	if (NULL == handle) return 0;

	//构建TCP首部
	TCP_HEADER tcpHeader;
	memset(&tcpHeader, 0, sizeof tcpHeader);
	*(unsigned short *)tcpHeader.srcPort = htons(srcPort);	//源端口，16bit
	*(unsigned short *)tcpHeader.dstPort = htons(dstPort);	//目的端口，16bit
	*(unsigned int *)tcpHeader.seqNumber = htonl(0x00);		//序号，32bit
	*(unsigned int *)tcpHeader.ackNumber = htonl(0x00);		//确认号，32bit
	tcpHeader.headLen = 5 << 4;								//数据偏移，高4bit: 0101 0000 表示5*4=20个字节——固定长度
	tcpHeader.contrl = 1 << 1;								//控制 0000 0010，SYN置1
	*(unsigned short *)tcpHeader.wndSize = htons(0xFFFF);	//窗口值，1111 1111 1111 1111

	//构建TCP伪首部
	PSDTCP_HEADER psdHeader;
	memset(&psdHeader, 0, sizeof psdHeader);
	*(unsigned int *)psdHeader.dstIpAddr = inet_addr(lpszSrcIp);	//点分十进制转化为长整数，目的ip地址
	*(unsigned int *)psdHeader.srcIpAddr = inet_addr(lpszDstIp);	//源ip地址
	psdHeader.protocol = 0x06;										//0x06为TCP，0x11为UDP
	*(unsigned short *)psdHeader.tcpLen = htons(sizeof(TCP_HEADER));//TCP长度

	//打包成TCP报文段字节流
	byte psdPacket[1024];
	memset(psdPacket, 0, sizeof psdPacket);
	memcpy(psdPacket, &psdHeader, sizeof psdHeader);	//伪首部
	memcpy(psdPacket + sizeof psdHeader, &tcpHeader, sizeof tcpHeader);	//TCP首部

	//计算检验和
	*(unsigned short *)tcpHeader.checkSum = CheckSum((unsigned short*)psdPacket, sizeof psdHeader + sizeof tcpHeader);

	//IP数据报头部
	IP_HEADER ipHeader;
	memset(&ipHeader, 0, sizeof ipHeader);
	unsigned char versionAndLen = 0x04;	//0000 1000
	versionAndLen <<= 4;				//1000 0000
	versionAndLen |= (sizeof ipHeader / 4); //版本 + 头长度(或运算)

	ipHeader.versionAndHeader = versionAndLen;
	*(unsigned short *)ipHeader.totalLen = htons(sizeof(IP_HEADER) + sizeof(TCP_HEADER));	//总长度=IP数据报首部长+TCP首部长

	ipHeader.ttl = 0xFF;	//生存时间 1111 1111
	ipHeader.hiProtovolType = 0x06;//协议 TCP协议

	*(unsigned int *)(ipHeader.srcIpAddr) = inet_addr(lpszSrcIp);	//点分十进制转为长整型，源IP地址
	*(unsigned int *)(ipHeader.dstIpAddr) = inet_addr(lpszDstIp);	//目的IP地址
	*(unsigned short *)(ipHeader.headerCheckSum) = CheckSum((unsigned short *)&ipHeader, sizeof ipHeader);//首部校验和

	//构建以太网MAC帧头部
	ETHERNET_HEADER ethHeader;
	memset(&ethHeader, 0, sizeof ethHeader);
	memcpy(ethHeader.dstMacAddr, dstMac, 6);	//目的mac地址
	memcpy(ethHeader.srcMacAddr, srcMac, 6);	//源mac地址
	*(unsigned short *)ethHeader.ethernetType = htons(0x0800);	//类型，0x0800表示上层使用的是ip数据报

	//打包mac帧
	byte packet[1024];
	memset(packet, 0, sizeof packet);

	memcpy(packet, &ethHeader, sizeof ethHeader);	//+mac帧头部
	memcpy(packet + sizeof ethHeader, &ipHeader, sizeof ipHeader);	//+ip数据报头部
	memcpy(packet + sizeof ethHeader + sizeof ipHeader, &tcpHeader, sizeof tcpHeader);	//+tcp报文段头部

	int size = sizeof ethHeader + sizeof ipHeader + sizeof tcpHeader;	//包总长度，字节数
	pcap_sendpacket(handle, packet, size);	//第一次握手，CRC不用添加，网卡驱动计算后自动添加
	printf("%-16s ------SYN-----> %-16s\n", lpszSrcIp, lpszDstIp);

	if (NULL == handle)
	{
		printf("\nUnable to open the adapter. %s is not supported by WinPcap\n");
		return 0;
	}

	byte param[1024];
	memset(param, 0x00, sizeof param);
	memcpy(param, &srcPort, sizeof srcPort);	//源端口
	memcpy(param + sizeof srcPort, handle, 512);	//网卡指针

	pcap_loop(handle, -1, HandlePacketCallBack, param);	//第一个参数是winpcap的句柄,第二个是指定捕获的数据包个数,如果为-1则无限循环捕获。
														//第三个参数是回调函数，第四个参数user是留给用户使用的，用于传递信息给回调函数
	pcap_close(handle);
	return 0;
}