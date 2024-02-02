#include <functional>
#include <iostream>
#include <string.h>
#include <stdio.h>
#include <chrono>

#include <fstream>

#include "hyperfs.h"

namespace hfs
{
	const int32_t 							 ERR_WR_NO_DEF = -1; //NUL_WRF
	const int32_t 							 ERR_RD_NO_DEF = -2; //NUL_RDF
	const int32_t 						  ERR_RD_WR_NO_DEF = -3; //NUL_WRF
	const int32_t			  ERR_HEADER_INVALID_DIRECTION = -4; //HED_DIR
	const int32_t		   ERR_HEADER_INVALID_CLUSTER_INFO = -5; //HED_CIF
	const int32_t			ERR_HEADER_UNSUPPORTED_VERSION = -6; //HED_VER
	const int32_t		ERR_HEADER_NON_FF_RESERVED_SEGMENT = -7; //HED_RSF
	const int32_t				  ERR_HEADER_ZERO_BOOT_SIG = -8; //HED_ZBS
	// const int32_t		  ERR_HEADER_PADDING_TOO_SHORT = -9; // INVALID! Would cause HED_ZBS
	const int32_t			 ERR_RFE_INVALID_PRESV_SEGMENT = -9; //RFE_PRS
	const int32_t							ERR_RFE_NO_END = -10;//RFE_NED
	const int32_t						 ERR_DATA_NO_SPACE = -11;//DTA_NSP
	const int32_t					   ERR_FILE_NOT_LOCKED = -12;//FIL_NLK
	const int32_t				 ERR_FILE_BUFFER_TOO_LARGE = -13;//FIL_BTL
	const int32_t				  ERR_FILE_DEPTH_TOO_LARGE = -14;//FIL_DTL

	const uint8_t HFS_SEEK_SET = 0;
	const uint8_t HFS_SEEK_CUR = 1;
	const uint8_t HFS_SEEK_END = 2;

	struct date
	{
		date()
		{
			year = 0;
			month = day = 0;
		}
	
		date(uint16_t compact)
		{
			year = ((compact >> 7) & 0x007F) + 2024;
			month = ((compact >> 5) & 0x000F) + 1;
			day = compact & 0x001F;
		}
	
		uint16_t year;
		uint8_t month;
		uint8_t day;
	};

	uint16_t create_date_16()
	{
		std::time_t t = std::time(0);
		std::tm* now = std::localtime(&t);
		return ((now->tm_year - 124) << 7) | (now->tm_mon << 5) | (now->tm_mday);
	}

	struct hfs_object
	{
		std::function<size_t(void*, size_t, size_t, void*)> read_fn; //new_pos, buffer, size, position, extra_args (checks if size is zero and returns current position (ftell))
		std::function<size_t(void*, size_t, size_t, void*)> write_fn;//new_pos, buffer, size, position, extra_args (overwrite) (checks if size is zero and goes to position without writing (fseek) with the seek_set/seek_cur/seek_end, being contained in the buffer as an uint8_t*)
		std::function<void(void*)> reset_file_fn; // extra_args, truncates file
	
		void* extra_args;
		hfs_header header;
		std::vector<hfs_reserved_file_entry> rfe;
		std::vector<uint64_t> lock_rfe;
		size_t position;

		int no_read = false;
		int bootable = false;

		void read(void* buffer, size_t size)
		{
			position = read_fn(buffer, size, position, extra_args);
		}
		void write(void* buffer, size_t size)
		{
			position = write_fn(buffer, size, position, extra_args);
		}
		size_t ftell()
		{
			return read_fn(nullptr, 0, 0, extra_args);
		}
		size_t fseek(size_t pos, uint8_t mode)
		{
			return position = write_fn((void*)&mode, 0, pos, extra_args);
		}

		int32_t init()
		{
			if (!read_fn && !write_fn)
				return ERR_RD_WR_NO_DEF;
			if (!read_fn)
				return ERR_RD_NO_DEF;
			if (!write_fn)
				return ERR_WR_NO_DEF;

			memset(&header, 0, HEADER_SIZE);
			header.c_pad = nullptr;
			return 0;
		}
		int uninit()
		{
			if (header.c_pad != 0)
				free(header.c_pad);
			return 0;
		}
		int32_t parse()
		{
			fseek(0, HFS_SEEK_SET);
			read(&header, HEADER_SIZE);
			if (header.boot_sig_0 == 0 || header.boot_sig_1 == 0)
				return ERR_HEADER_ZERO_BOOT_SIG;
			if (header.boot_sig_0 == 0x55 && header.boot_sig_1 == 0xAA)
				bootable = true;

			if (header.direction_b01 == 0xAA && header.direction_b10 == 0x55)
			{
				if (header.signature == HEADER_NOREAD_LSB_SIGNATURE)
					no_read = true;
			}
			else if (header.direction_b01 == 0x55 && header.direction_b10 == 0xAA)
			{
				if (header.signature == HEADER_NOREAD_SIGNATURE)
					no_read = true;
			}
			else if (header.direction_b01 == 0 || header.direction_b10 == 0)
			{
					return ERR_HEADER_INVALID_DIRECTION;
				}

			if (((header.cluster_size % CLUSTER_MULTIPLIER) > 0) || (header.clusters_available != 0 && header.cluster_to_be_allocated != 0 && (header.cluster_to_be_allocated + header.clusters_available != header.clusters)))
				return ERR_HEADER_INVALID_CLUSTER_INFO;
			if (header.attribute & 0b00000011)
				return ERR_HEADER_UNSUPPORTED_VERSION;
			if (header.reserved != 0xFF)
				return ERR_HEADER_NON_FF_RESERVED_SEGMENT;
			return 0;
		}
		int32_t read_rfe_chain()
		{
			rfe.clear();
			hfs_reserved_file_entry h_rfe;
			fseek(header.cluster_size, SEEK_SET);
			uint64_t num_of_rfes_per_cluster = (header.cluster_size - 24)/40;
			uint64_t index = 0;
			while (!h_rfe.is_last_rfe)
			{
				read(&h_rfe, sizeof(h_rfe));
				uint8_t process_pr = h_rfe.p_resv & 0b01111111;
				if (process_pr != 0b00111111 && process_pr != 0b00111110)
				{
					if (rfe.size() == 0)
						return 0;
					rfe.clear();
					return ERR_RFE_NO_END;
				}
				if (index == num_of_rfes_per_cluster)
				{
					hfs_reserved_chain_entry rce;
					read(&rce, sizeof(rce));
					if (rce.next_rfe_chain == CLUSTER_END)
						return 0;
					else
					{
						fseek(rce.next_rfe_chain * header.cluster_size, SEEK_SET);
						continue;
					}
				}
				if (process_pr == 0b00111111)
					rfe.push_back(h_rfe);
				index++;
			}
			return 0;
		}
		int32_t write_rfe_chain()
		{
			fseek(header.cluster_size, SEEK_SET);
			uint64_t num_of_rfes_per_cluster = (header.cluster_size - 24)/40;
			uint64_t index = 0;
			uint64_t t_index = 0;
			while (true)
			{
				write(&rfe[t_index], sizeof(hfs_reserved_file_entry));
				if (rfe[t_index].is_last_rfe)
					break;
				t_index++;
				if (index == num_of_rfes_per_cluster)
				{
					size_t p = ftell();
					hfs_reserved_chain_entry rce;
					read(&rce, sizeof(rce));
					if (rce.next_rfe_chain <= CLUSTER_END_NUB)
					{
						rce.next_rfe_chain = header.cluster_to_be_allocated;
						header.cluster_to_be_allocated++;
						header.clusters_available--;
						fseek(0, SEEK_SET);
						write(&header, HEADER_SIZE);
						fseek(p, SEEK_SET);
						write(&rce, sizeof(rce));
						fseek(rce.next_rfe_chain * header.cluster_size, SEEK_SET);
						continue;
					}
					fseek(rce.next_rfe_chain * header.cluster_size, SEEK_SET);
					if (rfe[t_index - 1].is_last_rfe)
					{
						hfs_reserved_file_entry rf;
						memset(&rf, 0, sizeof(rf));
						write(&rf, sizeof(rf));
						break;
					}
					index = 0;
					continue;
				}
				index++;
			}
			return 0;
		}
		int format(uint64_t cluster_size, uint64_t clusters, uint32_t signature, uint8_t* name, uint8_t attributes, uint8_t owner_id, uint8_t boot_sig_0, uint8_t boot_sig_1, uint8_t lname_len, uint8_t* lname) // name is 12 bytes
		{
			fseek(0, HFS_SEEK_SET);
			reset_file_fn(extra_args);
			header.signature = signature;
			header.direction_b01 = 0xAA;
			header.direction_b10 = 0x55;
			header.cluster_to_be_allocated = 0x2;
			header.cluster_size = cluster_size;
			header.clusters_available = clusters - 2;
			memcpy(header.name, name, 12);
			header.attribute = attributes;
			header.creation_date = create_date_16();
			header.owner_id = owner_id;
			header.reserved = 0xFF;
			header.clusters = clusters;
			memset(header.padding, 0, HEADER_PADDING_SIZE);
			if (lname_len > 0 && attributes & 0b10000000)
			{
				header.padding[0] = lname_len;
				for (int i = 0; i < lname_len; i++)
				{
					header.padding[i + 1] = lname[i];
				}
			}
			header.boot_sig_0 = boot_sig_0;
			header.boot_sig_1 = boot_sig_1;
			uint64_t c_pad_size = cluster_size - 512;
			header.c_pad = (uint8_t*)malloc(c_pad_size);
			memset(header.c_pad, 0, c_pad_size);
			void* zbuff = malloc(cluster_size);
			memset(zbuff, 0, cluster_size);
			for (uint64_t i = 0; i < clusters; i++)
			{
				write(zbuff, cluster_size);
			}
			free(zbuff);
			fseek(0, HFS_SEEK_SET);
			write(&header, HEADER_SIZE);
			return 0;
		}
		int32_t add_file(uint8_t* name, uint8_t* extention, uint8_t attribute, uint8_t owner_id)
		{
			if (header.clusters_available == 0 || header.cluster_to_be_allocated == 0)
			{
				return ERR_DATA_NO_SPACE;
			}
			hfs_reserved_file_entry h_rfe;
			memcpy(h_rfe.name, name, 12);
			memcpy(h_rfe.extention, extention, 4);
			h_rfe.attribute = attribute;
			h_rfe.p_resv = 0x3F;
			h_rfe.cluster_size = 1;
			h_rfe.modification_date = h_rfe.creation_date = create_date_16();
			h_rfe.owner_id = owner_id;
			h_rfe.is_last_rfe = 1;
			h_rfe.next_cluster = header.cluster_to_be_allocated;
			header.cluster_to_be_allocated++;
			header.clusters_available--;
			fseek(0, SEEK_SET);
			write(&header, HEADER_SIZE);
			fseek((header.cluster_size * (h_rfe.next_cluster)) - 10, HFS_SEEK_SET);
			uint64_t n_cluster = CLUSTER_END;
			uint16_t u_bytes = 0;
			write(&u_bytes, sizeof(u_bytes));
			write(&n_cluster, sizeof(n_cluster));
			int32_t rrc_val = read_rfe_chain();
			if (rrc_val < 0)
			{
				return rrc_val;
			}
			rfe.push_back(h_rfe);
			return write_rfe_chain();
		}
		// Returns 0 when failed.
		uint64_t lock_file(uint8_t* name, uint8_t* extention)
		{
			if (read_rfe_chain() < 0)
				return 0;
			uint64_t index = 0;
			for (hfs_reserved_file_entry h_rfe : rfe)
			{
				if (memcmp(h_rfe.name, name, 12) || memcmp(h_rfe.extention, extention, 4))
				{
					index++;
					continue;
				}
				if (lock_rfe.size() > 0)
				{
					for (uint64_t l_rfe : lock_rfe)
					{
						if (l_rfe == index)
							return 0;
					}
				}
			}
			lock_rfe.push_back(index);
			return index + 1;
		}
		int32_t unlock_file(uint64_t fptr)
		{
			fptr--;
			for (size_t i = 0; i < lock_rfe.size(); i++)
			{
				if (lock_rfe[i] == fptr)
				{
					lock_rfe.erase(lock_rfe.begin() + i);
					return 0;
				}
			}
			return ERR_FILE_NOT_LOCKED;
		}
		int is_locked(uint64_t fptr)
		{
			for (size_t i = 0; i < lock_rfe.size(); i++)
			{
				if (lock_rfe[i] == fptr)
					return 1;
			}
			return 0;
		}
		// Buffer max size is cluster_size - position - 10 // Depth: How many next_cluster chains will it seek before writing // Ex_buff: adds an extra buffer and seeks to it before writing (unless size is 0)
		int32_t write_buff(uint64_t fptr, void* buffer, uint64_t size, uint64_t position, uint64_t depth, int ex_buff)
		{
			fptr--;
			hfs_reserved_file_entry h_rfe = rfe[fptr];
			if ((depth - 1) > h_rfe.cluster_size && depth != 0)
				return ERR_FILE_DEPTH_TOO_LARGE;
			uint8_t lname_len = 0;
			if (h_rfe.attribute & 0b10000000 && (depth || ex_buff))
			{
				fseek(h_rfe.next_cluster * header.cluster_size, SEEK_SET);
				read(&lname_len, 1);
				lname_len++;
				position += lname_len;
			}
			if (position + size + 8 + lname_len > header.cluster_size)
				return ERR_FILE_BUFFER_TOO_LARGE;
			if (ex_buff)
				if (header.clusters_available == 0 || header.cluster_to_be_allocated == 0)
					return ERR_DATA_NO_SPACE;
			if (!is_locked(fptr))
				return ERR_FILE_NOT_LOCKED;
			fseek(h_rfe.next_cluster * header.cluster_size, HFS_SEEK_SET);
			for (uint64_t i = 0; i < depth; i++)
			{
				uint64_t n_cluster = 0;
				fseek(header.cluster_size - 8, HFS_SEEK_CUR);
				read(&n_cluster, sizeof(n_cluster));
				if (n_cluster == CLUSTER_END || n_cluster == CLUSTER_END_NUB)
					return ERR_FILE_DEPTH_TOO_LARGE;
				fseek(n_cluster * header.cluster_size, HFS_SEEK_SET);
			}
			if (ex_buff)
			{
				fseek(header.cluster_size - 8, HFS_SEEK_CUR);
				write(&header.cluster_to_be_allocated, sizeof(header.cluster_to_be_allocated));
				fseek(header.cluster_to_be_allocated * header.cluster_size, HFS_SEEK_SET);
				uint64_t p = ftell();
				header.cluster_to_be_allocated++;
				header.clusters_available--;
				fseek(0, HFS_SEEK_SET);
				write(&header, HEADER_SIZE);
				fseek(p, HFS_SEEK_SET);
				h_rfe.cluster_size++;
			}
			uint64_t p = ftell();
			fseek(position, HFS_SEEK_CUR);
			write(buffer, size);
			fseek(p, HFS_SEEK_SET);
			fseek(header.cluster_size - 10 - lname_len, HFS_SEEK_CUR);
			uint16_t bytes_used = size + position;
			if (bytes_used < 0xFF6)
				write(&bytes_used, sizeof(bytes_used));
			else
			{
				fseek(2, HFS_SEEK_CUR);
				uint64_t is_last = 0;
				uint64_t n_cl = CLUSTER_END_NUB;
				p = ftell();
				read(&is_last, sizeof(is_last));
				fseek(p, HFS_SEEK_SET);
				if (is_last == CLUSTER_END)
					write(&n_cl, sizeof(n_cl));
			}
			h_rfe.modification_date = create_date_16();
			rfe[fptr] = h_rfe;
			write_rfe_chain();
			return 0;
		}
		int32_t read_buff(uint64_t fptr, void* buffer, uint64_t size, uint64_t position, uint64_t depth)
		{
			fptr--;
			hfs_reserved_file_entry h_rfe = rfe[fptr];
			if ((depth - 1) > h_rfe.cluster_size && depth > 0)
				return ERR_FILE_DEPTH_TOO_LARGE;
			uint8_t lname_len = 0;
			if (h_rfe.attribute & 0b10000000 && depth)
			{
				fseek(h_rfe.next_cluster * header.cluster_size, SEEK_SET);
				read(&lname_len, 1);
				lname_len++;
				position += lname_len;
			}
			if (position + size + 8 + lname_len > header.cluster_size)
				return ERR_FILE_BUFFER_TOO_LARGE;
			fseek(h_rfe.next_cluster * header.cluster_size, HFS_SEEK_SET);
			for (uint64_t i = 0; i < depth; i++)
			{
				uint64_t n_cluster = 0;
				fseek(header.cluster_size - 8, HFS_SEEK_CUR);
				read(&n_cluster, sizeof(n_cluster));
				if (n_cluster == CLUSTER_END || n_cluster == CLUSTER_END_NUB)
					return ERR_FILE_DEPTH_TOO_LARGE;
				fseek(n_cluster * header.cluster_size, HFS_SEEK_SET);
			}
			uint64_t p = ftell();
			uint16_t bytes_used = 0;
			uint64_t n_cl = 0;
			fseek(header.cluster_size - 10, HFS_SEEK_CUR);
			read(&bytes_used, sizeof(bytes_used));
			read(&n_cl, sizeof(n_cl));
			if (size > bytes_used && n_cl == CLUSTER_END)
				return ERR_FILE_BUFFER_TOO_LARGE;
			fseek(p, HFS_SEEK_SET);
			fseek(position, HFS_SEEK_CUR);
			read(buffer, size);
			return 0;
		}
		// auth_level 0 = user 1 = root/owner
		int f_can_read(uint64_t fptr, int auth_level)
		{
			fptr--;
			auth_level *= 3;
			return (rfe[fptr].attribute & (0b01000000 >> auth_level)) > 0;
		}
		int f_can_write(uint64_t fptr, int auth_level)
		{
			fptr--;
			auth_level *= 3;
			return (rfe[fptr].attribute & (0b00100000 >> auth_level)) > 0;
		}
		int f_can_execute(uint64_t fptr, int auth_level)
		{
			fptr--;
			auth_level *= 3;
			return (rfe[fptr].attribute & (0b00010000 >> auth_level)) > 0;
		}
		int f_is_hidden(uint64_t fptr)
		{
			fptr--;
			return (rfe[fptr].attribute & 0b00000001) > 0;
		}
		uint16_t f_creation_date(uint64_t fptr)
		{
			fptr--;
			return rfe[fptr].creation_date;
		}
		uint16_t f_modification_date(uint64_t fptr)
		{
			fptr--;
			return rfe[fptr].modification_date;
		}
		uint8_t f_get_owner(uint64_t fptr)
		{
			fptr--;
			return rfe[fptr].owner_id;
		}
		// 12 bytes 4 bytes
		void f_get_name(uint64_t fptr, uint8_t* name, uint8_t* extention)
		{
			fptr--;
			memcpy(name, rfe[fptr].name, 12);
			memcpy(extention, rfe[fptr].extention, 4);
		}
		void f_set_read(uint64_t fptr, int auth_level, int val)
		{
			fptr--;
			uint8_t magic = 0b01000000 >> (auth_level * 3);
			rfe[fptr].attribute ^= magic;
			rfe[fptr].attribute |= val ? magic : 0;
			write_rfe_chain();
		}
		void f_set_write(uint64_t fptr, int auth_level, int val)
		{
			fptr--;
			uint8_t magic = 0b00100000 >> (auth_level * 3);
			rfe[fptr].attribute ^= magic;
			rfe[fptr].attribute |= val ? magic : 0;
			write_rfe_chain();
		}
		void f_set_execute(uint64_t fptr, int auth_level, int val)
		{
			fptr--;
			uint8_t magic = 0b00010000 >> (auth_level * 3);
			rfe[fptr].attribute ^= magic;
			rfe[fptr].attribute |= val ? magic : 0;
			write_rfe_chain();
		}
		void f_set_hidden(uint64_t fptr, int val)
		{
			fptr--;
			uint8_t magic = 0b00000001;
			rfe[fptr].attribute ^= magic;
			rfe[fptr].attribute |= val ? magic : 0;
			write_rfe_chain();
		}
		void f_set_owner(uint8_t owner)
		{
			header.owner_id = owner;
			fseek(0, HFS_SEEK_SET);
			write(&header, HEADER_SIZE);
		}
		void f_set_name(uint64_t fptr, uint8_t* name, uint8_t* extention)
		{
			fptr--;
			memcpy(rfe[fptr].name, name, 12);
			memcpy(rfe[fptr].extention, extention, 4);
			write_rfe_chain();
		}
		uint16_t vol_creation_date()
		{
			return header.creation_date;
		}
		uint64_t vol_size()
		{
			return header.cluster_size * header.clusters;
		}
		// 12 bytes
		void vol_get_name(uint8_t* name)
		{
			memcpy(name, header.name, 12);
		}
		void vol_set_name(uint8_t* name)
		{
			memcpy(header.name, name, 12);
			fseek(0, HFS_SEEK_SET);
			write(&header, HEADER_SIZE);
		}
		uint8_t vol_get_version()
		{
			return header.attribute & 0b00000011;
		}
		int vol_can_read(int auth_level)
		{
			return (header.attribute & (0b01000000 >> (auth_level * 3))) > 0;
		}
		int vol_can_write(int auth_level)
		{
			return (header.attribute & (0b00100000 >> (auth_level * 3))) > 0;
		}
		int vol_is_hidden()
		{
			return (header.attribute & 0b00000100) > 0;
		}
		void vol_set_read(int auth_level, int val)
		{
			uint8_t magic = 0b01000000 >> (auth_level * 3);
			header.attribute ^= magic;
			header.attribute |= val ? magic : 0;
			write(&header, HEADER_SIZE);
		}
		void vol_set_write(int auth_level, int val)
		{
			uint8_t magic = 0b00100000 >> (auth_level * 3);
			header.attribute ^= magic;
			header.attribute |= val ? magic : 0;
			write(&header, HEADER_SIZE);
		}
		void vol_set_hidden(int val)
		{
			uint8_t magic = header.attribute & 0b00000100;
			header.attribute ^= magic;
			header.attribute |= val ? magic : 0;
			fseek(0, HFS_SEEK_SET);
			write(&header, HEADER_SIZE);
		}
	};
}

// Example of RW functions with file_vptr being a pointer to an std::fstream
/*
size_t read(void* buff, size_t size, size_t position, void* file_vptr)
{
	std::fstream* file = (std::fstream*)file_vptr;
	if (size == 0)
		return file->tellp();
	file->seekp(position, std::ios::beg);
	file->read((char*)buff, size);
	return file->tellp();
}
size_t write(void* buff, size_t size, size_t position, void* file_vptr)
{
	std::fstream* file = (std::fstream*)file_vptr;
	if (size == 0)
	{
		switch (*(uint8_t*)buff)
		{
			case hfs::HFS_SEEK_SET:
				file->seekp(position, std::ios::beg);
				return file->tellp();
			case hfs::HFS_SEEK_CUR:
				file->seekp(position, std::ios::cur);
				return file->tellp();
			case hfs::HFS_SEEK_END:
				file->seekp(position, std::ios::end);
				return file->tellp();
			default:
				return position - 1;
		}
	}
	file->seekp(position, std::ios::beg);
	file->write((char*)buff, size);
	return file->tellp();
}
*/
