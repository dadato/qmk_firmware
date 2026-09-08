# Read-only pad scan while PINTX continuously shifts 0x55 on USART1.
# Uses mem2array-backed mrw (returns a value) instead of parsing mdw output.
# GPIOA IDR @0x48000010 (PA0, PA1, PA9, PA10), GPIOB IDR @0x48000410 (PB5, PB6).

init
echo "STEP init ok"

set pa0 0
set pa1 0
set pa9 0
set pa10 0
set pb5 0
set pb6 0
set n 0
for {set i 0} {$i < 500} {incr i} {
    set idra [mrw 0x48000010]
    set idrb [mrw 0x48000410]
    if {($idra & 0x1) == 0}   { incr pa0 }
    if {($idra & 0x2) == 0}   { incr pa1 }
    if {($idra & 0x200) == 0} { incr pa9 }
    if {($idra & 0x400) == 0} { incr pa10 }
    if {($idrb & 0x20) == 0}  { incr pb5 }
    if {($idrb & 0x40) == 0}  { incr pb6 }
    incr n
}
echo "RESULT samples=$n low_counts: PA0=$pa0 PA1=$pa1 PA9=$pa9 PA10=$pa10 PB5=$pb5 PB6=$pb6"
echo "DONE"
shutdown
