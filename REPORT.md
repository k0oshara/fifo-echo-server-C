# Описание

Программа представляет собой эхо-сервер, который использует именованный канал (FIFO) для приема данных и вывода их на экран (foreground-режим) или в файл протокола (daemon). Она может работать как в фоновом режиме (daemon), так и в обычном режиме (foreground).

```
OS: Ubuntu 24.10 oracular
Kernel: x86_64 Linux 6.14.1-061401-generic
Shell: bash 5.2.32
CPU: AMD Ryzen 7 6800H with Radeon Graphics @ 16x 3.201GHz
GPU: AMD Radeon Graphics (radeonsi, rembrandt, LLVM 19.1.1, DRM 3.61, 6.14.1-061401-generic)
RAM: 6237MiB / 13656MiB    
```

# Тестирование

**1. Сигнал Будильник (SIGALRM)** 

```
$ ./m -n 2

[INFO] started. fifo=/tmp/echo_fifo, log=/tmp/echo_fifo.log, interval=2 sec
[2025-04-11 16:16:08] waiting for data...
[2025-04-11 16:16:10] waiting for data...
[2025-04-11 16:16:12] waiting for data...
[2025-04-11 16:16:14] waiting for data...
```

Будильник срабатывает корректно по указаному интервалу.

**2. Передача данных**

`$ echo "hello from echo" > /tmp/echo_fifo`

```
$ cat <<EOF > /tmp/echo_fifo
line one
line two
line three
EOF
```

`$ printf "no-newline" > /tmp/echo_fifo` *Проверка на добавление '\n'*

```
$ ./m -n 10

[INFO] started. fifo=/tmp/echo_fifo, log=/tmp/echo_fifo.log, interval=10 sec
[2025-04-11 17:05:03] waiting for data...
hello from echo
[2025-04-11 17:05:13] waiting for data...
[2025-04-11 17:05:23] waiting for data...
=== statistics ===
Messages: 1
Bytes: 16
Alarms: 3
line one
line two
line three
[2025-04-11 17:05:33] waiting for data...
=== statistics ===
Messages: 2
Bytes: 45
Alarms: 4
no-newline
[2025-04-11 17:05:43] waiting for data...
=== statistics ===
Messages: 3
Bytes: 55
Alarms: 5
[2025-04-11 17:05:53] waiting for data...
```

Вывожу статистику, после каждой отправки:
```
$ ps -C m -o pid,cmd
$ kill -SIGUSR1 <PID>
```

***Проверка отправки файла целиком***
`$ cat /usr/share/dict/words > /tmp/echo_fifo`

```
$ ./m -n 10

[INFO] started. fifo=/tmp/echo_fifo, log=/tmp/echo_fifo.log, interval=10 sec
[2025-04-11 17:15:54] waiting for data...
...
...
...
zodiacs
zombi
zombie
zombie's
zombies
zombi's
zombis
zonal
zone
zoned
zone's
zones
zoning
zonked
zoo
zoological
zoologist
zoologist's
zoologists
zoology
zoology's
zoom
zoomed
zooming
zoom's
zooms
zoo's
zoos
zorch
zucchini
zucchini's
zucchinis
zwieback
zwieback's
zygote
zygote's
zygotes
[2025-04-11 17:16:04] waiting for data...
=== statistics ===
Messages: 1
Bytes: 985084
Alarms: 2
[2025-04-11 17:16:14] waiting for data...
```

***Проверка отправки данных в режиме демона (daemon)***

```
$ ./m -d -n 10

$ echo "test daemon" > /tmp/echo_fifo
$ tail -n 100 /tmp/echo_fifo.log

[INFO] started. fifo=/tmp/echo_fifo, log=/tmp/echo_fifo.log, interval=10 sec
[2025-04-11 18:41:56] waiting for data...
test daemon
[2025-04-11 18:42:06] waiting for data...
[INFO] SIGINT received – finishing current message and exiting
[INFO] termination requested – exiting
=== statistics ===
Messages: 1
Bytes: 12
Alarms: 2
```

**3. Сигналы**

***SIGUSR1 - вывод статистики***

```
$ echo -n "partial" > /tmp/echo_fifo
$ kill -SIGUSR1 <PID>
```

```
$ ./m -n 10

[INFO] started. fifo=/tmp/echo_fifo, log=/tmp/echo_fifo.log, interval=10 sec
partial
[2025-04-11 17:58:58] waiting for data...
[2025-04-11 17:59:08] waiting for data...
=== statistics ===
Messages: 1
Bytes: 7
Alarms: 2
[2025-04-11 17:59:18] waiting for data...
```

***SIGINT (Ctrl+C) - дочитать и выйти***

```
$ echo -n "partial" > /tmp/echo_fifo
$ kill -SIGINT <PID>
```

```
$ ./m -n 10

[INFO] started. fifo=/tmp/echo_fifo, log=/tmp/echo_fifo.log, interval=10 sec
partial
[2025-04-11 17:30:08] waiting for data...
[INFO] SIGINT received – finishing current message and exiting
[INFO] termination requested – exiting
=== statistics ===
Messages: 1
Bytes: 7
Alarms: 1
```

***SIGTERM - сразу выйти, ничего не дочитывать***

```
$ echo -n "partial" > /tmp/echo_fifo
$ kill -SIGTERM <PID>
```

```
$ ./m -n 10

[INFO] started. fifo=/tmp/echo_fifo, log=/tmp/echo_fifo.log, interval=10 sec
partial
[2025-04-11 18:05:06] waiting for data...
[INFO] termination requested – exiting
=== statistics ===
Messages: 1
Bytes: 7
Alarms: 1
```

***SIGHUP - переход в демон***

```
#!/bin/bash
./m -n 5 &
OLD=$!
sleep 1
kill -SIGHUP $OLD
sleep 1
NEW=$(pgrep -n -f "./m -n 5")
echo "new PID = $NEW"
echo "hello after hup" > /tmp/echo_fifo
sleep 1
tail -n 10 /tmp/echo_fifo.log
```

```
[INFO] started. fifo=/tmp/echo_fifo, log=/tmp/echo_fifo.log, interval=5 sec
new PID = 234905
=== statistics ===
Messages: 1
Bytes: 12
Alarms: 1
[INFO] switched to daemon mode by SIGHUP
=== statistics ===
Messages: 0
Bytes: 0
Alarms: 0
hello after hup
```

***SIGQUIT (Ctrl+\\) - успешно игнорирует***
