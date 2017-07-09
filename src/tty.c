/* Finit TTY handling
 *
 * Copyright (c) 2013       Mattias Walström <lazzer@gmail.com>
 * Copyright (c) 2013-2017  Joachim Nilsson <troglobit@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <ctype.h>		/* isdigit() */
#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <lite/lite.h>

#include "config.h"		/* Generated by configure script */
#include "finit.h"
#include "conf.h"
#include "helpers.h"
#include "tty.h"
#include "util.h"
#include "utmp-api.h"

#ifdef FALLBACK_SHELL
static pid_t fallback = 0;
#endif
static LIST_HEAD(, tty_node) tty_list = LIST_HEAD_INITIALIZER();

void tty_mark(void)
{
	tty_node_t *tty;

	LIST_FOREACH(tty, &tty_list, link) {
		if (tty->mtime.tv_sec)
			tty->dirty = -1;
	}
}

void tty_check(tty_node_t *tty, struct timeval *mtime)
{
	if (mtime && timercmp(&tty->mtime, mtime, !=))
		tty->dirty = 1;	/* Modified, restart */
	else
		tty->dirty = 0;	/* Not modified */

	/* Update mtime, if given */
	tty->mtime.tv_sec  = mtime ? mtime->tv_sec  : 0;
	tty->mtime.tv_usec = mtime ? mtime->tv_usec : 0;
}

void tty_sweep(void)
{
	tty_node_t *tty, *tmp;

	LIST_FOREACH_SAFE(tty, &tty_list, link, tmp) {
		if (tty->mtime.tv_sec && tty->dirty) {
			_d("TTY %s dirty, stopping ...", tty->data.name);
			tty_stop(&tty->data);

			if (tty->dirty == -1) {
				_d("TTY %s removed, cleaning up.", tty->data.name);
				LIST_REMOVE(tty, link);
				tty_unregister(tty);
			}
		}
	}
}

/* tty [!1-9,S] <DEV> [BAUD[,BAUD,...]] [TERM] [noclear] */
int tty_register(char *line, struct timeval *mtime)
{
	tty_node_t *entry;
	int         insert = 0, noclear = 0;
	char       *tok, *dev = NULL, *baud = NULL;
	char       *runlevels = NULL, *term = NULL;

	if (!line) {
		_e("Missing argument");
		return errno = EINVAL;
	}

	tok = strtok(line, " ");
	while (tok) {
		if (tok[0] == '[')
			runlevels = &tok[0];
		else if (tok[0] == '/')
			dev = tok;
		else if (isdigit(tok[0]))
			baud = tok;
		else if (!strcmp(tok, "noclear"))
			noclear = 1;
		else
			term = tok;

		tok = strtok(NULL, " ");
	}

	if (!dev) {
		_e("Incomplete tty, cannot register");
		return errno = EINVAL;
	}

	entry = tty_find(dev);
	if (!entry) {
		insert = 1;
		entry = calloc(1, sizeof(*entry));
		if (!entry)
			return errno = ENOMEM;
	}

	entry->data.name = strdup(dev);
	entry->data.baud = baud ? strdup(baud) : NULL;
	entry->data.term = term ? strdup(term) : NULL;
	entry->data.noclear = noclear;
	entry->data.runlevels = conf_parse_runlevels(runlevels);
	_d("Registering tty %s at %s baud with term=%s on runlevels %s",
	   dev, baud ?: "NULL", term ?: "N/A", runlevels ?: "[2-5]");

	if (insert)
		LIST_INSERT_HEAD(&tty_list, entry, link);

	tty_check(entry, mtime);
	_d("TTY %s is %sdirty", dev, entry->dirty ? "" : "NOT ");

	return 0;
}

int tty_unregister(tty_node_t *tty)
{
	if (!tty) {
		_e("Missing argument");
		return errno = EINVAL;
	}

	if (tty->data.name)
		free(tty->data.name);
	if (tty->data.baud)
		free(tty->data.baud);
	if (tty->data.term)
		free(tty->data.term);
	free(tty);

	return 0;
}

tty_node_t *tty_find(char *dev)
{
	tty_node_t *entry;

	LIST_FOREACH(entry, &tty_list, link) {
		if (!strcmp(dev, entry->data.name))
			return entry;
	}

	return NULL;
}

size_t tty_num(void)
{
	size_t num = 0;
	tty_node_t *entry;

	LIST_FOREACH(entry, &tty_list, link)
		num++;

	return num;
}

size_t tty_num_active(void)
{
	size_t num = 0;
	tty_node_t *entry;

	LIST_FOREACH(entry, &tty_list, link) {
		if (entry->data.pid)
			num++;
	}

	return num;
}

tty_node_t *tty_find_by_pid(pid_t pid)
{
	tty_node_t *entry;

	LIST_FOREACH(entry, &tty_list, link) {
		if (entry->data.pid == pid)
			return entry;
	}

	return NULL;
}

static char *canonicalize(char *tty)
{
	struct stat st;
	static char path[80];

	if (!tty)
		return NULL;

	strlcpy(path, tty, sizeof(path));
	if (stat(path, &st)) {
		snprintf(path, sizeof(path), "%s%s", _PATH_DEV, tty);
		if (stat(path, &st))
			return NULL;
	}

	if (!S_ISCHR(st.st_mode))
		return NULL;

	return path;
}

static int tty_exist(char *dev)
{
	int fd;
	struct termios c;

	if (access(dev, F_OK))
		return 1;

	fd = open(dev, O_RDONLY);
	if (-1 == fd)
		return 1;

	/* XXX: Add check for errno == EIO? */
	if (tcgetattr(fd, &c)) {
		close(fd);
		return 1;
	}
	close(fd);

	return 0;
}

void tty_start(finit_tty_t *tty)
{
	int is_console = 0;
	char *dev;

	if (tty->pid) {
		_d("%s: TTY already active", tty->name);
		return;
	}

	dev = canonicalize(tty->name);
	if (!dev) {
		_d("%s: Cannot find TTY device: %s", tty->name, strerror(errno));
		return;
	}

	if (console && !strcmp(dev, console))
		is_console = 1;

	if (tty_exist(dev)) {
		_d("%s: Not a valid TTY: %s", dev, strerror(errno));
		return;
	}

	tty->pid = run_getty(dev, tty->baud, tty->term, tty->noclear, is_console);
}

void tty_stop(finit_tty_t *tty)
{
	if (!tty->pid)
		return;

	/*
	 * XXX: TTY handling should be refactored to regular services,
	 * XXX: that way we could rely on the state machine to properly
	 * XXX: send SIGTERM, wait for max 2 sec to collect PID before
	 * XXX: sending SIGKILL.
	 */
	_d("Stopping TTY %s", tty->name);
	kill(tty->pid, SIGKILL);
	waitpid(tty->pid, NULL, 0);
	tty->pid = 0;
}

int tty_enabled(finit_tty_t *tty)
{
	if (!tty)
		return 0;

	if (!ISSET(tty->runlevels, runlevel))
		return 0;
	if (fexist(tty->name))
		return 1;

	return 0;
}

/*
 * Fallback shell if no TTYs are active
 */
int tty_fallback(pid_t lost)
{
#ifdef FALLBACK_SHELL
	if (lost == 1) {
		if (fallback) {
			kill(fallback, SIGKILL);
			fallback = 0;
		}

		return 0;
	}

	if (fallback != lost || tty_num_active())
		return 0;

	fallback = fork();
	if (fallback)
		return 1;

	/*
	 * Become session leader and set controlling TTY
	 * to enable Ctrl-C and job control in shell.
	 */
	setsid();
	ioctl(STDIN_FILENO, TIOCSCTTY, 1);

	_exit(execl(_PATH_BSHELL, _PATH_BSHELL, NULL));
#else
	(void)lost;
#endif /* FALLBACK_SHELL */

	return 0;
}

/*
 * TTY monitor, called by service_monitor()
 */
int tty_respawn(pid_t pid)
{
	tty_node_t *entry = tty_find_by_pid(pid);

	if (!entry)
		return tty_fallback(pid);

	/* Set DEAD_PROCESS UTMP entry */
	utmp_set_dead(pid);

	/* Clear PID to be able to respawn it. */
	entry->data.pid = 0;

	if (!tty_enabled(&entry->data))
		tty_stop(&entry->data);
	else
		tty_start(&entry->data);

	return 1;
}

/*
 * Called after reload of /etc/finit.d/, stop/start TTYs
 */
void tty_reload(void)
{
	tty_node_t *tty;

	tty_sweep();

	LIST_FOREACH(tty, &tty_list, link) {
		if (!tty_enabled(&tty->data))
			tty_stop(&tty->data);
		else
			tty_start(&tty->data);

		tty->dirty = 0;
	}
}

/* Start all TTYs that exist in the system and are allowed at this runlevel */
void tty_runlevel(void)
{
	tty_node_t *entry;

	LIST_FOREACH(entry, &tty_list, link) {
		if (!tty_enabled(&entry->data))
			tty_stop(&entry->data);
		else
			tty_start(&entry->data);
	}

	/* Start fallback shell if enabled && no TTYs */
	tty_fallback(tty_num_active() > 0 ? 1 : 0);
}

/**
 * Local Variables:
 *  indent-tabs-mode: t
 *  c-file-style: "linux"
 * End:
 */
