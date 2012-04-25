/*
 * Copyright (c) 1992, 1993, 1994
 *      The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2002 Scott Long
 *
 * This code is derived from software contributed to Berkeley
 * by Pace Willisson (pace@blitz.com).  The Rock Ridge Extension
 * Support code is derived from software contributed to Berkeley
 * by Atsushi Murai (amurai@spec.co.jp).
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sbin/mount_udf/mount_udf.c,v 1.13 2005/06/10 09:51:43 delphij Exp $
 */

/*
 * This is just a rip-off of mount_iso9660.c.  It's been vastly simplified
 * because UDF doesn't take any options at this time.
 */

#include <sys/cdio.h>
#include <sys/file.h>
#include <sys/iconv.h>
#include <sys/param.h>
#include <sys/linker.h>
#include <sys/module.h>
#include <sys/mount.h>
#include <sys/uio.h>
#include <sys/endian.h>
#include <sys/ioctl.h>
#include <sys/udfio.h>


#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>
#include <fcntl.h>

#include "mntopts.h"
#include "../udf2/udf_mount.h"


struct mntopt mopts[] = {
	MOPT_STDOPTS,
	MOPT_UPDATE,
	MOPT_END
};

static int set_charset(char **, char **, const char *);
static void getlastblock(char *dev, struct udf_session_info *usi, int session_num);
static void usage(void);

int
main(int argc, char **argv)
{
	struct iovec iov[18];
	int ch, i, mntflags, opts, udf_flags;
	char *dev, *dir, mntpath[MAXPATHLEN];
	char *cs_disk, *cs_local;
	int verbose;
	long session_num;
	char *endp;
	int32_t first_trackblank;
	struct udf_session_info usi;

	session_num = 0;

	i = mntflags = opts = udf_flags = verbose = 0;
	cs_disk = cs_local = NULL;
	while ((ch = getopt(argc, argv, "o:vC:s:")) != -1)
		switch (ch) {
		case 'o':
			getmntopts(optarg, mopts, &mntflags, &opts);
			break;
		case 'v':
			verbose++;
			break;
		case 'C':
			if (set_charset(&cs_disk, &cs_local, optarg) == -1)
				err(EX_OSERR, "udf2_iconv");
			udf_flags |= UDFMNT_KICONV;
			break;
		case 's':
			session_num = strtol(optarg, &endp, 10);
			if (optarg == endp || *endp != '\0')
				usage();	
			printf("%s : %ld", optarg, session_num);
			break;
		case '?':
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (argc != 2)
		usage();

	dev = argv[0];
	dir = argv[1];

	/*
	 * Resolve the mountpoint with realpath(3) and remove unnecessary
	 * slashes from the devicename if there are any.
	 */
	(void)checkpath(dir, mntpath);
	(void)rmslashes(dev, dev);

	/*
	 * UDF file systems are not writeable.
	 */
	mntflags |= MNT_RDONLY;

	iov[i].iov_base = "fstype";
	iov[i++].iov_len = sizeof("fstype");
	iov[i].iov_base = "udf2";
	iov[i].iov_len = strlen(iov[i].iov_base) + 1;
	i++;
	
	iov[i].iov_base = "fspath";
	iov[i++].iov_len = sizeof("fspath");
	iov[i].iov_base = mntpath;
	iov[i++].iov_len = strlen(mntpath) + 1;

	iov[i].iov_base = "from";
	iov[i++].iov_len = sizeof("from");
	iov[i].iov_base = dev;
	iov[i++].iov_len = strlen(dev) + 1;

	iov[i].iov_base = "flags";
	iov[i++].iov_len = sizeof("flags");
	iov[i].iov_base = &udf_flags;
	iov[i++].iov_len = sizeof(udf_flags);

	getlastblock(dev, &usi, session_num);

	iov[i].iov_base = "first_trackblank";
	iov[i++].iov_len = sizeof("first_trackblank");
	first_trackblank = 0;
	iov[i].iov_base = &first_trackblank;
	iov[i++].iov_len = sizeof(uint32_t);

uint32_t sessstart = usi.session_start_addr;
	iov[i].iov_base = "session_start_addr";
	iov[i++].iov_len = sizeof("session_start_addr");
	iov[i].iov_base = &sessstart;
	iov[i++].iov_len = sizeof(uint32_t);

uint32_t sessend = usi.session_end_addr;
	iov[i].iov_base = "session_end_addr";
	iov[i++].iov_len = sizeof("session_end_addr");
	iov[i].iov_base = &sessend;
	iov[i++].iov_len = sizeof(uint32_t);

	if (udf_flags & UDFMNT_KICONV) {
		iov[i].iov_base = "cs_disk";
		iov[i++].iov_len = sizeof("cs_disk");
		iov[i].iov_base = cs_disk;
		iov[i++].iov_len = strlen(cs_disk) + 1;
		iov[i].iov_base = "cs_local";
		iov[i++].iov_len = sizeof("cs_local");
		iov[i].iov_base = cs_local;
		iov[i++].iov_len = strlen(cs_local) + 1;
	}
	if (nmount(iov, i, mntflags) < 0)
		err(1, "%s", dev);
	exit(0);
}

static int
set_charset(char **cs_disk, char **cs_local, const char *localcs)
{
	int error;

	if (modfind("udf2_iconv") < 0)
		if (kldload("udf2_iconv") < 0 || modfind("udf2_iconv") < 0) {
			warnx( "cannot find or load \"udf2_iconv\" kernel module");
			return (-1);
		}

	if ((*cs_disk = malloc(ICONV_CSNMAXLEN)) == NULL)
		return (-1);
	if ((*cs_local = malloc(ICONV_CSNMAXLEN)) == NULL)
		return (-1);
	strncpy(*cs_disk, ENCODING_UNICODE, ICONV_CSNMAXLEN);
	strncpy(*cs_local, localcs, ICONV_CSNMAXLEN);
	error = kiconv_add_xlat16_cspairs(*cs_disk, *cs_local);
	if (error)
		return (-1);

	return (0);
}

/*uint32_t
getlastblock(char *dev)
{
	int fd, error;
	unsigned int out;
	struct ioc_read_toc_entry t;
	struct cd_toc_entry te;

	fd = open(dev, O_RDONLY, 0);
	if (fd < 0) 
		err(1, "open");

	t.address_format = CD_LBA_FORMAT;
	t.starting_track = 0xaa; //CD_TRACK_LEADOUT;
	t.data_len = sizeof(struct cd_toc_entry);
	t.data = &te;

	error = ioctl(fd, CDIOREADTOCENTRYS, &t);
	if (error)
		err(2, "ioctl");

	close(fd);
	out = (unsigned int)te.addr.lba;
	out = be32toh(out);
	printf("outout of ioctl:  %d\n", (int)out);

	return out;
}*/

static void
getlastblock(char *dev, struct udf_session_info *usi, int session_num)
{
	int fd, error;
	unsigned int out;
	fd = open(dev, O_RDONLY, 0);
	
	if (fd < 0)
		err(1, "open");
	
	bzero(usi, sizeof(struct udf_session_info));
	usi->session_num = session_num;
	error = ioctl(fd, UDFIOTEST, usi);
	if (error)
		err(2, "ioctl");
	
	close(fd);

	printf("session_num: %u\n", usi->session_num);
	
	printf("sector_size: %u\n", usi->sector_size);
	printf("num_sessions: %u\n", usi->num_sessions);
	printf("session_start_addr: %u\n", usi->session_start_addr);
	printf("session_end_addr: %u\n", usi->session_end_addr);
	
	printf("num_tracks: %u\n", usi->num_tracks);
	printf("first_track: %u\n", usi->first_track);
	printf("session_first_track: %u\n", usi->session_first_track);
	printf("session_last_track: %u\n", usi->session_last_track);
}



static void
usage(void)
{
	(void)fprintf(stderr,
		"usage: mount_udf [-v] [-o options] [-C charset] special node\n");
	exit(EX_USAGE);
}
