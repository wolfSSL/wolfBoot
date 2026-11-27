# Program a VEK385 boot PDI over JTAG (boot mode must be JTAG, SW1 = 0000).
# Usage: xsdb program_jtag.tcl <boot.pdi>
# The PDI carries TF-A and wolfBoot, so there is no separate ELF
# download. Prints PROGRAM_JTAG_OK.

if {$argc < 1} {
    puts stderr "usage: xsdb program_jtag.tcl <boot.pdi>"
    exit 2
}
set pdi [lindex $argv 0]
if {![file exists $pdi]} {
    puts stderr "ERROR: file not found: $pdi"
    exit 2
}

connect
puts "JTAG targets:"
puts [targets]

# A running PLM rejects a full boot PDI, so re-run the BootROM first.
targets -set -nocase -filter {name =~ "*A78*#0.0"}
puts "System reset ..."
rst -system
after 3000

targets -set -nocase -filter {name =~ "Versal*"}
puts "Programming PDI: $pdi"
if {[catch {device program $pdi} err]} {
    puts stderr "ERROR: device program failed: $err"
    catch {puts stderr "PLM log:\n[plm log]"}
    exit 3
}
# Give the PLM time to load TF-A and wolfBoot.
after 3000

puts "JTAG targets after boot:"
puts [targets]
puts "PLM log:"
catch {puts [plm log]}
puts "PROGRAM_JTAG_OK"
disconnect
exit 0
