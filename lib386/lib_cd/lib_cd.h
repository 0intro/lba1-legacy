
//----------------------------------------------------------------------------



extern  WORD    BufferData[]    ;
extern  WORD    DriveCDR        ;//     Lettre drive 0=A 1=B etc. apres InitCDR
extern  ULONG   LeadOut         ;
extern  UBYTE   LowTrack, HigTrack      ;
extern	LONG	FileCD_Start	;
extern	LONG	FileCD_Sect	;
extern	LONG	FileCD_Size	;

//----------------------------------------------------------------------------
//      Initialiser le lesteur de cdrom, si pas verif alors chaine vide
LONG    InitCDR( char *nameid )         ;

//      Jouer seulement une track précise
LONG    PlayTrackCDR( LONG track )      ;

//      Arreter le PlayTrackCDR
void    StopCDR( void )                 ;

//      Verouiller ou déverouiller la porte du lecteur
void    LockCDR( void )                 ;
void    UnLockCDR( void )               ;

//      Reset, Init ou suite a une erreur par exemple
void    ResetCDR( void )                ;
/* declared for C99: DOS calls them undeclared, as LBA.EXE was compiled */
#ifndef	DOS
void    ClearCDR( void )                ;
#endif

//      Ejecter le cdrom quand c'est possible ( pas toujours motorisé )
void    EjectCDR( void )                ;

//      Fermer tiroir electrique quand c'est possible ( pas toujours motorisé )
void    CloseTrayCDR( void )            ;

//      Reprends une track apres un stopCDR
void    ResumeCDR( void )               ;

#ifndef	DOS
//      FEATURE: the track playing paused where it is, then played on from
//      there. Taken from LBA2.
void    PauseCD( void )                 ;
void    ResumeCD( void )                ;
#endif

//-------------------------------------- Fonction bas niveau

//      Return pointeur sur Copyright File name ( pas forcement file )
char    *GetCopyrightName( void )       ;

//      Conversion Red to Hsg ( belle jambe non ;-) )
LONG    Red2Hsg( LONG value )           ;

//      Return la taille d'une track en HSG
LONG    GetLengthTrackCDR( LONG track ) ;

//      Return secteur depart Track
LONG    StartTrackCDR( LONG track )     ;

//      Return status of CD Drive
LONG    StatusCDR( void )               ;

//      Return Track currently play or 0
LONG    GetMusicCDR( void )             ;

//      Change volume selon chanel
void    ControlCDR( WORD chan0, WORD vol0, WORD chan1, WORD vol1 )      ;

//      Return position de la tete
LONG    LocateHeadCDR( void )           ;

//      Play a part of cd
LONG    PlayCDR( LONG debut, LONG duree );

//      Lecture data
LONG    ReadLongCDR( LONG start, LONG nbsect, void *buffer ) ;

//	Get Directory Entry
LONG	GetFileCDR( char *name )	;

//----------------------------------------------------------------------------


