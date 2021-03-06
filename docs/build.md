Building
========

* [Introduction](#introduction)
* [Configure](#configure)
* [Example](#example)
* [Running](#running)
* [Debugging](#debugging)


Introduction
------------

Finit comes with a traditional configure script to control features and
optional plugins to enable.  It depends on two external libraries:

- [libuEv][], the event loop
- [libite][] (-lite), much needed frog DNA

**NOTE:** Most free/open source software that uses `configure` default
  to install to `/usr/local`.  However, some Linux distributions do no
  longer search that path for installed software, e.g. Fedora and Alpine
  Linux.  To get finit's configure script to find its dependencies you
  have to help the `pkg-config` tool a bit if you do not change the
  default prefix path:

    PKG_CONFIG_LIBDIR=/usr/local/lib/pkgconfig ./configure

The configure script checks for all dependencies, including the correct
version of the above mentioned libraries.  Currently requried versions:

- libite v2.0.1
- libuEv v2.1.0


Configure
---------

Below are a few of the main switches to configure:

* `--disable-inetd`: Disable the built-in inetd server.

* `--enable-rw-rootfs`: Most desktop and server systems boot with the
  root file stystem read-only.  With this setting Finit will remount it
  as read-write early at boot so the `bootmisc.so` plugin can run.
  Usually not needed on embedded systems.

* `--enable-static`: Build Finit statically.  The plugins will be
  built-ins (.o files) and all external libraries, except the C library
  will be linked statically.

* `--enable-alsa-utils-plugin`: Enable the optional `alsa-utils.so` sound plugin.

* `--enable-dbus-plugin`: Enable the optional D-Bus `dbus.so` plugin.

* `--enable-resolvconf-plugin`: Enable the `resolvconf.so` optional plugin.

* `--enable-x11-common-plugin`: Enable the optional X Window `x11-common.so` plugin.

For more configure flags, see <kbd>./configure --help</kbd>


Example
-------

First, unpack the archive:

```shell
    $ tar xf finit-3.0.tar.xz
    $ cd finit-3.0/
```

Then configure, build and install:

```shell
    $ ./configure --enable-rw-rootfs            --enable-inetd-echo-plugin        \
                  --enable-inetd-chargen-plugin --enable-inetd-daytime-plugin     \
                  --enable-inetd-discard-plugin --enable-inetd-time-plugin        \
                  --with-heading="Alpine Linux 3.4" --with-hostname=alpine
    $ make
    .
    .
    .
    $ DESTDIR=/tmp/finit make install
```

In this example the [finit-3.0.tar.xz][1] archive is unpacked to the
user's home directory, configured, built and installed to a temporary
staging directory.  The environment variable `DESTDIR` controls the
destination directory when installing, very useful for building binary
standalone packages.

Finit 3.0 and later can detect if it runs on an embedded system, or a
system that use BusyBox tools instead of udev & C:o.  On such systems
`mdev` instead of `udev` is used.  However, remember to also change the
Linux config to:

    CONFIG_UEVENT_HELPER_PATH="/sbin/mdev"

**Note:** If you run into problems starting Finit, take a look at
  `finit.c`.  One of the most common problems is a custom Linux kernel
  build that lack `CONFIG_DEVTMPFS`.  Another is too much cruft in the
  system `/etc/fstab`.


Running
-------

Having successfully built Finit it is now be time to take it for a test
drive.  The `make install` attempts to set up finit as the system system
init, `/sbin/init`, but this is usually a symlink pointing to the
current init.

So either change the symlink, or change your boot loader (GRUB, LOADLIN,
LILO, U-Boot/Barebox or RedBoot) configuration to append the following
to the kernel command line:

```shell
append="init=/sbin/finit"
```

Remember to also set up an initial `/etc/finit.conf` before rebooting!


Recovery
--------

To rescue a system with Finit, append the following to the kernel
command line:

```shell
append="init=/sbin/finit rescue"
```

This tells Finit to start in a very limited recovery mode, no services
are loaded, no filesystems are mounted or checked, and no networking is
enabled.  The default Finit rescue mode configuration is installed into
`/lib/finit/rescue.conf`, which can be safely removed or changed.

By default the a root shell, without login, is started.


Debugging
---------

Edit, or append to, the kernel command line: remove `quiet` to enable
kernel messages and add `debug` to enable Finit debug messages.

```shell
append="init=/sbin/finit debug"
```

To debug startup issues, in particular issues with getty/login, try
`configure --enable-fallback-shell`.  When no TTYs are detected, and
Finit is configured with this option, Finit will try to start a bare
`/bin/sh` on the boot console.  Remember, this is only for debugging
and would leave your production system potentially wide open.

There is also a rescue shell available, in case Finit crashes and the
kernel usually reboots: `configure --enable-emergency-shell`.  However,
the behavior of Finit is severely limited when this is enabled, so use
it only for debugging start up issues when Finit crashes.

**NOTE:** Neither of these two configure options should be enabled on
  production systems since they can potentially give a user root access.


[1]:       ftp://troglobit.com/finit/finit-3.0.tar.xz
[libuEv]:  https://github.com/troglobit/libuev
[libite]:  https://github.com/troglobit/libite
