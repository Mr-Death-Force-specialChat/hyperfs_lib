#pragma once
#include <stdint.h>
#include <functional>

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

	struct hfs_object
	{
		// Buffer, size, position, extra_args
		// if size == 0
		// 		return position
		std::function<size_t(void*, size_t, size_t, void*)> read_fn;
		// Buffer, size, position, extra_args
		// if size == 0
		// 		set position with SEEK_MODE in buffer.
		std::function<size_t(void*, size_t, size_t, void*)> write_fn;
		// extra_args
		// Truncates file.
		std::function<void(void*)> reset_fn();

		void* extra_args;

		int no_read;
		int bootable;

		int32_t init();
		int uninit();
		int32_t parse();
		// Name is 12 bytes;
		int format(uint64_t cluster_size, uint64_t clusters, uint32_t signature, uint8_t* name, uint8_t attributes, uint8_t owner_id, uint8_t boot_sig_0, uint8_t boot_sig_1, uint8_t lname_len, uint8_t* lname);
		// Name is 12 bytes; Extention is 4 bytes;
		int32_t add_file(uint8_t* name, uint8_t* extention, uint8_t attribute, uint8_t owner_id);
		// Returns 0 when failed.
		uint64_t lock_file(uint8_t* name, uint8_t* extention);
		int32_t unlock_file(uint64_t fptr);
		// Buffer max size is cluster_size - position - 10 // Depth: How many next_cluster chains will it seek before writing // Ex_buff: adds an extra buffer and seeks to it before writing (unless size is 0)
		int32_t write_buff(uint64_t fptr, void* buffer, uint64_t size, uint64_t position, uint64_t depth, int ex_buff);
		int32_t read_buff(uint64_t fptr, void* buffer, uint64_t size, uint64_t position, uint64_t depth);
		// auth_level 0 = user 1 = root/owner
		int f_can_read(uint64_t fptr, int auth_level);
		int f_can_write(uint64_t fptr, int auth_level);
		int f_can_execute(uint64_t fptr, int auth_level);
		int f_is_hidden(uint64_t fptr);
		uint16_t f_creation_date(uint64_t fptr);
		uint16_t f_modification_date(uint64_t fptr);
		uint8_t f_get_owner(uint64_t fptr);
		// 12 bytes 4 bytes
		void f_get_name(uint64_t fptr, uint8_t* name, uint8_t* extention);
		void f_set_read(uint64_t fptr, int auth_level, int val);
		void f_set_write(uint64_t fptr, int auth_level, int val);
		void f_set_execute(uint64_t fptr, int auth_level, int val);
		void f_set_hidden(uint64_t fptr, int val);
		void f_set_owner(uint8_t owner);
		void f_set_name(uint64_t fptr, uint8_t* name, uint8_t* extention);
		uint16_t vol_creation_date();
		uint64_t vol_size();
		// 12 bytes
		void vol_get_name(uint8_t* name);
		void vol_set_name(uint8_t* name);
		uint8_t vol_get_version();
		int vol_can_read(int auth_level);
		int vol_can_write(int auth_level);
		int vol_is_hidden();
		void vol_set_read(int auth_level, int val);
		void vol_set_write(int auth_level, int val);
		void vol_set_hidden(int val);
	};
}
