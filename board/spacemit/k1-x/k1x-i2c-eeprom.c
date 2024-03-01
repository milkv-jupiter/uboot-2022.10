// SPDX-License-Identifier: GPL-2.0+

#include <env.h>
#include <i2c.h>
#include <asm/io.h>
#include <common.h>
#include <asm/global_data.h>

DECLARE_GLOBAL_DATA_PTR;

#define MUX_MODE0	0			/* func 0 */
#define MUX_MODE1	BIT(0)			/* func 1 */
#define MUX_MODE2	BIT(1)			/* func 2 */
#define MUX_MODE3	BIT(0) | BIT(1)		/* func 3 */
#define MUX_MODE4	BIT(2)			/* func 4 */
#define MUX_MODE5	BIT(0) | BIT(2)		/* func 5 */
#define EDGE_NONE	BIT(6)			/* edge-detection is unabled */
#define PAD_1V8_DS2	BIT(12)			/* voltage:1.8v, driver strength: 2 */
#define PULL_UP       	BIT(14) | BIT(15)	/* pull-up */

char *spacemit_i2c_eeprom[] = {
	"atmel,24c02",
};

struct tlv_eeprom {
	u8    type;
	u8  length;
};

int spacemit_eeprom_read(uint8_t chip, uint8_t *buffer, uint8_t id)
{
	struct tlv_eeprom tlv;
	int ret;
	uint8_t buf[1] = {0};
	uint8_t len[1] = {0};
    	u16 i = 0;
	u8 j;

	tlv.type = 0;
	tlv.length = 0;

	for (i = 11; i <= 256; i = i + tlv.length + 2) {
		ret = i2c_read(chip, i, 1, buf, 1);
		tlv.type = *buf;

		ret = i2c_read(chip, i + 1, 1, len, 1);
		tlv.length = *len;

		if (tlv.length == 0) {
			pr_err("Error: wrong tlv length\n");
			return -1;
		}

		if (tlv.type == id) {
			for(j = 0; j < tlv.length; j++) {
				ret = i2c_read(chip, i + 2 + j, 1, (char *)buffer, 1);
				buffer++;
			}
			return 0;
		}
	}

	pr_info("No 0x%x tlv type in eeprom\n", id);
	return -2;
}

void i2c_set_pinctrl(int bus, int pin)
{
	if ((bus == 2) && (pin == 0)) {
		//gpio84
		writel(MUX_MODE4 | EDGE_NONE | PULL_UP | PAD_1V8_DS2, (void __iomem *)0xd401e154);
		//gpio85
		writel(MUX_MODE4 | EDGE_NONE | PULL_UP | PAD_1V8_DS2, (void __iomem *)0xd401e158);
	} else if ((bus == 6) && (pin == 1)) {
		//gpio118
		writel(MUX_MODE2 | EDGE_NONE | PULL_UP | PAD_1V8_DS2, (void __iomem *)0xd401e228);
		//gpio119
		writel(MUX_MODE2 | EDGE_NONE | PULL_UP | PAD_1V8_DS2, (void __iomem *)0xd401e22c);
	}else
		pr_err("bus or pinctrl wrong\n");
}

const uint8_t eeprom_config[][2] = {
	// eeprom in evb: I2C6, pin group1(GPIO_118, GPIO_119)
	{2, 0},
	// eeprom in deb1 & deb2: I2C2, pin group0(GPIO_84, GPIO_85)
	{6, 1},
};

int k1x_eeprom_init(void)
{
	int offset, ret, saddr, i;
	char *name;
	uint8_t bus, pin;

	for(i = 0; i < ARRAY_SIZE(spacemit_i2c_eeprom); i++){
		name = spacemit_i2c_eeprom[i];
		offset = fdt_node_offset_by_compatible(gd->fdt_blob, -1, name);
		if(offset > 0){
			pr_info("Get %s node \n", name);
			break;
		}
	}

	if (offset < 0) {
		pr_err("%s Get eeprom node error\n", __func__);
		return -EINVAL;
	}

	saddr = fdtdec_get_uint(gd->fdt_blob, offset, "reg", 0);
	if (!saddr) {
		pr_err("%s: %s Node has no reg\n", __func__, name);
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(eeprom_config); i++) {
		bus = eeprom_config[i][0];
		pin = eeprom_config[i][1];
		i2c_set_pinctrl(bus, pin);

		ret = i2c_set_bus_num(bus);
		if (ret < 0) {
			pr_err("%s: %s set i2c bus number error\n", __func__, name);
			continue;
		}

		ret = i2c_probe(saddr);
		if (ret < 0) {
			pr_err("%s: %s probe i2c(%d) failed\n", __func__, name, bus);
			continue;
		}
		break;
	}

	if (i >= ARRAY_SIZE(eeprom_config))
		return -EINVAL;
	else {
		pr_info("find eeprom in bus %d, address %d\n", eeprom_config[i][0], saddr);
		return saddr;
	}
}
