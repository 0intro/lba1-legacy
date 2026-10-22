#include "adeline.h"
#include "lzss.h"

/*
 * FEATURE: the binary tree of the window's strings that LBA2's
 * Compress_LZSS (lzss.c) packs with, COMPRESS.ASM's procedures in C.
 * Taken from LBA2.
 */

static int Match_Length;	/* AddString's longest match */

/*
 * Replace_Node is LBA2's COMPRESS.ASM Replace_Node: node new, EBP, in
 * old's place, EDI, in the tree.
 */
static void
Replace_Node(int new, int old)
{
	long parent;

	parent = tree[old].parent;
	if (tree[parent].smaller_child == old)
		tree[parent].smaller_child = new;
	else
		tree[parent].larger_child = new;
	tree[new].parent = tree[old].parent;
	tree[new].larger_child = tree[old].larger_child;
	tree[new].smaller_child = tree[old].smaller_child;
	tree[old].parent = UNUSED;
	if (tree[new].smaller_child != UNUSED)
		tree[tree[new].smaller_child].parent = new;
	if (tree[new].larger_child != UNUSED)
		tree[tree[new].larger_child].parent = new;
}

/*
 * Find_Next_Node is LBA2's COMPRESS.ASM Find_Next_Node: the node before
 * node, ESI, the largest of its smaller side, in EAX.
 */
static int
Find_Next_Node(int node)
{
	long next;

	next = tree[node].smaller_child;
	while (tree[next].larger_child != UNUSED)
		next = tree[next].larger_child;
	return next;
}

/*
 * Contract_Node is LBA2's COMPRESS.ASM Contract_Node: old's child new,
 * EAX, in old's place, ESI. An unused new is given no parent, where
 * LZSS.CPP's ContractNode wrote tree[-1].
 */
static void
Contract_Node(int new, int old)
{
	long parent;

	parent = tree[old].parent;
	tree[old].parent = UNUSED;
	if (new != UNUSED)
		tree[new].parent = parent;
	if (tree[parent].larger_child == old)
		tree[parent].larger_child = new;
	else
		tree[parent].smaller_child = new;
}

/*
 * DeleteString is LBA2's COMPRESS.ASM DeleteString: node p out of the
 * tree, if it is in. Its value, which LZSS.H makes an int, is the 0 of
 * none: the procedure leaves EAX as its last move did, and no caller
 * reads it.
 */
int
DeleteString(int p)
{
	int next;

	if (tree[p].parent == UNUSED)
		return 0;
	if (tree[p].larger_child == UNUSED) {
		Contract_Node(tree[p].smaller_child, p);
	} else if (tree[p].smaller_child == UNUSED) {
		Contract_Node(tree[p].larger_child, p);
	} else {
		next = Find_Next_Node(p);
		DeleteString(next);
		Replace_Node(next, p);
	}
	return 0;
}

/*
 * AddString is LBA2's COMPRESS.ASM AddString: the string at
 * Current_position put in the tree, its longest match among those there
 * returned and its place left in Match_position, the later of two
 * equal matches kept. A string matched whole takes its match's node.
 */
int
AddString(void)
{
	int test, i, delta;
	long *child;

	test = tree[TREE_ROOT].larger_child;
	Match_Length = 0;
	for (;;) {
		delta = 0;
		for (i = 0; i < LOOK_AHEAD_SIZE; i++) {
			delta = window[MOD_WINDOW(Current_position + i)]
				- window[MOD_WINDOW(test + i)];
			if (delta != 0)
				break;
		}
		if (i >= Match_Length) {
			Match_Length = i;
			Match_position = test;
			if (i >= LOOK_AHEAD_SIZE) {
				Replace_Node(Current_position, test);
				return Match_Length;
			}
		}
		if (delta >= 0)
			child = &tree[test].larger_child;
		else
			child = &tree[test].smaller_child;
		if (*child == UNUSED) {
			*child = Current_position;
			tree[Current_position].parent = test;
			tree[Current_position].larger_child = UNUSED;
			tree[Current_position].smaller_child = UNUSED;
			return Match_Length;
		}
		test = *child;
	}
}
