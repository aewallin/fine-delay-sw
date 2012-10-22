#ifndef __FINE_DELAY_H__
#define __FINE_DELAY_H__

#define FDELAY_GATEWARE_NAME "fmc/fine-delay.bin"

#define FDELAY_VERSION		2 /* version of the layout of registers */
/*
 * ZIO concatenates device, cset and channel extended attributes in the 32
 * values that are reported in the control block. So we are limited to
 * 32 values at most, and the offset of cset attributes depends on the
 * number of device attributes. For this reason, we reserve a few, in
 * order not to increase the version number too often (we need to increase
 * it when the layout of attributes changes in incompatible ways)
 */

/*
 * NOTE: all tuples of 4 register must be enumerated in the proper order:
 * utc-h, utc-l, coarse, frac  __IN_THIS_ORDER__ because I make arith on them
 */

/* Device-wide ZIO attributes */
enum fd_zattr_dev_idx {
	FD_ATTR_DEV_VERSION = 0,
	FD_ATTR_DEV_UTC_H,
	FD_ATTR_DEV_UTC_L,
	FD_ATTR_DEV_COARSE,
	FD_ATTR_DEV_COMMAND, /* see below for commands */
	FD_ATTR_DEV_TEMP,
	FD_ATTR_DEV_RESERVE_6,
	FD_ATTR_DEV_RESERVE_7,
	FD_ATTR_DEV__LAST,
};

enum fd_command {
	FD_CMD_HOST_TIME = 0,
	FD_CMD_WR_ENABLE,
	FD_CMD_WR_DISABLE,
	FD_CMD_WR_QUERY,
	FD_CMD_DUMP_MCP,
	FD_CMD_PURGE_FIFO = 5,
};


/* Input ZIO attributes (i.e. TDC attributes) */
enum fd_zattr_in_idx {
	/* PLEASE check "NOTE:" above if you edit this*/
	FD_ATTR_TDC_UTC_H = FD_ATTR_DEV__LAST,
	FD_ATTR_TDC_UTC_L,
	FD_ATTR_TDC_COARSE,
	FD_ATTR_TDC_FRAC,
	FD_ATTR_TDC_SEQ,
	FD_ATTR_TDC_CHAN,
	FD_ATTR_TDC_FLAGS, /* enable, termination, see below */
	FD_ATTR_TDC_OFFSET,
	FD_ATTR_TDC_USER_OFF,
	FD_ATTR_TDC__LAST,
};
/* Names have been chosen so that 0 is the default at load time */
#define FD_TDCF_DISABLE_INPUT	1
#define FD_TDCF_DISABLE_TSTAMP	2
#define FD_TDCF_TERM_50		4

/* Output ZIO attributes */
enum fd_zattr_out_idx {
	FD_ATTR_OUT_MODE = FD_ATTR_DEV__LAST,
	FD_ATTR_OUT_REP,
	/* PLEASE check "NOTE:" above if you edit this*/
	/* Start (or delay) is 4 registers */
	FD_ATTR_OUT_START_H,
	FD_ATTR_OUT_START_L,
	FD_ATTR_OUT_START_COARSE,
	FD_ATTR_OUT_START_FINE,
	/* End (start + width) is 4 registers */
	FD_ATTR_OUT_END_H,
	FD_ATTR_OUT_END_L,
	FD_ATTR_OUT_END_COARSE,
	FD_ATTR_OUT_END_FINE,
	/* Delta is 3 registers */
	FD_ATTR_OUT_DELTA_L,
	FD_ATTR_OUT_DELTA_COARSE,
	FD_ATTR_OUT_DELTA_FINE,
	/* The two offsets */
	FD_ATTR_OUT_DELAY_OFF,
	FD_ATTR_OUT_USER_OFF,
	FD_ATTR_OUT__LAST,
};
enum fd_output_mode {
	FD_OUT_MODE_DISABLED = 0,
	FD_OUT_MODE_DELAY,
	FD_OUT_MODE_PULSE,
};

/*
 * Cset attributes are concatenated to device attributes in the control
 * structure, but they start from 0 when allocate for the individual cset
 */
#define FD_CSET_INDEX(i) ((i) - FD_ATTR_DEV__LAST)



#ifdef __KERNEL__ /* All the rest is only of kernel users */
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,25)
#include <linux/math64.h>
#else
/* Hack to compile under 2.6.24: this comes from 2418f4f2 (Roman Zippel) */
static inline u64 div_u64_rem(u64 dividend, u32 divisor, u32 *remainder)
{
	*remainder = do_div(dividend, divisor);
	return dividend;
}
#endif

/* This is somehow generic, but I find no better place at this time */
#ifndef SET_HI32
#  if BITS_PER_LONG > 32
#    define SET_HI32(var, value)        ((var) |= (value) << 32)
#    define GET_HI32(var)               ((var) >> 32)
#  else
#    define SET_HI32(var, value)        ((var) |= 0)
#    define GET_HI32(var)               0
#  endif
#endif

#define FD_REGS_BASE	0x80000 /* sdb_find_device(cern, f19ede1a) */

struct fd_calib {
	int64_t frr_poly[3];		/* SY89295 delay/temp poly coeffs */
	uint32_t magic;			/* magic ID: 0xf19ede1a */
	uint32_t zero_offset[4];	/* Output zero offset, fixed point */
	uint32_t adsfr_val;		/* ADSFR register value */
	uint32_t acam_start_offset;	/* ACAM Start offset value */
	uint32_t atmcr_val;		/* ATMCR register value */
	uint32_t tdc_zero_offset;	/* Zero offset of the TDC, in ps */
	/* The user can add a signed offset, in picoseconds */
	int32_t tdc_user_offset;
	int32_t ch_user_offset[4];
	int32_t tdc_flags;
};

struct fd_calib_on_eeprom {
	u32 hash;
	u16 size;
	u16 version;
	struct fd_calib calib;
};

/* Channels are called 1..4 in all docs. Internally it's 0..3 */
#define FD_CH_1		0
#define FD_CH_LAST	3
#define FD_CH_NUMBER	4
#define FD_CH_INT(i)	((i) - 1)
#define FD_CH_EXT(i)	((i) + 1)

#define FD_NUM_TAPS	1024	/* This is an hardware feature of SY89295U */
#define FD_CAL_STEPS	1024	/* This is a parameter: must be power of 2 */

struct fd_ch {
	/* Offset between FRR measured at known T at startup and poly-fitted */
	uint32_t frr_offset;
	/* Fine range register for each ch, current value (after T comp.) */
	uint32_t frr_cur;
};

/* This is the device we use all around */
struct fd_dev {
	spinlock_t lock;
	unsigned long flags;
	struct fmc_device *fmc;
	struct zio_device *zdev, *hwzdev;
	struct timer_list fifo_timer;
	struct timer_list temp_timer;
	struct fd_calib calib;
	struct fd_ch ch[FD_CH_NUMBER];
	unsigned char __iomem *base;	/* regs files are byte-oriented */
	unsigned char __iomem *regs;
	unsigned char __iomem *ow_regs;
	uint32_t bin;
	int acam_addr;			/* cache of currently active addr */
	uint8_t ds18_id[8];
	unsigned long next_t;
	int temp;			/* temperature: scaled by 4 bits */
	int verbose;
	uint32_t tdc_attrs[FD_ATTR_TDC__LAST - FD_ATTR_DEV__LAST];
	uint16_t mcp_iodir, mcp_olat;
};

/* We act on flags using atomic ops, so flag is the number, not the mask */
enum fd_flags {
	FD_FLAG_INITED = 0,
	FD_FLAG_DO_INPUT,
	FD_FLAG_INPUT_READY,
	FD_FLAG_DO_OUTPUT,
	_FD_FLAG_DO_OUTPUT1, /* use: FD_FLAG_DO_OUTPUT + cset->index */
	_FD_FLAG_DO_OUTPUT2,
	_FD_FLAG_DO_OUTPUT3,
	_FD_FLAG_DO_OUTPUT4,
	FD_FLAG_WR_MODE,
};

/* Internal time: the first three fields should be converted to zio time */
struct fd_time {
	uint64_t utc;
	uint32_t coarse;
	uint32_t frac;
	uint32_t channel;
	uint32_t seq_id;
};

/* Split a pico value into coarse and frac */
static inline void fd_split_pico(uint64_t pico,
				 uint32_t *coarse, uint32_t *frac)
{
	/* This works for less than 1s delays */
	BUG_ON(pico > 1000ULL * NSEC_PER_SEC);
	*coarse = div_u64_rem(pico, 8000, frac);
	*frac = (*frac << 12) / 8000;
}

static inline uint32_t fd_readl(struct fd_dev *fd, unsigned long reg)
{
	return readl(fd->regs + reg);
}
static inline void fd_writel(struct fd_dev *fd, uint32_t v, unsigned long reg)
{
	writel(v, fd->regs + reg);
}

static inline void __check_chan(int x)
{
	BUG_ON(x < 0 || x > 3);
}


static inline uint32_t fd_ch_readl(struct fd_dev *fd, int ch,
				   unsigned long reg)
{
	__check_chan(ch);
	return fd_readl(fd, 0x100 + ch * 0x100 + reg);
}

static inline void fd_ch_writel(struct fd_dev *fd, int ch,
				uint32_t v, unsigned long reg)
{
	__check_chan(ch);
	fd_writel(fd, v, 0x100 + ch * 0x100 + reg);
}

#define FD_REGS_OFFSET	0x80000		/* can be changed by "regs=" */
#define FD_MAGIC_FPGA	0xf19ede1a	/* FD_REG_IDR content */

/* Values for the configuration of the acam PLL. Can be changed */
#define ACAM_DESIRED_BIN	80.9553
#define ACAM_CLOCK_FREQ_KHZ	31250

/* ACAM TDC operation modes */
enum fd_acam_modes {
	ACAM_RMODE,
	ACAM_IMODE
};

/*
 * You can change the following value to have a pll with smaller divisor,
 * at the cost of potentially less precision in the desired bin value.
 */
#define ACAM_MAX_REFDIV		7

#define ACAM_MASK		((1<<29) - 1) /* 28 bits */

/* SPI Bus chip selects */
#define FD_CS_NONE	0	/* Nothing */
#define FD_CS_PLL	1	/* AD9516 PLL */
#define FD_CS_GPIO	2	/* MCP23S17 GPIO */

/* MCP23S17 register addresses (only ones which are used by the lib) */
#define FD_MCP_IODIR	0x00
#define FD_MCP_IPOL	0x01
#define FD_MCP_IOCON	0x0a
#define FD_MCP_GPIO	0x12
#define FD_MCP_OLAT	0x14

/*
 * MCP23S17 GPIO direction and meaning
 * NOTE: outputs are called 1..4 to match hw schematics
 */
#define FD_GPIO_IN	0
#define FD_GPIO_OUT	1

static inline void __check_output(int x)
{
	BUG_ON(x < 1 || x > 4);
}

#define FD_GPIO_TERM_EN		0x0001		/* Input terminator enable */
#define FD_GPIO_OUTPUT_EN(x)	\
	({__check_output(x); 1 << (6-(x));})	/* Output driver enable */
#define FD_GPIO_OUTPUT_MASK	0x003c		/* Output driver enable */
#define FD_GPIO_TRIG_INTERNAL	0x0040		/* TDC trig (1=in, 1=fpga) */
#define FD_GPIO_CAL_DISABLE	0x0080		/* 0 enables calibration */

/* Functions exported by spi.c */
extern int fd_spi_xfer(struct fd_dev *fd, int ss, int num_bits,
		       uint32_t in, uint32_t *out);
extern int fd_spi_init(struct fd_dev *fd);
extern void fd_spi_exit(struct fd_dev *fd);

/* Functions exported by pll.c */
extern int fd_pll_init(struct fd_dev *fd);
extern void fd_pll_exit(struct fd_dev *fd);

/* Functions exported by onewire.c */
extern int fd_onewire_init(struct fd_dev *fd);
extern void fd_onewire_exit(struct fd_dev *fd);
extern int fd_read_temp(struct fd_dev *fd, int verbose);

/* Functions exported by acam.c */
extern int fd_acam_init(struct fd_dev *fd);
extern void fd_acam_exit(struct fd_dev *fd);
extern uint32_t acam_readl(struct fd_dev *fd, int reg);
extern void acam_writel(struct fd_dev *fd, int val, int reg);

/* Functions exported by calibrate.c, called within acam.c */
extern int fd_calibrate_outputs(struct fd_dev *fd);
extern void fd_update_calibration(unsigned long arg);
extern int fd_calib_period_s;


/* Functions exported by gpio.c */
extern int fd_gpio_init(struct fd_dev *fd);
extern void fd_gpio_exit(struct fd_dev *fd);
extern void fd_gpio_dir(struct fd_dev *fd, int pin, int dir);
extern void fd_gpio_val(struct fd_dev *fd, int pin, int val);
extern void fd_gpio_set_clr(struct fd_dev *fd, int pin, int set);
extern int fd_dump_mcp(struct fd_dev *fd);
#define fd_gpio_set(fd, pin) fd_gpio_set_clr((fd), (pin), 1)
#define fd_gpio_clr(fd, pin) fd_gpio_set_clr((fd), (pin), 0)

/* Functions exported by time.c */
extern int fd_time_init(struct fd_dev *fd);
extern void fd_time_exit(struct fd_dev *fd);
extern int fd_time_set(struct fd_dev *fd, struct fd_time *t,
		       struct timespec *ts);
extern int fd_time_get(struct fd_dev *fd, struct fd_time *t,
		       struct timespec *ts);

/* Functions exported by fd-zio.c */
extern int fd_zio_register(void);
extern void fd_zio_unregister(void);
extern int fd_zio_init(struct fd_dev *fd);
extern void fd_zio_exit(struct fd_dev *fd);

/* Functions exported by fd-spec.c */
extern int fd_spec_init(void);
extern void fd_spec_exit(void);

/* Functions exported by i2c.c */
extern int fd_i2c_init(struct fd_dev *fd);
extern void fd_i2c_exit(struct fd_dev *fd);
extern int fd_eerom_read(struct fd_dev *fd, int i2c_addr, uint32_t offset,
			 void *buf, size_t size);
extern int fd_eeprom_write(struct fd_dev *fd, int i2c_addr, uint32_t offset,
			void *buf, size_t size);



#endif /* __KERNEL__ */
#endif /* __FINE_DELAY_H__ */
