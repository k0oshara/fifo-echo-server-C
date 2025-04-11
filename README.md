# fifo-echo-server-C

Эхо-сервер посредством FIFO на C

`gcc -Wall -Wextra -pedantic -O2 main.c -o m`

```
Usage: ./m [-d] [-p fifo_path] [-l log_file] [-n seconds]
  -d            start as daemon
  -p <path>     fifo path (default /tmp/echo_fifo)
  -l <file>     log file  (default /tmp/echo_fifo.log)
  -n <seconds>  diagnostic interval (default 5)
```

Подробнее в `REPORT.md`