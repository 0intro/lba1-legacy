#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#include "dat.h"
#include "fns.h"

/*
 * The game's memory, from one arena, as lib386/lib_sys/malloc.c gave it
 * under DOS/4GW: Malloc and its kin, memory below the first megabyte
 * (DosMalloc) ordinary memory here. p_anim keeps the address of a
 * body's frame in 4 bytes, as the assembly kept it, sign-extended on a
 * 64-bit host, where the arena lies below 2 GB: the arena lies at the
 * same address on every host where it can, the first stretch free from
 * 512 MB up, and its blocks at the same places, so that a run is the
 * same on each. For that, the blocks that hold the host's pointers,
 * larger on a 64-bit host, come from a pool of their own after the
 * game's (HostMalloc). DOS had no protection, and the assembly read a
 * little past the ends of some buffers: each block is zeroed and has
 * some slack after it. A block is first fit among its pool's holes,
 * kept in order of address and joined when freed.
 */

enum
{
	Arenasize = 128 << 20,
	Hostsize = 8 << 20,
	Slack = 4096,
	Head = 16,	/* a block's size, before it */
	Grain = 16,
	Freemem = 16 << 20,	/* what Malloc(-1) reports: the caches at their largest */
	Dosmem = 400 << 10,	/* the DOS memory DOS/4GW would leave free */
	Lowfirst = 512,	/* MB, the first place tried for the arena */
	Lowstep = 64,	/* MB, between the places tried */
};

typedef struct Hole Hole;

/* a stretch of the arena free, its size including this header */
struct Hole
{
	uint32_t	size;
	Hole	*next;
};

typedef struct Pool Pool;

/* a part of the arena and its holes */
struct Pool
{
	uint8_t	*base;
	uint32_t	size;
	Hole	*holes;
};

static Pool	game;	/* the game's blocks */
static Pool	host;	/* the blocks that hold the host's pointers */

#ifdef _WIN32
/* n bytes at a, or NULL where VirtualAlloc grants them elsewhere or not at all */
static void*
mapat(uintptr_t a, size_t n)
{
	return VirtualAlloc((void*)a, n, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
}
#else
/* n bytes at a, or NULL where mmap gives them elsewhere or not at all */
static void*
mapat(uintptr_t a, size_t n)
{
	void *p;

	p = mmap((void*)a, n, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED)
		return NULL;
	if (p != (void*)a) {
		munmap(p, n);
		return NULL;
	}
	return p;
}
#endif

/* memory below 2 GB: the first stretch the host grants from Lowfirst up */
static void*
lowmap(size_t n)
{
	uintptr_t a;
	void *p;

	for (a = (uintptr_t)Lowfirst << 20; a + n <= (uintptr_t)2 << 30; a += (uintptr_t)Lowstep << 20)
		if ((p = mapat(a, n)) != NULL)
			return p;
	return NULL;
}

/* pool p over the size bytes at base, its first grain kept so that no block is at its start */
static void
poolinit(Pool *p, uint8_t *base, uint32_t size)
{
	p->base = base;
	p->size = size;
	p->holes = (Hole*)(base + Grain);
	p->holes->size = size - Grain;
	p->holes->next = NULL;
}

/* meminit: the arena, mapped before SDL and its drivers take the low addresses. */
void
meminit(void)
{
	uint8_t *a;

	a = lowmap(Arenasize + Hostsize);
	if (a == NULL || (sizeof(void*) > 4 && (uintptr_t)a + Arenasize + Hostsize > (uintptr_t)INT32_MAX)) {
		fprintf(stderr, "lba: cannot map %d MB for the game below 2 GB\n", (Arenasize + Hostsize) >> 20);
		exit(1);
	}
	poolinit(&game, a, Arenasize);
	poolinit(&host, a + Arenasize, Hostsize);
}

/* the pool of the block at v */
static Pool*
poolof(void *v)
{
	if ((uint8_t*)v >= host.base && (uint8_t*)v < host.base + host.size)
		return &host;
	return &game;
}

/* n zeroed bytes of pool o */
static void*
poolmalloc(Pool *o, size_t n)
{
	Hole **h, *p, *r;
	uint32_t need;

	if (n > o->size)
		return NULL;
	need = ((uint32_t)n + Slack + Head + Grain-1) & ~(uint32_t)(Grain-1);
	for (h = &o->holes; (p = *h) != NULL; h = &p->next)
		if (p->size >= need)
			break;
	if (p == NULL)
		return NULL;
	if (p->size - need >= Head + Grain) {
		r = (Hole*)((uint8_t*)p + need);
		r->size = p->size - need;
		r->next = p->next;
		*h = r;
	} else {
		need = p->size;
		*h = p->next;
	}
	memset(p, 0, need);
	memcpy(p, &need, sizeof need);
	return (uint8_t*)p + Head;
}

/* a stretch of pool o given back to its holes, joined with its neighbours */
static void
release(Pool *o, uint8_t *at, uint32_t size)
{
	Hole **h, *p, *prev;

	p = (Hole*)at;
	prev = NULL;
	for (h = &o->holes; *h != NULL && *h < p; h = &(*h)->next)
		prev = *h;
	p->size = size;
	p->next = *h;
	*h = p;
	if (p->next != NULL && (uint8_t*)p + p->size == (uint8_t*)p->next) {
		p->size += p->next->size;
		p->next = p->next->next;
	}
	if (prev != NULL && (uint8_t*)prev + prev->size == (uint8_t*)p) {
		prev->size += p->size;
		prev->next = p->next;
	}
}

/* Malloc: n zeroed bytes of the game's pool, NULL where it holds none, and Freemem for -1. */
void*
Malloc(int32_t n)
{
	if (n == -1)
		return (void*)(uintptr_t)Freemem;
	if (game.base == NULL)
		meminit();
	return poolmalloc(&game, (size_t)(uint32_t)n);
}

/* HostMalloc is Malloc for the blocks that hold the host's pointers: n zeroed bytes of their pool. */
void*
HostMalloc(int32_t n)
{
	if (game.base == NULL)
		meminit();
	return poolmalloc(&host, (size_t)(uint32_t)n);
}

void*
SmartMalloc(int32_t n)
{
	return Malloc(n);
}

void*
DosMalloc(int32_t n, uint32_t *handle)
{
	if (n == -1)
		return (void*)(uintptr_t)Dosmem;
	if (handle != NULL)
		*handle = 1;
	return Malloc(n);
}

void
Free(void *v)
{
	uint8_t *p;
	uint32_t size;

	if (v == NULL)
		return;
	p = (uint8_t*)v - Head;
	memcpy(&size, p, sizeof size);
	release(poolof(p), p, size);
}

/*
 * Mshrink was _expand: the block v made n bytes in place, NULL where it
 * cannot grow there. It shrinks always, the rest of the block given
 * back.
 */
void*
Mshrink(void *v, uint32_t n)
{
	uint8_t *p;
	uint32_t size, need;

	p = (uint8_t*)v - Head;
	memcpy(&size, p, sizeof size);
	if (n > Arenasize)
		return NULL;
	need = (n + Slack + Head + Grain-1) & ~(uint32_t)(Grain-1);
	if (need > size)
		return NULL;
	if (size - need >= Head + Grain) {
		memcpy(p, &need, sizeof need);
		release(poolof(p), p + need, size - need);
	}
	return v;
}
