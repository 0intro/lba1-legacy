/* emu8kdiff's stand-in: no bus, the harness calls the handlers. */
void	io_sethandler(uint16_t, int, uint8_t (*)(uint16_t, void*), uint16_t (*)(uint16_t, void*),
	uint32_t (*)(uint16_t, void*), void (*)(uint16_t, uint8_t, void*), void (*)(uint16_t, uint16_t, void*),
	void (*)(uint16_t, uint32_t, void*), void*);
void	io_removehandler(uint16_t, int, uint8_t (*)(uint16_t, void*), uint16_t (*)(uint16_t, void*),
	uint32_t (*)(uint16_t, void*), void (*)(uint16_t, uint8_t, void*), void (*)(uint16_t, uint16_t, void*),
	void (*)(uint16_t, uint32_t, void*), void*);
