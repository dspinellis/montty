# montty
This program will unobtrusively monitor input coming on a serial port.
It can do this even if another process is likely to use the port
for another purpose, e.g. to call out.  This is done by only locking
the port when data appears on it, and by refraining to read data
when the port is locked by another process.
