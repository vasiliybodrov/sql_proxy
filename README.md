# sql_proxy

For debug:
$ sudo gdbserver 0.0.0.0:4444 ./proxy -p 4880 -i '127.0.0.1' -d 7777 -o DEBUG -t 1000 -c 10000 --force --show-variable --no-daemon
$ nc -l -p 7777
$ cat foo.file | nc 127.0.0.1 4880

