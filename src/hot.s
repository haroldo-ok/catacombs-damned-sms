; ---------------------------------------------------------------------------
; Hand written hot paths that are not framebuffer primitives.
;
;   Map_GetCell / Map_GetCellSafe / Map_IsSolid / Map_IsBlocked
;       DrawCell alone asks "is this neighbour solid?" ~600 times a frame and
;       the C chain Map_IsSolid -> Map_GetCellSafe cost ~490 cycles a go.
;
;   isFrustrumClipped
;       runs for all 256 cells of the draw window every frame.
;
; Semantics are identical to the C they replace (tests/golden.txt compares the
; rendered pixels, tests/playtest.txt the game state).
;
; ABI: SDCC __sdcccall(1).  f(u8) -> A, f(u8,u8) -> A and L, u8 result in A.
; IX/IY are never touched.
; ---------------------------------------------------------------------------
        .module hot

        .globl  _Map_GetCell
        .globl  _Map_GetCellSafe
        .globl  _Map_IsSolid
        .globl  _Map_IsBlocked
        .globl  _isFrustrumClipped

        .globl  _mapLevel
        .globl  _camera
        .globl  _clipCosTab
        .globl  _clipSinTab

MAP_WIDTH       = 32
MAP_HEIGHT      = 24
CELL_BRICKWALL  = 13            ; CellType_FirstSolidCell
CELL_URN        = 9             ; CellType_FirstCollidableCell

CAM_CELLX       = 13            ; byte offsets into Camera (src/draw.h)
CAM_CELLY       = 14

        .area   _DATA
icDx:   .ds     1
icDy:   .ds     1

        .area   _CODE

; ---------------------------------------------------------------------------
; CellType Map_GetCell(u8 x, u8 y)             A = x, L = y   -> A = cell
;
; No bounds check, exactly like the C: the generator really does pass x = 255
; and the index is y * MAP_WIDTH + x folded to (y << 4) + (x >> 1).
; ---------------------------------------------------------------------------
_Map_GetCell::
        ld      c, a                    ; C = x
        ld      h, #0
        add     hl, hl
        add     hl, hl
        add     hl, hl
        add     hl, hl                  ; HL = y * 16
        srl     a                       ; A = x >> 1
        ld      e, a
        ld      d, #0
        add     hl, de
        ld      de, #_mapLevel
        add     hl, de
        ld      a, (hl)
        bit     0, c
        jr      Z, 1$
        rrca
        rrca
        rrca
        rrca
1$:
        and     #0x0f
        ret

; ---------------------------------------------------------------------------
; CellType Map_GetCellSafe(u8 x, u8 y)         A = x, L = y   -> A = cell
; Out of bounds reads as a brick wall.
; ---------------------------------------------------------------------------
_Map_GetCellSafe::
        cp      #MAP_WIDTH
        jr      NC, 2$
        ld      c, a
        ld      a, l
        cp      #MAP_HEIGHT
        jr      NC, 2$
        ld      a, c
        jr      _Map_GetCell
2$:
        ld      a, #CELL_BRICKWALL
        ret

; ---------------------------------------------------------------------------
; u8 Map_IsSolid(u8 x, u8 y)       -> 1 when the cell is a wall
; u8 Map_IsBlocked(u8 x, u8 y)     -> 1 when the cell stops movement
; ---------------------------------------------------------------------------
_Map_IsSolid::
        call    _Map_GetCellSafe
        cp      #CELL_BRICKWALL         ; CY = cell < 13
        ccf                             ; CY = cell >= 13
        ld      a, #0
        rla
        ret

_Map_IsBlocked::
        call    _Map_GetCellSafe
        cp      #CELL_URN
        ccf
        ld      a, #0
        rla
        ret

; ---------------------------------------------------------------------------
; u8 isFrustrumClipped(u8 x, u8 y)             A = x, L = y   -> A = 0/1
;
;   dx = x - camera.cellX + 32 ; dy = y - camera.cellY + 32   (always 1..63,
;   both coordinates are cell indices inside a 32x24 map, so the C fallback
;   path for out-of-range offsets was unreachable - the AND keeps it safe)
;   clipped if  clipCosTab[dx] - clipSinTab[dy] < -512
;           or  clipSinTab[dx] + clipCosTab[dy] < -512
; ---------------------------------------------------------------------------
_isFrustrumClipped::
        ld      c, l                    ; C = y
        ld      hl, #(_camera + CAM_CELLX)
        sub     (hl)
        add     a, #32
        and     #63
        add     a, a                    ; dx * 2 (s16 table)
        ld      (icDx), a
        ld      a, c
        ld      hl, #(_camera + CAM_CELLY)
        sub     (hl)
        add     a, #32
        and     #63
        add     a, a
        ld      (icDy), a

        ; ---- clipCosTab[dx] - clipSinTab[dy] -------------------------------
        ld      b, #0
        ld      a, (icDx)
        ld      c, a
        ld      hl, #_clipCosTab
        add     hl, bc
        ld      e, (hl)
        inc     hl
        ld      d, (hl)                 ; DE = clipCosTab[dx]
        ld      a, (icDy)
        ld      c, a
        ld      hl, #_clipSinTab
        add     hl, bc
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a                    ; HL = clipSinTab[dy]
        ex      de, hl
        or      a
        sbc     hl, de                  ; HL = cos[dx] - sin[dy]
        ld      de, #512
        add     hl, de
        bit     7, h
        jr      NZ, 3$                  ; < -512

        ; ---- clipSinTab[dx] + clipCosTab[dy] -------------------------------
        ld      b, #0
        ld      a, (icDx)
        ld      c, a
        ld      hl, #_clipSinTab
        add     hl, bc
        ld      e, (hl)
        inc     hl
        ld      d, (hl)                 ; DE = clipSinTab[dx]
        ld      a, (icDy)
        ld      c, a
        ld      hl, #_clipCosTab
        add     hl, bc
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a                    ; HL = clipCosTab[dy]
        add     hl, de
        ld      de, #512
        add     hl, de
        bit     7, h
        jr      NZ, 3$
        xor     a
        ret
3$:
        ld      a, #1
        ret

; ---------------------------------------------------------------------------
; void TransformCorner(u8 cx, u8 cy)           A = cx, L = cy
;
;     tcvZ = cornerAx[cx] - cornerBy[cy]
;     tcvX = cornerCx[cx] + cornerDy[cy]
;
; This is the old TransformCornerToViewSpace with the camera constants folded
; into the tables (draw.c) and the two out-parameters replaced by globals -
; SDCC spent ~990 cycles a call on what is four table reads and two adds.
; ---------------------------------------------------------------------------
        .globl  _TransformCorner
        .globl  _cornerAx
        .globl  _cornerBy
        .globl  _cornerCx
        .globl  _cornerDy
        .globl  _tcvX
        .globl  _tcvZ

_TransformCorner::
        add     a, a                    ; cx * 2
        ld      c, a
        ld      b, #0
        ld      a, l
        add     a, a                    ; cy * 2
        ld      e, a
        ld      d, #0

        push    de                      ; keep cy*2

        ld      hl, #_cornerAx
        add     hl, bc
        ld      a, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, a                    ; HL = cornerAx[cx]
        ex      de, hl                  ; DE = cornerAx[cx]
        pop     hl                      ; HL = cy*2
        push    hl
        push    bc                      ; keep cx*2
        ld      bc, #_cornerBy
        add     hl, bc
        ld      c, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, c                    ; HL = cornerBy[cy]
        ex      de, hl                  ; HL = Ax, DE = By
        or      a
        sbc     hl, de
        ld      (_tcvZ), hl

        pop     bc                      ; cx*2
        ld      hl, #_cornerCx
        add     hl, bc
        ld      e, (hl)
        inc     hl
        ld      d, (hl)                 ; DE = cornerCx[cx]
        pop     hl                      ; cy*2
        ld      bc, #_cornerDy
        add     hl, bc
        ld      c, (hl)
        inc     hl
        ld      h, (hl)
        ld      l, c                    ; HL = cornerDy[cy]
        add     hl, de
        ld      (_tcvX), hl
        ret

; ---------------------------------------------------------------------------
; void WallColumnLoop(void)
;
; The per-column loop of DrawWallSegment - the single hottest piece of the
; renderer (10% of all cycles on its own).  SDCC ran out of registers and put
; every loop variable in the IX frame, where each access costs 19 cycles; the
; reference C version is still in src/draw.c under WALL_COLUMN_LOOP_IN_C and
; the two must stay in step.
;
; State is passed in the _wc* globals (draw.c writes them, reads _wcClipL and
; _wcClipR back afterwards).  Register use inside the loop:
;
;       B  = high byte of &wBuffer[0]   (the buffers are page aligned)
;       C  = xi, which doubles as the low byte of &wBuffer[xi]
;       D  = dw          E = w          HL = werror
;
; so wBuffer[xi] is  (bc)  and horizonBuffer[xi] is  (b, c+0x80).
; ---------------------------------------------------------------------------
        .globl  _WallColumnLoop
        .globl  _wBuffer
        .globl  _wcXi
        .globl  _wcW
        .globl  _wcDw
        .globl  _wcWstep
        .globl  _wcWerror
        .globl  _wcDx
        .globl  _wcXEnd
        .globl  _wcBlankL
        .globl  _wcBlankR
        .globl  _wcShade
        .globl  _wcClipL
        .globl  _wcClipR
        .globl  _wcCapLim
        .globl  _wcHorizon
        .globl  _wcWbv
        .globl  _wallPattern
        .globl  _numBufferSlicesFilled
        .globl  _Platform_DrawWallColumn
        .globl  _Platform_PutPixelBlack

DISPLAY_W       = 104
HORIZ_OFF       = 0x80          ; horizonBuffer - wBuffer

_WallColumnLoop::
        ld      hl, #_wBuffer           ; L is 0 by construction (page aligned)
        ld      b, h
        ld      a, (_wcXi)
        ld      c, a                    ; BC = &wBuffer[xi]
        ld      a, (_wcW)
        ld      e, a                    ; E = w
        ld      a, (_wcDw)
        ld      d, a                    ; D = dw
        ld      hl, (_wcWerror)         ; HL = werror

; --- top of the column loop ------------------------------------------------
wc_loop:
        ld      a, c
        cp      a, #DISPLAY_W
        jp      NC, wc_exit             ; xi >= DISPLAY_WIDTH -> done

        ld      a, (bc)                 ; wbv = wBuffer[xi]
        cp      a, e                    ; carry <=> wbv < w  <=> draw this one
        jp      NC, wc_occluded

; --- the slice is in front of whatever is already in the column ------------
        ld      (_wcWbv), a             ; remember it for the "was empty" test

        push    hl                      ; horizon = horizonBuffer[xi]
        ld      h, b
        ld      a, c
        add     a, #HORIZ_OFF
        ld      l, a
        ld      a, (hl)
        pop     hl
        ld      (_wcHorizon), a

        ld      a, (_wcBlankL)          ; sliceMask
        cp      a, c
        jr      Z, wc_mask0
        ld      a, (_wcBlankR)
        cp      a, c
        jr      Z, wc_mask0
        ld      a, (_wcShade)
        or      a, a
        jr      Z, wc_maskff
        bit     0, c
        jr      NZ, wc_maskff
        ld      a, #0x55                ; shaded edge, even column
        jr      wc_setmask
wc_mask0:
        xor     a, a
        jr      wc_setmask
wc_maskff:
        ld      a, #0xff
wc_setmask:
        ld      (_wallPattern), a

        push    de                      ; dw:w
        push    bc                      ; the column pointer
        push    hl                      ; werror

        ld      a, e                    ; extent = min(w, 64)
        cp      a, #65
        jr      C, wc_ext
        ld      a, #64
wc_ext:
        ld      l, a                    ; keep extent
        ld      a, (_wcHorizon)
        add     a, l
        ld      d, a                    ; y1 = horizon + extent
        ld      a, (_wcHorizon)
        sub     a, l
        ld      e, a                    ; y0 = horizon - extent
        ld      a, c                    ; x  = xi
        call    _Platform_DrawWallColumn

        pop     hl
        pop     bc
        pop     de

        ld      a, (_wcWbv)             ; if (wbv == 0) numBufferSlicesFilled++
        or      a, a
        jr      NZ, wc_nofill
        ld      a, (_numBufferSlicesFilled)
        inc     a
        ld      (_numBufferSlicesFilled), a
wc_nofill:
        ld      a, e                    ; wBuffer[xi] = w
        ld      (bc), a

        ld      a, #33                  ; the inner loop may draw cap pixels
        ld      (_wcCapLim), a
        jr      wc_tail

; --- the column is already covered by something closer ---------------------
wc_occluded:
        xor     a, a
        ld      (_wcCapLim), a          ; no cap pixels from this column

        ld      a, (_wcClipL)
        cp      a, c
        jr      NZ, wc_notleft
        inc     a                       ; segmentClipLeft++
        ld      (_wcClipL), a
        jr      wc_tail
wc_notleft:
        ld      a, (_wcClipR)
        cp      a, c                    ; segmentClipRight - xi
        jr      C, wc_tail              ; clipRight < xi  -> nothing to do
        jr      Z, wc_tail              ; clipRight == xi -> nothing to do
        ld      a, c
        ld      (_wcClipR), a           ; segmentClipRight = xi; break
        jr      wc_exit

; --- common tail: step the Bresenham state ---------------------------------
wc_tail:
        ld      a, (_wcXEnd)
        cp      a, c
        jr      Z, wc_exit              ; xi == xEnd -> break

        ld      a, l                    ; werror -= dw
        sub     a, d
        ld      l, a
        ld      a, h
        sbc     a, #0
        ld      h, a
        bit     7, h
        jr      Z, wc_next              ; werror >= 0 -> no height step

        ld      a, e                    ; move w into B and dx into DE for
        push    de                      ; the inner loop
        ld      b, a
        ld      de, (_wcDx)
        ; The cap pixels are only drawn while w < capLim (33 when this column
        ; was painted, 0 when it was occluded).  Close walls spend hundreds of
        ; iterations here with w far above that, so when no step of this run
        ; can possibly draw a cap, use a loop with the test taken out.
        ld      a, (_wcCapLim)
        cp      a, b                    ; capLim - w
        jr      C, wc_in_fastsel        ; capLim < w: w is already above it
        jr      NZ, wc_in_slowsel       ; capLim > w: caps are possible
wc_in_fastsel:
        ld      a, (_wcWstep)           ; w >= capLim, so only a downward run
        inc     a                       ; can still reach the cap range
        jr      Z, wc_in_slowdown
        jr      wc_in_fastup
wc_in_slowsel:
        ld      a, (_wcWstep)
        inc     a                       ; +1 -> 2, -1 -> 0
        jr      Z, wc_in_down
        jr      wc_in_up

; w only grows and is already past the cap range: no test needed at all
wc_in_fastup:
        inc     b
        add     hl, de
        bit     7, h
        jr      NZ, wc_in_fastup
        jr      wc_in_done

; w shrinks from above the cap range: run free until it gets close, then fall
; into the full version
wc_in_slowdown:
        ld      a, b
        sub     a, #34
        jr      C, wc_in_down           ; w < 34: too close to the cap range
        ld      a, (_wcCapLim)
        or      a, a
        jr      NZ, wc_in_downnear
wc_in_fastdown:                         ; occluded column: never any caps
        dec     b
        add     hl, de
        bit     7, h
        jr      NZ, wc_in_fastdown
        jr      wc_in_done
wc_in_downnear:
        dec     b
        add     hl, de
        bit     7, h
        jr      Z, wc_in_done
        ld      a, b
        cp      a, #34                  ; still clear of the cap range?
        jr      NC, wc_in_downnear
        jr      wc_in_down
wc_in_done:
        ld      a, b                    ; w back out of B
        pop     de
        ld      e, a
        ld      b, #>_wBuffer           ; restore the page byte
wc_next:
        inc     c
        jp      wc_loop

wc_exit:
        ld      a, e
        ld      (_wcW), a
        ld      (_wcWerror), hl
        ld      a, c
        ld      (_wcXi), a
        ret

; --- inner loop: walk w towards w2 while werror is negative ----------------
; B = w, C = xi, DE = dx, HL = werror.  Two copies so the +1/-1 step is free.
wc_in_up:
        inc     b
        add     hl, de
        bit     7, h
        jr      Z, wc_in_done
        ld      a, (_wcCapLim)          ; 33 when drawing, 0 when occluded
        cp      a, b                    ; capLim - w;  caps iff w < capLim
        jr      C, wc_in_up
        jr      Z, wc_in_up
        call    wc_caps
        jr      wc_in_up

wc_in_down:
        dec     b
        add     hl, de
        bit     7, h
        jr      Z, wc_in_done
        ld      a, (_wcCapLim)
        cp      a, b
        jr      C, wc_in_down
        jr      Z, wc_in_down
        call    wc_caps
        jr      wc_in_down

; --- the two black pixels that cap a sloping wall edge ---------------------
wc_caps:
        push    de
        push    hl
        push    bc
        ld      a, (_wcHorizon)
        add     a, b
        dec     a
        ld      l, a                    ; y = horizon + w - 1
        ld      a, c                    ; x = xi
        call    _Platform_PutPixelBlack
        pop     bc
        push    bc
        ld      a, (_wcHorizon)
        sub     a, b
        ld      l, a                    ; y = horizon - w
        ld      a, c
        call    _Platform_PutPixelBlack
        pop     bc
        pop     hl
        pop     de
        ret
