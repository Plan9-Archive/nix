#include <u.h>
#include <libc.h>


uvlong
rdpmc(int index)
{
	int fd, n;
	char name[16+2+1];	/* 0x0000000000000000\0 */
	
	snprint(name, sizeof(name), "/dev/ctr%2.2ud", index);

	fd = open(name, OREAD);
	if (fd < 0)
		return 0xcafebabe;
	n = read(fd, name, sizeof(name) - 1);
	if (n < 0)
		return 0xcafebabe;
	close(fd);
	name[n] = '\0';
	return atoi(name);
}
