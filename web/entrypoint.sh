#!/bin/sh
chown demo:demo /home/demo
su -s /bin/sh demo -c 'cp -a /etc/skel/. ~/'
exec su -s /bin/bash -l demo -c 'exec /home/demo/velvet/release/vv'
