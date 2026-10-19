; ---------------------------------------------------------------------------
; Hand written framebuffer primitives for the wall renderer.
;
; DrawWallSegment calls three C leaves for every screen column - a vertical
; line plus two black cap pixels - and they came to ~8% of the frame: SDCC
; reloads screenBuffer and pageOffsets on each call, indexes its mask tables
; through RAM and pays a prologue every time.
;
; Platform_DrawWallColumn does all three in one call, computing the page
; pointer once.  Semantics are bit-for-bit those of the C originals, including
; every clipping corner case; tests/golden.txt proves the pixels are identical.
;
; ABI: SDCC __sdcccall(1).  f(u8) -> A;  f(u8,u16) -> A, DE.  IX and IY are
; never touched; A/BC/DE/HL are free.
; ---------------------------------------------------------------------------
        .module prims

        .globl  _Platform_PutPixelBlack
        .globl  _Platform_DrawWallColumn
        .globl  _wallPattern
        .globl  _screenBuffer

DISPLAY_WIDTH   =  104
DISPLAY_HEIGHT  =  64

        .area   _DATA
_wallPattern::
        .ds     1
wcX:                            ; x of the column being drawn
        .ds     1
wcY0:                           ; y0/y1 after clipping to 0..63
        .ds     1
wcY1:
        .ds     1
wcCapY0:                        ; y0/y1 as handed in, for the cap pixels
        .ds     1
wcCapY1:
        .ds     1
wcP1:                           ; pointer to the last page of the line
        .ds     2

        .area   _CODE

; screenBuffer + page * DISPLAY_WIDTH, indexed by (y & 0x38) >> 2
pageTab:
        .dw     _screenBuffer + 0 * DISPLAY_WIDTH
        .dw     _screenBuffer + 1 * DISPLAY_WIDTH
        .dw     _screenBuffer + 2 * DISPLAY_WIDTH
        .dw     _screenBuffer + 3 * DISPLAY_WIDTH
        .dw     _screenBuffer + 4 * DISPLAY_WIDTH
        .dw     _screenBuffer + 5 * DISPLAY_WIDTH
        .dw     _screenBuffer + 6 * DISPLAY_WIDTH
        .dw     _screenBuffer + 7 * DISPLAY_WIDTH

maskTabC:                       ; ~(1 << (y & 7))
        .db     0xfe,0xfd,0xfb,0xf7,0xef,0xdf,0xbf,0x7f
topMask:
        .db     0xff,0xfe,0xfc,0xf8,0xf0,0xe0,0xc0,0x80
bottomMask:
        .db     0x01,0x03,0x07,0x0f,0x1f,0x3f,0x7f,0xff

; ---------------------------------------------------------------------------
; void Platform_PutPixelBlack(u8 x, u8 y)        A = x, L = y
;
; Platform_PutPixel(x, y, COLOUR_BLACK).  x >= 104 or y >= 64 does nothing -
; a negative s8 passed as u8 is >= 64 and is dropped, exactly as in C.
; ---------------------------------------------------------------------------
_Platform_PutPixelBlack::
        cp      #DISPLAY_WIDTH
        ret     NC
        ld      c, a                    ; C = x
        ld      a, l
        cp      #DISPLAY_HEIGHT
        ret     NC
        ld      b, a                    ; B = y
        and     #0x38
        rrca
        rrca                            ; (y >> 3) * 2
        ld      l, a
        ld      h, #0
        ld      de, #pageTab
        add     hl, de
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a                    ; HL = screenBuffer + page row
        ld      a, c
        add     a, l
        ld      l, a
        ld      a, #0
        adc     a, h
        ld      h, a                    ; HL = byte holding the pixel
        ld      a, b
        and     #7
        ld      e, a
        ld      d, #0
        push    hl
        ld      hl, #maskTabC
        add     hl, de
        ld      a, (hl)                 ; ~(1 << (y & 7))
        pop     hl
        and     (hl)
        ld      (hl), a
        ret

; ---------------------------------------------------------------------------
; void Platform_DrawWallColumn(u8 x, u16 y0y1)   A = x, E = y0, D = y1
;
; Equivalent to:
;     Platform_DrawVLine(x, y0, y1, wallPattern);
;     Platform_PutPixel(x, (u8)y1, COLOUR_BLACK);
;     Platform_PutPixel(x, (u8)y0, COLOUR_BLACK);
; with y0/y1 signed and possibly off-screen.
; ---------------------------------------------------------------------------
_Platform_DrawWallColumn::
        cp      #DISPLAY_WIDTH
        ret     NC
        ld      (wcX), a
        ld      a, e
        ld      (wcCapY0), a
        ld      a, d
        ld      (wcCapY1), a

        ; ---- the three early-outs of Platform_DrawVLine --------------------
        bit     7, d
        jr      NZ, wcCaps              ; y1 < 0
        ld      a, e
        xor     #0x80
        ld      b, a
        ld      a, d
        xor     #0x80
        cp      b
        jr      C, wcCaps               ; y1 < y0 (signed)
        ld      a, e
        bit     7, a
        jr      NZ, wcY0Neg
        cp      #DISPLAY_HEIGHT
        jr      NC, wcCaps              ; y0 > 63
        jr      wcHaveY0
wcY0Neg:
        xor     a                       ; y0 < 0 -> 0
wcHaveY0:
        ld      (wcY0), a
        ld      a, d
        cp      #DISPLAY_HEIGHT
        jr      C, wcHaveY1
        ld      a, #DISPLAY_HEIGHT - 1  ; y1 > 63 -> 63
wcHaveY1:
        ld      (wcY1), a

        ; ---- pointer to the last page --------------------------------------
        call    wcRowPtr                ; A = wcY1 already in A
        ld      (wcP1), hl

        ; ---- pointer to the first page -------------------------------------
        ld      a, (wcY0)
        call    wcRowPtr                ; HL = page0

        ld      de, (wcP1)
        ld      a, l
        cp      e
        jr      NZ, wcMulti
        ld      a, h
        cp      d
        jr      NZ, wcMulti

        ; ---- single page: mask = topMask[y0&7] & bottomMask[y1&7] ----------
        call    wcTopMask               ; A = top mask
        ld      c, a
        call    wcBottomMask            ; A = bottom mask
        and     c
        ld      b, a                    ; B = mask
        cpl
        and     (hl)
        ld      c, a                    ; C = *p & ~mask
        ld      a, (_wallPattern)
        and     b
        or      c
        ld      (hl), a
        jr      wcCaps

wcMulti:
        ; ---- first (partial) page ------------------------------------------
        call    wcTopMask
        ld      b, a
        cpl
        and     (hl)
        ld      c, a
        ld      a, (_wallPattern)
        and     b
        or      c
        ld      (hl), a

        ; ---- whole pages in between ----------------------------------------
        ld      a, (_wallPattern)
        ld      c, a                    ; C = pattern
wcMidLoop:
        ld      a, l
        add     a, #DISPLAY_WIDTH
        ld      l, a
        jr      NC, wcNoCarry
        inc     h
wcNoCarry:
        ld      a, l
        cp      e
        jr      NZ, wcMidFill
        ld      a, h
        cp      d
        jr      Z, wcLastPage
wcMidFill:
        ld      (hl), c
        jr      wcMidLoop

        ; ---- last (partial) page -------------------------------------------
wcLastPage:
        call    wcBottomMask
        ld      b, a
        cpl
        and     (hl)
        ld      c, a
        ld      a, (_wallPattern)
        and     b
        or      c
        ld      (hl), a

        ; ---- the two black caps, on the *unclipped* coordinates ------------
wcCaps:
        ld      a, (wcCapY1)
        ld      l, a
        ld      a, (wcX)
        call    _Platform_PutPixelBlack
        ld      a, (wcCapY0)
        ld      l, a
        ld      a, (wcX)
        jp      _Platform_PutPixelBlack

; A = y (0..63) -> HL = screenBuffer + (y >> 3) * DISPLAY_WIDTH + wcX
; clobbers A, BC, DE
wcRowPtr:
        and     #0x38
        rrca
        rrca
        ld      c, a
        ld      b, #0
        ld      hl, #pageTab
        add     hl, bc
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a
        ld      a, (wcX)
        ld      c, a
        add     hl, bc                  ; B is still 0
        ret

; A = topMask[wcY0 & 7], preserves HL/DE
wcTopMask:
        push    hl
        ld      a, (wcY0)
        and     #7
        ld      l, a
        ld      h, #0
        push    de
        ld      de, #topMask
        add     hl, de
        pop     de
        ld      a, (hl)
        pop     hl
        ret

; A = bottomMask[wcY1 & 7], preserves HL/DE
wcBottomMask:
        push    hl
        ld      a, (wcY1)
        and     #7
        ld      l, a
        ld      h, #0
        push    de
        ld      de, #bottomMask
        add     hl, de
        pop     de
        ld      a, (hl)
        pop     hl
        ret
