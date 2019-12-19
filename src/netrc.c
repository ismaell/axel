/*
  Axel -- A lighter download accelerator for Linux and other Unices

  Copyright 2019      David da Silva Polverari

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  In addition, as a special exception, the copyright holders give
  permission to link the code of portions of this program with the
  OpenSSL library under certain conditions as described in each
  individual source file, and distribute linked combinations including
  the two.

  You must obey the GNU General Public License in all respects for all
  of the code used other than OpenSSL. If you modify file(s) with this
  exception, you may extend this exception to your version of the
  file(s), but you are not obligated to do so. If you do not wish to do
  so, delete this exception statement from your version. If you delete
  this exception statement from all source files in the program, then
  also delete it here.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

/* .netrc parsing implementation */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include "axel.h"
#include "netrc.h"

typedef struct {
	char *data;
	size_t len;
} buffer_t;

static buffer_t tok, save_buf;
static const char *tok_delim = " \t\n";

static size_t
memspn(const char *s, const char *accept, size_t len)
{
	size_t sz = 0;

	while (*s && len-- && strchr(accept, *s++))
		sz++;
	return sz;
}

static size_t
memcspn(const char *s, const char *reject, size_t len)
{
	size_t sz = 0;

	while (*s && len--)
		if (strchr(reject, *s))
			return sz;
		else
			s++, sz++;
	return sz;
}

static buffer_t
memtok(const char *addr, size_t len, const char *delim, buffer_t *save_ptr)
{
	size_t sz;
	char *p, *q;
	buffer_t ret;

	if (!addr) {
		p = save_ptr->data;
	} else {
		p = (char *) addr;
		save_ptr->len = len;
	}
	sz = memspn(p, delim, save_ptr->len);
	p += sz;
	save_ptr->len -= sz;
	q = p;
	sz = memcspn(q, delim, save_ptr->len);
	q += sz;
	save_ptr->data = q;
	ret.len = q - p;
	ret.data = p;
	return ret;
}

static size_t
file_size(int fd)
{
	struct stat st;
	fstat(fd, &st);
	return st.st_size;
}

static size_t
netrc_mmap(const char *filename, char **addr)
{
	int fd;
	size_t sz;
	char *aux = NULL;
	char *home = NULL;
	char *path = NULL;
	const char suffix[] = "/.netrc";

	if (filename && *filename) {
		aux = path = (char *)filename;
	} else if ((path = getenv("NETRC"))) {
		aux = path;
	} else if ((home = getenv("HOME"))) {
		size_t i = strlen(home);
		if ((path = malloc(i + sizeof(suffix)))) {
			memcpy(path, home, i);
			memcpy(path+i, suffix, sizeof(suffix));
		}
	}
	if (!path)
		return 0;
	fd = open(path, O_RDONLY,0);
	if (!aux)
		free(path);
	if (fd == -1) {
		return 0;
	}
	sz = file_size(fd);
	*addr = mmap(NULL, sz, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0);
	close(fd);
	return sz;
}

static void
get_creds(netrc_t *netrc, char *user, size_t user_len, char *pass, size_t pass_len)
{
	while (tok.len) {
		if (!strncmp("login", tok.data, tok.len)) {
			tok = memtok(NULL, 0, tok_delim, &save_buf);
			if (tok.len <= user_len)
				strlcpy(user, tok.data, tok.len+1);
		} else if (!strncmp("password", tok.data, tok.len)) {
			tok = memtok(NULL, 0, tok_delim, &save_buf);
			if (tok.len <= pass_len)
				strlcpy(pass, tok.data, tok.len+1);
		} else if (!strncmp("machine", tok.data, tok.len) ||
			   !strncmp("default", tok.data, tok.len)) {
			save_buf.data -= tok.len;
			save_buf.len += tok.len;
			break;
		}
		tok = memtok(NULL, 0, tok_delim, &save_buf);
	}
}

netrc_t *
netrc_init(const char *file)
{
	netrc_t *netrc;

	netrc = calloc(1, sizeof(netrc_t));
	if (!netrc)
		return NULL;
	netrc->file = file;
	return netrc;
}

int
netrc_parse(netrc_t *netrc, const char *host, char *user, size_t user_len, char *pass, size_t pass_len)
{
	size_t sz;
	char *s_addr = NULL;

	if (!(sz = netrc_mmap(netrc->file, &s_addr)))
		return 0;
	tok = memtok(s_addr, sz, tok_delim, &save_buf);
	while (tok.len) {
		if (!strncmp("default", tok.data, tok.len)) {
			get_creds(netrc, user, user_len, pass, pass_len);
			break;
		} else if (!strncmp("machine", tok.data, tok.len)) {
			tok = memtok(NULL, 0, tok_delim, &save_buf);
			if ((tok.len && !strncmp(host, tok.data, tok.len))) {
				get_creds(netrc, user, user_len, pass, pass_len);
				break;
			}
		}
		tok = memtok(NULL, 0, tok_delim, &save_buf);
	}
	munmap(s_addr, sz);
	return 1;
}

void netrc_close(netrc_t *netrc)
{
	if (netrc)
		free(netrc);
}
