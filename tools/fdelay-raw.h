#include <stdio.h>
#include <glob.h>
#include <errno.h>

/* return an array of sysfs pathnames, array is preallocated. Returns count */
static inline int fdelay_get_sysnames(char *result[])
{
	glob_t glob_buf = {0,};
	int i;

	glob("/sys/bus/zio/devices/zio-fd-*",0 , NULL, &glob_buf);
	for (i = 0; i < glob_buf.gl_pathc; i++)
		result[i] = strdup(glob_buf.gl_pathv[i]);
	globfree(&glob_buf);
	return i;
}

static inline int fdelay_sysfs_get(char *path, uint32_t *resp)
{
	FILE *f = fopen(path, "r");

	if (!f)
		return -1;
	if (fscanf(f, "%i", resp) != 1) {
		fclose(f);
		errno = EINVAL;
		return -1;
	}
	fclose(f);
	return 0;
}

static inline int fdelay_sysfs_set(char *path, uint32_t *value)
{
	char s[16];
	int fd, ret, len;

	len = sprintf(s, "%i\n", *value);
	fd = open(path, O_WRONLY);
	if (fd < 0)
		return -1;
	ret = write(fd, s, len);
	close(fd);
	if (ret < 0)
		return -1;
	if (ret == len)
		return 0;
	errno = EINVAL;
	return -1;
}
