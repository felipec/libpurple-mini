/*
 *  (C) Copyright 2001-2009 Wojtek Kaniewski <wojtekka@irc.pl>
 *                          Robert J. Woźny <speedy@ziew.org>
 *                          Arkadiusz Miśkiewicz <arekm@pld-linux.org>
 *                          Tomasz Chiliński <chilek@chilan.com>
 *                          Adam Wysocki <gophi@ekg.chmurka.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License Version
 *  2.1 as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307,
 *  USA.
 */

/**
 * \file resolver.c
 *
 * \brief Funkcje rozwiązywania nazw
 */

#ifndef _WIN32
#  include <sys/wait.h>
#  include <netdb.h>
#endif
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifndef _WIN32
#  include <signal.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#endif

#include "libgadu.h"
#include "libgadu-config.h"
#include "resolver.h"
#include "compat.h"
#include "session.h"

/** Sposób rozwiązywania nazw serwerów */
static gg_resolver_t gg_global_resolver_type = GG_RESOLVER_DEFAULT;

/** Funkcja rozpoczynająca rozwiązywanie nazwy */
static int (*gg_global_resolver_start)(int *fd, void **private_data, const char *hostname);

/** Funkcja zwalniająca zasoby po rozwiązaniu nazwy */
static void (*gg_global_resolver_cleanup)(void **private_data, int force);

#ifdef GG_CONFIG_HAVE_PTHREAD

#include <pthread.h>

/**
 * \internal Funkcja pomocnicza zwalniająca zasoby po rozwiązywaniu nazwy
 * w wątku.
 *
 * \param data Wskaźnik na wskaźnik bufora zaalokowanego w wątku
 */
static void gg_gethostbyname_cleaner(void *data)
{
	char **buf_ptr = (char**) data;

	if (buf_ptr != NULL) {
		free(*buf_ptr);
		*buf_ptr = NULL;
	}
}

#endif /* GG_CONFIG_HAVE_PTHREAD */

/**
 * \internal Odpowiednik \c gethostbyname zapewniający współbieżność.
 *
 * Jeśli dany system dostarcza \c gethostbyname_r, używa się tej wersji, jeśli
 * nie, to zwykłej \c gethostbyname. Wynikiem jest tablica adresów zakończona
 * wartością INADDR_NONE, którą należy zwolnić po użyciu.
 *
 * \param hostname Nazwa serwera
 * \param result Wskaźnik na wskaźnik z tablicą adresów zakończoną INADDR_NONE
 * \param count Wskaźnik na zmienną, do ktorej zapisze się liczbę wyników
 * \param pthread Flaga blokowania unicestwiania wątku podczas alokacji pamięci
 *
 * \return 0 jeśli się powiodło, -1 w przypadku błędu
 */
int gg_gethostbyname_real(const char *hostname, struct in_addr **result, int *count, int pthread)
{
#ifdef GG_CONFIG_HAVE_GETHOSTBYNAME_R
	char *buf = NULL;
	char *new_buf = NULL;
	struct hostent he;
	struct hostent *he_ptr = NULL;
	size_t buf_len = 1024;
	int res = -1;
	int h_errnop;
	int ret = 0;
#ifdef GG_CONFIG_HAVE_PTHREAD
	int old_state;
#endif

	if (result == NULL) {
		errno = EINVAL;
		return -1;
	}

#ifdef GG_CONFIG_HAVE_PTHREAD
	pthread_cleanup_push(gg_gethostbyname_cleaner, &buf);

	if (pthread)
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old_state);
#endif

	buf = malloc(buf_len);

#ifdef GG_CONFIG_HAVE_PTHREAD
	if (pthread)
		pthread_setcancelstate(old_state, NULL);
#endif

	if (buf != NULL) {
#ifndef sun
		while ((ret = gethostbyname_r(hostname, &he, buf, buf_len, &he_ptr, &h_errnop)) == ERANGE) {
#else
		while (((he_ptr = gethostbyname_r(hostname, &he, buf, buf_len, &h_errnop)) == NULL) && (errno == ERANGE)) {
#endif
			buf_len *= 2;

#ifdef GG_CONFIG_HAVE_PTHREAD
			if (pthread)
				pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old_state);
#endif

			new_buf = realloc(buf, buf_len);

			if (new_buf != NULL)
				buf = new_buf;

#ifdef GG_CONFIG_HAVE_PTHREAD
			if (pthread)
				pthread_setcancelstate(old_state, NULL);
#endif

			if (new_buf == NULL) {
				ret = ENOMEM;
				break;
			}
		}

		if (ret == 0 && he_ptr != NULL && he_ptr->h_addr_list[0] != NULL) {
			int i;

			/* Policz liczbę adresów */

			for (i = 0; he_ptr->h_addr_list[i] != NULL; i++)
				;

			/* Zaalokuj */

#ifdef GG_CONFIG_HAVE_PTHREAD
			if (pthread)
				pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old_state);
#endif

			*result = malloc((i + 1) * sizeof(struct in_addr));

#ifdef GG_CONFIG_HAVE_PTHREAD
			if (pthread)
				pthread_setcancelstate(old_state, NULL);
#endif

			if (*result == NULL)
				return -1;

			/* Kopiuj */

			for (i = 0; he_ptr->h_addr_list[i] != NULL; i++)
				memcpy(&((*result)[i]), he_ptr->h_addr_list[i], sizeof(struct in_addr));

			(*result)[i].s_addr = INADDR_NONE;

			*count = i;

			res = 0;
		}

#ifdef GG_CONFIG_HAVE_PTHREAD
		if (pthread)
			pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old_state);
#endif

		free(buf);
		buf = NULL;

#ifdef GG_CONFIG_HAVE_PTHREAD
		if (pthread)
			pthread_setcancelstate(old_state, NULL);
#endif
	}

#ifdef GG_CONFIG_HAVE_PTHREAD
	pthread_cleanup_pop(1);
#endif

	return res;
#else /* GG_CONFIG_HAVE_GETHOSTBYNAME_R */
	struct hostent *he;
	int i;

	if (result == NULL || count == NULL) {
		errno = EINVAL;
		return -1;
	}

	he = gethostbyname(hostname);

	if (he == NULL || he->h_addr_list[0] == NULL)
		return -1;

	/* Policz liczbę adresów */

	for (i = 0; he->h_addr_list[i] != NULL; i++)
		;

	/* Zaalokuj */

	*result = malloc((i + 1) * sizeof(struct in_addr));

	if (*result == NULL)
		return -1;

	/* Kopiuj */

	for (i = 0; he->h_addr_list[i] != NULL; i++)
		memcpy(&((*result)[i]), he->h_addr_list[0], sizeof(struct in_addr));

	(*result)[i].s_addr = INADDR_NONE;

	*count = i;

	return 0;
#endif /* GG_CONFIG_HAVE_GETHOSTBYNAME_R */
}

#if defined(GG_CONFIG_HAVE_PTHREAD) || !defined(_WIN32)
/**
 * \internal Rozwiązuje nazwę i zapisuje wynik do podanego desktyptora.
 *
 * \param fd Deskryptor
 * \param hostname Nazwa serwera
 *
 * \return 0 jeśli się powiodło, -1 w przypadku błędu
 */
static int gg_resolver_run(int fd, const char *hostname)
{
	struct in_addr addr_ip[2], *addr_list;
	int addr_count;
	int res = 0;

	gg_debug(GG_DEBUG_MISC, "// gg_resolver_run(%d, %s)\n", fd, hostname);

	if ((addr_ip[0].s_addr = inet_addr(hostname)) == INADDR_NONE) {
		if (gg_gethostbyname_real(hostname, &addr_list, &addr_count, 1) == -1) {
			addr_list = addr_ip;
			/* addr_ip[0] już zawiera INADDR_NONE */
		}
	} else {
		addr_list = addr_ip;
		addr_ip[1].s_addr = INADDR_NONE;
		addr_count = 1;
	}

	gg_debug(GG_DEBUG_MISC, "// gg_resolver_run() count = %d\n", addr_count);

	if (write(fd, addr_list, (addr_count + 1) * sizeof(struct in_addr)) != (addr_count + 1) * sizeof(struct in_addr))
		res = -1;

	if (addr_list != addr_ip)
		free(addr_list);

	return res;
}
#endif

/**
 * \internal Odpowiednik \c gethostbyname zapewniający współbieżność.
 *
 * Jeśli dany system dostarcza \c gethostbyname_r, używa się tej wersji, jeśli
 * nie, to zwykłej \c gethostbyname. Funkcja służy do zachowania zgodności
 * ABI i służy do pobierania tylko pierwszego adresu -- pozostałe mogą
 * zostać zignorowane przez aplikację.
 *
 * \param hostname Nazwa serwera
 *
 * \return Zaalokowana struktura \c in_addr lub NULL w przypadku błędu.
 */
struct in_addr *gg_gethostbyname(const char *hostname)
{
	struct in_addr *result;
	int count;

	if (gg_gethostbyname_real(hostname, &result, &count, 0) == -1)
		return NULL;

	return result;
}

/**
 * \internal Struktura przekazywana do wątku rozwiązującego nazwę.
 */
struct gg_resolver_fork_data {
	int pid;		/*< Identyfikator procesu */
};

#ifdef _WIN32
/**
 *  Deal with the fact that you can't select() on a win32 file fd.
 *  This makes it practically impossible to tie into purple's event loop.
 *
 *  -This is thanks to Tor Lillqvist.
 *  XXX - Move this to where the rest of the the win32 compatiblity stuff goes when we push the changes back to libgadu.
 */
static int
socket_pipe (int *fds)
{
	SOCKET temp, socket1 = -1, socket2 = -1;
	struct sockaddr_in saddr;
	int len;
	u_long arg;
	fd_set read_set, write_set;
	struct timeval tv;

	temp = socket(AF_INET, SOCK_STREAM, 0);

	if (temp == INVALID_SOCKET) {
		goto out0;
	}

	arg = 1;
	if (ioctlsocket(temp, FIONBIO, &arg) == SOCKET_ERROR) {
		goto out0;
	}

	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_port = 0;
	saddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	if (bind(temp, (struct sockaddr *)&saddr, sizeof (saddr))) {
		goto out0;
	}

	if (listen(temp, 1) == SOCKET_ERROR) {
		goto out0;
	}

	len = sizeof(saddr);
	if (getsockname(temp, (struct sockaddr *)&saddr, &len)) {
		goto out0;
	}

	socket1 = socket(AF_INET, SOCK_STREAM, 0);

	if (socket1 == INVALID_SOCKET) {
		goto out0;
	}

	arg = 1;
	if (ioctlsocket(socket1, FIONBIO, &arg) == SOCKET_ERROR) {
		goto out1;
	}

	if (connect(socket1, (struct sockaddr  *)&saddr, len) != SOCKET_ERROR ||
			WSAGetLastError() != WSAEWOULDBLOCK) {
		goto out1;
	}

	FD_ZERO(&read_set);
	FD_SET(temp, &read_set);

	tv.tv_sec = 0;
	tv.tv_usec = 0;

	if (select(0, &read_set, NULL, NULL, NULL) == SOCKET_ERROR) {
		goto out1;
	}

	if (!FD_ISSET(temp, &read_set)) {
		goto out1;
	}

	socket2 = accept(temp, (struct sockaddr *) &saddr, &len);
	if (socket2 == INVALID_SOCKET) {
		goto out1;
	}

	FD_ZERO(&write_set);
	FD_SET(socket1, &write_set);

	tv.tv_sec = 0;
	tv.tv_usec = 0;

	if (select(0, NULL, &write_set, NULL, NULL) == SOCKET_ERROR) {
		goto out2;
	}

	if (!FD_ISSET(socket1, &write_set)) {
		goto out2;
	}

	arg = 0;
	if (ioctlsocket(socket1, FIONBIO, &arg) == SOCKET_ERROR) {
		goto out2;
	}

	arg = 0;
	if (ioctlsocket(socket2, FIONBIO, &arg) == SOCKET_ERROR) {
		goto out2;
	}

	fds[0] = socket1;
	fds[1] = socket2;

	closesocket (temp);

	return 0;

out2:
	closesocket (socket2);
out1:
	closesocket (socket1);
out0:
	closesocket (temp);
	errno = EIO;            /* XXX */

	return -1;
}
#endif



#ifdef _WIN32
struct gg_resolve_win32thread_data {
	char *hostname;
	int fd;
};

static DWORD WINAPI gg_resolve_win32thread_thread(LPVOID arg)
{
	struct gg_resolve_win32thread_data *d = arg;
	struct in_addr addr_ip[2], *addr_list;
	int addr_count;

	gg_debug(GG_DEBUG_MISC, "// gg_resolve_win32thread_thread() host: %s, fd: %i called\n", d->hostname, d->fd);

	if ((addr_ip[0].s_addr = inet_addr(d->hostname)) == INADDR_NONE) {
		/* W przypadku błędu gg_gethostbyname_real() zwróci -1
					 * i nie zmieni &addr. Tam jest już INADDR_NONE,
					 * więc nie musimy robić nic więcej. */
		if (gg_gethostbyname_real(d->hostname, &addr_list, &addr_count, 0) == -1)
		{
		    addr_list = addr_ip;
		}
	} else {
		addr_list = addr_ip;
		addr_ip[1].s_addr = INADDR_NONE;
		addr_count = 1;
	}

	gg_debug(GG_DEBUG_MISC, "// gg_resolve_win32thread_thread() count = %d\n", addr_count);

	write(d->fd, addr_list, (addr_count+1) * sizeof(struct in_addr));
	close(d->fd);

	free(d->hostname);
	d->hostname = NULL;

	free(d);

    if (addr_list != addr_ip)
		free(addr_list);

	gg_debug(GG_DEBUG_MISC, "// gg_resolve_win32thread_thread() done\n");

	return 0;
}


static int gg_resolve_win32thread(int *fd, void **resolver, const char *hostname)
{
	struct gg_resolve_win32thread_data *d = NULL;
	HANDLE h;
	DWORD dwTId;
	int pipes[2], new_errno;

	gg_debug(GG_DEBUG_FUNCTION, "** gg_resolve_win32thread(%p, %p, \"%s\");\n", fd, resolver, hostname);

	if (!resolver || !fd || !hostname) {
		gg_debug(GG_DEBUG_MISC, "// gg_resolve_win32thread() invalid arguments\n");
		errno = EFAULT;
		return -1;
	}

	if (socket_pipe(pipes) == -1) {
		gg_debug(GG_DEBUG_MISC, "// gg_resolve_win32thread() unable to create pipes (errno=%d, %s)\n", errno, strerror(errno));
		return -1;
	}

	if (!(d = malloc(sizeof(*d)))) {
		gg_debug(GG_DEBUG_MISC, "// gg_resolve_win32thread() out of memory\n");
		new_errno = errno;
		goto cleanup;
	}

	d->hostname = NULL;

	if (!(d->hostname = strdup(hostname))) {
		gg_debug(GG_DEBUG_MISC, "// gg_resolve_win32thread() out of memory\n");
		new_errno = errno;
		goto cleanup;
	}

	d->fd = pipes[1];

	h = CreateThread(NULL, 0, gg_resolve_win32thread_thread,
		d, 0, &dwTId);

	if (h == NULL) {
		gg_debug(GG_DEBUG_MISC, "// gg_resolve_win32thread() unable to create thread\n");
		new_errno = errno;
		goto cleanup;
	}

	*resolver = h;
	*fd = pipes[0];

	gg_debug(GG_DEBUG_MISC, "// gg_resolve_win32thread() done\n");

	return 0;

cleanup:
	if (d) {
		free(d->hostname);
		free(d);
	}

	close(pipes[0]);
	close(pipes[1]);

	errno = new_errno;

	return -1;

}

static void gg_resolve_win32thread_cleanup(void **priv_data, int force)
{
	struct gg_resolve_win32thread_data *data;

	gg_debug(GG_DEBUG_MISC, "// gg_resolve_win32thread_cleanup() force: %i called\n", force);

	if (priv_data == NULL || *priv_data == NULL)
		gg_debug(GG_DEBUG_MISC, "// gg_resolve_win32thread_cleanup() priv_data: NULL\n");
		return;

	data = (struct gg_resolve_win32thread_data*) *priv_data;
	gg_debug(GG_DEBUG_MISC, "// gg_resolve_win32thread_cleanup() data: %s called\n", data->hostname);
	*priv_data = NULL;

	if (force) {
		gg_debug(GG_DEBUG_MISC, "// gg_resolve_win32thread_cleanup() force called\n", force);
		//pthread_cancel(data->thread);
		//pthread_join(data->thread, NULL);
	}

	free(data->hostname);
	data->hostname = NULL;

	if (data->fd != -1) {
		close(data->fd);
		data->fd = -1;
	}
	gg_debug(GG_DEBUG_MISC, "// gg_resolve_win32thread_cleanup() done\n");
	free(data);
}
#endif

#ifndef _WIN32
/**
 * \internal Rozwiązuje nazwę serwera w osobnym procesie.
 *
 * Połączenia asynchroniczne nie mogą blokować procesu w trakcie rozwiązywania
 * nazwy serwera. W tym celu tworzony jest potok, nowy proces i dopiero w nim
 * przeprowadzane jest rozwiązywanie nazwy. Deskryptor strony do odczytu 
 * zapisuje się w strukturze sieci i czeka na dane w postaci struktury
 * \c in_addr. Jeśli nie znaleziono nazwy, zwracana jest \c INADDR_NONE.
 *
 * \param fd Wskaźnik na zmienną, gdzie zostanie umieszczony deskryptor
 *           potoku
 * \param priv_data Wskaźnik na zmienną, gdzie zostanie umieszczony wskaźnik
 *                  do numeru procesu potomnego rozwiązującego nazwę
 * \param hostname Nazwa serwera do rozwiązania
 *
 * \return 0 jeśli się powiodło, -1 w przypadku błędu
 */
static int gg_resolver_fork_start(int *fd, void **priv_data, const char *hostname)
{
	struct gg_resolver_fork_data *data = NULL;
	int pipes[2], new_errno;

	gg_debug(GG_DEBUG_FUNCTION, "** gg_resolver_fork_start(%p, %p, \"%s\");\n", fd, priv_data, hostname);

	if (fd == NULL || priv_data == NULL || hostname == NULL) {
		gg_debug(GG_DEBUG_MISC, "// gg_resolver_fork_start() invalid arguments\n");
		errno = EFAULT;
		return -1;
	}

	data = malloc(sizeof(struct gg_resolver_fork_data));

	if (data == NULL) {
		gg_debug(GG_DEBUG_MISC, "// gg_resolver_fork_start() out of memory for resolver data\n");
		return -1;
	}

	if (pipe(pipes) == -1) {
		gg_debug(GG_DEBUG_MISC, "// gg_resolver_fork_start() unable to create pipes (errno=%d, %s)\n", errno, strerror(errno));
		free(data);
		return -1;
	}

	data->pid = fork();

	if (data->pid == -1) {
		new_errno = errno;
		goto cleanup;
	}

	if (data->pid == 0) {
		close(pipes[0]);

		if (gg_resolver_run(pipes[1], hostname) == -1)
			_exit(1);
		else
			_exit(0);
	}

	close(pipes[1]);

	gg_debug(GG_DEBUG_MISC, "// gg_resolver_fork_start() %p\n", data);

	*fd = pipes[0];
	*priv_data = data;

	return 0;

cleanup:
	free(data);
	close(pipes[0]);
	close(pipes[1]);

	errno = new_errno;

	return -1;
}

/**
 * \internal Usuwanie zasobów po procesie rozwiązywaniu nazwy.
 *
 * Funkcja wywoływana po zakończeniu rozwiązanywania nazwy lub przy zwalnianiu
 * zasobów sesji podczas rozwiązywania nazwy.
 *
 * \param priv_data Wskaźnik na zmienną przechowującą wskaźnik do prywatnych
 *                  danych
 * \param force Flaga usuwania zasobów przed zakończeniem działania
 */
static void gg_resolver_fork_cleanup(void **priv_data, int force)
{
	struct gg_resolver_fork_data *data;

	if (priv_data == NULL || *priv_data == NULL)
		return;

	data = (struct gg_resolver_fork_data*) *priv_data;
	*priv_data = NULL;

	if (force)
		kill(data->pid, SIGKILL);

	waitpid(data->pid, NULL, WNOHANG);

	free(data);
}
#endif

#ifdef GG_CONFIG_HAVE_PTHREAD

/**
 * \internal Struktura przekazywana do wątku rozwiązującego nazwę.
 */
struct gg_resolver_pthread_data {
	pthread_t thread;	/*< Identyfikator wątku */
	char *hostname;		/*< Nazwa serwera */
	int rfd;		/*< Deskryptor do odczytu */
	int wfd;		/*< Deskryptor do zapisu */
};

/**
 * \internal Usuwanie zasobów po wątku rozwiązywaniu nazwy.
 *
 * Funkcja wywoływana po zakończeniu rozwiązanywania nazwy lub przy zwalnianiu
 * zasobów sesji podczas rozwiązywania nazwy.
 *
 * \param priv_data Wskaźnik na zmienną przechowującą wskaźnik do prywatnych
 *                  danych
 * \param force Flaga usuwania zasobów przed zakończeniem działania
 */
static void gg_resolver_pthread_cleanup(void **priv_data, int force)
{
	struct gg_resolver_pthread_data *data;

	if (priv_data == NULL || *priv_data == NULL)
		return;

	data = (struct gg_resolver_pthread_data *) *priv_data;
	*priv_data = NULL;

	if (force) {
		pthread_cancel(data->thread);
		pthread_join(data->thread, NULL);
	}

	free(data->hostname);
	data->hostname = NULL;

	if (data->wfd != -1) {
		close(data->wfd);
		data->wfd = -1;
	}

	free(data);
}

/**
 * \internal Wątek rozwiązujący nazwę.
 *
 * \param arg Wskaźnik na strukturę \c gg_resolver_pthread_data
 */
static void *gg_resolver_pthread_thread(void *arg)
{
	struct gg_resolver_pthread_data *data = arg;

	pthread_detach(pthread_self());

	if (gg_resolver_run(data->wfd, data->hostname) == -1)
		pthread_exit((void*) -1);
	else
		pthread_exit(NULL);

	return NULL;	/* żeby kompilator nie marudził */
}

/**
 * \internal Rozwiązuje nazwę serwera w osobnym wątku.
 *
 * Funkcja działa analogicznie do \c gg_resolver_fork_start(), z tą różnicą,
 * że działa na wątkach, nie procesach. Jest dostępna wyłącznie gdy podczas
 * kompilacji włączono odpowiednią opcję.
 *
 * \param fd Wskaźnik na zmienną, gdzie zostanie umieszczony deskryptor
 *           potoku
 * \param priv_data Wskaźnik na zmienną, gdzie zostanie umieszczony wskaźnik
 *                  do prywatnych danych wątku rozwiązującego nazwę
 * \param hostname Nazwa serwera do rozwiązania
 *
 * \return 0 jeśli się powiodło, -1 w przypadku błędu
 */
static int gg_resolver_pthread_start(int *fd, void **priv_data, const char *hostname)
{
	struct gg_resolver_pthread_data *data = NULL;
	int pipes[2], new_errno;

	gg_debug(GG_DEBUG_FUNCTION, "** gg_resolver_pthread_start(%p, %p, \"%s\");\n", fd, priv_data, hostname);

	if (fd == NULL || priv_data == NULL || hostname == NULL) {
		gg_debug(GG_DEBUG_MISC, "// gg_resolver_pthread_start() invalid arguments\n");
		errno = EFAULT;
		return -1;
	}

	data = malloc(sizeof(struct gg_resolver_pthread_data));

	if (data == NULL) {
		gg_debug(GG_DEBUG_MISC, "// gg_resolver_pthread_start() out of memory for resolver data\n");
		return -1;
	}

	if (pipe(pipes) == -1) {
		gg_debug(GG_DEBUG_MISC, "// gg_resolver_pthread_start() unable to create pipes (errno=%d, %s)\n", errno, strerror(errno));
		free(data);
		return -1;
	}

	data->hostname = strdup(hostname);

	if (data->hostname == NULL) {
		gg_debug(GG_DEBUG_MISC, "// gg_resolver_pthread_start() out of memory\n");
		new_errno = errno;
		goto cleanup;
	}

	data->rfd = pipes[0];
	data->wfd = pipes[1];

	if (pthread_create(&data->thread, NULL, gg_resolver_pthread_thread, data)) {
		gg_debug(GG_DEBUG_MISC, "// gg_resolver_pthread_start() unable to create thread\n");
		new_errno = errno;
		goto cleanup;
	}

	gg_debug(GG_DEBUG_MISC, "// gg_resolver_pthread_start() %p\n", data);

	*fd = pipes[0];
	*priv_data = data;

	return 0;

cleanup:
	if (data != NULL)
		free(data->hostname);

	free(data);

	close(pipes[0]);
	close(pipes[1]);

	errno = new_errno;

	return -1;
}

#endif /* GG_CONFIG_HAVE_PTHREAD */

/**
 * Ustawia sposób rozwiązywania nazw w sesji.
 *
 * \param gs Struktura sesji
 * \param type Sposób rozwiązywania nazw (patrz \ref build-resolver)
 *
 * \return 0 jeśli się powiodło, -1 w przypadku błędu
 */
int gg_session_set_resolver(struct gg_session *gs, gg_resolver_t type)
{
	GG_SESSION_CHECK(gs, -1);

	if (type == GG_RESOLVER_DEFAULT) {
		if (gg_global_resolver_type != GG_RESOLVER_DEFAULT) {
			gs->resolver_type = gg_global_resolver_type;
			gs->resolver_start = gg_global_resolver_start;
			gs->resolver_cleanup = gg_global_resolver_cleanup;
			return 0;
		}

#if !defined(GG_CONFIG_HAVE_PTHREAD) || !defined(GG_CONFIG_PTHREAD_DEFAULT)
#  ifdef _WIN32
		type = GG_RESOLVER_WIN32;
#  else
		type = GG_RESOLVER_FORK;
#  endif
#else
		type = GG_RESOLVER_PTHREAD;
#endif
	}

	switch (type) {
#ifdef _WIN32
		case GG_RESOLVER_WIN32:
			gs->resolver_type = type;
			gs->resolver_start = gg_resolve_win32thread;
			gs->resolver_cleanup = gg_resolve_win32thread_cleanup;
			return 0;
#else
		case GG_RESOLVER_FORK:
			gs->resolver_type = type;
			gs->resolver_start = gg_resolver_fork_start;
			gs->resolver_cleanup = gg_resolver_fork_cleanup;
			return 0;
#endif

#ifdef GG_CONFIG_HAVE_PTHREAD
		case GG_RESOLVER_PTHREAD:
			gs->resolver_type = type;
			gs->resolver_start = gg_resolver_pthread_start;
			gs->resolver_cleanup = gg_resolver_pthread_cleanup;
			return 0;
#endif

		default:
			errno = EINVAL;
			return -1;
	}
}

/**
 * Zwraca sposób rozwiązywania nazw w sesji.
 *
 * \param gs Struktura sesji
 *
 * \return Sposób rozwiązywania nazw
 */
gg_resolver_t gg_session_get_resolver(struct gg_session *gs)
{
	GG_SESSION_CHECK(gs, (gg_resolver_t) -1);

	return gs->resolver_type;
}

/**
 * Ustawia własny sposób rozwiązywania nazw w sesji.
 *
 * \param gs Struktura sesji
 * \param resolver_start Funkcja rozpoczynająca rozwiązywanie nazwy
 * \param resolver_cleanup Funkcja zwalniająca zasoby
 *
 * Parametry funkcji rozpoczynającej rozwiązywanie nazwy wyglądają następująco:
 *  - \c "int *fd" &mdash; wskaźnik na zmienną, gdzie zostanie umieszczony deskryptor potoku
 *  - \c "void **priv_data" &mdash; wskaźnik na zmienną, gdzie można umieścić wskaźnik do prywatnych danych na potrzeby rozwiązywania nazwy
 *  - \c "const char *name" &mdash; nazwa serwera do rozwiązania
 *
 * Parametry funkcji zwalniającej zasoby wyglądają następująco:
 *  - \c "void **priv_data" &mdash; wskaźnik na zmienną przechowującą wskaźnik do prywatnych danych, należy go ustawić na \c NULL po zakończeniu
 *  - \c "int force" &mdash; flaga mówiąca o tym, że zasoby są zwalniane przed zakończeniem rozwiązywania nazwy, np. z powodu zamknięcia sesji.
 *
 * Własny kod rozwiązywania nazwy powinien stworzyć potok, parę gniazd lub
 * inny deskryptor pozwalający na co najmniej jednostronną komunikację i 
 * przekazać go w parametrze \c fd. Po zakończeniu rozwiązywania nazwy,
 * powinien wysłać otrzymany adres IP w postaci sieciowej (big-endian) do
 * deskryptora. Jeśli rozwiązywanie nazwy się nie powiedzie, należy wysłać
 * \c INADDR_NONE. Następnie zostanie wywołana funkcja zwalniająca zasoby
 * z parametrem \c force równym \c 0. Gdyby sesja została zakończona przed
 * rozwiązaniem nazwy, np. za pomocą funkcji \c gg_logoff(), funkcja
 * zwalniająca zasoby zostanie wywołana z parametrem \c force równym \c 1.
 *
 * \return 0 jeśli się powiodło, -1 w przypadku błędu
 */
int gg_session_set_custom_resolver(struct gg_session *gs, int (*resolver_start)(int*, void**, const char*), void (*resolver_cleanup)(void**, int))
{
	GG_SESSION_CHECK(gs, -1);

	if (resolver_start == NULL || resolver_cleanup == NULL) {
		errno = EINVAL;
		return -1;
	}

	gs->resolver_type = GG_RESOLVER_CUSTOM;
	gs->resolver_start = resolver_start;
	gs->resolver_cleanup = resolver_cleanup;

	return 0;
}

/**
 * Ustawia sposób rozwiązywania nazw połączenia HTTP.
 *
 * \param gh Struktura połączenia
 * \param type Sposób rozwiązywania nazw (patrz \ref build-resolver)
 *
 * \return 0 jeśli się powiodło, -1 w przypadku błędu
 */
int gg_http_set_resolver(struct gg_http *gh, gg_resolver_t type)
{
	if (gh == NULL) {
		errno = EINVAL;
		return -1;
	}

	if (type == GG_RESOLVER_DEFAULT) {
		if (gg_global_resolver_type != GG_RESOLVER_DEFAULT) {
			gh->resolver_type = gg_global_resolver_type;
			gh->resolver_start = gg_global_resolver_start;
			gh->resolver_cleanup = gg_global_resolver_cleanup;
			return 0;
		}

#if !defined(GG_CONFIG_HAVE_PTHREAD) || !defined(GG_CONFIG_PTHREAD_DEFAULT)
#  ifdef _WIN32
		type = GG_RESOLVER_WIN32;
#  else
		type = GG_RESOLVER_FORK;
#  endif
#else
		type = GG_RESOLVER_PTHREAD;
#endif
	}

	switch (type) {
#ifdef _WIN32
		case GG_RESOLVER_WIN32:
			gh->resolver_type = type;
			gh->resolver_start = gg_resolve_win32thread;
			gh->resolver_cleanup = gg_resolve_win32thread_cleanup;
			return 0;
#else
		case GG_RESOLVER_FORK:
			gh->resolver_type = type;
			gh->resolver_start = gg_resolver_fork_start;
			gh->resolver_cleanup = gg_resolver_fork_cleanup;
			return 0;
#endif

#ifdef GG_CONFIG_HAVE_PTHREAD
		case GG_RESOLVER_PTHREAD:
			gh->resolver_type = type;
			gh->resolver_start = gg_resolver_pthread_start;
			gh->resolver_cleanup = gg_resolver_pthread_cleanup;
			return 0;
#endif

		default:
			errno = EINVAL;
			return -1;
	}
}

/**
 * Zwraca sposób rozwiązywania nazw połączenia HTTP.
 *
 * \param gh Struktura połączenia
 *
 * \return Sposób rozwiązywania nazw
 */
gg_resolver_t gg_http_get_resolver(struct gg_http *gh)
{
	if (gh == NULL) {
		errno = EINVAL;
		return GG_RESOLVER_INVALID;
	}

	return gh->resolver_type;
}

/**
 * Ustawia własny sposób rozwiązywania nazw połączenia HTTP.
 *
 * \param gh Struktura sesji
 * \param resolver_start Funkcja rozpoczynająca rozwiązywanie nazwy
 * \param resolver_cleanup Funkcja zwalniająca zasoby
 *
 * \return 0 jeśli się powiodło, -1 w przypadku błędu
 */
int gg_http_set_custom_resolver(struct gg_http *gh, int (*resolver_start)(int*, void**, const char*), void (*resolver_cleanup)(void**, int))
{
	if (gh == NULL || resolver_start == NULL || resolver_cleanup == NULL) {
		errno = EINVAL;
		return -1;
	}

	gh->resolver_type = GG_RESOLVER_CUSTOM;
	gh->resolver_start = resolver_start;
	gh->resolver_cleanup = resolver_cleanup;

	return 0;
}

/**
 * Ustawia sposób rozwiązywania nazw globalnie dla biblioteki.
 *
 * \param type Sposób rozwiązywania nazw (patrz \ref build-resolver)
 *
 * \return 0 jeśli się powiodło, -1 w przypadku błędu
 */
int gg_global_set_resolver(gg_resolver_t type)
{
	switch (type) {
		case GG_RESOLVER_DEFAULT:
			gg_global_resolver_type = type;
			gg_global_resolver_start = NULL;
			gg_global_resolver_cleanup = NULL;
			return 0;

#ifdef _WIN32
		case GG_RESOLVER_WIN32:
			gg_global_resolver_type = type;
			gg_global_resolver_start = gg_resolve_win32thread;
			gg_global_resolver_cleanup = gg_resolve_win32thread_cleanup;
			return 0;
#else
		case GG_RESOLVER_FORK:
			gg_global_resolver_type = type;
			gg_global_resolver_start = gg_resolver_fork_start;
			gg_global_resolver_cleanup = gg_resolver_fork_cleanup;
			return 0;
#endif

#ifdef GG_CONFIG_HAVE_PTHREAD
		case GG_RESOLVER_PTHREAD:
			gg_global_resolver_type = type;
			gg_global_resolver_start = gg_resolver_pthread_start;
			gg_global_resolver_cleanup = gg_resolver_pthread_cleanup;
			return 0;
#endif

		default:
			errno = EINVAL;
			return -1;
	}
}

/**
 * Zwraca sposób rozwiązywania nazw globalnie dla biblioteki.
 *
 * \return Sposób rozwiązywania nazw
 */
gg_resolver_t gg_global_get_resolver(void)
{
	return gg_global_resolver_type;
}

/**
 * Ustawia własny sposób rozwiązywania nazw globalnie dla biblioteki.
 *
 * \param resolver_start Funkcja rozpoczynająca rozwiązywanie nazwy
 * \param resolver_cleanup Funkcja zwalniająca zasoby
 *
 * Patrz \ref gg_session_set_custom_resolver.
 *
 * \return 0 jeśli się powiodło, -1 w przypadku błędu
 */
int gg_global_set_custom_resolver(int (*resolver_start)(int*, void**, const char*), void (*resolver_cleanup)(void**, int))
{
	if (resolver_start == NULL || resolver_cleanup == NULL) {
		errno = EINVAL;
		return -1;
	}

	gg_global_resolver_type = GG_RESOLVER_CUSTOM;
	gg_global_resolver_start = resolver_start;
	gg_global_resolver_cleanup = resolver_cleanup;

	return 0;
}

