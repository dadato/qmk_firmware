# scan_all.tcl - locate which GPIO pin the host COM TX (0x00 stream) drives.
# Counts how many samples each pin reads LOW. Also reports USART1 RXNE.
proc mrw2 {reg} {
    set v ""
    mem2array v 32 $reg 1
    return $v(0)
}

halt

set a_idr 0x48000010   ;# GPIOA IDR
set b_idr 0x48000410   ;# GPIOB IDR
set c_idr 0x48000810   ;# GPIOC IDR
set usr   0x40013800   ;# USART1 SR

set n 300
set cnt [dict create]
foreach {name} {A B C} {
    for {set b 0} {$b < 16} {incr b} {
        dict set cnt "$name$b" 0
    }
}
set rxne 0

for {set i 0} {$i < $n} {incr i} {
    set a [mrw2 $a_idr]
    set b [mrw2 $b_idr]
    set c [mrw2 $c_idr]
    for {set bit 0} {$bit < 16} {incr bit} {
        if {($a & (1 << $bit)) == 0} { dict incr cnt "A$bit" }
        if {($b & (1 << $bit)) == 0} { dict incr cnt "B$bit" }
        if {($c & (1 << $bit)) == 0} { dict incr cnt "C$bit" }
    }
    set s [mrw2 $usr]
    if {($s & 0x00000020) != 0} { incr rxne }
}

puts "samples=$n USART1_RXNE=$rxne"
foreach {k v} $cnt {
    if {$v > 0} { puts "LOW $k = $v" }
}
resume
shutdown
