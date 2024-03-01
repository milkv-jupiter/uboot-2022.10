/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 2023 Spacemit, Inc
 */

#ifndef _FB_SPACEMIT_H_
#define _FB_SPACEMIT_H_

#include <mtd.h>

/*define max partition number*/
#define MAX_PARTITION_NUM (20)

#define MAX_BLK_WRITE (16384)
#define RESULT_OK (0)
#define RESULT_FAIL (1)

/*recovery folder name*/
#define FLASH_IMG_FOLDER ("")
#define FLASH_IMG_FACTORY_FOLDER ("factory")
/*FLASH_CONFIG_FILE_NAME would used as flag to excute card flash*/
#define FLASH_CONFIG_FILE_NAME ("partition_universal.json")
#define FLASH_IMG_PARTNAME ("bootfs")
#define BIG_IMG_PARTNAME ("rootfs")

/*if they have different addr, it can define here*/
#ifdef CONFIG_ENV_OFFSET
#define FLASH_ENV_OFFSET_MMC (CONFIG_ENV_OFFSET)
#define FLASH_ENV_OFFSET_NOR (CONFIG_ENV_OFFSET)
#define FLASH_ENV_OFFSET_NAND (CONFIG_ENV_OFFSET)
#endif

/*define bootinfo for emmc*/
#define BOOT_INFO_EMMC_MAGICCODE (0xb00714f0)
#define BOOT_INFO_EMMC_VERSION (0x00010001)
#define BOOT_INFO_EMMC_PAGESIZE (0x200)
#define BOOT_INFO_EMMC_BLKSIZE (0x10000)
#define BOOT_INFO_EMMC_TOTALSIZE (0x10000000)
#define BOOT_INFO_EMMC_SPL0_OFFSET (0x200)
#define BOOT_INFO_EMMC_SPL1_OFFSET (0x0)
#define BOOT_INFO_EMMC_LIMIT (CONFIG_SPL_SIZE_LIMIT)

typedef enum {
	DEVICE_MMC,
	DEVICE_USB,
	DEVICE_NET,
} DeviceType;

struct flash_parts_info {
	char *part_name;
	char *file_name;
	/*partition size info, such as 128MiB*/
	char *size;
	/*use for fsbl, if hidden that gpt would reserve a raw memeory
	  for fsbl and the partition is not available.
	*/
	bool hidden;
};

struct gpt_info {
	char *gpt_table;
	/*save gpt start offset*/
	u32 gpt_start_offset;
	bool fastboot_flash_gpt;
};

struct flash_dev {
	char *device_name;
	char *dev_str;
	struct flash_parts_info parts_info[MAX_PARTITION_NUM];
	struct gpt_info gptinfo;
	struct disk_partition *d_info;
	struct blk_desc *dev_desc;
	char *mtd_table;
	char mtd_partition_file[30];

	/*mtd write func*/
	int (*mtd_write)(struct mtd_info *mtd,
					const char *part_name,
					void *buffer,
					u32 download_bytes);

	/*blk write func*/
	int (*blk_write)(struct blk_desc *block_dev,
					struct disk_partition *info,
					const char *part_name,
					void *buffer,
					u32 download_bytes);
};

/**
 * @brief boot info struct
 *
 */
struct boot_parameter_info {
	uint32_t magic_code;
	uint32_t version_number;

	/* flash info */
	uint8_t flash_type[4];
	uint8_t mfr_id;
	uint8_t reserved1[1];
	uint16_t dev_id;
	uint32_t page_size;
	uint32_t block_size;
	uint32_t total_size;
	uint8_t multi_plane;
	uint8_t reserved2[3];

	/* spl partition */
	uint32_t spl0_offset;
	uint32_t spl1_offset;
	uint32_t spl_size_limit;

	/* partitiontable offset */
	uint32_t partitiontable0_offset;
	uint32_t partitiontable1_offset;

	uint32_t reserved[3];
	uint32_t crc32;
} __attribute__((packed));

/**
 * @brief Set the boot mode object, it would set boot mode to register
 *
 * @param boot_mode
 */
void set_boot_mode(enum board_boot_mode boot_mode);

/**
 * @brief Get the boot mode object, it would get boot mode from register,
 * the register would save boot_mode while boot from emmc/nor/nand success.
 * if not set boot mode, it would return get_boot_pin_select.
 *
 * @return u32
 */
enum board_boot_mode get_boot_mode(void);

/**
 * @brief Get the boot pin select object. it would get boot pin select,
 * which is different from get_boot_mode.
 *
 * @return u32
 */
enum board_boot_mode get_boot_pin_select(void);

/**
 * fastboot_oem_flash_gpt() - parse flash config and write gpt table.
 *
 * @cmd: Named partition to write image to
 * @download_buffer: Pointer to image data
 * @download_bytes: Size of image data
 * @response: Pointer to fastboot response buffer
 */
void fastboot_oem_flash_gpt(const char *cmd, void *download_buffer, u32 download_bytes,
							char *response, struct flash_dev *fdev);

/**
 * fastboot_mmc_flash_offset() - Write fsbl image to eMMC
 *
 * @start_offset: start offset to write.
 * @download_buffer: Pointer to image data
 * @download_bytes: Size of image data
 */
int fastboot_mmc_flash_offset(u32 start_offset, void *download_buffer, u32 download_bytes);

/**
 * @brief check image crc at blk dev. if crc is same it would return RESULT_OK(0).
 *
 * @param dev_desc struct blk_desc.
 * @param crc_compare need to be compare crc.
 * @param part_start_cnt read from blk offset.
 * @param blksz normally is 0x200.
 * @param image_size
 * @return int
 */
int check_blk_image_crc(struct blk_desc *dev_desc, ulong crc_compare, lbaint_t part_start_cnt,
						ulong blksz, int image_size);

/**
 * @brief check image crc at mtd dev. if crc is same it would return RESULT_OK(0).
 *
 * @param mtd mtd dev.
 * @param crc_compare need to be compare crc.
 * @param image_size
 * @return int
*/
int check_mtd_image_crc(struct mtd_info *mtd, ulong crc_compare, int image_size);

/**
 * @brief transfer the string of size 'KiB' or 'MiB' to u32 type.
 *
 * @param reserve_size , the string of size 'xiB'
 * @return int , return the transfer result.
 */
int transfer_string_to_ul(const char *reserve_size);

/**
 * @brief parse the flash_config and save partition info
 *
 * @param fdev , struct flash_dev
 * @return int , return 0 if parse config success.
 */
int _parse_flash_config(struct flash_dev *fdev, void *load_flash_addr);

/**
 * @brief update env to storage.
 *
 * @param download_buffer
 * @param download_bytes
 * @param response
 * @param fdev
 * @return int
 */
int _update_partinfo_to_env(void *download_buffer, u32 download_bytes,
							struct flash_dev *fdev);

/**
 * @brief flash env to reserve partition.
 *
 * @param cmd env
 * @param download_buffer load env.bin to addr
 * @param download_bytes env.bin size
 * @param response
 * @param fdev
 */
void fastboot_oem_flash_env(const char *cmd, void *download_buffer, u32 download_bytes,
							char *response, struct flash_dev *fdev);

/**
 * @brief flash bootinfo to reserve partition.
 *
 * @param cmd
 * @param download_buffer load env.bin to addr
 * @param download_bytes env.bin size
 * @param response
 * @param fdev
 */
void fastboot_oem_flash_bootinfo(const char *cmd, void *download_buffer, u32 download_bytes,
								 char *response, struct flash_dev *fdev);

/**
 * @brief flash mmc boot option
 *
 * @param dev_desc
 * @param buffer
 * @param hwpart
 * @param buff_sz
 * @return int
 */
int flash_mmc_boot_op(struct blk_desc *dev_desc, void *buffer,
					  int hwpart, u32 buff_sz, u32 offset);

char *parse_mtdparts_and_find_bootfs(void);
int get_partition_index_by_name(const char *part_name, int *part_index);

#endif
