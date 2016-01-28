#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/event.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>
#include <signal.h>

#include <string.h>

#define PORT 5000

#define MAXFD 1000

#define BUFSIZE 10240

#define SENDBYTES 1048576
#define SENDBYTES_S "1048576"

char empty[BUFSIZE];

typedef enum {
	INIT,
	FIRST_R,
	FIRST_N,
	SECOND_R,
	SECOND_N,
	END_READ,
	WRITE_HDR,
	WRITE,
	EMPTY,
} fd_state;

struct fdinfo {
	fd_state state;
	int sent;
	int hdr_sent;
	int reading;
	int writing;
};

void diep(const char *s);
void handle_client(int c, int kq, struct fdinfo *fi, struct kevent *ke);
void close_client(int c, struct fdinfo *fi);
void kev(int s, int kq, short filter, u_short flags);
void dbg(const char *format, ...);
void dbgc(int c, const char *format, ...);
int require_filt(int c, struct kevent *ke, short filter);

int main(void) {
	int l4, l6, i, c;
	socklen_t len;
	struct sockaddr_in sa4;
	struct sockaddr_in6 sa6;
	struct sockaddr sa;
	int yes = 1;

	int kq;
	struct kevent ke;

	struct fdinfo fi[MAXFD+1];

	memset(fi, 0, sizeof(fi));
	memset(empty, 0, sizeof(empty));

	signal(SIGPIPE, SIG_IGN);

	/* listen on ipv4 address */

	if ((l4 = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
		diep("socket");
	}

	if (setsockopt(l4, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
		diep("setsockopt");
	}

	sa4.sin_family = AF_INET;
	sa4.sin_addr.s_addr = htonl(INADDR_ANY);
	sa4.sin_port = htons((unsigned short)PORT);

	if (bind(l4, (struct sockaddr *) &sa4, sizeof(sa4)) < 0) {
		diep("bind");
	}

	if (listen(l4, 5) < 0) {
		diep("listen");
	}

	/* listen on ipv6 address */

	if ((l6 = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP)) < 0) {
		diep("socket");
	}

	if (setsockopt(l6, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
		diep("setsockopt");
	}


	sa6.sin6_family = AF_INET6;
	sa6.sin6_addr = in6addr_any;
	sa6.sin6_port = htons((unsigned short)PORT);

	if (bind(l6, (struct sockaddr *) &sa6, sizeof(sa6)) < 0) {
		diep("bind");
	}

	if (listen(l6, 5) < 0) {
		diep("listen");
	}

	/* setup kqueue */

	if ((kq = kqueue()) < 0) {
		diep("kqueue");
	}

	kev(l4, kq, EVFILT_READ, EV_ADD);

	kev(l6, kq, EVFILT_READ, EV_ADD);

	for (;;) {
		memset(&ke, 0, sizeof(ke));

		i = kevent(kq, NULL, 0, &ke, 1, NULL);
		if (i < 0) {
			diep("kevent");
		}
		if (i == 0) {
			continue;
		}

		if (ke.ident == l4 || ke.ident == l6) {
			len = (socklen_t)sizeof(sa);
			c = accept(ke.ident, &sa, &len);
			if (c > MAXFD) {
				fprintf(stderr, "oops, accept returned fd greater than MAXFD: %d\n", c);
				exit(1);
			}
			handle_client(c, kq, fi, &ke);
			continue;
		}
		handle_client(ke.ident, kq, fi, &ke);
	}
}

void handle_client(int c, int kq, struct fdinfo *fi, struct kevent *ke) {
	char buf[BUFSIZE];
	int n=0;
	int i=0;
	char find = 0;
	int to_send;

	static const char hdr[] = "HTTP/1.1 200 OK\r\nServer: test\r\nContent-Length: " SENDBYTES_S "\r\n\r\n";

	for (;;) {
	start:
		switch (fi->state) {
		case INIT:
			if (fcntl(c, F_SETFL, O_NONBLOCK) < 0) {
				diep("fcntl");
			}
			fi->state = FIRST_R;
			dbgc(c, "init");
			break;
		case FIRST_R:
		case FIRST_N:
		case SECOND_R:
		case SECOND_N:
			/* determine what to look for */
			if (fi->state == FIRST_R || fi->state == SECOND_R) {
				find = '\r';
			} else {
				find = '\n';
			}
			/* if there is no existing data to read, read some */
			if (n == 0 || i >= n) {
				n = read(c, buf, BUFSIZE);
				if (n < 0) {
					if (errno != EAGAIN) {
						perror("read");
						close_client(c, fi);
						return;
					}
					if (!fi->reading) {
						kev(c, kq, EVFILT_READ, EV_ADD);
						fi->reading = 1;
					}
					return;
				}
				if (n == 0) {
					dbgc(c, "client closed connection prior to sending request");
					close_client(c, fi);
					return;
				}
				i=0;
			}
			/* if we are looking for the first \r, scan for it */
			if (fi->state == FIRST_R) {
				for (; i<n; i++) {
					if (buf[i] == find) {
						fi->state++;
						i++;
						goto start;
					}
				}
                        /* for anything else, it has to be the next character */
			} else {
				if (buf[i] == find) {
					fi->state++;
					i++;
					break;
				}
			}
			/* if we haven't matched anything, start over */
			fi->state = FIRST_R;
			break;
		case END_READ:
			dbgc(c, "header read");
			if (fi->reading) {
				kev(c, kq, EVFILT_READ, EV_DELETE);
				fi->reading = 0;
			}
			fi->state = WRITE_HDR;
			break;
		case WRITE_HDR:
			to_send = sizeof(hdr) - 1 - fi->hdr_sent;
			n = write(c, hdr + fi->hdr_sent, to_send);
			if (n < 0) {
				if (errno != EAGAIN) {
					perror("write");
					close_client(c, fi);
					return;
				}
				if (!fi->writing) {
					kev(c, kq, EVFILT_WRITE, EV_ADD);
					fi->writing = 1;
				}
				return;
			}
			fi->hdr_sent += n;
			if (fi->hdr_sent >= sizeof(hdr) - 1) {
				dbgc(c, "response header written");
				fi->state = WRITE;
			}
			break;
		case WRITE:
			to_send = SENDBYTES - fi->sent;
			if (to_send > BUFSIZE) {
				to_send = BUFSIZE;
			}
			n = write(c, empty, to_send);
			if (n < 0) {
				if (errno != EAGAIN) {
					perror("write");
					close_client(c, fi);
					return;
				}
				if (!fi->writing) {
					kev(c, kq, EVFILT_WRITE, EV_ADD);
					fi->writing = 1;
				}
				return;
			}
			fi->sent += n;
			if (fi->sent >= SENDBYTES) {
				dbgc(c, "object written");
				if (fi->writing) {
					kev(c, kq, EVFILT_WRITE, EV_DELETE);
					fi->writing = 0;
				}
				kev(c, kq, EVFILT_EMPTY, EV_ADD);
				fi->state = EMPTY;
			}
			break;
		case EMPTY:
			if (!require_filt(c, ke, EVFILT_EMPTY)) {
				return;
			}
			dbgc(c, "send buffer empty");
			close_client(c, fi);
			return;
			break;
		}
	}
}

int require_filt(int c, struct kevent *ke, short filter) {
	if (ke->filter != filter) {
		dbgc(c, "spurious filter %hd received, expected %hd", ke->filter, filter);
		return 0;
	}
	return 1;
}

void diep(const char *s) {
	perror(s);
	exit(1);
}

void close_client(int c, struct fdinfo *fi) {
	close(c);
	dbgc(c, "closed");
	memset(fi, 0, sizeof(*fi));
}

void kev(int s, int kq, short filter, u_short flags) {
	struct kevent ke;
	memset(&ke, 0, sizeof(ke));
	EV_SET(&ke, s, filter, flags, 0, 0, NULL);
	if (kevent(kq, &ke, 1, NULL, 0, NULL) < 0) {
		diep("kevent");
	}
}

void dbg(const char *format, ...) {
	struct timespec spec;
	va_list args;
	va_start(args, format);

	clock_gettime(CLOCK_REALTIME, &spec);

	printf("%lld.%ld ", (long long)spec.tv_sec, spec.tv_nsec);

	vprintf(format, args);

	printf("\n");

	va_end(args);
}

void dbgc(int c, const char *format, ...) {
	struct timespec spec;
	va_list args;
	va_start(args, format);

	clock_gettime(CLOCK_REALTIME, &spec);

	printf("%lld.%09ld fd %d ", (long long)spec.tv_sec, spec.tv_nsec, c);

	vprintf(format, args);

	printf("\n");

	va_end(args);
}
