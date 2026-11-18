


#ifndef __Packet_h
#define __Packet_h

#define MAX_PACKET_DATA	1500
//##################################################################################################
class PacketStoredInfo	
{
public:
	u_int32_t		tv_sec;
	u_int32_t		tv_usec;

	u_int16_t		pkt_len;
	u_int16_t		stored_len;

public:
	PacketStoredInfo(void){memset(this, 0, sizeof(PacketStoredInfo));};
	PacketStoredInfo(PacketStoredInfo *r){memcpy(this, r, sizeof(PacketStoredInfo));};
	~PacketStoredInfo(void){};

	void reset(){memset(this, 0, sizeof(PacketStoredInfo));};
	void set(PacketStoredInfo* r){memcpy(this, r, sizeof(PacketStoredInfo));};
	void print()
	{
		printf("%2d.%03d : [pkt_len:%6d]  [stored_len:%6d]\n", tv_sec%60, tv_usec/1000, pkt_len, stored_len);
	};
};



//##################################################################################################
class Packet
{
public:
	u_int32_t		time_sec;
	u_int32_t		time_usec;


	u_int32_t		src_addr;
	u_int32_t		dst_addr;

	u_int16_t		src_port;
	u_int16_t		dst_port;

	u_int16_t		tcp_window;
	u_int16_t		ip_offset; 

	u_int32_t		tcp_sn;
	u_int32_t		tcp_an;
	
	u_int8_t		tcp_flags;		
	u_int8_t		ip_proto;
	u_int8_t		ip_tos;
	u_int8_t		ip_ttl;		

	u_int8_t		ip_hlen;
	u_int8_t		tcp_hlen;
	u_int8_t		udp_hlen;
	u_int8_t		pad_2;

	u_int16_t		real_pkt_len;
	u_int16_t		real_payload_len;

	u_int16_t		stored_pkt_len;
	u_int16_t		stored_payload_len;

	unsigned char	*stored_pkt;
	unsigned char	*stored_payload;
public:
	Packet(void){memset(this, 0, sizeof(Packet));};
	Packet(Packet *r){memcpy(this, r, sizeof(Packet));};
	~Packet(void){};

	void reset(){memset(this, 0, sizeof(Packet));};
	void set(Packet* r){memcpy(this, r, sizeof(Packet));};

	void set(PacketStoredInfo *pktInfo, unsigned char *p);

	void print();
	void printDetail();
};




#endif

