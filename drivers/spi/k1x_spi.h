// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Spacemit k1x spi controller
 *
 * Copyright (c) 2023, spacemit Corporation.
 *
 */

#ifndef _K1X_SPI_H
#define _K1X_SPI_H

/* registers definitions for SSP0 */
#define REG_SSP_TOP_CTRL			(0x00)/*SSP Top Control Register*/
#define REG_SSP_FIFO_CTRL			(0x04)/*SSP FIFO Control Register*/
#define REG_SSP_INT_EN				(0x08)/*SSP Interrupt Enable Register*/
#define REG_SSP_TO				(0x0C)/*SSP Time Out Register*/
#define REG_SSP_DATAR				(0x10)/*SSP Data Register*/
#define REG_SSP_STATUS				(0x14)/*SSP Status Register*/
#define REG_SSP_PSP_CTRL			(0x18)/*SSP Programmable Serial Protocol Control Register*/
#define REG_SSP_NET_WORK_CTRL			(0x1C)/*SSP Net Work Control Register*/
#define REG_SSP_NET_WORK_STATUS			(0x20)/*SSP Net Work Status Register*/
#define REG_SSP_RWOT_CTRL			(0x24)/*SSP RWOT Control Register*/
#define REG_SSP_RWOT_CCM			(0x28)/*SSP RWOT Counter Cycles Match Register*/
#define REG_SSP_RWOT_CVWRn			(0x2C)/*SSP RWOT Counter Value Write for Read Request Register*/

/* bits definitions for register REG_SSP0_SSP_TOP_CTRL */
#define BIT_SSP_TTELP				( BIT(18) )
#define BIT_SSP_TTE				( BIT(17) )
#define BIT_SSP_SCFR				( BIT(16) )
#define BIT_SSP_IFS				( BIT(15) )
#define BIT_SSP_HOLD_FRAME_LOW			( BIT(14) )
#define BIT_SSP_TRAIL				( BIT(13) )
#define BIT_SSP_LBM				( BIT(12) )
#define BIT_SSP_SPH				( BIT(11) )
#define BIT_SSP_SPO				( BIT(10) )
#define BITS_SSP_DSS(_X_)			( (_X_) << 5 & (BIT(5)|BIT(6)|BIT(7)|BIT(8)|BIT(9)) )
#define BIT_SSP_SFRMDIR				( BIT(4) )
#define BIT_SSP_SCLKDIR				( BIT(3) )
#define BITS_SSP_FRF(_X_)			( (_X_) << 1 & (BIT(1)|BIT(2)) )
#define BIT_SSP_SSE				( BIT(0) )

/* bits definitions for register REG_SSP0_SSP_FIFO_CTRL */
#define BIT_SSP_STRF				( BIT(19) )
#define BIT_SSP_EFWR				( BIT(18) )
#define BIT_SSP_RXFIFO_AUTO_FULL_CTRL		( BIT(17) )
#define BIT_SSP_FPCKE				( BIT(16) )
#define BITS_SSP_TXFIFO_WR_ENDIAN(_X_)		( (_X_) << 14 & (BIT(14)|BIT(15)) )
#define BITS_SSP_RXFIFO_RD_ENDIAN(_X_)		( (_X_) << 12 & (BIT(12)|BIT(13)) )
#define BIT_SSP_RSRE				( BIT(11) )
#define BIT_SSP_TSRE				( BIT(10) )
#define BITS_SSP_RFT(_X_)			( (_X_) << 5 & (BIT(5)|BIT(6)|BIT(7)|BIT(8)|BIT(9)) )
#define BITS_SSP_TFT(_X_)			( (_X_) & (BIT(0)|BIT(1)|BIT(2)|BIT(3)|BIT(4)) )

/* bits definitions for register REG_SSP0_SSP_INT_EN */
#define BIT_SSP_EBCEI				( BIT(6) )
#define BIT_SSP_TIM				( BIT(5) )
#define BIT_SSP_RIM				( BIT(4) )
#define BIT_SSP_TIE				( BIT(3) )
#define BIT_SSP_RIE				( BIT(2) )
#define BIT_SSP_TINTE				( BIT(1) )
#define BIT_SSP_PINTE				( BIT(0) )

/* bits definitions for register REG_SSP0_SSP_TO */
#define BITS_SSP_TIMEOUT(_X_)			( (_X_) & (BIT(0)|BIT(1)|BIT(2)|BIT(3)|BIT(4)|BIT(5)|BIT(6)|BIT(7)|BIT(8)|BIT(9)|BIT(10)|BIT(11)|BIT(12)|BIT(13)|BIT(14)|BIT(15)|BIT(16)|BIT(17)|BIT(18)|BIT(19)|BIT(20)|BIT(21)|BIT(22)|BIT(23)) )

/* bits definitions for register REG_SSP0_SSP_DATAR */
#define BITS_SSP_DATA(_X_)			(_X_)

/* bits definitions for register REG_SSP0_SSP_STATUS */
#define BIT_SSP_OSS				( BIT(23) )
#define BIT_SSP_TX_OSS				( BIT(22) )
#define BIT_SSP_BCE				( BIT(21) )
#define BIT_SSP_ROR				( BIT(20) )
#define BITS_SSP_RFL(_X_)			( (_X_) << 15 & (BIT(15)|BIT(16)|BIT(17)|BIT(18)|BIT(19)) )
#define SSP_SSSR_RFL_MSK			(0x1f << 15)	/* Receive FIFO Level */
#define BIT_SSP_RNE				( BIT(14) )
#define BIT_SSP_RFS				( BIT(13) )
#define BIT_SSP_TUR				( BIT(12) )
#define BITS_SSP_TFL(_X_)			( (_X_) << 7 & (BIT(7)|BIT(8)|BIT(9)|BIT(10)|BIT(11)) )
#define SSP_SSSR_TFL_MSK			(0x1f << 7)	/* Transmit FIFO Level */
#define BIT_SSP_TNF				( BIT(6) )
#define BIT_SSP_TFS				( BIT(5) )
#define BIT_SSP_EOC				( BIT(4) )
#define BIT_SSP_TINT				( BIT(3) )
#define BIT_SSP_PINT				( BIT(2) )
#define BIT_SSP_CSS				( BIT(1) )
#define BIT_SSP_BSY				( BIT(0) )

/* bits definitions for register REG_SSP0_SSP_PSP_CTRL */
#define BITS_SSP_EDMYSTOP(_X_)			( (_X_) << 27 & (BIT(27)|BIT(28)|BIT(29)) )
#define BITS_SSP_DMYSTOP(_X_)			( (_X_) << 25 & (BIT(25)|BIT(26)) )
#define BITS_SSP_EDMYSTRT(_X_)			( (_X_) << 23 & (BIT(23)|BIT(24)) )
#define BITS_SSP_DMYSTRT(_X_)			( (_X_) << 21 & (BIT(21)|BIT(22)) )
#define BITS_SSP_STRTDLY(_X_)			( (_X_) << 18 & (BIT(18)|BIT(19)|BIT(20)) )
#define BITS_SSP_SFRMWDTH(_X_)			( (_X_) << 12 & (BIT(12)|BIT(13)|BIT(14)|BIT(15)|BIT(16)|BIT(17)) )
#define SSP_SSPSP_SFRMWDTH_BASE			12
#define SSP_SSPSP_SFRMWDTH_MSK			(BIT(12)|BIT(13)|BIT(14)|BIT(15)|BIT(16)|BIT(17))

#define BITS_SSP_SFRMDLY(_X_)			( (_X_) << 5 & (BIT(5)|BIT(6)|BIT(7)|BIT(8)|BIT(9)|BIT(10)|BIT(11)) )
#define BIT_SSP_SFRMP				( BIT(4) )
#define BIT_SSP_FSRT				( BIT(3) )
#define BIT_SSP_ETDS				( BIT(2) )
#define BITS_SSP_SCMODE(_X_)			( (_X_) & (BIT(0)|BIT(1)) )

/* bits definitions for register REG_SSP0_SSP_NET_WORK_CTRL */
#define BITS_SSP_RTSA(_X_)			( (_X_) << 12 & (BIT(12)|BIT(13)|BIT(14)|BIT(15)|BIT(16)|BIT(17)|BIT(18)|BIT(19)) )
#define BITS_SSP_TTSA(_X_)			( (_X_) << 4 & (BIT(4)|BIT(5)|BIT(6)|BIT(7)|BIT(8)|BIT(9)|BIT(10)|BIT(11)) )
#define BITS_SSP_FRDC(_X_)			( (_X_) << 1 & (BIT(1)|BIT(2)|BIT(3)) )
#define BIT_SSP_NET_WORK_MOD			( BIT(0) )

/* bits definitions for register REG_SSP0_SSP_NET_WORK_STATUS */
#define BIT_SSP_NMBSY				( BIT(3) )
#define BITS_SSP_TSS(_X_)			( (_X_) & (BIT(0)|BIT(1)|BIT(2)) )

/* bits definitions for register REG_SSP0_SSP_RWOT_CTRL */
#define BIT_SSP_MASK_RWOT_LAST_SAMPLE		( BIT(4) )
#define BIT_SSP_CLR_RWOT_CYCLE			( BIT(3) )
#define BIT_SSP_SET_RWOT_CYCLE			( BIT(2) )
#define BIT_SSP_CYCLE_RWOT_EN			( BIT(1) )
#define BIT_SSP_RWOT				( BIT(0) )

/* bits definitions for register REG_SSP0_SSP_RWOT_CCM */
#define BITS_SSP_SSPRWOTCCM(_X_)		(_X_)

/* bits definitions for register REG_SSP0_SSP_RWOT_CVWRn */
#define BITS_SSP_SSPRWOTCVWR(_X_)		(_X_)

#endif /* _K1X_SPI_H */
