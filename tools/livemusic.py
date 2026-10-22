"""livemusic: test/livemusic's checks of a run, the music's stream in play.

    python3 -I tools/livemusic.py name log data music.raw

log and data are test/livemusic.c's: the stream's open (its rate, the
device's rate and frames), each put (its time in ns from the open, its
bytes, the bytes queued before it), each query of the bytes queued,
each pull of the device (the bytes it lacked, the bytes it asked for),
each stall made (its time, its length, when it began and ended), the
end, and in data the bytes put. music.raw is the chip's frames as the
port handed them on (lba -m), 16-bit stereo. A frame is loud when a
sample of it lies past 16 from 0, as sys/sdl.c has it.

The run counts if one stream opened, at the device's rate, the device
(the preload's, on the port's clock) asked for its frames at each pull,
a period apart, and took what the real clock gives within half a
percent, each stall began within 50 ms of its time and lasted its
length within 10 ms, those under 0.1 s in loud music (a loud frame from
0.3 s before them to 0.5 s after), one of the longer in loud music and
one in quiet, 1 s at least was logged after the last stall, and the
pauses below left half the
tenths of a second at least to look at. Else the run is inconclusive, a
failure, its cause named.

A pause is a stall or a gap between the port's queries of 40 ms or
more. One of 130 ms or more, the clock's Late, a pull and a chunk, is
long: the clock may give its lag up, and the lead run out. Then these
must hold, each a failure else:
  - No pull lacked anything, but from a long pause to 50 ms after it.
    What a pull lacks plays as silence.
  - In each tenth of a second away from the pauses (and 0.2 s after
    them), the most the port read queued is at least the clock's Late
    and a pull: the lag the clock catches up after, and a pull to
    spare.
  - In quiet, from 1 s after the jingle's last loud frame to the long
    stall in quiet, the port read at most the lead, a chunk and 5 ms
    queued, the lead as sys/sdl.c sets it, and never more than half a
    second and a chunk.
  - The loud frames of music.raw are those put, in order.
It prints a line for the case, with its margins or what failed, and
exits 1 on a failure.
"""
import sys
from array import array

QUIET = 16
LATE = 0.1      # s: the lag sys/timer.c's clock catches up after
SHORT = 0.1     # s: a stall under this is caught up after, in loud music
LONG = 0.13     # s: a pause this long or more may run the lead out
PAUSE = 0.04    # s: a gap between queries this long or more is a pause


def samples(path):
    """A file's 16-bit little-endian samples."""
    a = array('h')
    with open(path, 'rb') as f:
        a.frombytes(f.read())
    if sys.byteorder == 'big':
        a.byteswap()
    return a


def loud(a):
    """The loud frames of samples a, in order, as pairs."""
    out = []
    for i in range(0, len(a) - 1, 2):
        l, r = a[i], a[i+1]
        if l > QUIET or l < -QUIET or r > QUIET or r < -QUIET:
            out.append((l, r))
    return out


def loudat(a, rate, t0, t1):
    """Whether samples a hold a loud frame from t0 to t1 seconds."""
    i0 = max(0, int(t0 * rate)) * 2
    i1 = min(len(a), int(t1 * rate) * 2)
    for i in range(i0, i1):
        if a[i] > QUIET or a[i] < -QUIET:
            return True
    return False


def lastloud(a, rate, before):
    """The time of the last loud frame of samples a before a time, or 0."""
    for i in range(min(len(a), int(before * rate) * 2) - 1, -1, -1):
        if a[i] > QUIET or a[i] < -QUIET:
            return i // 2 / rate
    return 0.0


def readlog(path):
    opened, rec, end = [], {'put': [], 'query': [], 'pull': [], 'stall': []}, None
    with open(path) as f:
        for line in f:
            w = line.split()
            if not w:
                continue
            if w[0] == 'open':
                opened.append(tuple(int(x) for x in w[2:]))
            elif w[0] in rec:
                rec[w[0]].append(tuple(int(x) for x in w[1:]))
            elif w[0] == 'end':
                end = int(w[1])
    for k in rec:
        rec[k].sort()
    return opened, rec, end


class Fail(Exception):
    pass


def check(name, logpath, datapath, musicpath):
    try:
        opened, rec, end = readlog(logpath)
    except OSError as e:
        raise Fail('inconclusive: no log: %s' % e)
    if len(opened) != 1:
        raise Fail('inconclusive: %d music streams opened' % len(opened))
    rate, devrate, devframes = opened[0]
    puts, queries, pulls, stalls = rec['put'], rec['query'], rec['pull'], rec['stall']
    if len(pulls) < 10 or len(queries) < 10:
        raise Fail('inconclusive: %d pulls, %d queries' % (len(pulls), len(queries)))
    period = devframes / devrate
    gaps = sorted(b[0] - a[0] for a, b in zip(pulls, pulls[1:]))
    if abs(gaps[len(gaps) // 2] / 1e9 - period) > period / 10:
        raise Fail('inconclusive: pulls %.1f ms apart, the period %.1f ms'
                   % (gaps[len(gaps) // 2] / 1e6, period * 1e3))
    pace = len(pulls) * period / ((pulls[-1][0] - pulls[0][0]) / 1e9 + period)
    if abs(pace - 1) > 0.005:
        raise Fail('inconclusive: the device took %.4f s of audio a second' % pace)

    # the stalls, in seconds
    st = []
    for at, ns, s0, s1 in stalls:
        if s0 == 0 or s1 == 0:
            raise Fail('inconclusive: the stall at %.1f s was never made' % (at / 1e9))
        if s0 - at > 50e6 or not (ns <= s1 - s0 <= ns + 10e6):
            raise Fail('inconclusive: the stall at %.1f s of %.0f ms began at %.3f s and lasted %.1f ms'
                       % (at / 1e9, ns / 1e6, s0 / 1e9, (s1 - s0) / 1e6))
        st.append((s0 / 1e9, s1 / 1e9, ns / 1e9))
    if not st:
        raise Fail('inconclusive: no stall')
    music = samples(musicpath)
    loudlong = quietlong = None
    for s0, s1, length in st:
        sounds = loudat(music, rate, s0 - 0.3, s0 + 0.5)
        if length < SHORT and not sounds:
            raise Fail('inconclusive: the stall at %.3f s fell in quiet' % s0)
        if length >= SHORT and sounds and loudlong is None:
            loudlong = s0
        if length >= SHORT and not sounds and quietlong is None:
            quietlong = s0
    if loudlong is None or quietlong is None:
        raise Fail('inconclusive: no long stall in loud music, or none in quiet')
    last = (end if end is not None else queries[-1][0]) / 1e9
    if last - max(s1 for _, s1, _ in st) < 1:
        raise Fail('inconclusive: %.3f s logged after the last stall' % (last - max(s1 for _, s1, _ in st)))

    # the pauses: the stalls, and the gaps of PAUSE or more between two
    # queries that hold none
    qt = [q[0] / 1e9 for q in queries]
    pauses = [(s0, s1) for s0, s1, _ in st]
    for a, b in zip(qt, qt[1:]):
        if b - a >= PAUSE and not any(a < s1 and b > s0 for s0, s1, _ in st):
            pauses.append((a, b))
    pauses.sort()

    # A1: no pull short, but from a long pause to 50 ms after
    longs = [(a, b + 0.05) for a, b in pauses if b - a >= LONG]
    short = [p for p in pulls if p[1] > 0 and not any(a <= p[0] / 1e9 <= b for a, b in longs)]
    if short:
        raise Fail('%d pulls short, the first at %.3f s by %.1f ms'
                   % (len(short), short[0][0] / 1e9, short[0][1] / 4 / rate * 1e3))
    inlong = sum(1 for p in pulls if p[1] > 0)

    # A2: the most queued in each tenth of a second away from the pauses
    need = (LATE + period) * rate * 4
    away = [(a, b + 0.2) for a, b in pauses]
    least = None
    seen = total = 0
    t = 0.3
    while t + 0.1 <= last:
        total += 1
        if not any(t < b and t + 0.1 > a for a, b in away):
            most = max((q[1] for q in queries if t <= q[0] / 1e9 < t + 0.1), default=None)
            if most is not None:
                if most < need:
                    raise Fail('at %.1f s the port read %.1f ms queued at most, %.1f ms needed'
                               % (t, most / 4 / rate * 1e3, need / 4 / rate * 1e3))
                least = most if least is None else min(least, most)
                seen += 1
        t += 0.1
    if seen < total / 2:
        raise Fail('inconclusive: pauses left %d tenths of a second of %d to look at' % (seen, total))
    lead = 4 * (rate // 10 + 2 * devframes * rate // devrate + rate // 60 + rate // 200)
    chunk = 4 * (rate // 120 + 1)
    top = max(q[1] for q in queries)
    if top > 4 * (rate // 2) + chunk:
        raise Fail('the port read %.1f ms queued' % (top / 4 / rate * 1e3))
    q0 = lastloud(music, rate, quietlong) + 1
    quiet = [q[1] for q in queries if q0 <= q[0] / 1e9 < quietlong]
    if not quiet:
        raise Fail('inconclusive: no query in quiet from %.3f s to %.3f s' % (q0, quietlong))
    if max(quiet) > lead + chunk + 4 * rate // 200:
        raise Fail('in quiet from %.3f s the port read %.1f ms queued, the lead %.1f ms'
                   % (q0, max(quiet) / 4 / rate * 1e3, lead / 4 / rate * 1e3))

    # A3: the loud frames handed on are those put
    want, got = loud(music), loud(samples(datapath))
    if want != got:
        n = next((i for i, (a, b) in enumerate(zip(want, got)) if a != b), min(len(want), len(got)))
        raise Fail('the loud frames part at the %dth: %d handed on, %d put' % (n, len(want), len(got)))

    print('livemusic: %s: lead %.1f ms, at least %.1f ms queued away from the pauses (%.1f needed), '
          'at most %.1f in quiet, %d pauses (%d long), %d pulls short in the long ones, %d loud frames put'
          % (name, lead / 4 / rate * 1e3, least / 4 / rate * 1e3, need / 4 / rate * 1e3,
             max(quiet) / 4 / rate * 1e3, len(pauses), len(longs), inlong, len(want)))


def main():
    if len(sys.argv) != 5:
        print('usage: livemusic.py name log data music.raw', file=sys.stderr)
        sys.exit(2)
    try:
        check(*sys.argv[1:])
    except Fail as e:
        print('livemusic: %s: %s' % (sys.argv[1], e))
        sys.exit(1)


if __name__ == '__main__':
    main()
