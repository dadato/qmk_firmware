# usb_probe.tcl - snapshot SK32 USB subsystem state while QMK firmware runs
proc mrb2 {reg} {
    set v ""
    mem2array v 8 $reg 1
    return $v(0)
}
proc mrw2 {reg} {
    set v ""
    mem2array v 32 $reg 1
    return $v(0)
}

halt

puts "-- clocks/power --"
set cr  [mrw2 0x40021000]
set cr2 [mrw2 0x40021034]
set cf3 [mrw2 0x40021030]
set pwr [mrw2 0x40007000]
puts [format "RCC_CR   =0x%08X" $cr]
puts [format "RCC_CR2  =0x%08X (PLL48ON=bit16 PLL48RDY=bit17)" $cr2]
puts [format "RCC_CFGR3=0x%08X (USBSW=bit7, 0=PLL48->USB)" $cf3]
puts [format "PWR_CR   =0x%08X (LDO_EN=bit16 LDO_SEL=bit17 VS=bits19:18)" $pwr]

puts "-- nvic/usb --"
set iser [mrw2 0xE000E100]
puts [format "NVIC_ISER0=0x%08X (bit31=USB IRQ31)" $iser]

set base 0x40005C00
set faddr  [mrb2 [expr {$base + 0x00}]]
set power  [mrb2 [expr {$base + 0x01}]]
set iin1   [mrb2 [expr {$base + 0x02}]]
set iout1  [mrb2 [expr {$base + 0x04}]]
set iusb   [mrb2 [expr {$base + 0x06}]]
set iin1e  [mrb2 [expr {$base + 0x07}]]
set iusbe  [mrb2 [expr {$base + 0x0B}]]
set index  [mrb2 [expr {$base + 0x0E}]]
set pull   [mrb2 [expr {$base + 0x38}]]
puts [format "USB FADDR=0x%02X POWER=0x%02X PULL=0x%02X" $faddr $power $pull]
puts [format "USB INTRIN1=0x%02X INTROUT1=0x%02X INTRUSB=0x%02X" $iin1 $iout1 $iusb]
puts [format "USB INTRIN1E=0x%02X INTRUSBE=0x%02X INDEX=0x%02X" $iin1e $iusbe $index]

# EP0 window CSR at offset 0x11 (select INDEX=0 first)
set old_index [mrb2 [expr {$base + 0x0E}]]
mwb [expr {$base + 0x0E}] 0
set csr0 [mrb2 [expr {$base + 0x11}]]
mwb [expr {$base + 0x0E}] $old_index
puts [format "EP0 CSR=0x%02X (OPKTRD=01 IPKTRD=02 SENTST=04 DATEND=08 SETUPEND=10 SENDST=20)" $csr0]

resume
shutdown
