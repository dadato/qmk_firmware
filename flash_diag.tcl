# Read F_SIZE register at 0x1FFFF7CC
adapter speed 1000
transport select swd
init
halt
poll off
set v [mrw 0x1FFFF7CC]
puts "F_SIZE (0x1FFFF7CC) = 0x[format %08x $v]"
set v8 [mrw 0x1FFFF7CE]
puts "16-bit read  (0x1FFFF7CE) = 0x[format %04x $v8]"
poll on
exit
