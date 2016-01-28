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
1453961435.369544866 fd 6 init
1453961435.369596177 fd 6 header read
1453961435.369610859 fd 6 response header written
1453961442.430326808 fd 6 object written
1453961442.442290183 fd 6 spurious filter -2 received, expected -12
1453961444.561565669 fd 6 send buffer empty
1453961444.561598331 fd 6 closed
```
