set architecture i386:x86-64
target remote localhost:1234
break *0x7c00
break *0x7e00
continue