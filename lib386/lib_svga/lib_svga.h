/*-----------------------------------------------------------------------------
;                       Extern lib_svga 386
;-----------------------------------------------------------------------------*/

/*-----------------------------------------------------------------------------*/

#define SVGA_VESA               0	/* VESA compliant board at Adress 0xA0000 */
#define SVGA_TRIDENT            1	/* Trident 8800 & 8900 */
#define SVGA_ATI                2	/* ATI Chipset */
#define SVGA_S3                 3	/* S3 Boards (expect init with VESA mode 102) */
#define SVGA_CIRRUS             4	/* Cirrus (some Genoa) (expect init with VESA mode 102) */
#define SVGA_TSENG              5	/* Tseng ET3000 & ET4000 */
#define SVGA_WESTERN            6	/* Western Digital / Paradise */
#define SVGA_GENOA              7	/* REAL Genoa (not Cirrus or ET3000) */

/*-----------------------------------------------------------------------------*/
#ifdef	DOS
// les dd de l'assembleur, que le C de 1994 lisait en WORD
extern  WORD    ClipXmin        ;
extern  WORD    ClipYmin        ;
extern  WORD    ClipXmax        ;
extern  WORD    ClipYmax        ;
#else
extern  LONG    ClipXmin        ;
extern  LONG    ClipYmin        ;
extern  LONG    ClipXmax        ;
extern  LONG    ClipYmax        ;
#endif

extern  ULONG   TabOffLine[]    ;
extern  UBYTE   *Phys           ;
extern  UBYTE   *Log            ;
extern  UBYTE   *MemoLog        ;
extern  LONG    Screen_X        ;
extern  LONG    Screen_Y        ;
extern  UBYTE   Text_Ink        ;
extern  UBYTE   Text_Paper      ;
extern  UBYTE   OldVideo        ;
extern  WORD    SizeCar         ;
/*-----------------------------------------------------------------------------*/
extern  WORD    Svga_Card       ;
extern  void    *BankChange     ;
extern  LONG    BankCurrent     ;
/*-----------------------------------------------------------------------------*/

extern  WORD    TypePoly        ;

extern  WORD    NbPolyPoints    ;

extern  WORD    TabPoly[]       ;
extern  WORD    TabText[]       ;

extern  WORD    TabVerticG[]    ;
extern  WORD    TabVerticD[]    ;
extern  WORD    Ymin ;
extern  WORD    Ymax ;

extern  WORD    TabCoulG[]      ;
extern  WORD    TabCoulD[]      ;
#ifndef	DOS
extern  WORD    TabX1[]         ;
extern  WORD    TabY1[]         ;
#endif

/*──────────────────────────────────────────────────────────────────────────*/
/*                              MOUSE.C                                     */
extern  volatile LONG   Click   ;
extern  volatile LONG   Mouse_X ;
extern  volatile LONG   Mouse_Y ;
extern  LONG            Mouse_X_Dep ;
extern  LONG            Mouse_Y_Dep ;
extern	UBYTE		*GphMouse ;

extern void SetMouseBox(int32_t ,int32_t ,int32_t ,int32_t );
extern void SetMouseSpeed(int32_t ,int32_t );
extern void SetMousePos(int32_t ,int32_t );
extern void GetMouseDep(void);
extern void ShowMouse(int32_t );
extern void AffMouse(void);
extern void SetMouse(int16_t );
extern void InitMouse(void);
extern void ClearMouse(void);

/*-----------------------------------------------------------------------------*/
/*                              S_PAL.asm                                       */
void    PalOne( UBYTE coul, UBYTE r, UBYTE v, UBYTE b ) ;
void    PalMulti( WORD startcoul, WORD nbcoul, UBYTE *pal ) ;
void    Palette( void *pal )                            ;
/*-----------------------------------------------------------------------------*/
/*                              FONT.ASM                                        */
void    CoulFont( LONG couleur )                                ;
void    SetFont( void *font, LONG interleave, LONG interspace ) ;
LONG    SizeFont( void *chaine )                                ;
LONG    CarFont( LONG xcar, LONG ycar, LONG car )               ;
void    Font( LONG xchaine, LONG ychaine, void *chaine )        ;
/*-----------------------------------------------------------------------------*/
/*                              GRAPH_A.ASM                                     */
void    AffGraph( LONG num, LONG x, LONG y, void *bank )                ;
void    GetDxDyGraph( LONG num, LONG *dx, LONG *dy, void *bank )        ;
/*-----------------------------------------------------------------------------*/
/*                              MASK_A.ASM                                      */
void    AffMask( LONG num, LONG x, LONG y, void *bank ) ;
/*-----------------------------------------------------------------------------*/
/*                      InitSvga.asm                                            */
void    SetClip( LONG x0, LONG y0, LONG x1, LONG y1 )   ;
void    UnSetClip( void )                               ;
void    MemoClip( void )                                ;
void    RestoreClip( void )                             ;
/* declared for C99: DOS calls them undeclared, as LBA.EXE was compiled */
#ifndef	DOS
void    InitMcgaMode( void )                            ;
void    SimpleInitSvga( void )                          ;
#endif
/*-----------------------------------------------------------------------------*/
/*                      Box.asm                                                 */
void    Box( LONG x0, LONG y0, LONG x1, LONG y1, LONG couleur ) ;
/*-----------------------------------------------------------------------------*/
/*                      Line.asm                                                        */
void    Line( LONG x0, LONG y0, LONG x1, LONG y1, LONG couleur )        ;
/*-----------------------------------------------------------------------------*/
/*                      S_Poly.asm                                              */
#ifndef	DOS
LONG    ComputePoly( void )                                     ;
LONG    ComputeSphere( LONG xc, LONG yc, LONG rayon )           ;
#endif
/*-----------------------------------------------------------------------------*/
/*                      S_FillV.asm                                             */
#ifndef	DOS
void    FillVertic( LONG typepoly, LONG coulpoly )              ;
void    SetFillDetails( LONG level )                            ;
#endif
/*-----------------------------------------------------------------------------*/
/*                      Texture.asm                                             */
#ifndef	DOS
void    AsmTexturedTriangleNoClip( void )                       ;
void    FillTextPolyNoClip( LONG ymin, LONG ymax, void *source ) ;
#endif
/*-----------------------------------------------------------------------------*/
/*                      Gif.c                                                   */
#ifndef	DOS
void    Read_Gif( LONG fd, UBYTE *screen, UBYTE *tabcol, UBYTE *buffers ) ;
#endif
/*-----------------------------------------------------------------------------*/
/*                      Zoom.asm                                                        */
void    ScaleLine( LONG xa0, LONG xa1, void *src, LONG xb0, LONG xb1, void *dst )       ;
void    ScaleBox( LONG xa0, LONG ya0, LONG xa1, LONG ya1, void *src,
		  LONG xb0, LONG yb0, LONG xb1, LONG yb1, void *dst )   ;
/*-----------------------------------------------------------------------------*/
/*                      CopyMask.asm                                                    */
void    CopyMask( LONG num, LONG x, LONG y, void *bank, void* screen ) ;
/*-----------------------------------------------------------------------------*/
/*                      Mask_A.asm                                                      */
void    CoulMask( LONG coulmask )                               ;
void    AffMask( LONG num, LONG x, LONG y, void *bank )         ;
void    GetDxDyMask( LONG num, LONG *dx, LONG *dy, void *bank ) ;
/*-----------------------------------------------------------------------------*/
/*                      S_PLOT.asm.asm                                                  */
void    Plot( LONG x, LONG y, LONG couleur )    ;
LONG    GetPlot( LONG x, LONG y )               ;
/*-----------------------------------------------------------------------------*/
/*                      S_SCREEN.asm                                                    */
void    Cls( void )                             ;
void    CopyScreen( void *src, void *dst )      ;
void    Vsync( void )                           ;
void    Flip( void )                            ;
void    FlipComp( void )                        ;
/*-----------------------------------------------------------------------------*/
/*                      S_STRING.asm                                                    */
void    AffString( LONG x, LONG y, void *text ) ;
void    CoulText( LONG coul0, LONG coul1 )	;
extern	UBYTE	Font8X8 ;

/*-----------------------------------------------------------------------------*/
/*                      S_BLOCK2.asm                                                    */
void    CopyBlockOnBlack( LONG x0, LONG y0, LONG x1, LONG y1, void *src, void *dst )    ;
/*-----------------------------------------------------------------------------*/
/*                      GRAPHMSK.asm                                                    */
LONG    CalcGraphMsk( LONG num, void *bank, void *mask )        ;
/*-----------------------------------------------------------------------------*/
/*                      S_BLOCK.asm                                                     */
void    CopyBlock( LONG x0, LONG y0, LONG x1, LONG y1, void *src,
		   LONG xd, LONG yd, void *dst )                        ;

/*-----------------------------------------------------------------------------*/
/*                      S_BLOCK3.asm                                                     */
void    CopyBlockIncrust( LONG x0, LONG y0, LONG x1, LONG y1, void *src,
		   LONG xd, LONG yd, void *dst )                        ;

/*-----------------------------------------------------------------------------*/
/*                      S_PHYS.asm                                                     */
// void    CopyBlockIncrust( LONG x0, LONG y0, LONG x1, LONG y1 )          ;
void    CopyBlockPhysClip( LONG x0, LONG y0, LONG x1, LONG y1 )         ;
void    CopyBlockPhys( LONG x0, LONG y0, LONG x1, LONG y1 )             ;
void    SaveBlock( void *screen, void *buffer, LONG x, LONG y, LONG dx, LONG dy ) ;
void    RestoreBlock( void *screen, void *buffer, LONG x, LONG y, LONG dx, LONG dy ) ;

/*-----------------------------------------------------------------------------*/
/*                      LBM.c	320*200					*/

extern	WORD Lbm_Width ;	// 640 or 320 (for source format)

extern unsigned char ReadIff(void);
extern void WriteIff(void *,uint32_t );
extern void WriteIffLong(uint32_t );
extern void itoml(void *);
extern void lit_bmhd(void);
extern void lit_bloc(void);
extern void lit_ligne(unsigned char *,int16_t );
extern void ConvertMCGA(unsigned char *,unsigned char *,uint16_t ,int16_t );
extern void lit_bodyILBM(unsigned char *);
extern void lit_bodyPBM(unsigned char *);
extern int32_t Load_Lbm(char *,unsigned char *,unsigned char *);
extern int32_t Save_Lbm(char *,unsigned char *,unsigned char *);

/*-----------------------------------------------------------------------------*/
/*                      PCX.c	640*480					*/

extern void line_out(unsigned char *);
extern unsigned char next_char(uint32_t ,unsigned char *);
extern void Load_Pcx(char *,unsigned char *,unsigned char *);
extern void Save_Pcx(char *,unsigned char *,unsigned char *);

/*-----------------------------------------------------------------------------*/
/*                      s_dll.c						*/

extern int32_t SvgaInitDLL(char *);
/*-----------------------------------------------------------------------------*/
/*                      s_text.c					       */
extern void Text(int16_t ,int16_t ,char * ,... );

/*-----------------------------------------------------------------------------*/
/*                      s_rect.c					       */

extern void Rect(int32_t ,int32_t ,int32_t ,int32_t ,int32_t );

/*-----------------------------------------------------------------------------*/
/*                      initmode.c					       */

extern void InitGraphSvga(void);
extern void ClearGraphSvga(void);
extern void InitGraphMcga(void);
extern void ClearGraphMcga(void);
