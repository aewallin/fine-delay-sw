#ifndef __FINE_DELAY_H__
#define __FINE_DELAY_H__

struct spec_fd {
	struct spec_dev *spec;
	unsigned char __iomem *base;	/* regs files are byte-oriented */
	unsigned char __iomem *regs;
	unsigned char __iomem *ow_regs;
	int acam_addr;			/* cache of currently active addr */
	uint8_t ds18_id[8];
	unsigned long next_t;
	int temp;			/* scaled by 4 bits */
};

#define FD_REGS_OFFSET	0x84000

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
#define FD_CS_PLL	1	/* AD9516 PLL */
#define FD_CS_GPIO	2	/* MCP23S17 GPIO */

/* MCP23S17 register addresses (only ones which are used by the lib) */
#define FD_MCP_IODIR	0x00
#define FD_MCP_IPOL	0x01
#define FD_MCP_IOCON	0x0a
#define FD_MCP_GPIO	0x12
#define FD_MCP_OLAT	0x14

#define FD_GPIO_IN	0
#define FD_GPIO_OUT	1

/* Functions exported by fd-core.c */
extern int fd_probe(struct spec_dev *dev);
extern void fd_remove(struct spec_dev *dev);

/* Functions exported by spi.c */
extern int fd_spi_xfer(struct spec_fd *fd, int ss, int num_bits,
		       uint32_t in, uint32_t *out);
extern int fd_spi_init(struct spec_fd *fd);
extern void fd_spi_exit(struct spec_fd *fd);

/* Functions exported by pll.c */
extern int fd_pll_init(struct spec_fd *fd);
extern void fd_pll_exit(struct spec_fd *fd);

/* Functions exported by onewire.c */
extern int fd_onewire_init(struct spec_fd *fd);
extern void fd_onewire_exit(struct spec_fd *fd);
extern int fd_read_temp(struct spec_fd *fd, int verbose);

/* Functions exported by gpio.c */
extern int fd_gpio_init(struct spec_fd *fd);
extern void fd_gpio_exit(struct spec_fd *fd);
extern void fd_gpio_dir(struct spec_fd *fd, int pin, int dir);
extern void fd_gpio_val(struct spec_fd *fd, int pin, int val);

/* Functions exported by fd-zio.c */
extern int fd_zio_register(void);
extern void fd_zio_unregister(void);
extern int fd_zio_init(struct spec_fd *fd);
extern void fd_zio_exit(struct spec_fd *fd);

/* Functions exported by fd-spec.c */
extern int fd_spec_init(void);
extern void fd_spec_exit(void);


#endif /* __FINE_DELAY_H__ */
