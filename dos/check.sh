#!/usr/bin/env bash
#
# check.sh: play the scripts of dos/keys on LBA.EXE, then on our build.
#
#     dos/check.sh [-c] [exe]
#
# exe is build/dos-386-openwatcom/lba0.exe unless given. Each program
# plays, in DOSBox under tools/xkeys:
#
#     newgame    a new game: the name, the introduction, the first scene
#     saveload   the inventory, the behaviour menu, quitting (which saves),
#                the save loaded back
#     options    the option menus, the save in the saved games list
#     holomap    the holomap, from that save given it, outside the prison
#     sound      a new game recorded (dosrun -r)
#     volume     the volume screen's slider recorded (dosrun -r)
#     quit       quitting from the menu, what the program printed (dosrun -o)
#
# The waits of the scripts compare the screen, in areas where nothing
# moves, with $LBATMP/cap/ref, which the first run of LBA.EXE makes when
# it is empty: our build's waits find the original's screens. A wait that
# fails stops the check. What each run saw goes to
# $LBATMP/cap/check/{orig,ours}. -c compares what is there without playing.
#
# Then, for each screen, the share of its pixels that differ between the
# two (where something moves: the menus' fire, Twinsen standing, a fade or
# a movie caught a frame apart), whether the saves are the same bytes,
# whether both printed the same bytes (the version line too: both carry
# LBA.EXE's stamp), and the loudness of both recordings, side by side, the
# volume screen's channel by channel. The status is 1 for a screen missing
# on one side, saves that differ or output that differs. A share of pixels
# is the reader's to judge.

set -Eeuo pipefail

DOS=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO=$(cd -- "$DOS/.." && pwd)
LBATMP=${LBATMP:-$(cd -- "$REPO/.." && pwd)/lba1-tmp}
TOOLS=$REPO/tools
compare=
if [[ ${1:-} == -c ]]; then
	compare=1
	shift
fi
exe=${1:-$REPO/build/dos-386-openwatcom/lba0.exe}
out=$LBATMP/cap/check
ref=$LBATMP/cap/ref
run=$LBATMP/dos/run/LBA

[[ -n $compare || -e $exe ]] || { echo "check.sh: no $exe" >&2; exit 1; }
mkdir -p "$ref"

# play who script dosrun-options...
play() {
	local who=$1 s=$2
	shift 2
	echo "$who $s"
	if ! "$TOOLS/xkeys" -o "$out/$who/$s" -r "$ref" "$DOS/keys/$s.keys" \
		"$TOOLS/dosrun" -q "$@" > "$out/$who/$s.log" 2>&1; then
		grep -P '^\S+\t[0-9.]+' "$out/$who/$s.log" >&2 || true
		echo "check.sh: $who $s failed; see $out/$who/$s.log" >&2
		exit 1
	fi
	grep -P '^\S+\t[0-9.]+' "$out/$who/$s.log" | sed 's/^/	/' || true
	# a wait LBA.EXE slept through for want of a reference gives it one
	if [[ $who == orig ]]; then
		awk '$1 == "wait" { print $2 }' "$DOS/keys/$s.keys" | while read -r w; do
			[[ -e $ref/$w.png ]] || cp "$out/$who/$s/$w.png" "$ref/"
		done
	fi
}

for who in orig ours; do
	[[ -n $compare ]] && break
	e=()
	[[ $who == ours ]] && e=(-e "$exe")
	rm -rf "${out:?}/$who"
	mkdir -p "$out/$who"

	play "$who" newgame "${e[@]}"
	play "$who" saveload "${e[@]}"
	cp "$run"/S*.LBA "$out/$who/saveload/"
	play "$who" options -k "${e[@]}"

	# the holomap, outside the prison: see dos/keys/holomap.keys
	save=$(echo "$run"/S*.LBA)
	printf '\001' | dd of="$save" bs=1 seek=6 conv=notrunc status=none
	printf '\001' | dd of="$save" bs=1 seek=261 conv=notrunc status=none
	printf '\377\377' | dd of="$save" bs=1 seek=270 conv=notrunc status=none
	play "$who" holomap -k "${e[@]}"

	rm -f "$LBATMP/cap/dos/sound.raw"
	play "$who" sound -r "${e[@]}"
	mv "$LBATMP/cap/dos/sound.raw" "$out/$who/sound/"

	rm -f "$LBATMP/cap/dos/sound.raw"
	play "$who" volume -r "${e[@]}"
	mv "$LBATMP/cap/dos/sound.raw" "$out/$who/volume/"

	play "$who" quit -o "${e[@]}"
	cp "$run/OUT.TXT" "$out/$who/quit/"
done

bad=
echo
echo "screens, the share of pixels that differ"
for f in "$out"/orig/*/*.png; do
	rel=${f#"$out"/orig/}
	g=$out/ours/$rel
	if [[ -e $g ]]; then
		r=$(magick "$f" "$g" -compose difference -composite -colorspace gray \
			-threshold 0 -format '%[fx:mean*100]' info:)
		r=$(printf '%.2f%%' "$r")
	else
		r=missing
		bad=1
	fi
	printf '\t%s\t%s\n' "${rel%.png}" "$r"
done

echo
if cmp -s "$out"/orig/saveload/S*.LBA "$out"/ours/saveload/S*.LBA; then
	echo "saves: same"
else
	echo "saves: differ"
	bad=1
fi
if cmp -s "$out/orig/quit/OUT.TXT" "$out/ours/quit/OUT.TXT"; then
	echo "printed: same"
else
	echo "printed: differs"
	bad=1
fi

echo
echo "loudness (orig, ours)"
python3 -I "$TOOLS/loudness.py" "$out/orig/sound/sound.raw" "$out/ours/sound/sound.raw"
echo
echo "the volume screen, left and right (orig, ours)"
python3 -I "$TOOLS/loudness.py" -c "$out/orig/volume/sound.raw" "$out/ours/volume/sound.raw"
[[ -z $bad ]]
