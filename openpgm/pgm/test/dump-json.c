/* vim:ts=8:sts=8:sw=4:noai:noexpandtab
 *
 * JSON packet dump.
 *
 * Copyright (c) 2006-2007 Miru Limited.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>

#include <glib.h>

#include "pgm/packet.h"


/* globals */

#define OPTIONS_TOTAL_LEN(x)	*(guint16*)( ((char*)(x)) + sizeof(struct pgm_opt_header) )


int verify_ip_header (struct iphdr*, int);
void print_ip_header (struct iphdr*);
int verify_pgm_header (struct pgm_header*, int);
void print_pgm_header (struct pgm_header*);
int verify_spm (struct pgm_header*, char*, int);
void print_spm (struct pgm_header*, char*);
int verify_poll (struct pgm_header*, char*, int);
void print_poll (struct pgm_header*, char*);
int verify_polr (struct pgm_header*, char*, int);
void print_polr (struct pgm_header*, char*);
int verify_odata (struct pgm_header*, char*, int);
void print_odata (struct pgm_header*, char*);
int verify_rdata (struct pgm_header*, char*, int);
void print_rdata (struct pgm_header*, char*);
static int generic_verify_nak (const char*, struct pgm_header*, char*, int);
static void generic_print_nak (const char*, struct pgm_header*, char*);
int verify_nak (struct pgm_header*, char*, int);
void print_nak (struct pgm_header*, char*);
int verify_nnak (struct pgm_header*, char*, int);
void print_nnak (struct pgm_header*, char*);
int verify_ncf (struct pgm_header*, char*, int);
void print_ncf (struct pgm_header*, char*);
int verify_spmr (struct pgm_header*, char*, int);
void print_spmr (struct pgm_header*, char*);
int verify_options (char*, int);
void print_options (char*);


int
monitor_packet (
	char*	data,
	int	len
	)
{
	static int count = 0;

	puts ("{");
	printf ("\t\"id\": %i,\n", ++count);

	int retval = 0;

	struct iphdr* ip = (struct iphdr*)data;
	if (verify_ip_header (ip, len) < 0) {
		puts ("\t\"valid\": false");
		retval = -1;
		goto out;
	}

	struct pgm_header* pgm = data + (ip->ihl * 4);
	int pgm_len = len - (ip->ihl * 4);
	if (verify_pgm_header (pgm, pgm_len) < 0) {
		puts ("\t\"valid\": false");
		retval = -1;
		goto out;
	}

	char* pgm_data = (char*)(pgm + 1);
	pgm_len -= sizeof(struct pgm_header);
	switch (pgm->pgm_type) {
	case PGM_SPM:	retval = verify_spm (pgm, pgm_data, pgm_len); break;
	case PGM_POLL:	retval = verify_poll (pgm, pgm_data, pgm_len); break;
	case PGM_POLR:	retval = verify_polr (pgm, pgm_data, pgm_len); break;
	case PGM_ODATA:	retval = verify_odata (pgm, pgm_data, pgm_len); break;
	case PGM_RDATA:	retval = verify_rdata (pgm, pgm_data, pgm_len); break;
	case PGM_NAK:	retval = verify_nak (pgm, pgm_data, pgm_len); break;
	case PGM_NNAK:	retval = verify_nnak (pgm, pgm_data, pgm_len); break;
	case PGM_NCF:	retval = verify_ncf (pgm, pgm_data, pgm_len); break;
	case PGM_SPMR:	retval = verify_spmr (pgm, pgm_data, pgm_len); break;
	}

	if (retval < 0) {
		puts ("\t\"valid\": false");
		goto out;
	}

/* packet verified correct */
	puts ("\t\"valid\": true,");

	print_ip_header (ip);
	print_pgm_header (pgm);

	switch (pgm->pgm_type) {
	case PGM_SPM:	print_spm (pgm, pgm_data); break;
	case PGM_POLL:	print_poll (pgm, pgm_data); break;
	case PGM_POLR:	print_polr (pgm, pgm_data); break;
	case PGM_ODATA:	print_odata (pgm, pgm_data); break;
	case PGM_RDATA:	print_rdata (pgm, pgm_data); break;
	case PGM_NAK:	print_nak (pgm, pgm_data); break;
	case PGM_NNAK:	print_nnak (pgm, pgm_data); break;
	case PGM_NCF:	print_ncf (pgm, pgm_data); break;
	case PGM_SPMR:	print_spmr (pgm, pgm_data); break;
	}

out:
	puts ("}");
	return retval;
}

	
int
verify_ip_header (
	struct iphdr*	ip,
	int		len
	)
{
/* minimum size should be IP header plus PGM header */
	if (len < (sizeof(struct iphdr) + sizeof(struct pgm_header))) 
	{
		printf ("\t\"message\": \"IP: packet size too small: %i bytes, expecting at least %" G_GSIZE_FORMAT " bytes.\",\n", len, sizeof(struct pgm_header));
		return -1;
	}

/* IP packet header: IPv4
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |Version|  HL   |      ToS      |            Length             |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |           Fragment ID         |R|D|M|     Fragment Offset     |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |      TTL      |    Protocol   |           Checksum            |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                       Source IP Address                       |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                    Destination IP Address                     |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | IP Options when present ...
 * +-+-+-+-+-+-+-+-+-+-+-+-+ ...
 * | Data ...
 * +-+-+- ...
 *
 * IPv6: n/a
 */

/* decode IP header */
	if (ip->version != 4 && ip->version != 6) {	/* IP version, 4 or 6 */
		printf ("\t\"message\": \"IP: unknown IP version %i.\",\n", ip->version);
		return -1;
	}

	guint ip_header_length = ip->ihl * 4;		/* IP header length in 32bit octets */
	if (ip_header_length < sizeof(struct iphdr)) {
		printf ("\t\"message\": \"IP: bad IP header length %i, should be at least %" G_GSIZE_FORMAT "lu bytes.\",\n", ip_header_length, sizeof(struct iphdr));
		return -1;
	}

/* ip_len can equal packet_length - ip_header_length in FreeBSD/NetBSD
 * Stevens/Fenner/Rudolph, Unix Network Programming Vol.1, p.739 
 * 
 * RFC3828 allows partial packets such that len < packet_length with UDP lite
 */
	int packet_length = g_ntohs(ip->tot_len);	/* total packet length */
	if (len < packet_length) {			/* redundant: often handled in kernel */
		printf ("\t\"message\": \"IP: truncated IP packet: header reports %i actual length %i bytes.\",\n", (int)len, (int)packet_length);
		return -1;
	}

/* TCP Segmentation Offload (TSO) might have zero length here */
	if (packet_length < ip_header_length) {
		printf ("\t\"message\": \"IP: header reports %i less than IP header length %i.\",\n", (int)packet_length, (int)ip_header_length);
		return -1;
	}

/* packets that fail checksum will generally not be passed upstream except with rfc3828
 */
	int sum = pgm_inet_checksum((char*)ip, ip_header_length, 0);
	if (sum != 0) {
		int ip_sum = g_ntohs(ip->check);
		printf ("\t\"message\": \"IP: IP header checksum incorrect: 0x%x.\",\n", ip_sum);
		return -2;
	}

	if (ip->protocol != IPPROTO_PGM) {
		printf ("\t\"message\": \"IP: packet IP protocol not PGM: %i.\",\n", ip->protocol);
		return -1;
	}

/* fragmentation offset, bit 0: 0, bit 1: do-not-fragment, bit 2: more-fragments */
	int offset = g_ntohs(ip->frag_off);
	if ((offset & 0x1fff) != 0) {
		printf ("\t\"message\": \"IP: fragmented IP packet, ignoring.\",\n");
		return -1;
	}

	return 0;
}

void
print_ip_header (
	struct iphdr*	ip
	)
{
	puts ("\t\"IP\": {");
	printf ("\t\t\"version\": %i,\n", ip->version);
	printf ("\t\t\"headerLength\": %i,\n", ip->ihl);
	printf ("\t\t\"ToS\": %i,\n", ip->tos & 0x3);
	printf ("\t\t\"length\": %i,\n", g_ntohs(ip->tot_len));
	printf ("\t\t\"fragmentId\": %i,\n", g_ntohs(ip->id));
    	printf ("\t\t\"DF\": %s,\n", (g_ntohs(ip->frag_off) & 0x4000) ? "true" : "false");
    	printf ("\t\t\"MF\": %s,\n", (g_ntohs(ip->frag_off) & 0x2000) ? "true" : "false");
    	printf ("\t\t\"fragmentOffset\": %i,\n", g_ntohs(ip->frag_off) & 0x1fff);
	printf ("\t\t\"TTL\": %i,\n", ip->ttl);
	printf ("\t\t\"protocol\": %i,\n", ip->protocol);
	printf ("\t\t\"sourceIp\": \"%s\",\n", inet_ntoa(*(struct in_addr*)&ip->saddr));
	printf ("\t\t\"destinationIp\": \"%s\",\n", inet_ntoa(*(struct in_addr*)&ip->daddr));
	puts ("\t\t\"IpOptions\": {");
	puts ("\t\t}");
	puts ("\t},");
}

int
verify_pgm_header (
	struct pgm_header*	pgm,
	int		pgm_len
	)
{

/* PGM payload, header looks as follows:
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |         Source Port           |       Destination Port        |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |      Type     |    Options    |           Checksum            |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                        Global Source ID                   ... |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | ...    Global Source ID       |           TSDU Length         |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | Type specific data ...
 * +-+-+-+-+-+-+-+-+-+- ...
 */
	if (pgm_len < sizeof(pgm)) {
		printf ("\t\"message\": \"PGM: packet size less than PGM header: %i bytes.\",\n", pgm_len);
		return -1;
	}

	if (pgm->pgm_checksum)
	{
		int sum = pgm->pgm_checksum;
		pgm->pgm_checksum = 0;
		int pgm_sum = pgm_checksum((const char*)pgm, pgm_len, 0);
		if (pgm_sum != sum) {
			printf ("\t\"message\": \"PGM: PGM packet checksum incorrect: 0x%x.\",\n", sum);
			return -2;
		}
	} else {
		if (pgm->pgm_type != PGM_ODATA && pgm->pgm_type != PGM_RDATA) {
			printf ("\t\"message\": \"PGM: No PGM checksum value, mandatory for ODATA/RDATA.\",\n");
			return -1;
		}
	}

	if (	pgm->pgm_type != PGM_SPM &&
		pgm->pgm_type != PGM_POLL &&
		pgm->pgm_type != PGM_POLR &&
		pgm->pgm_type != PGM_ODATA &&
		pgm->pgm_type != PGM_RDATA &&
		pgm->pgm_type != PGM_NAK &&
		pgm->pgm_type != PGM_NNAK &&
		pgm->pgm_type != PGM_NCF &&
		pgm->pgm_type != PGM_SPMR	)
	{
		printf ("\t\"message\": \"PGM: Not a valid PGM packet type: %i.\",\n", pgm->pgm_type);
		return -1;
	}

	return 0;
}

/* note: output trails tsdu length line to allow for comma
 */

void
print_pgm_header (
	struct pgm_header*	pgm
	)
{
	puts ("\t\"PGM\": {");
	printf ("\t\t\"sourcePort\": %i,\n", g_ntohs(pgm->pgm_sport));
	printf ("\t\t\"destinationPort\": %i,\n", g_ntohs(pgm->pgm_dport));
	printf ("\t\t\"type\": \"%s\",\n", pgm_type_string(pgm->pgm_type & 0xf));
	printf ("\t\t\"version\": %i,\n", (pgm->pgm_type & 0xc0) >> 6);
	puts ("\t\t\"options\": {");
	printf ("\t\t\t\"networkSignificant\": %s,\n", (pgm->pgm_options & (0x1 << 6)) ? "true" : "false");
	printf ("\t\t\t\"parityPacket\": %s,\n", (pgm->pgm_options & (0x1 << 0)) ? "true" : "false");
	printf ("\t\t\t\"variableLength\": %s\n", (pgm->pgm_options & (0x1 << 1)) ? "true" : "false");
	puts ("\t\t},");
	printf ("\t\t\"checksum\": %i,\n", pgm->pgm_checksum);
	printf ("\t\t\"gsi\": \"%i.%i.%i.%i.%i.%i\",\n",
		pgm->pgm_gsi[0],
		pgm->pgm_gsi[1],
		pgm->pgm_gsi[2],
		pgm->pgm_gsi[3],
		pgm->pgm_gsi[4],
		pgm->pgm_gsi[5]);
	printf ("\t\t\"tsduLength\": %i", g_ntohs(pgm->pgm_tsdu_length));
}

/* 8.1.  Source Path Messages (SPM)
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                     SPM's Sequence Number                     |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                 Trailing Edge Sequence Number                 |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                 Leading Edge Sequence Number                  |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |            NLA AFI            |          Reserved             |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                            Path NLA                     ...   |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-...-+-+
 * | Option Extensions when present ...                            |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+- ... -+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * NLA = Network Layer Address
 * NLA AFI = NLA Address Family Indicator: rfc 1700 (ADDRESS FAMILY NUMBERS)
 * => Path NLA = IP address of last network element
 */

int
verify_spm (
	struct pgm_header*	header,
	char*			data,
	int			len
	)
{
	int retval = 0;

/* truncated packet */
	if (len < sizeof(struct pgm_spm)) {
		printf ("\t\"message\": \"SPM: packet length: %i less than minimum SPM length: %" G_GSIZE_FORMAT "lu bytes.\",\n", len, sizeof(struct pgm_spm));
		retval = -1;
		goto out;
	}

	struct pgm_spm* spm = (struct pgm_spm*)data;
	char* opt_offset = (char*)(spm + 1);
	int opt_len = len - sizeof(spm);

	switch (g_ntohs(spm->spm_nla_afi)) {
	case AFI_IP6:
		if (len < sizeof(struct pgm_spm6)) {
			printf ("\t\"message\": \"SPM: packet length: %i less than minimum IPv6 SPM length: %" G_GSIZE_FORMAT "lu bytes.\",\n", len, sizeof(struct pgm_spm6));
			retval = -1;
			goto out;
		}
		opt_offset += sizeof(struct pgm_spm6) - sizeof(struct pgm_spm);
		opt_len -= sizeof(struct pgm_spm6) - sizeof(struct pgm_spm);

	case AFI_IP:
		break;

	default:
		printf ("\t\"message\": \"SPM: invalid AFI of source NLA: %i.\",\n", g_ntohs(spm->spm_nla_afi));
		retval = -1;
		goto out;
	}

/* option extensions */
	if (header->pgm_options & PGM_OPT_PRESENT)
	{
		retval = verify_options (opt_offset, opt_len);
	}

out:
	return retval;
}

void
print_spm (
	struct pgm_header*	header,
	char*			data
	)
{
	struct pgm_spm* spm = (struct pgm_spm*)data;
	struct pgm_spm6* spm6 = (struct pgm_spm6*)data;
	char* opt_offset = (char*)(spm + 1);

	puts (",");
	printf ("\t\t\"spmSqn\": %i,\n", g_ntohl(spm->spm_sqn));
	printf ("\t\t\"spmTrail\": %i,\n", g_ntohl(spm->spm_trail));
	printf ("\t\t\"spmLead\": %i,\n", g_ntohl(spm->spm_lead));
	printf ("\t\t\"spmNlaAfi\": %i,\n", g_ntohs (spm->spm_nla_afi));

	char s[INET6_ADDRSTRLEN];
	switch (g_ntohs(spm->spm_nla_afi)) {
	case AFI_IP:
		inet_ntop ( AF_INET, &spm->spm_nla, s, sizeof (s) );
		break;

	case AFI_IP6:
		inet_ntop ( AF_INET6, &spm6->spm6_nla, s, sizeof (s) );
		opt_offset += sizeof(struct pgm_spm6) - sizeof(struct pgm_spm);
		break;
	}

	printf ("\t\t\"spmNla\": \"%s\"\n", s);

/* option extensions */
	if (header->pgm_options & PGM_OPT_PRESENT)
	{
		print_options (opt_offset);
		puts ("");
	}

	puts ("\t}");
}

/* 14.7.1.  Poll Request
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                    POLL's Sequence Number                     |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |         POLL's Round          |       POLL's Sub-type         |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |            NLA AFI            |          Reserved             |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                            Path NLA                     ...   |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-...-+-+
 * |                  POLL's  Back-off Interval                    |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                        Random String                          |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                      Matching Bit-Mask                        |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | Option Extensions when present ...                            |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+- ... -+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * Sent to ODATA multicast group with IP Router Alert option.
 */

#define PGM_MIN_POLL_SIZE	( sizeof(struct pgm_poll) )

int
verify_poll (
	struct pgm_header*	header,
	char*			data,
	int			len
	)
{
	return -1;
}

void
print_poll (
	struct pgm_header*	header,
	char*			data
	)
{
}

/* 14.7.2.  Poll Response
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                    POLR's Sequence Number                     |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |         POLR's Round          |           reserved            |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | Option Extensions when present ...                            |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+- ... -+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */

int
verify_polr (
	struct pgm_header*	header,
	char*			data,
	int			len
	)
{
	return -1;
}

void
print_polr (
	struct pgm_header*	header,
	char*			data
	)
{
}

/* 8.2.  Data Packet
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                  Data Packet Sequence Number                  |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                 Trailing Edge Sequence Number                 |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | Option Extensions when present ...                            |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+- ... -+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | Data ...
 * +-+-+- ...
 */

int
verify_odata (
	struct pgm_header*	header,
	char*			data,
	int			len
	)
{
	int retval = 0;

	if (len < sizeof(struct pgm_data)) {
		printf ("\t\"message\": \"ODATA: packet length: %i less than minimum ODATA length: %" G_GSIZE_FORMAT " bytes.\",\n", len, sizeof(struct pgm_data));
		retval = -1;
		goto out;
	}

	char* tsdu = data + sizeof(struct pgm_data);
	int tsdu_len = len - sizeof(struct pgm_data);
	if (header->pgm_options & PGM_OPT_PRESENT)
	{
		retval = verify_options (tsdu, tsdu_len);
		tsdu += OPTIONS_TOTAL_LEN(tsdu);
		tsdu_len -= OPTIONS_TOTAL_LEN(tsdu);
	}

	if (!retval && g_ntohs(header->pgm_tsdu_length) != tsdu_len) {
		printf ("\t\"message\": \"ODATA: TSDU truncated expected %i, found %i bytes.\",\n", g_ntohs(header->pgm_tsdu_length), tsdu_len);
		retval = -1;
	}
out:
	return retval;
}

void
print_odata (
	struct pgm_header*	header,
	char*			data
	)
{
	struct pgm_data* odata = (struct pgm_data*)data;
	char* tsdu = data + sizeof(struct pgm_data);

	puts (",");
	printf ("\t\t\"odSqn\": %lu,\n", (gulong)g_ntohl(odata->data_sqn));
	printf ("\t\t\"odTrail\": %lu,\n", (gulong)g_ntohl(odata->data_trail));

/* option extensions */
	if (header->pgm_options & PGM_OPT_PRESENT)
	{
		print_options (tsdu);
		tsdu += OPTIONS_TOTAL_LEN(tsdu);
		puts (",");
	}

/* data */
	printf ("\t\t\"data\": \"");
	char* end = tsdu + g_ntohs (header->pgm_tsdu_length);
	while (tsdu < end) {
		if (isprint(*tsdu))
			putchar(*tsdu);
		else
			putchar('.');
		tsdu++;
	}

	puts ("\"");
	puts ("\t}");
}

/* 8.2.  Repair Data
 */

int
verify_rdata (
	struct pgm_header*	header,
	char*			data,
	int			len
	)
{
	int retval = 0;

	if (len < sizeof(struct pgm_data)) {
		printf ("\t\"message\": \"RDATA: packet length: %i less than minimum RDATA length: %" G_GSIZE_FORMAT " bytes.\",\n", len, sizeof(struct pgm_data));
		retval = -1;
		goto out;
	}

	char* tsdu = data + sizeof(struct pgm_data);
	int tsdu_len = len - sizeof(struct pgm_data);
	if (header->pgm_options & PGM_OPT_PRESENT)
	{
		retval = verify_options (tsdu, tsdu_len);
		tsdu += OPTIONS_TOTAL_LEN(tsdu);
		tsdu_len -= OPTIONS_TOTAL_LEN(tsdu);
	}

	if (!retval && g_ntohs(header->pgm_tsdu_length) != tsdu_len) {
		printf ("\t\"message\": \"RDATA: tsdu truncated expected %i, found %i bytes.\",\n", g_ntohs(header->pgm_tsdu_length), tsdu_len);
		retval = -1;
	}
out:
	return retval;
}

void
print_rdata (
	struct pgm_header*	header,
	char*			data
	)
{
	struct pgm_data* rdata = (struct pgm_data*)data;
	char* tsdu = data + sizeof(struct pgm_data);

	puts (",");
	printf ("\t\t\"rdSqn\": %lu,\n", (gulong)g_ntohl(rdata->data_sqn));
	printf ("\t\t\"rdTrail\": %lu,\n", (gulong)g_ntohl(rdata->data_trail));

/* option extensions */
	if (header->pgm_options & PGM_OPT_PRESENT)
	{
		print_options (tsdu);
		tsdu += OPTIONS_TOTAL_LEN(tsdu);
		puts (",");
	}

/* data */
	printf ("\t\t\"data\": \"");
	char* end = tsdu + g_ntohs (header->pgm_tsdu_length);
	while (tsdu < end) {
		if (isprint(*tsdu))
			putchar(*tsdu);
		else
			putchar('.');
		tsdu++;
	}

	puts ("\"");
	puts ("\t}");
}

/* 8.3.  NAK
 *
 * Technically the AFI of the source and multicast group can be different
 * but that would be very wibbly wobbly.  One example is using a local DLR
 * with a IPv4 address to reduce NAK cost for recovery on wide IPv6
 * distribution.
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                   Requested Sequence Number                   |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |            NLA AFI            |          Reserved             |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                           Source NLA                    ...   |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-...-+-+
 * |            NLA AFI            |          Reserved             |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                      Multicast Group NLA                ...   |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-...-+-+
 * | Option Extensions when present ...
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+- ...
 */

int
verify_nak (
	struct pgm_header*	header,
	char*			data,
	int			len
	)
{
	return generic_verify_nak ("NAK", header, data, len);
}

int
verify_ncf (
	struct pgm_header*	header,
	char*			data,
	int			len
	)
{
	return generic_verify_nak ("NCF", header, data, len);
}

int
verify_nnak (
	struct pgm_header*	header,
	char*			data,
	int			len
	)
{
	return generic_verify_nak ("NNAK", header, data, len);
}

static int
generic_verify_nak (
	const char*		name,		/* upper case */
	struct pgm_header*	header,
	char*			data,
	int			len
	)
{
	int retval = 0;

/* truncated packet */
	if (len < sizeof(struct pgm_nak)) {
		printf ("\t\"message\": \"%s: packet length: %i less than minimum %s length: %" G_GSIZE_FORMAT " bytes.\",\n",
			name, len, name, sizeof(struct pgm_nak));
		retval = -1;
		goto out;
	}

	struct pgm_nak* nak = (struct pgm_nak*)data;
	int nak_src_nla_afi = g_ntohs (nak->nak_src_nla_afi);
	int nak_grp_nla_afi = -1;

/* check source NLA: unicast address of the ODATA sender */
	switch (nak_src_nla_afi) {
	case AFI_IP:
		nak_grp_nla_afi = g_ntohs (nak->nak_grp_nla_afi);
		break;

	case AFI_IP6:
		nak_grp_nla_afi = g_ntohs (((struct pgm_nak6*)nak)->nak6_grp_nla_afi);
		break;

	default:
		printf ("\t\"message\": \"%s: invalid AFI of source NLA: %i.\",\n",
			name, nak_src_nla_afi);
		retval = -1;
		goto out;
	}

/* check multicast group NLA */
	switch (nak_grp_nla_afi) {
	case AFI_IP6:
		switch (nak_src_nla_afi) {
/* IPv4 + IPv6 NLA */
		case AFI_IP:
			if (len < ( sizeof(struct pgm_nak) + sizeof(struct in6_addr) - sizeof(struct in_addr) )) {
				printf ("\t\"message\": \"%s: packet length: %i less than joint IPv4/6 %s length: %" G_GSIZE_FORMAT " bytes.\",\n",
					name, len, name, ( sizeof(struct pgm_nak) + sizeof(struct in6_addr) - sizeof(struct in_addr) ));
				retval = -1;
			}
			break;

/* IPv6 + IPv6 NLA */
		case AFI_IP6:
			if (len < sizeof(struct pgm_nak6)) {
				printf ("\t\"message\": \"%s: packet length: %i less than IPv6 %s length: %" G_GSIZE_FORMAT " bytes.\",\n",
					name, len, name, sizeof(struct pgm_nak6));
				retval = -1;
			}
			break;
		}
		break;

	case AFI_IP:
		if (nak_src_nla_afi == AFI_IP6) {
			if (len < ( sizeof(struct pgm_nak) + sizeof(struct in6_addr) - sizeof(struct in_addr) )) {
				printf ("\t\"message\": \"%s: packet length: %i less than joint IPv6/4 %s length: %" G_GSIZE_FORMAT " bytes.\",\n",
					name, len, name, ( sizeof(struct pgm_nak) + sizeof(struct in6_addr) - sizeof(struct in_addr) ));
				retval = -1;
			}
		}
		break;

	default:
		printf ("\t\"message\": \"%s: invalid AFI of group NLA: %i.\",\n",
			name, nak_grp_nla_afi);
		retval = -1;
		break;
	}

out:
	return retval;
}

void
print_nak (
	struct pgm_header*	header,
	char*			data
	)
{
	generic_print_nak ("nak", header, data);
}

void
print_ncf (
	struct pgm_header*	header,
	char*			data
	)
{
	generic_print_nak ("ncf", header, data);
}

void
print_nnak (
	struct pgm_header*	header,
	char*			data
	)
{
	generic_print_nak ("nnak", header, data);
}

static void
generic_print_nak (
	const char*		name,		/* lower case */
	struct pgm_header*	header,
	char*			data
	)
{
	struct pgm_nak* nak = (struct pgm_nak*)data;
	struct pgm_nak6* nak6 = (struct pgm_nak6*)data;
	char* opt_offset = (char*)(nak + 1);

	puts (",");
	printf ("\t\t\"%sSqn\": %lu,\n", name, (gulong)g_ntohl(nak->nak_sqn));

	guint16 nak_src_nla_afi = g_ntohs (nak->nak_src_nla_afi);
	guint16 nak_grp_nla_afi;
	char s[INET6_ADDRSTRLEN];

	printf ("\t\t\"%sSourceNlaAfi\": %i,\n", name, nak_src_nla_afi);

/* source nla */
	switch (nak_src_nla_afi) {
	case AFI_IP:
		inet_ntop ( AF_INET, &nak->nak_src_nla, s, sizeof(s) );
		nak_grp_nla_afi = g_ntohs (nak->nak_grp_nla_afi);
		break;

	case AFI_IP6:
		inet_ntop ( AF_INET6, &nak6->nak6_src_nla, s, sizeof(s) );
		nak_grp_nla_afi = g_ntohs (nak6->nak6_grp_nla_afi);
		opt_offset += sizeof(struct in6_addr) - sizeof(struct in_addr);
		break;
	}

	printf ("\t\t\"%sSourceNla\": \"%s\",\n", name, s);
	printf ("\t\t\"%sGroupNlaAfi\": %i,\n", name, nak_grp_nla_afi);

	switch (nak_grp_nla_afi) {
	case AFI_IP6:
		switch (nak_src_nla_afi) {
/* IPv4 + IPv6 NLA */
		case AFI_IP:
			inet_ntop ( AF_INET6, &nak->nak_grp_nla, s, sizeof(s) );
			break;

/* IPv6 + IPv6 NLA */
		case AFI_IP6:
			inet_ntop ( AF_INET6, &nak6->nak6_grp_nla, s, sizeof(s) );
			break;
		}
		opt_offset += sizeof(struct in6_addr) - sizeof(struct in_addr);
		break;

	case AFI_IP:
		switch (nak_src_nla_afi) {
/* IPv4 + IPv4 NLA */
		case AFI_IP:
			inet_ntop ( AF_INET, &nak->nak_grp_nla, s, sizeof(s) );
			break;

/* IPv6 + IPv4 NLA */
		case AFI_IP6:
			inet_ntop ( AF_INET, &nak6->nak6_grp_nla, s, sizeof(s) );
			break;
		}
		break;
	}

	printf ("\t\t\"%sGroupNla\": \"%s\"", name, s);

/* option extensions */
	if (header->pgm_options & PGM_OPT_PRESENT)
	{
		puts (",");
		print_options (opt_offset);
	}

	puts ("\n\t}");
}


/* 13.6.  SPM Request
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | Option Extensions when present ...
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+- ...
 */

int
verify_spmr (
	struct pgm_header*	header,
	char*			data,
	int			len
	)
{
	int retval = 0;

	char* opt_offset = data;
	int opt_len = len;

/* option extensions */
	if (header->pgm_options & PGM_OPT_PRESENT)
	{
		retval = verify_options (opt_offset, opt_len);
	}

	return -1;
}

void
print_spmr (
	struct pgm_header*	header,
	char*			data
	)
{
	char* opt_offset = data;

/* option extensions */
	if (header->pgm_options & PGM_OPT_PRESENT)
	{
		print_options (opt_offset);
		puts ("");
	}

	puts ("\t}");
}

/* Parse PGM options fields:
 *
 * assume at least two options, one the mandatory OPT_LENGTH
 */

#define PGM_MIN_OPT_SIZE	( 2 * (sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_length)) )

int
verify_options (
	char*		data,
	int		len
	)
{
	int retval = 0;

	if (len < PGM_MIN_OPT_SIZE) {
		printf ("\t\"message\": \"PGM options: packet size too small for options.\",\n");
		retval = -1;
		goto out;
	}

/* OPT_LENGTH first */	
	struct pgm_opt_header* opt_header = (struct pgm_opt_header*)data;
	struct pgm_opt_length* opt_length = (struct pgm_opt_length*)(opt_header + 1);
	if ((opt_header->opt_type & PGM_OPT_MASK) != PGM_OPT_LENGTH) {
		printf ("\t\"message\": \"PGM options: first option not OPT_LENGTH.\",\n");
		retval = -1;
		goto out;
	}

	if (opt_header->opt_length != sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_length)) {
		printf ("\t\"message\": \"PGM options: OPT_LENGTH incorrect option length: %i, expecting %" G_GSIZE_FORMAT " bytes.\",\n", opt_header->opt_length, sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_length));
		retval = -1;
		goto out;
	}

	if (g_ntohs(opt_length->opt_total_length) < PGM_MIN_OPT_SIZE) {
		printf ("\t\"message\": \"PGM options: OPT_LENGTH total length too short: %i bytes.\",\n", g_ntohs(opt_length->opt_total_length));
		retval = -1;
		goto out;
	}

	if (g_ntohs(opt_length->opt_total_length) > len) {
		printf ("\t\"message\": \"PGM options: OPT_LENGTH total length longer than packet allows: %i bytes.\",\n", g_ntohs(opt_length->opt_total_length));
		retval = -1;
		goto out;
	}

/* iterate through options (max 16) */
	int count = 0;
	int total_length = g_ntohs(opt_length->opt_total_length);

	int opt_counters[256];
	memset (&opt_counters, 0, sizeof(opt_counters));

	for (;;) {
		total_length -= opt_header->opt_length;
		if (total_length < (sizeof(struct pgm_opt_header) + sizeof(guint16))) {
			printf ("\t\"message\": \"PGM options: option #%i shorter than minimum option size.\",\n", count + 1);
			retval = -1;
			goto out;
		}
		opt_header = (struct pgm_opt_header*)( ((char*)opt_header) + opt_header->opt_length );
		if ((total_length - opt_header->opt_length) < 0) {
			printf ("\t\"message\": \"PGM options: option #%i shorter than embedded size.\",\n", count + 1);
			retval = -1;
			goto out;
		}

		if (opt_counters[opt_header->opt_type]++) {
			printf ("\t\"message\": \"PGM options: duplicate option %i.\",\n", opt_header->opt_type);
			retval = -1;
			goto out;
		}

/* check option types */
		switch (opt_header->opt_type) {
		case PGM_OPT_FRAGMENT:
		{
			if (opt_header->opt_length != sizeof(struct pgm_opt_header) + sizeof(struct pgm_opt_fragment)) {
				printf ("\t\"message\": \"PGM options: OPT_FRAGMENT incorrect size: %i bytes.\",\n", opt_header->opt_length);
				retval = -1;
				goto out;
			}
			
			struct pgm_opt_fragment* opt_fragment = (struct pgm_opt_fragment*)(opt_header + 1);
			if (g_ntohl(opt_fragment->opt_frag_off) > g_ntohl(opt_fragment->opt_frag_len)) {
				printf ("\t\"message\": \"PGM options: fragment offset longer than original packet.\",\n");
				retval = -1;
				goto out;
			}
			break;
		}

		case PGM_OPT_NAK_LIST:
		{
			int list_len = opt_header->opt_length - sizeof(struct pgm_opt_header) - sizeof(guint16);
			if (list_len & 1) {
				printf ("\t\"message\": \"PGM options: OPT_NAK_LIST invalid odd length: %i bytes.\",\n", opt_header->opt_length);
				retval = -1;
				goto out;
			}

			list_len /= 2;
			if (list_len == 0) {
				printf ("\t\"message\": \"PGM options: OPT_NAK_LIST empty.\",\n");
				retval = -1;
				goto out;
			}

			if (list_len > 62) {
				printf ("\t\"message\": \"PGM options: OPT_NAK_LIST too long: %i sqns.\",\n", list_len);
				retval = -1;
				goto out;
			}
			break;
		}

		default:
			printf ("\t\"message\": \"PGM options: unknown option: %i.\",\n", opt_header->opt_type);
			retval = -1;
			goto out;
		}
/* end option types */

		if (opt_header->opt_type & PGM_OPT_END) {
			break;
		}

		if (count++ == 16) {
			printf ("\t\"message\": \"PGM options: more than 16 options found.\",\n");
			retval = -1;
			goto out;
		}
	}

out:
	return retval;
}

void
print_options (
	char*		data
	)
{
	struct pgm_opt_header* opt_header = (struct pgm_opt_header*)data;
	struct pgm_opt_length* opt_length = (struct pgm_opt_length*)(opt_header + 1);

	puts ("\t\t\"pgmOptions\": [");
	puts ("\t\t\t{");
	puts ("\t\t\t\t\"type\": \"OPT_LENGTH\",");
	printf ("\t\t\t\t\"totalLength\": %i\n", g_ntohs (opt_length->opt_total_length));
	printf ("\t\t\t}");

/* iterate through options */
	do {
		opt_header = (struct pgm_opt_header*)( ((char*)opt_header) + opt_header->opt_length );
		puts (",");
		switch (opt_header->opt_type) {
		case PGM_OPT_FRAGMENT:
		{
			struct pgm_opt_fragment* opt_fragment = (struct pgm_opt_fragment*)(opt_header + 1);
			puts ("\t\t\t{");
			puts ("\t\t\t\t\"type\": \"OPT_FRAGMENT\",");
			printf ("\t\t\t\t\"firstSqn\": %i,\n", g_ntohl(opt_fragment->opt_sqn));
			printf ("\t\t\t\t\"fragmentOffset\": %i,\n", g_ntohl(opt_fragment->opt_frag_off));
			printf ("\t\t\t\t\"originalLength\": %i\n", g_ntohl(opt_fragment->opt_frag_len));
			printf ("\t\t\t}");
			break;
		}

		case PGM_OPT_NAK_LIST:
		{
			struct pgm_opt_nak_list* opt_nak_list = (struct pgm_opt_nak_list*)(opt_header + 1);
			char* end = (char*)opt_header + opt_header->opt_length;
			puts ("\t\t\t{");
			puts ("\t\t\t\t\"type\": \"OPT_NAK_LIST\",");
			char sqns[1024] = "";
			int i = 0;
			do {
				char sqn[1024];
				sprintf (sqn, "%s%i", (i>0)?", ":"", opt_nak_list->opt_sqn[i]);
				strcat (sqns, sqn);
				i++;
			} while ((char*)&opt_nak_list->opt_sqn[i] < end);
			printf ("\t\t\t\t\"sqn\": [%s]\n", sqns);
			printf ("\t\t\t}");
			break;
		}

		default:
		{
		}
			puts ("\t\t\t{");
			printf ("\t\t\t\t\"type\": 0x%x\n", opt_header->opt_type);
			printf ("\t\t\t}");
		}

	} while (!(opt_header->opt_type & PGM_OPT_END));

	printf ("\n\t\t]");
}

/* eof */
