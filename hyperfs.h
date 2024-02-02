/*
hyperfs.h
Contains the structure data for a hyperfs file system driver.

Copyright (C) 2024 Mdfx

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#pragma once
#include <stdint.h>

#define CLUSTER_MULTIPLIER 4096
#define CLUSTER_CHAIN_SIZE 8
#define CLUSTER_END (uint64_t)0x0000000000000000
#define CLUSTER_END_NUB (uint64_t)0x0000000000000001 // NoUsedBytes

#define HEADER_NOREAD_SIGNATURE (uint32_t)0x4E4F5244 // ASCII "NORD"
#define HEADER_NOREAD_LSB_SIGNATURE (uint32_t)0x44524F4E // ASCII "DRON"
#define HEADER_DIRECTION_SIGN (uint16_t)0x55AA // 0b0101010110101010
#define HEADER_SIZE (sizeof(hfs_header) - sizeof(uint8_t*)) // The actual cluster size after allocating the c_pad is header.cluster_size
#define HEADER_PADDING_SIZE 456

//struct data_cluster // CLUSTER_SIZE (Size varies by cluster size)
//{
//	uint8_t data[CLUSTER_SIZE - CLUSTER_CHAIN_SIZE - 2];
//	uint16_t used_bytes; // Bytes used by file in cluster overridden if next_cluster is not CLUSTER_END or is CLUSTER_END_NUB as that means that the used bytes is the entire cluster.
//	uint64_t next_cluster; // Points to next cluster. If last cluster then it's set to CLUSTER_END or CLUSTER_END_NUB because CLUSTER_END points to the header and CLUSTER_END_NUB points to the first rfe chain.
//}__attribute__((packed));

struct hfs_header // 512 bytes (however CLUSTER_SIZE are written in total) 55 + padding[456] + boot_sig (2)
{
	uint32_t signature; // If the signature is "NORD" then it shouldn't be read (such as a boot drive)
	uint8_t direction_b01; // Set to 0x55 0xAA (MSB 55 LSB AA) so that the driver would know if the header and file entry are written in MSB or LSB format.
	uint8_t direction_b10; // If its read as AA55 then its written in LSB format.
	uint64_t cluster_to_be_allocated; // The cluster after the last allocated cluster, after formatting is 0x2 as cluster 0 is the header, even if a long name is used as a long name is stored in cluster 0 (padding) in the
									// following format: [uint8_t SIZE] [uint8_t[SIZE] long_name], And cluster 0x1 is the master RFEC (RFE Chain). It's also the same as the clusters allocated. 0x0 if read-only or full.
	uint64_t cluster_size; // CLUSTER_SIZE
	uint64_t clusters_available; // (Size of disk / CLUSTER_SIZE) - 2 (2 to account for the header and master RFEC)
	uint8_t name[12]; // zero-terminated. Still part of the drive name if a long name is used.
	uint8_t attribute; // LONG NAME (LN) | USER READ (UR) | USER WRITE (UW) | ROOT READ (RR) | ROOT WRITE (RW) | HIDDEN (H) | 2 BIT VERSION (V) // Example: (Norm: 01111000, URO: 01011000, UNA: 00011000, UNAH: 00011100)
	uint16_t creation_date; // 15-9: Year (0: 2024, 2151) 8-5: Month (1-12) 4-0: Day (1-31) | EG: 2024/1/24 0000000|0000|11000
	uint8_t owner_id;
	uint8_t reserved; // Could be used in later versions. In version 0 it's always 0xFF
	uint64_t clusters;
	uint8_t padding[HEADER_PADDING_SIZE]; // 456
	uint8_t boot_sig_0; // If bootable then it's set to 0x55AA, if not the anything else other than zero.
	uint8_t boot_sig_1; // If bootable then it's set to 0x55AA, if not the anything else other than zero.
	// Extra padding to fill in the rest of the cluster
	uint8_t* c_pad; // malloc(CLUSTER_SIZE - HEADER_SIZE)
}__attribute__((packed));

struct hfs_reserved_file_entry // 40 bytes, stored in reserved clusters with the last entry being a special reserved_chain_entry pointing to a cluster with another reserved_file_entry chain.
{						//	 reserved clusters being cluster 1 and any clusters containing rfe_chains pointed to by reserved_chain_entry(s)
	uint8_t name[12]; // zero-terminated.
	uint8_t extention[4]; // While the extention field is zero-terminated if the extention is 4 bytes long it is NOT zero-terminated
	uint8_t attribute; // LONG NAME (LN) | USER READ (UR) | USER WRITE (UW) | USER EXECUTE (UX) | ROOT READ (RR) | ROOT WRITE (RW) | ROOT EXECUTE (RX) | HIDDEN (H) // Long names are stored inside the first cluster in
						// the following format: [uint8_t SIZE] [uint8_t[SIZE] long_name]; The extention is determined by [name+extention] ONLY WHEN using a long name and is zero-terminated ONLY
						// WHEN it is NOT 16 bytes long.
	uint8_t p_resv; // Most significant bit determines if this is a directory or not. Second most significant bit is always 0
					// (after the long name if defined) which is of size 8 bytes (uint64_t). If its a directory it points to an RFE chain. The rest of the bits are 1 except the last one which determines if this is a deleted rfe if its 0x3E or 0x5E (DIR).
	uint64_t cluster_size;
	uint16_t creation_date; // 15-9: Year (0: 2024, 2151) 8-5: Month (1-12) 4-0: Day (1-31) | EG: 2024/1/24 0000000|0000|11000
	uint16_t modification_date; // EG: 2027/5/20 0000011|0100|10100
	uint8_t owner_id;
	uint8_t is_last_rfe;
	uint64_t next_cluster; // Points to first cluster for file data.
}__attribute__((packed));

struct hfs_reserved_chain_entry // 24 bytes
{
	uint64_t next_rfe_chain; // if it doesn't point to an rfe_chain then CLUSTER_END
	uint8_t reserved[16];
};
