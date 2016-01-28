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
