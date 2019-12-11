Summary: periodically moves logs into a rotation before deletion
Name: watchlogd
Version: 0.91a
Release: 5
Copyright: GPL
Group: System/Daemons
Source: watchlogd-0.91a.tar.gz
Source1: initd.watchlog
Patch0: watchlogd-0.91-make.patch
Patch1: watchlogd-0.91-date.patch
Patch2: watchlogd-0.91-erropen.patch
BuildRoot: /var/tmp/ins-watchlogd

%description
watchlogd is a daemon that rotates backlogs daily, weekly,
monthly or when a maximum file size is reached. It will wake up once or
more a day and check if any logs need processing. Old logs can be saved
for a number of rotations and optionally compressed. The oldest log that
is removed from the rotational sequence can also be emailed before
deletion. A test mode can be enabled to debug and trace operational
scenarios without actually processing log files. For singular processing
chores, the non-daemon variant watchlog can be run.


%prep 

%setup -n watchlogd-0.91a
%patch0 -p1 -b .flags
%patch1 -p1 -b .baddate
%patch2 -p1 -b .errlogopen


%build
make PKG_CFLAGS="$PKG_CFLAGS" > A 2> A2


%pre

if [ -f /etc/watchlogd.conf ]; then
   cp /etc/watchlogd.conf \
      /etc/watchlogd.conf.$PKG_PREV
fi



%install
mkdir -p $PKG_INS/usr/bin
mkdir -p $PKG_INS/usr/sbin
mkdir -p $PKG_INS/usr/lib
mkdir -p $PKG_INS/var/run

for i in 1 3 5 7 8; do
 mkdir -p $PKG_INS/usr/man/man$i
done

mkdir -p $PKG_INS/etc
mkdir -p $PKG_INS/etc/rc.d/init.d

for i in 0 1 2 3 4 5 6; do
  mkdir -p $PKG_INS/etc/rc.d/rc$i.d
done

install -d -m 750 -o root -g root $PKG_INS/etc/watchlog.d
install -d -m 700 -o root -g root $PKG_INS/var/lib/watchlogd

make install > I 2> I2

#will patch make later
install -m 550 -o root -g root /usr/sbin/watchlog $PKG_INS/usr/sbin/watchlog
cp /usr/sbin/watchlogd $PKG_INS/usr/sbin/watchlogd
strip $PKG_INS/usr/sbin/watchlog

cp /usr/man/man8/watchlog.8 $PKG_INS/usr/man/man8/watchlog.8
cp /usr/man/man8/watchlogd.8 $PKG_INS/usr/man/man8/watchlogd.8


##INSTALL-CONFIG-NEW##
if [ ! -f $PKG_INS/etc/watchlogd.conf ]; then
install -m 644 -o 0 -g 0 watchlogd.conf.example $PKG_INS/etc/watchlogd.conf
fi

##INSTALL-CONFIG-ALL##
install -m 750 -o root -g root $PKG_SRC/initd.watchlog \
        $PKG_INS/etc/rc.d/init.d/watchlog

ln -sf ../init.d/watchlog $PKG_INS/etc/rc.d/rc0.d/K70watchlog
ln -sf ../init.d/watchlog $PKG_INS/etc/rc.d/rc3.d/S30watchlog
ln -sf ../init.d/watchlog $PKG_INS/etc/rc.d/rc1.d/K70watchlog
ln -sf ../init.d/watchlog $PKG_INS/etc/rc.d/rc5.d/S30watchlog
ln -sf ../init.d/watchlog $PKG_INS/etc/rc.d/rc2.d/S30watchlog
ln -sf ../init.d/watchlog $PKG_INS/etc/rc.d/rc6.d/K70watchlog


%post

##POST-INSTALL-CONFIG-RESTORE##

if [ -f /etc/watchlogd.conf.$PKG_PREV ]; then
   install -m 640 /etc/watchlogd.conf.$PKG_PREV \
      /etc/watchlogd.conf
fi

%clean
rm -rf $PKG_INS


%preun

##PREUN-CONFIG-SAVE##
if [ -f /etc/watchlogd.conf ]; then
   cp /etc/watchlogd.conf \
      /etc/watchlogd.conf.$PKG_PREV
fi



%postun

##POSTUN-CONFIG-RESTORE##
if [ -f /etc/watchlogd.conf.$PKG_PREV ]; then
   install -m 640 /etc/watchlogd.conf.$PKG_PREV \
      /etc/watchlogd.conf
fi


%files
%doc COPYING CHANGES
/usr/sbin/watchlog
/usr/sbin/watchlogd
/usr/man/man8/watchlog.8
/usr/man/man8/watchlogd.8
/etc/rc.d/init.d/watchlog
/etc/rc.d/rc0.d/K70watchlog
/etc/rc.d/rc3.d/S30watchlog
/etc/rc.d/rc1.d/K70watchlog
/etc/rc.d/rc5.d/S30watchlog
/etc/rc.d/rc2.d/S30watchlog
/etc/rc.d/rc6.d/K70watchlog
/etc/watchlogd.conf

%dir /etc/watchlog.d

%changelog
* Tue Mar 25 1998  mike knerr  <mk@metrotron.com>
- 0.91a-5
- initial open errlog will truncate if exists
  instead of returning an error
  
* Tue Mar 24 1998  mike knerr  <mk@metrotron.com>
- 0.91a-4
- clean up start/stop script and pkg script as separate source 

* Tue Mar 17 1998  mike knerr  <mk@metrotron.com>
- 0.91a-3
- 0.91a source changes (still keeping 0.91-3 patches )
- watchlog.c pidFetch logs at debug level instead of error
  level (no pidifle is not always an error -- depends on when called)
- log.c move unlink to remove error log file
  thanks to Claus-Justus Heine <claus@momo.math.rwth-aachen.de>
  for pointing it out
- log.c not using /tmp for error log (for the usual reasons) create
  in /var/lib/watchlogd instead
- clean up junk in source dir and reissue tgz as watchlogd-0.91a.tar.gz

* Thu Mar 12 1998 mike knerr  <mk@metrotron.com>
- 0.91-3
- patch for long overdue fix for day of the month checking in logrotate

* Wed May 7 1997  mike knerr  <mk@metrotron.com>
- 0.91-2
- update spec to pkg vars and config handling standard

* Wed Apr 9 1997   <mk@metrotron.com>
- 0.91-1
- fix install of /etc/watchlog.conf to default gets put in if new install


