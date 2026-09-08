# freeze_frame.tcl - dump ChibiOS USBDriver + EP0 state while QMK firmware halted
proc mrb3 {reg} {
    set v ""
    mem2array v 8 $reg 1
    return $v(0)
}
proc mrw3 {reg} {
    set v ""
    mem2array v 32 $reg 1
    return $v(0)
}

halt

set base 0x40005C00
puts "-- usb regs --"
puts [format "FADDR=0x%02X POWER=0x%02X PULL=0x%02X INTRUSB=0x%02X INTRIN1=0x%02X INTRUSBE=0x%02X" \
      [mrb3 [expr {$base+0x00}]] [mrb3 [expr {$base+0x01}]] [mrb3 [expr {$base+0x38}]] \
      [mrb3 [expr {$base+0x06}]] [mrb3 [expr {$base+0x02}]] [mrb3 [expr {$base+0x0B}]]]
set oi [mrb3 [expr {$base+0x0E}]]
mwb [expr {$base+0x0E}] 0
puts [format "EP0 CSR=0x%02X COUNT=%d" [mrb3 [expr {$base+0x11}]] [mrb3 [expr {$base+0x16}]]]
mwb [expr {$base+0x0E}] $oi

puts "-- USBD1 @0x20001590 --"
set st  [mrw3 0x20001590]
set cfg [mrw3 0x20001594]
set epc0 [mrw3 0x2000159C]
set es  [mrb3 0x20001600]
set ep0next [mrw3 0x20001604]
set ep0n [mrw3 0x20001608]
set endcb [mrw3 0x2000160C]
puts [format "state=%d config=0x%08X epc0=0x%08X" $st $cfg $epc0]
puts [format "ep0state=%d ep0next=0x%08X ep0n=%d ep0endcb=0x%08X" $es $ep0next $ep0n $endcb]
puts [format "transmitting=0x%04X receiving=0x%04X addr=0x%02X cfgsel=%d" \
      [expr {([mrb3 0x20001598] | ([mrb3 0x20001599]<<8)) & 0xFFFF}] \
      [expr {([mrb3 0x2000159A] | ([mrb3 0x2000159B]<<8)) & 0xFFFF}] \
      [mrb3 0x2000161A] [mrb3 0x2000161B]]

puts "-- usbp->setup[8] (last SETUP consumed by state machine) @0x20001610 --"
set sb [expr {0x20001590 + 0x80}]
set s  ""
for {set i 0} {$i < 8} {incr i} {
    append s [format " %02X" [mrb3 [expr {$sb+$i}]]]
}
puts "setup:$s"

puts "-- ep0_state @0x20001394 (IN view) --"
set txs  [mrw3 0x20001394]
set txc  [mrw3 0x20001398]
set txb  [mrw3 0x2000139C]
set txl  [mrw3 0x200013A4]
puts [format "txsize=%d txcnt=%d txbuf=0x%08X txlast=%d" $txs $txc $txb $txl]

puts "-- core regs --"
puts [format "PC=0x%08X LR=0x%08X" [reg pc] [expr {[reg lr] & 0xFFFFFFFE}]]

resume
shutdown
