montty: montty.c

install: montty
	install montty /usr/local/sbin/
	install initd.sh /etc/init.d/montty
	systemctl daemon-reload
