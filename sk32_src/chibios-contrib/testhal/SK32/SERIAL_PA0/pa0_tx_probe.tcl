# SK32F0xx USART1 TX PA0 probe (read-only pin sampling).
# The running firmware already mapped PA0/PA1 to AF10 and enabled USART1, so
# only USART1 register writes are performed (they are known to be safe).
# A slow BRR is programmed so one byte takes ~8.3ms; while bytes are being
# shifted out the SR TXE flag is read as an internal "transmission active"
# control, and the PA0 pad level is sampled to see whether it toggles.

proc rd32 {a} {
    set s [mdw $a]
    if {[regexp {\s0x([0-9a-fA-F]+)\s*$} $s -> h]} {
        return [expr 0x$h]
    }
    return 0
}

proc wr32 {a v} {
    mww $a $v
}

# ---- bring the system up ----
init
echo "STEP init ok"
halt
echo "STEP halt ok"

# USART1: UE+TE+RE, interrupts disabled (same value the firmware uses).
wr32 0x4001380C 0x200C
echo "STEP cr1 ok"
# Very slow baud: BRR = fck/(16*baud) in 12.4 form.  BRR 0xEA60 (DIV 3750)
# gives ~1200 baud at 72MHz, so each byte lasts about 8.3 ms.
wr32 0x40013808 0xEA60
echo "STEP brr ok"

set low 0
set high 0
set txact 0
set n 0
for {set i 0} {$i < 600} {incr i} {
    # Sample the PA0 pad level (GPIOA IDR bit0).
    set idr [rd32 0x48000010]
    if {$idr & 1} {
        incr high
    } else {
        incr low
    }
    set sr [rd32 0x40013800]
    if {($sr & 0x80) == 0} {
        incr txact
    }
    # Feed the next byte whenever TXE is set.
    if {$sr & 0x80} {
        wr32 0x40013804 0x55
    }
    incr n
}
echo "RESULT PA0 low=$low high=$high tx_active_samples=$txact total=$n"

echo "DONE"
reset run
shutdown
