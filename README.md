Compile:

```
cc -Wall test.c
```

run:

```
./a.out
```

This daemon listens on port 5000 and responds to http requests, serving a 1 megabyte object
and logging the timing of events. Designed to exercise EVFILT_EMPTY.

Current test: run ./a.out on cds1166.lax and execute the curl below from some other host:

```
curl --limit-rate 100k -vo/dev/null -x cds1166.lax.llnw.net:5000 http://test.vo.llnwd.net/hi && perl -e 'use Time::HiRes qw(time); print time, "\n";'
```

Current results:

```
root@cds1166.lax:~# ./a.out
1462405783.396314603 fd 6 init
1462405783.396501372 fd 6 header read
1462405783.396532128 fd 6 response header written
1462405790.470430280 fd 6 object written
1462405792.586527057 fd 6 send buffer empty
1462405792.586551212 fd 6 sender limited: 9078 cwnd limited low: 0 cwnd limited high: 0 receiver limited 0: fastsoft: 0
1462405792.586563196 fd 6 closed
```
