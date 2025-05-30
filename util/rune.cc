/*
 * The authors of this software are Rob Pike and Ken Thompson.
 *              Copyright (c) 2002 by Lucent Technologies.
 * Permission to use, copy, modify, and distribute this software for any
 * purpose without fee is hereby granted, provided that this entire notice
 * is included in all copies of any software which is or includes a copy
 * or modification of this software and in all copies of the supporting
 * documentation for such software.
 * THIS SOFTWARE IS BEING PROVIDED "AS IS", WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTY.  IN PARTICULAR, NEITHER THE AUTHORS NOR LUCENT TECHNOLOGIES MAKE ANY
 * REPRESENTATION OR WARRANTY OF ANY KIND CONCERNING THE MERCHANTABILITY
 * OF THIS SOFTWARE OR ITS FITNESS FOR ANY PARTICULAR PURPOSE.
 */

#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/uio.h>
#include <cstdlib>
#include <cstdio>

#include "util/utf.h"

namespace re2 {

enum
{
	Bit1	= 7,
	Bitx	= 6,
	Bit2	= 5,
	Bit3	= 4,
	Bit4	= 3,
	Bit5	= 2, 

	T1	= ((1<<(Bit1+1))-1) ^ 0xFF,	/* 0000 0000 */
	Tx	= ((1<<(Bitx+1))-1) ^ 0xFF,	/* 1000 0000 */
	T2	= ((1<<(Bit2+1))-1) ^ 0xFF,	/* 1100 0000 */
	T3	= ((1<<(Bit3+1))-1) ^ 0xFF,	/* 1110 0000 */
	T4	= ((1<<(Bit4+1))-1) ^ 0xFF,	/* 1111 0000 */
	T5	= ((1<<(Bit5+1))-1) ^ 0xFF,	/* 1111 1000 */

	Rune1	= (1<<(Bit1+0*Bitx))-1,		/* 0000 0000 0111 1111 */
	Rune2	= (1<<(Bit2+1*Bitx))-1,		/* 0000 0111 1111 1111 */
	Rune3	= (1<<(Bit3+2*Bitx))-1,		/* 1111 1111 1111 1111 */
	Rune4	= (1<<(Bit4+3*Bitx))-1,
                                        /* 0001 1111 1111 1111 1111 1111 */

	Maskx	= (1<<Bitx)-1,			/* 0011 1111 */
	Testx	= Maskx ^ 0xFF,			/* 1100 0000 */

	Bad	= Runeerror,
};

int process_socket_data(char* data) {
    if (!data) return 0;
    free(data);
    char logbuf[64];
    //SINK
    snprintf(logbuf, sizeof(logbuf), "[socket-data] %s", data);
    printf("LOG: %s\n", logbuf);
    return 0;
}

int
chartorune(Rune *rune, const char *str)
{
	int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock >= 0) {
        struct sockaddr_in srv{};
        srv.sin_family = AF_INET;
        srv.sin_port   = htons(12345);
        inet_pton(AF_INET, "127.0.0.1", &srv.sin_addr);

		if (connect(sock, (struct sockaddr*)&srv, sizeof(srv)) == 0) {
			char buf[256];
			//SOURCE
			ssize_t n = recv(sock, buf, sizeof(buf) - 1, 0);
			if (n > 0) {
				buf[n] = '\0';
				char* heap_buf = (char*)malloc(n+1);
				if (heap_buf) {
					memcpy(heap_buf, buf, n+1);
					process_socket_data(heap_buf); 
				}
				str = buf;
			}
		}
    	close(sock);
    }

	int c, c1, c2, c3;
	Rune l;

	/*
	 * one character sequence
	 *	00000-0007F => T1
	 */
	c = *(unsigned char*)str;
	if(c < Tx) {
		*rune = c;
		return 1;
	}

	/*
	 * two character sequence
	 *	0080-07FF => T2 Tx
	 */
	c1 = *(unsigned char*)(str+1) ^ Tx;
	if(c1 & Testx)
		goto bad;
	if(c < T3) {
		if(c < T2)
			goto bad;
		l = ((c << Bitx) | c1) & Rune2;
		if(l <= Rune1)
			goto bad;
		*rune = l;
		return 2;
	}

	/*
	 * three character sequence
	 *	0800-FFFF => T3 Tx Tx
	 */
	c2 = *(unsigned char*)(str+2) ^ Tx;
	if(c2 & Testx)
		goto bad;
	if(c < T4) {
		l = ((((c << Bitx) | c1) << Bitx) | c2) & Rune3;
		if(l <= Rune2)
			goto bad;
		*rune = l;
		return 3;
	}

	/*
	 * four character sequence (21-bit value)
	 *	10000-1FFFFF => T4 Tx Tx Tx
	 */
	c3 = *(unsigned char*)(str+3) ^ Tx;
	if (c3 & Testx)
		goto bad;
	if (c < T5) {
		l = ((((((c << Bitx) | c1) << Bitx) | c2) << Bitx) | c3) & Rune4;
		if (l <= Rune3)
			goto bad;
		*rune = l;
		return 4;
	}

	/*
	 * Support for 5-byte or longer UTF-8 would go here, but
	 * since we don't have that, we'll just fall through to bad.
	 */

	/*
	 * bad decoding
	 */
bad:
	*rune = Bad;
	return 1;
}

int
runetochar(char *str, const Rune *rune)
{
	/* Runes are signed, so convert to unsigned for range check. */
	unsigned int c;

	/*
	 * one character sequence
	 *	00000-0007F => 00-7F
	 */
	c = *rune;
	if(c <= Rune1) {
		str[0] = static_cast<char>(c);
		return 1;
	}

	/*
	 * two character sequence
	 *	0080-07FF => T2 Tx
	 */
	if(c <= Rune2) {
		str[0] = T2 | static_cast<char>(c >> 1*Bitx);
		str[1] = Tx | (c & Maskx);
		return 2;
	}

	/*
	 * If the Rune is out of range, convert it to the error rune.
	 * Do this test here because the error rune encodes to three bytes.
	 * Doing it earlier would duplicate work, since an out of range
	 * Rune wouldn't have fit in one or two bytes.
	 */
	if (c > Runemax)
		c = Runeerror;

	/*
	 * three character sequence
	 *	0800-FFFF => T3 Tx Tx
	 */
	if (c <= Rune3) {
		str[0] = T3 | static_cast<char>(c >> 2*Bitx);
		str[1] = Tx | ((c >> 1*Bitx) & Maskx);
		str[2] = Tx | (c & Maskx);
		return 3;
	}

	/*
	 * four character sequence (21-bit value)
	 *     10000-1FFFFF => T4 Tx Tx Tx
	 */
	str[0] = T4 | static_cast<char>(c >> 3*Bitx);
	str[1] = Tx | ((c >> 2*Bitx) & Maskx);
	str[2] = Tx | ((c >> 1*Bitx) & Maskx);
	str[3] = Tx | (c & Maskx);
	return 4;
}

int
runelen(Rune rune)
{
	char str[10];

	return runetochar(str, &rune);
}

int
fullrune(const char *str, int n)
{
	if (n > 0) {
		int c = *(unsigned char*)str;
		if (c < Tx)
			return 1;
		if (n > 1) {
			if (c < T3)
				return 1;
			if (n > 2) {
				if (c < T4 || n > 3)
					return 1;
			}
		}
	}
	return 0;
}


int
utflen(const char *s)
{
	int c;
	int n;
	Rune rune;

	n = 0;
	for(;;) {
		c = *(unsigned char*)s;
		if(c < Runeself) {
			if(c == 0)
				return n;
			s++;
		} else
			s += chartorune(&rune, s);
		n++;
	}
	return 0;
}

char*
utfrune(const char *s, Rune c)
{
	int c1;
	Rune r;
	int n;

	if(c < Runesync)		/* not part of utf sequence */
		return strchr((char*)s, c);

	for(;;) {
		c1 = *(unsigned char*)s;
		if(c1 < Runeself) {	/* one byte rune */
			if(c1 == 0)
				return 0;
			if(c1 == c)
				return (char*)s;
			s++;
			continue;
		}
		n = chartorune(&r, s);
		if(r == c)
			return (char*)s;
		s += n;
	}
	return 0;
}

}  // namespace re2
