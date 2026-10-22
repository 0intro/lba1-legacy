extern	char	Midi_Driver[_MAX_PATH] ;
extern  char	Midi_Driver_Name[80] ;
extern	WORD	Midi_Driver_Enable ;

extern	char GTL_filename[_MAX_PATH] ;		// fichier des sons

extern void AskMidiVars(char ***,int32_t **);
extern int32_t InitMidiDLL(char *);
extern int32_t InitMidi(void);
extern void InitPathMidiSampleFile(unsigned char *);
extern void ClearMidi(void);
extern void PlayMidi(unsigned char *);
extern void StopMidi(void);
extern int32_t IsMidiPlaying(void);
extern void FadeMidiDown(int16_t );
extern void FadeMidiUp(int16_t );
extern void WaitFadeMidi(void);
extern void VolumeMidi(int16_t );
extern void SetLoopMidi(int16_t );
extern void DoLoopMidi(void);
