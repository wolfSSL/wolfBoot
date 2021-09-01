with open("image_v2_signed.bin", "rb+") as f:
    contents = f.read()
    pos = contents.find(b'\x20\x00\x40\x00') + 0x04
    new_contents = contents[:pos]
    sig_sz = 0x40
    new_sig = bytes([i for i in range(2, sig_sz+2)])
    new_contents += new_sig
    new_contents += contents[pos+sig_sz:]
    f.seek(0)
    f.write(new_contents)
    f.close()
