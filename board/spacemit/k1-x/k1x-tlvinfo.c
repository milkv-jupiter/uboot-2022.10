// SPDX-License-Identifier: GPL-2.0+

#include <common.h>
#include <asm/io.h>
#include <stdlib.h>
#include <tlv_eeprom.h>
#include <u-boot/crc.h>
#include <net.h>

#define EEPROM_SIZE       (256)
#define EEPROM_SIZE_MAX_TLV_LEN (EEPROM_SIZE - sizeof(struct tlvinfo_header))

extern int k1x_eeprom_init(void);
extern int _read_from_i2c(int chip, u32 addr, u32 size, uchar *buf);
extern int _write_to_i2c(int chip, u32 addr, u32 size, uchar *buf);

/* File scope function prototypes */
static bool is_checksum_valid(u8 *eeprom);
static void update_crc(u8 *eeprom);
static bool tlvinfo_find_tlv(u8 *eeprom, u8 tcode, int *eeprom_index);
static bool tlvinfo_delete_tlv(u8 *eeprom, u8 code);
static bool tlvinfo_add_tlv(u8 *eeprom, int tcode, char *strval);
static int set_mac(char *buf, const char *string);
static int set_date(char *buf, const char *string);
static int set_bytes(char *buf, const char *string, int *converted_accum);

static u8 tlvinfo_eeprom[EEPROM_SIZE];
static bool had_read_tlvinfo = false;

/**
 *  _is_valid_tlvinfo_header
 *
 *  Perform sanity checks on the first 11 bytes of the TlvInfo EEPROM
 *  data pointed to by the parameter:
 *      1. First 8 bytes contain null-terminated ASCII string "TlvInfo"
 *      2. Version byte is 1
 *      3. Total length bytes contain value which is less than or equal
 *         to the allowed maximum (2048-11)
 *
 */
static bool _is_valid_tlvinfo_header(struct tlvinfo_header *hdr)
{
	return ((strcmp(hdr->signature, TLV_INFO_ID_STRING) == 0) &&
		(hdr->version == TLV_INFO_VERSION) &&
		(be16_to_cpu(hdr->totallen) <= TLV_TOTAL_LEN_MAX));
}

static inline bool is_valid_tlv(struct tlvinfo_tlv *tlv)
{
	return((tlv->type != 0x00) && (tlv->type != 0xFF));
}

static inline bool is_digit(char c)
{
	return (c >= '0' && c <= '9');
}

/**
 *  is_hex
 *
 *  Tests if character is an ASCII hex digit
 */
static inline u8 is_hex(char p)
{
	return (((p >= '0') && (p <= '9')) ||
		((p >= 'A') && (p <= 'F')) ||
		((p >= 'a') && (p <= 'f')));
}

/**
 *  is_checksum_valid
 *
 *  Validate the checksum in the provided TlvInfo EEPROM data. First,
 *  verify that the TlvInfo header is valid, then make sure the last
 *  TLV is a CRC-32 TLV. Then calculate the CRC over the EEPROM data
 *  and compare it to the value stored in the EEPROM CRC-32 TLV.
 */
static bool is_checksum_valid(u8 *eeprom)
{
	struct tlvinfo_header *eeprom_hdr = (struct tlvinfo_header *)eeprom;
	struct tlvinfo_tlv    *eeprom_crc;
	unsigned int       calc_crc;
	unsigned int       stored_crc;

	// Is the eeprom header valid?
	if (!_is_valid_tlvinfo_header(eeprom_hdr)){
		pr_err("%s, not valid tlv info header\n", __func__);
		return false;
	}

	// Is the last TLV a CRC?
	eeprom_crc = (struct tlvinfo_tlv *)(&eeprom[sizeof(struct tlvinfo_header) +
		be16_to_cpu(eeprom_hdr->totallen) - (sizeof(struct tlvinfo_tlv) + 4)]);
	if (eeprom_crc->type != TLV_CODE_CRC_32 || eeprom_crc->length != 4)
		return false;

	// Calculate the checksum
	calc_crc = crc32(0, (void *)eeprom,
			 sizeof(struct tlvinfo_header) + be16_to_cpu(eeprom_hdr->totallen) - 4);
	stored_crc = (eeprom_crc->value[0] << 24) |
		(eeprom_crc->value[1] << 16) |
		(eeprom_crc->value[2] <<  8) |
		eeprom_crc->value[3];

	return calc_crc == stored_crc;
}

/**
 *  set_mac
 *
 *  Converts a string MAC address into a binary buffer.
 *
 *  This function takes a pointer to a MAC address string
 *  (i.e."XX:XX:XX:XX:XX:XX", where "XX" is a two-digit hex number).
 *  The string format is verified and then converted to binary and
 *  stored in a buffer.
 */
static int set_mac(char *buf, const char *string)
{
	char *p = (char *)string;
	int   i;
	int   err = 0;
	char *end;

	if (!p) {
		pr_err("ERROR: NULL mac addr string passed in.\n");
		return -1;
	}

	if (strlen(p) != 17) {
		pr_err("ERROR: MAC address strlen() != 17 -- %zu\n", strlen(p));
		pr_err("ERROR: Bad MAC address format: %s\n", string);
		return -1;
	}

	for (i = 0; i < 17; i++) {
		if ((i % 3) == 2) {
			if (p[i] != ':') {
				err++;
				pr_err("ERROR: mac: p[%i] != :, found: `%c'\n",
				       i, p[i]);
				break;
			}
			continue;
		} else if (!is_hex(p[i])) {
			err++;
			pr_err("ERROR: mac: p[%i] != hex digit, found: `%c'\n",
			       i, p[i]);
			break;
		}
	}

	if (err != 0) {
		pr_err("ERROR: Bad MAC address format: %s\n", string);
		return -1;
	}

	/* Convert string to binary */
	for (i = 0, p = (char *)string; i < 6; i++) {
		buf[i] = p ? hextoul(p, &end) : 0;
		if (p)
			p = (*end) ? end + 1 : end;
	}

	if (!is_valid_ethaddr((u8 *)buf)) {
		pr_err("ERROR: MAC address must not be 00:00:00:00:00:00, a multicast address or FF:FF:FF:FF:FF:FF.\n");
		pr_err("ERROR: Bad MAC address format: %s\n", string);
		return -1;
	}

	return 0;
}

/**
 *  set_date
 *
 *  Validates the format of the data string
 *
 *  This function takes a pointer to a date string (i.e. MM/DD/YYYY hh:mm:ss)
 *  and validates that the format is correct. If so the string is copied
 *  to the supplied buffer.
 */
static int set_date(char *buf, const char *string)
{
	int i;

	if (!string) {
		pr_err("ERROR: NULL date string passed in.\n");
		return -1;
	}

	if (strlen(string) != 19) {
		pr_err("ERROR: Date strlen() != 19 -- %zu\n", strlen(string));
		pr_err("ERROR: Bad date format (MM/DD/YYYY hh:mm:ss): %s\n",
		       string);
		return -1;
	}

	for (i = 0; string[i] != 0; i++) {
		switch (i) {
		case 2:
		case 5:
			if (string[i] != '/') {
				pr_err("ERROR: Bad date format (MM/DD/YYYY hh:mm:ss): %s\n",
				       string);
				return -1;
			}
			break;
		case 10:
			if (string[i] != ' ') {
				pr_err("ERROR: Bad date format (MM/DD/YYYY hh:mm:ss): %s\n",
				       string);
				return -1;
			}
			break;
		case 13:
		case 16:
			if (string[i] != ':') {
				pr_err("ERROR: Bad date format (MM/DD/YYYY hh:mm:ss): %s\n",
				       string);
				return -1;
			}
			break;
		default:
			if (!is_digit(string[i])) {
				pr_err("ERROR: Bad date format (MM/DD/YYYY hh:mm:ss): %s\n",
				       string);
				return -1;
			}
			break;
		}
	}

	strcpy(buf, string);
	return 0;
}

/**
 *  set_bytes
 *
 *  Converts a space-separated string of decimal numbers into a
 *  buffer of bytes.
 *
 *  This function takes a pointer to a space-separated string of decimal
 *  numbers (i.e. "128 0x55 0321") with "C" standard radix specifiers
 *  and converts them to an array of bytes.
 */
static int set_bytes(char *buf, const char *string, int *converted_accum)
{
	char *p = (char *)string;
	int   i;
	uint  byte;

	if (!p) {
		pr_err("ERROR: NULL string passed in.\n");
		return -1;
	}

	/* Convert string to bytes */
	for (i = 0, p = (char *)string; (i < TLV_VALUE_MAX_LEN) && (*p != 0);
			i++) {
		while ((*p == ' ') || (*p == '\t') || (*p == ',') ||
		       (*p == ';')) {
			p++;
		}
		if (*p != 0) {
			if (!is_digit(*p)) {
				pr_err("ERROR: Non-digit found in byte string: (%s)\n",
				       string);
				return -1;
			}
			byte = simple_strtoul(p, &p, 0);
			if (byte >= EEPROM_SIZE) {
				pr_err("ERROR: The value specified is greater than 255: (%u) in string: %s\n",
				       byte, string);
				return -1;
			}
			buf[i] = byte & 0xFF;
		}
	}

	if (i == TLV_VALUE_MAX_LEN && (*p != 0)) {
		pr_err("ERROR: Trying to assign too many bytes (max: %d) in string: %s\n",
		       TLV_VALUE_MAX_LEN, string);
		return -1;
	}

	*converted_accum = i;
	return 0;
}


/**
 *  tlvinfo_find_tlv
 *
 *  This function finds the TLV with the supplied code in the EERPOM.
 *  An offset from the beginning of the EEPROM is returned in the
 *  eeprom_index parameter if the TLV is found.
 */
static bool tlvinfo_find_tlv(u8 *eeprom, u8 tcode, int *eeprom_index)
{
	struct tlvinfo_header *eeprom_hdr = (struct tlvinfo_header *)eeprom;
	struct tlvinfo_tlv    *eeprom_tlv;
	int eeprom_end;

	// Search through the TLVs, looking for the first one which matches the
	// supplied type code.
	*eeprom_index = sizeof(struct tlvinfo_header);
	eeprom_end = sizeof(struct tlvinfo_header) + be16_to_cpu(eeprom_hdr->totallen);
	while (*eeprom_index < eeprom_end) {
		eeprom_tlv = (struct tlvinfo_tlv *)(&eeprom[*eeprom_index]);
		if (!is_valid_tlv(eeprom_tlv))
			return false;
		if (eeprom_tlv->type == tcode)
			return true;
		*eeprom_index += sizeof(struct tlvinfo_tlv) + eeprom_tlv->length;
	}
	return(false);
}


/**
 *  update_crc
 *
 *  This function updates the CRC-32 TLV. If there is no CRC-32 TLV, then
 *  one is added. This function should be called after each update to the
 *  EEPROM structure, to make sure the CRC is always correct.
 */
static void update_crc(u8 *eeprom)
{
	struct tlvinfo_header *eeprom_hdr = (struct tlvinfo_header *)eeprom;
	struct tlvinfo_tlv    *eeprom_crc;
	unsigned int      calc_crc;
	int               eeprom_index;

	// Discover the CRC TLV
	if (!tlvinfo_find_tlv(eeprom, TLV_CODE_CRC_32, &eeprom_index)) {
		unsigned int totallen = be16_to_cpu(eeprom_hdr->totallen);

		if ((totallen + sizeof(struct tlvinfo_tlv) + 4) > EEPROM_SIZE_MAX_TLV_LEN)
			return;
		eeprom_index = sizeof(struct tlvinfo_header) + totallen;
		eeprom_hdr->totallen = cpu_to_be16(totallen + sizeof(struct tlvinfo_tlv) + 4);
	}
	eeprom_crc = (struct tlvinfo_tlv *)(&eeprom[eeprom_index]);
	eeprom_crc->type = TLV_CODE_CRC_32;
	eeprom_crc->length = 4;

	// Calculate the checksum
	calc_crc = crc32(0, (void *)eeprom,
			 sizeof(struct tlvinfo_header) + be16_to_cpu(eeprom_hdr->totallen) - 4);
	eeprom_crc->value[0] = (calc_crc >> 24) & 0xFF;
	eeprom_crc->value[1] = (calc_crc >> 16) & 0xFF;
	eeprom_crc->value[2] = (calc_crc >>  8) & 0xFF;
	eeprom_crc->value[3] = (calc_crc >>  0) & 0xFF;
}

static bool tlvinfo_add_tlv(u8 *eeprom, int tcode, char *strval)
{
	struct tlvinfo_header *eeprom_hdr = (struct tlvinfo_header *)eeprom;
	struct tlvinfo_tlv *eeprom_tlv;
	int new_tlv_len = 0;
	u32 value;
	char data[EEPROM_SIZE];
	int eeprom_index;

	// Encode each TLV type into the format to be stored in the EERPOM
	switch (tcode) {
	case TLV_CODE_PRODUCT_NAME:
	case TLV_CODE_PART_NUMBER:
	case TLV_CODE_SERIAL_NUMBER:
	case TLV_CODE_LABEL_REVISION:
	case TLV_CODE_PLATFORM_NAME:
	case TLV_CODE_ONIE_VERSION:
	case TLV_CODE_MANUF_NAME:
	case TLV_CODE_MANUF_COUNTRY:
	case TLV_CODE_VENDOR_NAME:
	case TLV_CODE_DIAG_VERSION:
	case TLV_CODE_SERVICE_TAG:
		strncpy(data, strval, EEPROM_SIZE);
		new_tlv_len = min_t(size_t, EEPROM_SIZE, strlen(strval));
		break;
	case TLV_CODE_DEVICE_VERSION:
		value = simple_strtoul(strval, NULL, 0);
		if (value >= EEPROM_SIZE) {
			pr_err("ERROR: Device version must be 255 or less. Value supplied: %u",
			       value);
			return false;
		}
		data[0] = value & 0xFF;
		new_tlv_len = 1;
		break;
	case TLV_CODE_MAC_SIZE:
		value = simple_strtoul(strval, NULL, 0);
		if (value >= 65536) {
			pr_err("ERROR: MAC Size must be 65535 or less. Value supplied: %u",
			       value);
			return false;
		}
		data[0] = (value >> 8) & 0xFF;
		data[1] = value & 0xFF;
		new_tlv_len = 2;
		break;
	case TLV_CODE_MANUF_DATE:
		if (set_date(data, strval) != 0)
			return false;
		new_tlv_len = 19;
		break;
	case TLV_CODE_MAC_BASE:
		if (set_mac(data, strval) != 0)
			return false;
		new_tlv_len = 6;
		break;
	case TLV_CODE_CRC_32:
		pr_err("WARNING: The CRC TLV is set automatically and cannot be set manually.\n");
		return false;
	case TLV_CODE_VENDOR_EXT:
	default:
		if (set_bytes(data, strval, &new_tlv_len) != 0)
			return false;
		break;
	}

	// Is there room for this TLV?
	if ((be16_to_cpu(eeprom_hdr->totallen) + sizeof(struct tlvinfo_tlv) + new_tlv_len) >
			EEPROM_SIZE_MAX_TLV_LEN) {
		pr_err("ERROR: There is not enough room in the EERPOM to save data.\n");
		return false;
	}

	// Add TLV at the end, overwriting CRC TLV if it exists
	if (tlvinfo_find_tlv(eeprom, TLV_CODE_CRC_32, &eeprom_index))
		eeprom_hdr->totallen =
			cpu_to_be16(be16_to_cpu(eeprom_hdr->totallen) -
					sizeof(struct tlvinfo_tlv) - 4);
	else
		eeprom_index = sizeof(struct tlvinfo_header) + be16_to_cpu(eeprom_hdr->totallen);
	eeprom_tlv = (struct tlvinfo_tlv *)(&eeprom[eeprom_index]);
	eeprom_tlv->type = tcode;
	eeprom_tlv->length = new_tlv_len;
	memcpy(eeprom_tlv->value, data, new_tlv_len);

	// Update the total length and calculate (add) a new CRC-32 TLV
	eeprom_hdr->totallen = cpu_to_be16(be16_to_cpu(eeprom_hdr->totallen) +
			sizeof(struct tlvinfo_tlv) + new_tlv_len);
	update_crc(eeprom);

	return true;
}

static bool tlvinfo_delete_tlv(u8 *eeprom, u8 code)
{
	int eeprom_index;
	int tlength;
	struct tlvinfo_header *eeprom_hdr = (struct tlvinfo_header *)eeprom;
	struct tlvinfo_tlv *eeprom_tlv;

	// Find the TLV and then move all following TLVs "forward"
	if (tlvinfo_find_tlv(eeprom, code, &eeprom_index)) {
		eeprom_tlv = (struct tlvinfo_tlv *)(&eeprom[eeprom_index]);
		tlength = sizeof(struct tlvinfo_tlv) + eeprom_tlv->length;
		memcpy(&eeprom[eeprom_index], &eeprom[eeprom_index + tlength],
		       sizeof(struct tlvinfo_header) +
		       be16_to_cpu(eeprom_hdr->totallen) - eeprom_index -
		       tlength);
		eeprom_hdr->totallen =
			cpu_to_be16(be16_to_cpu(eeprom_hdr->totallen) -
				    tlength);
		update_crc(eeprom);
		return true;
	}
	return false;
}


static int read_tlvinfo_from_eeprom(u8 *eeprom)
{
	struct tlvinfo_header *eeprom_hdr = (struct tlvinfo_header *)eeprom;
	struct tlvinfo_tlv *eeprom_tlv = (struct tlvinfo_tlv *)(&eeprom[sizeof(struct tlvinfo_header)]);
	int chip;

	chip = k1x_eeprom_init();
	if (chip < 0){
		pr_err("can not get i2c bus addr\n");
		return -1;
	}

	/*read tlv head info*/
	if (_read_from_i2c(chip, 0, sizeof(struct tlvinfo_header), (uchar *)eeprom_hdr)){
		pr_err("read tlvinfo_header from i2c fail\n");
		return -1;
	}

	if (_is_valid_tlvinfo_header(eeprom_hdr)){
		if (_read_from_i2c(chip, sizeof(struct tlvinfo_header), be16_to_cpu(eeprom_hdr->totallen),
							(uchar *)eeprom_tlv)){
			pr_err("read tlvinvo_tlv from i2c fail\n");
			return -1;
		}
	}

	// If the contents are invalid, start over with default contents
	if (!_is_valid_tlvinfo_header(eeprom_hdr) ||
	    !is_checksum_valid(eeprom)) {
		strcpy(eeprom_hdr->signature, TLV_INFO_ID_STRING);
		eeprom_hdr->version = TLV_INFO_VERSION;
		eeprom_hdr->totallen = cpu_to_be16(0);
		pr_info("update new tlv info\n");
		update_crc(eeprom);
	}
	return 0;
}

int get_tlvinfo_from_eeprom(int tcode, char *buf)
{
	int tlv_end;
	int curr_tlv;
	u8 eeprom[EEPROM_SIZE];
	memset(eeprom, 0, EEPROM_SIZE);

	if (read_tlvinfo_from_eeprom(eeprom)){
		pr_err("read tlv info fail\n");
		return -1;
	}

	struct tlvinfo_header *eeprom_hdr = (struct tlvinfo_header *)eeprom;
	struct tlvinfo_tlv    *eeprom_tlv;

	curr_tlv = sizeof(struct tlvinfo_header);
	tlv_end  = sizeof(struct tlvinfo_header) + be16_to_cpu(eeprom_hdr->totallen);
	while (curr_tlv < tlv_end) {
		eeprom_tlv = (struct tlvinfo_tlv *)(&eeprom[curr_tlv]);
		if (!is_valid_tlv(eeprom_tlv)) {
			pr_err("Invalid TLV field starting at EEPROM offset %d\n",
			       curr_tlv);
			return -1;
		}

		if (eeprom_tlv->type == tcode){
			memcpy(buf, eeprom_tlv->value, eeprom_tlv->length);
			pr_info("get tlvinfo value:%x,%s\n", tcode, buf);
			return 0;
		}
		curr_tlv += sizeof(struct tlvinfo_tlv) + eeprom_tlv->length;
	}
	pr_info("can not get tlvinfo index:%x\n", tcode);
	return -1;
}


int set_val_to_tlvinfo(int tcode, char *val)
{
	if (!had_read_tlvinfo){
		/*read tlvinfo at first*/
		read_tlvinfo_from_eeprom(tlvinfo_eeprom);
		had_read_tlvinfo = true;
	}

	tlvinfo_delete_tlv(tlvinfo_eeprom, tcode);
	if (val != NULL)
		tlvinfo_add_tlv(tlvinfo_eeprom, tcode, val);

	return 0;
}

int write_tlvinfo_to_eeprom(void)
{
	struct tlvinfo_header *eeprom_hdr;
	int eeprom_len;

	int chip = k1x_eeprom_init();
	if (chip < 0){
		pr_err("can not get i2c bus addr\n");
		return -1;
	}

	if (!had_read_tlvinfo){
		/*read tlvinfo at first*/
		read_tlvinfo_from_eeprom(tlvinfo_eeprom);
		had_read_tlvinfo = true;
	}

	eeprom_hdr = (struct tlvinfo_header *)tlvinfo_eeprom;
	update_crc(tlvinfo_eeprom);

	eeprom_len = sizeof(struct tlvinfo_header) + be16_to_cpu(eeprom_hdr->totallen);
	if (_write_to_i2c(chip, 0, eeprom_len, tlvinfo_eeprom)){
		pr_err("write to eeprom fail\n");
		return -1;
	}

	return 0;
}
