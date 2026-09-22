# SK32F077 via DAPLink (CMSIS-DAP SWD), full chip erase + word-aligned program.
adapter speed 1000
transport select swd

proc wait_not_busy {} {
    set sr 1
    while {[expr {$sr & 1}]} {
        set sr [mrw 0x4002200c]
    }
}

proc flash_unlock {} {
    mww 0x40022004 0x45670123
    mww 0x40022004 0xCDEF89AB
}

init
halt
poll off

# Mass erase
flash_unlock
mww 0x40022010 0x00000004;  # CR = MER
mww 0x40022010 0x00000044;  # CR = MER | STRT
wait_not_busy
# Clear STRT, re-lock
mww 0x40022010 0x00000080

# Program: STM32F0 requires 32-bit word aligned writes.
flash_unlock
mww 0x40022010 0x00000001;  # CR = PG

set src "D:/GitHub/My/qmk_firmware/firmware/sk32duino_kb17.bin"
set fp [open $src r]
fconfigure $fp -translation binary
set data [read $fp]
close $fp

set bytes [string length $data]
set words [expr {$bytes / 4}]
set addr 0x08000000
for {set i 0} {$i < $words} {incr i} {
    set b0 [scan [string index $data [expr {$i*4}]] %c]
    set b1 [scan [string index $data [expr {$i*4+1}]] %c]
    set b2 [scan [string index $data [expr {$i*4+2}]] %c]
    set b3 [scan [string index $data [expr {$i*4+3}]] %c]
    set val [expr {$b0 | ($b1 << 8) | ($b2 << 16) | ($b3 << 24)}]
    mww $addr $val
    set addr [expr {$addr + 4}]
}
wait_not_busy
mww 0x40022010 0x00000080;  # re-lock

# Verify
set addr 0x08000000
set ok 1
for {set i 0} {$i < $words} {incr i} {
    set r [mrw $addr]
    set b0 [scan [string index $data [expr {$i*4}]] %c]
    set b1 [scan [string index $data [expr {$i*4+1}]] %c]
    set b2 [scan [string index $data [expr {$i*4+2}]] %c]
    set b3 [scan [string index $data [expr {$i*4+3}]] %c]
    set val [expr {$b0 | ($b1 << 8) | ($b2 << 16) | ($b3 << 24)}]
    if {$r != $val} {
        puts "VERIFY FAIL at 0x[format %08x $addr]: read 0x[format %08x $r] want 0x[format %08x $val]"
        set ok 0
        break
    }
    set addr [expr {$addr + 4}]
}
if {$ok} {
    puts "VERIFY OK: $words words programmed"
}

poll on
reset run
exit
