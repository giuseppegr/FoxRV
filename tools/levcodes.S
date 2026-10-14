# LEVCODES.COM - prints the Titus the Fox level codes for the machine it runs on.
#
# The game does not store level codes: FOX.COM computes them at run time (image 0x6782)
# from the BIOS ROM (segment F000) and the CPU class (detected at image 0x129F).
# This program repeats both routines instruction for instruction, so it must run on the
# same machine / DOSBox configuration (CPU type, BIOS) as the game.
#
# Build (GNU binutils):  as --32 -o levcodes.o levcodes.S
#                        ld -m i386pe -Ttext 0x100 --image-base 0 -e _start -o levcodes.pe levcodes.o
#                        objcopy -O binary -j .text levcodes.pe levcodes.com
        .code16
        .intel_syntax noprefix
        .text
        .global _start
_start:
        mov     dx, offset banner
        call    puts
        call    detect_cpu

        mov     dx, offset cpu_msg              # "CPU class: n   BIOS date: mm/dd/yy"
        call    puts
        mov     al, byte ptr [cpu]
        add     al, '0'
        call    putc
        mov     dx, offset date_msg
        call    puts
        push    ds
        mov     ax, 0xF000
        mov     ds, ax
        mov     si, 0xFFF5
        mov     cx, 8
date:   lodsb
        call    putc
        loop    date
        pop     ds
        mov     dx, offset header
        call    puts

        mov     si, offset levels               # byte slot, then '$'-terminated name
        mov     bp, 1
next:   lodsb
        cmp     al, 0xFF
        je      done
        mov     bl, al
        xor     bh, bh
        mov     ax, bp
        call    print_dec2
        mov     dx, offset sep
        call    puts
        call    level_code
        call    print_hex4
        mov     dx, offset sep
        call    puts
        mov     dx, si
        call    puts
skip:   lodsb
        cmp     al, '$'
        jne     skip
        mov     dx, offset crlf
        call    puts
        inc     bp
        jmp     next
done:   mov     ax, 0x4C00
        int     0x21

# --- CPU class exactly as the game decides it: 0 = 8086/8088, 2 = 80286, 3 = 386+ ---
detect_cpu:
        mov     al, 1
        mov     cl, 0x20
        shl     al, cl                          # 8086 does not mask the count -> 0
        cmp     al, 1
        je      not8086
        mov     byte ptr [cpu], 0
        ret
not8086:
        push    es
        xor     ax, ax
        mov     es, ax
        mov     ax, word ptr es:[0x18]          # save INT 6 (invalid opcode)
        mov     word ptr [old6], ax
        mov     ax, word ptr es:[0x1A]
        mov     word ptr [old6 + 2], ax
        cli
        mov     word ptr es:[0x18], offset int6
        mov     word ptr es:[0x1A], cs
        sti
        mov     byte ptr [cpu], 3
        .byte   0x66                            # xor eax, eax: faults on a 286
        xor     ax, ax
        cli
        mov     ax, word ptr [old6]
        mov     word ptr es:[0x18], ax
        mov     ax, word ptr [old6 + 2]
        mov     word ptr es:[0x1A], ax
        sti
        pop     es
        ret
int6:   push    bp                              # 286: skip the 3-byte instruction
        mov     bp, sp
        add     word ptr [bp + 2], 3
        pop     bp
        mov     byte ptr cs:[cpu], 2
        iret

# --- the game's code routine (image 0x6782): in bx = level file slot, out ax = code ---
level_code:
        push    bx
        push    cx
        push    si
        push    es
        mov     ax, 0xF000
        mov     es, ax
        mov     si, 0xFFF0
        mov     ax, word ptr es:[bx]
        mov     cl, byte ptr [cpu]
        add     al, byte ptr es:[bx - 0x1000]
        sub     ah, byte ptr es:[bx - 0x0FFE]
        ror     ax, cl
        mov     cx, 8
sum:    adc     ax, word ptr es:[si]
        inc     si
        inc     si
        loop    sum
        mov     cl, bl
        rol     ax, cl
        pop     es
        pop     si
        pop     cx
        pop     bx
        ret

# --- output helpers ---
puts:   push    ax                              # dx -> '$'-terminated string
        mov     ah, 9
        int     0x21
        pop     ax
        ret
putc:   push    ax                              # al = character
        push    dx
        mov     dl, al
        mov     ah, 2
        int     0x21
        pop     dx
        pop     ax
        ret
print_dec2:                                     # ax = 0..99, two digits
        push    ax
        mov     dl, 10
        div     dl
        add     ax, 0x3030
        push    ax
        call    putc
        pop     ax
        mov     al, ah
        call    putc
        pop     ax
        ret
print_hex4:                                     # like the game's routine at image 0x665
        push    ax
        push    cx
        push    dx
        mov     dx, ax
        mov     cx, 4
hex:    xor     ax, ax
        shl     dx, 1
        rcl     ax, 1
        shl     dx, 1
        rcl     ax, 1
        shl     dx, 1
        rcl     ax, 1
        shl     dx, 1
        rcl     ax, 1
        cmp     al, 9
        jbe     digit
        add     al, 7
digit:  add     al, '0'
        call    putc
        loop    hex
        pop     dx
        pop     cx
        pop     ax
        ret

# --- data ---
cpu:      .byte 0
old6:     .word 0, 0
banner:   .ascii "Titus the Fox - level codes for this machine\r\n$"
cpu_msg:  .ascii "CPU class: $"
date_msg: .ascii "   BIOS date: $"
header:   .ascii "\r\n\r\nLv  Code  Name\r\n$"
sep:      .ascii "  $"
crlf:     .ascii "\r\n$"
# play order: file slot ('0'-'9' = 0-9, 'A'-'J' = 10-19), name
levels:
        .byte 0
        .ascii "ON THE FOXY TRAIL$"
        .byte 19
        .ascii "LOOKING FOR CLUES$"
        .byte 1
        .ascii "ROAD WORKS AHEAD$"
        .byte 2
        .ascii "GOING UNDERGROUND$"
        .byte 3
        .ascii "FLAMING CATACOMBES$"
        .byte 4
        .ascii "COMING TO TOWN$"
        .byte 5
        .ascii "FOXYS DEN$"
        .byte 6
        .ascii "ON THE ROAD TO MARRAKESH$"
        .byte 7
        .ascii "HOME OF THE PHARAOHS$"
        .byte 8
        .ascii "DESERT EXPERIENCE$"
        .byte 9
        .ascii "WALLS OF SAND$"
        .byte 11
        .ascii "A BEACON OF HOPE$"
        .byte 12
        .ascii "A PIPE DREAM$"
        .byte 14
        .ascii "GOING HOME$"
        .byte 16
        .ascii "JUST MARRIED$"
        .byte 0xFF
