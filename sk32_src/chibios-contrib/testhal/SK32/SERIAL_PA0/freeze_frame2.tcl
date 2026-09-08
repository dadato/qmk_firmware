# freeze_frame2.tcl - correct-offset dump of USBDriver + LLD EP0 statics (QMK onekey sk32f077)
proc mrb3 {reg} { set v ""; mem2array v 8 $reg 1; return $v(0) }
proc mrw3 {reg} { set v ""; mem2array v 32 $reg 1; return $v(0) }

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

set U 0x20001590
puts "-- USBD1 @0x20001590 --"
puts [format "state=%d config=0x%08X tx=0x%04X rx=0x%04X" \
      [mrw3 $U] [mrw3 [expr {$U+4}]] \
      [expr {([mrb3 [expr {$U+8}]] | ([mrb3 [expr {$U+9}]]<<8)) & 0xFFFF}] \
      [expr {([mrb3 [expr {$U+0xA}]] | ([mrb3 [expr {$U+0xB}]]<<8)) & 0xFFFF}]]
puts [format "epc0=0x%08X ep0state=0x%02X ep0next=0x%08X ep0n=%d ep0endcb=0x%08X" \
      [mrw3 [expr {$U+0x0C}]] [mrb3 [expr {$U+0x70}]] \
      [mrw3 [expr {$U+0x74}]] [mrw3 [expr {$U+0x78}]] [mrw3 [expr {$U+0x7C}]]]
puts [format "status=0x%04X address=0x%02X configuration=0x%02X saved_state=%d" \
      [expr {([mrb3 [expr {$U+0x88}]] | ([mrb3 [expr {$U+0x89}]]<<8)) & 0xFFFF}] \
      [mrb3 [expr {$U+0x8A}]] [mrb3 [expr {$U+0x8B}]] [mrw3 [expr {$U+0x8C}]]]
set s ""
for {set i 0} {$i < 8} {incr i} { append s [format " %02X" [mrb3 [expr {$U+0x80+$i}]]] }
puts "usbp->setup(parsed):$s"

puts "-- LLD statics --"
puts [format "ep0_setup_pending=%d ep0_out_status_pending=%d" \
      [mrb3 0x20001389] [mrb3 0x20001388]]
set s ""
for {set i 0} {$i < 8} {incr i} { append s [format " %02X" [mrb3 [expr {0x2000138A+$i}]]] }
puts "ep0setup_buffer:$s"
# ep0_state union @0x20001394 (USBIn/OutEndpointState, USB_USE_WAIT=FALSE layout)
puts [format "ep0_state.in: txsize=%d txcnt=%d txbuf=0x%08X txlast=%d" \
      [mrw3 0x20001394] [mrw3 0x20001398] [mrw3 0x2000139C] [mrw3 0x200013A0]]
puts [format "ep0_state.out: rxsize=%d rxcnt=%d rxbuf=0x%08X rxpkts=%d" \
      [mrw3 0x20001394] [mrw3 0x20001398] [mrw3 0x2000139C] [mrw3 0x200013A0]]

puts "-- core --"
puts [format "PC=0x%08X LR=0x%08X SP=0x%08X" [reg pc] [expr {[reg lr]&0xFFFFFFFE}] [reg sp]]

resume
shutdown
