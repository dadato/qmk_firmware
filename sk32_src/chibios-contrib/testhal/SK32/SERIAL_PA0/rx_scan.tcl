# rx_scan.tcl - Determine which of PA0/PA1 is being driven (toggled) while
# the host COM port transmits a 0x00 byte stream at 921600.
#   low_PA0 / low_PA1 : number of samples where the pin read LOW
#   RXNE_set_count    : number of samples where USART1 RXNE is set
#                       (RXNE set == silicon really received a byte)
proc mrw2 {reg} {
    set v ""
    mem2array v 32 $reg 1
    return $v(0)
}

halt

set gidr 0x48000010      ;# GPIOA IDR
set usr  0x40013800      ;# USART1 SR
set udr  0x40013804      ;# USART1 DR

set n 400
set low0 0
set low1 0
set rxne 0

for {set i 0} {$i < $n} {incr i} {
    set idr [mrw2 $gidr]
    if {($idr & 0x00000001) == 0} { incr low0 }
    if {($idr & 0x00000002) == 0} { incr low1 }
    set sr [mrw2 $usr]
    if {($sr & 0x00000020) != 0} { incr rxne }
}

set lastdr -1
if {$rxne > 0} { set lastdr [mrw2 $udr] }

puts "scan_samples=$n low_PA0=$low0 low_PA1=$low1 USART1_RXNE_set_count=$rxne last_DR=0x[format %08X $lastdr]"

resume
shutdown
