﻿#define __STDC_WANT_LIB_EXT1__ 1

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <string.h>

#include <stdio.h>
#include "headers\main.h"

const char *const USING_GUIDE = "How to use: traceroute [flags] <destination IP or domain name>\nflags:\n-h <max_hops>\t\t define the maximum hops count (>= 1)\n\
-n <pack_num>\t\t define how many packetes (>= 1) to send to each node\n-w <time_to_wait>\t define time-out for each packet in milliseconds\n\
-d\t\t do not resolve IPs to domain names\n";

const char *const STARTUP_MSG = "Tracing route to ";
const char *const STARTUP_MSG2 = ", sending ";
const char *const STARTUP_MSG3 = " packets of size 32 to each router with maximum of ";
const char *const STARTUP_MSG4 = " hops\n\n";
const char *const INVALID_ARGS = "Invalid params\n";
const char *const SENDING_FAILED = "Failed to send data, exiting...\n";
const char *const TIME_OUT = "   T/O";
const char *const SUCCESS_END = "Tracing completed\n";
const char *const NOT_ENOUGH_HOPS = "Hops limit depleted, destination was not reached\n";
const char *const FAIL_END = "traceroute ended with failure\n";

const char *const CANNOT_RESOLVE = "Destination IP address is invalid,\nor cannot find IP address of provided domain name\n";

const char *const DLL_FAILED = "DLL failed to load!\n";
const char *const SCKT_FAILED = "Failed to create RAW-socket\n";
const char *const BIND_FAILED = "Cannot bind socket to a local address\n";
const char *const ADDR_NOTFOUND = "Appropriate address for connection not found\n";
const char *const ARGS_ERR = "Error in args\n";
const char *const CANNOT_RECIEVE = "Critical error when tried to receive data! Exiting...\n";
const char *const SELECT_FAILED = "Select() failed! Exiting...\n";
const char *const PRESS_ENTER = "Press [ENTER] to continue\n";

void cleanUp(const char *const msg, struct addrinfo *result)
{
	printf(msg);
	printf("WSAGetLastError: %d\n", WSAGetLastError());
	WSACleanup();
	if (result != NULL) freeaddrinfo(result);
	printf(PRESS_ENTER);
	getchar();
}

void invalidArgs(void)
{
	printf(INVALID_ARGS);
	printf(USING_GUIDE);
	printf(PRESS_ENTER);
	getchar();
}

USHORT calcICMPChecksum(USHORT *packet, int size)
{
	ULONG checksum = 0;
	while (size > 1) {
		checksum += *(packet++);
		size -= sizeof(USHORT);
	}
	if (size) checksum += *(UCHAR *)packet;

	checksum = (checksum >> 16) + (checksum & 0xFFFF);
	checksum += (checksum >> 16);

	return (USHORT)(~checksum);
}

void initPingPacket(PICMPHeader icmp_hdr, int seqNo)
{
	icmp_hdr->msg_type = ICMP_ECHO_REQUEST;
	icmp_hdr->msg_code = 0;
	icmp_hdr->checksum = 0;
	icmp_hdr->id = LOWORD(GetCurrentProcessId());
	icmp_hdr->seq = seqNo;

	int bytesLeft = DEFAULT_PACKET_SIZE - sizeof(struct _tag_ICMPHeader);
	char *newData = (char *)icmp_hdr + sizeof(struct _tag_ICMPHeader);
	char symb = 'a';
	while (bytesLeft > 0) {
		*(newData++) = symb++;
		bytesLeft--;
	}
	icmp_hdr->checksum = calcICMPChecksum((USHORT *)icmp_hdr, DEFAULT_PACKET_SIZE);
}

int sendPingReq(SOCKET traceSckt, PICMPHeader sendBuf, const struct sockaddr_in *dest)
{
	int sendRes = sendto(traceSckt, (char *)sendBuf, DEFAULT_PACKET_SIZE, 0, (struct sockaddr *)dest, sizeof(struct sockaddr_in));

	if (sendRes == SOCKET_ERROR) return sendRes;
	return 0;
}

int recvPingResp(SOCKET traceSckt, PIPHeader recvBuf, struct sockaddr_in *source, long timeout)
{
	int srcLen = sizeof(struct sockaddr_in);

	fd_set singleSocket;
	singleSocket.fd_count = 1;
	singleSocket.fd_array[0] = traceSckt;

	long microseconds = timeout * 1000;

	struct timeval timeToWait = {microseconds / 1000000, microseconds % 1000000};

	int selectRes;
	if ((selectRes = select(0, &singleSocket, NULL, NULL, &timeToWait)) == 0) return 0; // time-out
	if (selectRes == SOCKET_ERROR) return 1;
	
	return recvfrom(traceSckt, (char *)recvBuf, MAX_PING_PACKET_SIZE, 0, (struct sockaddr *)source, &srcLen);
}

void printPackInfo(PPacketDetails details, BOOL printIP, BOOL resolveName)
{
	printf("%6d", details->ping);

	if (printIP) {
		char *srcAddr = inet_ntoa(details->source->sin_addr);
		if (srcAddr != NULL) {
			printf("\t%s", srcAddr);
			
			if (resolveName) {
				char hostName[32];
				getnameinfo((struct sockaddr *)(details->source), sizeof(struct sockaddr_in), hostName, 32, NULL, 0, 0);
				printf("\t[%s]", hostName);
			}
		} else {
			printf("\t\tINVALID IP");
		}
	}
}

int decodeReply(PIPHeader ipHdr, struct sockaddr_in *source, USHORT seqNo, ULONG sendingTime, PPacketDetails decodeResult)
{
	DWORD arrivalTime = GetTickCount();

	unsigned short ipHdrLen = (ipHdr->ver_n_len & IPHDR_LEN_MASK) * 4;
	PICMPHeader icmpHdr = (PICMPHeader)((char *)ipHdr + ipHdrLen);

	if (icmpHdr->msg_type == ICMP_TTL_EXPIRE) {
		PIPHeader requestIPHdr = (PIPHeader)((char *)icmpHdr + 8);
		unsigned short requestIPHdrLen = (requestIPHdr->ver_n_len & IPHDR_LEN_MASK) * 4;

		PICMPHeader requestICMPHdr = (PICMPHeader)((char *)requestIPHdr + requestIPHdrLen);

		if ((requestICMPHdr->id == LOWORD(GetCurrentProcessId())) && (requestICMPHdr->seq == seqNo)) {
			decodeResult->source = source;
			decodeResult->ping = arrivalTime - sendingTime;
			return TRACE_TTL_EXP;
		}
	}

	if (icmpHdr->msg_type == ICMP_ECHO_REPLY) {
		if ((icmpHdr->id == LOWORD(GetCurrentProcessId())) && (icmpHdr->seq == seqNo)) {
			decodeResult->source = source;
			decodeResult->ping = arrivalTime - sendingTime;
			return TRACE_END_SUCCESS;
		}
	}

	return WRONG_PACKET;
}

int parseArgs(int argc, char *argv[], PArguments args)
{
	if (argc < 2) return 1;

	char *flags[FLAGS_COUNT] = {"-h", "-n", "-w", "-d"};

	args->hopsCount = DEFAULT_HOPS;
	args->packetCount = DEFAULT_PACK_COUNT;
	args->timeOut = DEFAULT_TIME_OUT;
	args->resolveName = DEFAULT_RESOLVING_MODE;

	for (int i = 1; i < argc - 1; i++) {
		int j = 0;
		while ((strcmp(argv[i], flags[j]) != 0) && (j < FLAGS_COUNT)) j++; 
		if (j == FLAGS_COUNT) return 1; // if j == FLAGS_COUNT then flag wasn't found -- param is invalid

		switch (j) {
			case 0:
				if (i + 1 == argc - 1) return 1; // if next param is IP, then there's no value for the flag
				if ((args->hopsCount = atoi(argv[i + 1])) == 0) {
					return 1;
				}
				i++;
				break;
			case 1:
				if (i + 1 == argc - 1) return 1;
				if ((args->packetCount = atoi(argv[i + 1])) == 0) {
					return 1;
				}
				i++;
				break;
			case 2:
				if (i + 1 == argc - 1) return 1;
				if ((args->timeOut = atoi(argv[i + 1])) == 0) {
					return 1;
				}
				i++;
				break;
			case 3:
				args->resolveName = FALSE;
				break;
		}
	}
	return 0;
}

int main(int argc, char *argv[])
{
	Arguments args;
	if (parseArgs(argc, argv, &args) != 0) {
		invalidArgs();
		return 0;
	}

	WSADATA wsaData;

	if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
		cleanUp(DLL_FAILED, NULL);
		return 1;
	}

	struct addrinfo hints, *result;

	char *strDestIP = "";

	BOOL DNProvided = FALSE;
	UINT destAddr = inet_addr(argv[argc - 1]); // getting the IP addr from cmd params
	if (destAddr == INADDR_NONE) {	
		ZeroMemory(&hints, sizeof(hints));
		hints.ai_family = AF_INET;
		
		getaddrinfo(argv[argc - 1], NULL, &hints, &result);
		if (result != NULL) {
			strDestIP = inet_ntoa(((struct sockaddr_in *)result->ai_addr)->sin_addr);
			destAddr = ((struct sockaddr_in *)result->ai_addr)->sin_addr.s_addr;
			DNProvided = TRUE;
			freeaddrinfo(result);
		} else {
			cleanUp(CANNOT_RESOLVE, result);
			invalidArgs();
			return 1;
		}
	}

	printf(STARTUP_MSG);
	printf(argv[argc - 1]);
	if (DNProvided) {
		printf(" [%s]", strDestIP);
	}
	printf(STARTUP_MSG2);
	printf("%d", args.packetCount);
	printf(STARTUP_MSG3);
	printf("%d", args.hopsCount);
	printf(STARTUP_MSG4);

	struct sockaddr_in dest, source;
	SOCKET traceSckt = WSASocket(AF_INET, SOCK_RAW, IPPROTO_ICMP, NULL, 0, 0);

	if (traceSckt == INVALID_SOCKET) {
		cleanUp(SCKT_FAILED, NULL);
		return 1;
	}

	ZeroMemory(&dest, sizeof(dest));
	dest.sin_addr.s_addr = destAddr;
	dest.sin_family = AF_INET;

	PICMPHeader sendBuf = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, DEFAULT_PACKET_SIZE);
	PIPHeader recvBuf = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, MAX_PING_PACKET_SIZE);

	int routerNo = 1;
	USHORT seqNo = 10;	// unsigned overflowing is OK
	ULONG sendingTime;

	BOOL traceEnd = FALSE, error = FALSE, printIP;

	DWORD packageTTL = 0;

	do {
		packageTTL++;
		setsockopt(traceSckt, IPPROTO_IP, IP_TTL, (char *)&packageTTL, sizeof(DWORD));

		printIP = FALSE;
		printf("%3d.", routerNo++);
		for (int packNo = 1; packNo <= args.packetCount; packNo++) {
			if (packNo == args.packetCount) printIP = TRUE;

			initPingPacket(sendBuf, seqNo);
			sendingTime = GetTickCount();
			if (sendPingReq(traceSckt, sendBuf, &dest) == SOCKET_ERROR) {
				printf(SENDING_FAILED);
				error = TRUE;
				break;
			}
			
			PacketDetails details;
			ZeroMemory(&details, sizeof(details));

			int recvRes = 2, wrongCount = 0, decodeRes = WRONG_PACKET;

			// if we get 10 wrong packets in a row, our is probably lost somewhere
			while ((decodeRes == WRONG_PACKET) && (recvRes > 1) && (wrongCount++ <= 10) && !error) {
				recvRes = recvPingResp(traceSckt, recvBuf, &source, args.timeOut);
				if (recvRes == 0) {
					printf(TIME_OUT);
				} else if (recvRes == 1) {
					printf(SELECT_FAILED);
					error = TRUE;
				} else if (recvRes == SOCKET_ERROR) {
					printf(CANNOT_RECIEVE);
					error = TRUE;
				} else {
					decodeRes = decodeReply(recvBuf, &source, seqNo, sendingTime, &details);
				}
			}

			if (recvRes > 1) {
				if (decodeRes == WRONG_PACKET) {
					printf(TIME_OUT);
				} else {
					if (decodeRes == TRACE_END_SUCCESS) traceEnd = TRUE;
					printPackInfo(&details, printIP, args.resolveName);
				}
			}
			seqNo++;
		}
		printf("\n");
	} while (!traceEnd && !error && (packageTTL < args.hopsCount));
	
	closesocket(traceSckt);
	WSACleanup();
	if (traceEnd) printf(SUCCESS_END);
	else if (!error && !traceEnd) printf(NOT_ENOUGH_HOPS);
	else if (error) printf(FAIL_END);
	printf(PRESS_ENTER);
	getchar();
	return 0;
}
