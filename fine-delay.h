#ifndef __FINE_DELAY_H__
#define __FINE_DELAY_H__

struct spec_fd {
	struct spec_dev *spec;
	unsigned char __iomem *base; /* regs files are byte-oriented */
	unsigned char __iomem *regs;
};

#define FD_REGS_OFFSET	0x84000

/* SPI Bus chip selects */
#define FD_CS_PLL	1	/* AD9516 PLL */
#define FD_CS_GPIO	2	/* MCP23S17 GPIO */

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

/* Functions exported by fd-zio.c */
extern int fd_zio_register(void);
extern void fd_zio_unregister(void);
extern int fd_zio_init(struct spec_fd *fd);
extern void fd_zio_exit(struct spec_fd *fd);

/* Functions exported by fd-spec.c */
extern int fd_spec_init(void);
extern void fd_spec_exit(void);


#endif /* __FINE_DELAY_H__ */
