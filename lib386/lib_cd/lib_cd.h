
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

//      FEATURE: the PlayStation's track of a tune, a file on the CD, its
//      length in frames, 0 if the CD lacks it, played in place of the
//      XMIDI, and whether it plays. Taken from the PlayStation.
LONG    pstracklength( char *file )     ;
void    pstrackplay( char *file )       ;
int     pstrackplaying( void )          ;

//      FEATURE: a tune's cut from a track of the CD, as DotEmu cut it for
//      its recording, its length in frames, 0 if the CD lacks its track,
//      played in place of the XMIDI. Taken from DotEmu 1.06.
LONG    cutlength( LONG num )           ;
void    cutplay( LONG num )             ;

//      FEATURE: the music's sources, lba.cfg's MusicTracks for the tunes
//      asked of the CD and MusicJingles for those asked of the XMIDI.
//      The port's own.
#define TRACKS_CD               0       // the CD's tracks
#define TRACKS_MIDI             1       // the XMIDI, as the disk version
#define JINGLES_MIDI            0       // the XMIDI
#define JINGLES_PLAYSTATION     1       // the PlayStation's recordings
#define JINGLES_CD              2       // cut from the CD's tracks, as DotEmu cut them
extern  LONG    MusicTracks     ;
extern  LONG    MusicJingles    ;
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


