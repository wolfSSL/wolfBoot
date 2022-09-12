if [ "$2" = "dual" ]; then
    org_primary=0xffc10000
    org_update=0xffe10000
    echo === Dual Bank mode === 
else
    if [ "$2" = "tsip" ]; then
    org_primary=0xffc50000
    org_update=0xffe10000
    echo === Linear mode with TSIP ===
    else
    org_primary=0xffc20000
    org_update=0xffe00000
    echo === Linear mode ===
    fi
fi

echo "Primary:" $org_primary
echo "Update: " $org_update

cd wolfBoot/HardwareDebug
rx-elf-objcopy.exe -O binary -R '$ADDR_C_FE7F5D00' -R '$ADDR_C_FE7F5D10' -R '$ADDR_C_FE7F5D20' -R '$ADDR_C_FE7F5D30' -R '$ADDR_C_FE7F5D40' -R '$ADDR_C_FE7F5D48' -R '$ADDR_C_FE7F5D50' -R '$ADDR_C_FE7F5D64'  -R '$ADDR_C_FE7F5D70' -R EXCEPTVECT -R RESETVECT wolfBoot.x wolfBoot.bin
rx-elf-objcopy.exe -I binary -O srec --change-addresses=0xffc00000 wolfBoot.bin ../../download/wolfBoot_0.hex
rx-elf-objcopy.exe -I binary -O srec --change-addresses=0xffe00000 wolfBoot.bin ../../download/wolfBoot_1.hex

cd ../..
cd $1/HardwareDebug
rx-elf-objcopy.exe -O binary -R '$ADDR_C_FE7F5D00' -R '$ADDR_C_FE7F5D10' -R '$ADDR_C_FE7F5D20' -R '$ADDR_C_FE7F5D30' -R '$ADDR_C_FE7F5D40' -R '$ADDR_C_FE7F5D48' -R '$ADDR_C_FE7F5D50' -R '$ADDR_C_FE7F5D64'  -R '$ADDR_C_FE7F5D70' -R EXCEPTVECT -R RESETVECT $1.x $1.bin
sign --rsa2048 $1.bin ../../../../../pri-rsa2048.der 1.0
rx-elf-objcopy.exe -I binary -O srec --change-addresses=$org_primary $1_v1.0_signed.bin ../../download/$1_v1.0_signed.hex
sign --rsa2048 $1.bin ../../../../../pri-rsa2048.der 2.0
rx-elf-objcopy.exe -I binary -O srec --change-addresses=$org_update $1_v2.0_signed.bin ../../download/$1_v2.0_signed.hex
cd ../..
