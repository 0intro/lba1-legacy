/* dll.c */
uint32_t	get16(uint8_t*);
uint32_t	get32(uint8_t*);
uint8_t*	readfile(char*, long*);
int	hqr(char*, uint8_t***);
void	dllmap(char*);
void	dllload(char*);
uint32_t	dllproc(int);
uint32_t	dllcall(uint32_t, int, int, uint32_t*);
void	dllio(void (*)(uint32_t, uint32_t, int), uint32_t (*)(uint32_t, int));
void	dllcover(uint32_t*, int, int*);
