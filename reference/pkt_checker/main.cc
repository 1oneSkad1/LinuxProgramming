//#####################################################################
//
//		main.c
//		programmed by myung-sup kim
//		at 2025.02.27, 2007.04.04
//
//######################################################################

#include "include.h" 
#include "util.h"
#include "packet.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>


FILE			*pkt_fp = NULL;
Packet			pkt;
PacketStoredInfo 	pktInfo;


//#######################################################################
void functionCtrlC(int signo)
{
	printf("Ctrl-C is pressed !! \n");
	exit(0);
}


//#######################################################################
void init()
{
	g_p_time();

	pkt_fp	= NULL;
	pktInfo.reset();
	pkt.reset();
}

//***************************************************************************************
int main(int argc, char** argv)
{ 
	int len;
	unsigned char payload[3000];
	unsigned long long int pkts	=0, c_pkts=0;
	unsigned long long int bytes=0, c_bytes=0;

	if( argc != 2 ) g_err("usage: ./m <filename>");

	init();
	printf("############### flow record checker start with:\n [%s] \n\n", argv[1]);

	if( (pkt_fp = fopen( argv[1], "rb")) == NULL ) g_err( "FILE OPEN ....!!!");

	while ( len = fread( &pktInfo, 1, sizeof(PacketStoredInfo),  pkt_fp) )
	{
		if(pktInfo.stored_len == 0 ) continue;
		//pktInfo.print();
	
		memset(payload, 0, sizeof(payload) );
		len = fread(payload, 1, pktInfo.stored_len, pkt_fp);
		if(len != pktInfo.stored_len)
		{
			printf("\n\n");
			printf("ERROR: stored_len=%d, read_len=%d ...\n",pktInfo.stored_len, len );
			printf("\n\n");
			break;
		}

		pkt.set(&pktInfo, payload);

	    //if(pkt.src_port == 50889 || pkt.dst_port ==50889) 
		{
			pkt.printDetail();

			pkts  ++;
			bytes += pkt.real_pkt_len;
		}

		pktInfo.reset();
		pkt.reset();


		//getchar();
	}
	fclose( pkt_fp );

/*
	printf("\n#Summary: [pkts: %10llu] [bytes: %10llu] \n", pkts, bytes);

	printf("############## flow record checker end   ###############\n\n\n");
*/

}



