# Site Awareness: from cameras to a camera system

*Idea document, longer-term direction. Written 2026-10-26 from a design
discussion and an exploratory analysis on a real three-camera install.*

## Summary

Revere today treats each camera on its own: each has its own motion ring, its
own motion tier, its own events and alerts. But cameras at one property are
looking at one place, and people and vehicles move through that place along
habitual paths. That shows up as consistent **timing between motion on
different cameras**, and it can be learned automatically, with no calibration,
floor plan or bounding boxes.

Once the system knows the property's paths it can do things no single camera
can: confirm a detection because it followed a familiar flow, spread a class
seen on one camera to the motion it is linked to on others, group a
multi-camera event into one **visit**, flag what doesn't fit, and notice when a
camera has moved or failed. A system with more information should do better
than any one camera, and this is how it gets that information.

The analysis below shows the signal is strong on real data. The proposal at the
end reframes Revere's analytics around the **site** rather than the camera, and
gives an incremental path that starts with a small, cheap step.

## Contents

- [The core idea](#the-core-idea)
- [Evidence from a real install](#evidence-from-a-real-install)
- [Learning the patterns](#learning-the-patterns)
- [From pairs to visits](#from-pairs-to-visits)
- [Real-time matching and confidence](#real-time-matching-and-confidence)
- [Spreading detections across a visit](#spreading-detections-across-a-visit)
- [A system-oriented architecture](#a-system-oriented-architecture)
- [Cost](#cost)
- [Constraints and lessons](#constraints-and-lessons)
- [Incremental path](#incremental-path)
- [Open questions](#open-questions)
- [Appendix: method and reproduction](#appendix-method-and-reproduction)

## The core idea

When two fixed cameras overlap slightly, an object crossing the overlap
triggers motion on both at the same moment. Generalise that: even when cameras
**don't** overlap, people follow typical routes (street → driveway → porch →
front door), so motion on camera A tends to be followed by motion on camera B
after a fairly consistent delay. Overlap is just the zero-lag case of a lagged
correlation between cameras.

If you histogram "time from an A motion start to nearby B motion starts", a
real path shows up as a peak at its typical travel time, and unrelated cameras
give a flat line. The sign of the lag gives direction: a peak at +Δ is A then
B, a peak at −Δ is B then A, so traffic in both directions appears in one
histogram without having to be modelled.

Prior art: camera network topology inference, e.g. Makris, Ellis & Black,
*Bridging the gaps between cameras* (CVPR 2004), which learns links between
non-overlapping views from entry/exit event timing. The cross-correlogram and
its null-model correction are standard in neuroscience (spike-train analysis).

## Evidence from a real install

Data: the per-second motion rings (`<camera>.mdb`) of a home install, read
directly. Cameras:

- **Driveway**: outdoor, looks across the driveway at the street.
- **Porch**: outdoor, on the porch, sees the front door side, part of the
  driveway, and a sliver of the street. Driveway and Porch overlap slightly near
  the mailbox at the end of the driveway.
- **desk**: indoor. Pointed at the ceiling until mid-October 2026, then at the
  front door from inside; repositioned to a stable, door-centred view on
  2026-10-26 at about 16:45.

Driveway and Porch were on motion tier 2 (camera ONVIF events), desk on tier 3.

### Driveway ↔ Porch: the street, both directions

8.8 days (Oct 17 – 26), 3,662 Driveway and 1,712 Porch motion starts:

| Lag (Porch − Driveway) | Observed pairs | Chance | Ratio |
|---|---|---|---|
| −2 s (Porch first) | 138 | 10.0 | **13.8×** |
| −1 s | 71 | 9.9 | 7.2× |
| 0 s | 47 | 9.8 | 4.8× |
| +1 s | 65 | 10.0 | 6.5× |
| +2 s (Driveway first) | 119 | 9.9 | **12.0×** |
| +3 s | 86 | 10.1 | 8.5× |

- Two sharp peaks at ±2 s: cars on the street crossing one camera's stretch of
  street and then the other's, one peak per direction. The dip at 0 s fits a
  small overlap. Excess over chance in ±1–4 s: about 515 pairs, z ≈ 40 per side.
- A weaker shoulder at 5–20 s on both sides (+53 and +88 pairs, z ≈ 4–7):
  probably pedestrians and slower traffic, a second "speed class".
- **The two directions have different daily rhythms.** Porch → Driveway peaks
  at 8am; Driveway → Porch peaks at 3–4pm. Commute or school-run traffic going
  one way in the morning and coming back the other way in the afternoon. Nobody
  told the model about time of day.

### Visits on the same two cameras

Merging both cameras' motion into episodes (gaps ≤ 15 s) gave 724 episodes
involving both cameras. Their durations split into separate populations:

- **5–10 s, a sharp spike (320):** street crossings.
- **30 s – 5 min, a long tail (173, about 20 a day):** visits that use the
  driveway: package drop-offs, cars going into the garage, arrivals.
- **Over 10 min (21):** mostly late at night with Driveway active for 10–40
  minutes and Porch barely: almost certainly infrared or weather noise with
  unrelated crossings chained in. These form their own "not a visit" cluster.

### Driveway → desk: a link that wasn't a path

Over 84 days, desk motion started 0–14 s after Driveway motion at about 2.5×
chance, only in daylight. By week, the link was **strongest (6.5–9.5×) in
August and early September, when desk was pointed at the ceiling**, and faint
once it faced the door. A camera aimed at the ceiling sees almost nothing but
room brightness changes, and the same passing cloud changes the light on the
driveway. Shared lighting creates convincing timing links. This is the main
caution for the whole approach (see [Constraints](#constraints-and-lessons)).

### Porch ↔ desk: the door, faintly

From Oct 17 (desk facing the door): a broad, slow excess. desk first by 1–10 s
(leaving: +28 pairs) and Porch first, desk 11–30 s later (arriving: +37). Small
counts, but the shape fits keys, unlocking and packages: people at a door
produce a **broad** lag distribution, unlike the tight peak of a car at steady
speed.

### Negative-control lesson

desk was meant as the negative control and turned out positive. The method
finds coupling but cannot tell *why* two cameras are coupled. A learned link is
a hypothesis until class evidence or user feedback backs it.

## Learning the patterns

Building up from simple to principled:

1. **Cross-correlogram with a jitter null.** For each camera pair, histogram
   onset lags within ±60 s. To remove shared time-of-day rhythms (both cameras
   busy at 5pm), compare against the same histogram with one camera's onsets
   randomly shifted by up to ±10 minutes. Only sharp structure survives. In
   production, an analytic baseline from hourly event rates replaces the
   Monte-Carlo null at the cost of one histogram.
2. **Mixtures per pair.** One pair can have several peaks per direction (cars
   vs pedestrians). A Gaussian mixture over lags, ideally split by class
   (person/vehicle from the camera or detection), separates them.
3. **Multivariate Hawkes process.** One model across all cameras: each camera
   has a background rate, and an event on camera i raises the rate on camera j
   by a learned kernel φᵢⱼ(τ). Fit by EM. It handles chains properly (A→B→C
   doesn't produce a spurious direct A→C link), gives each pair its own lag
   shape (sharp for cars, broad for doors), and gives each event a probability
   of having been caused by an earlier one, which is directly an anomaly score.
   Needed once there are enough cameras for pairwise analysis to get confused.
4. **Entry/exit zones (deferred).** Clustering where in each frame tracks
   start and end turns camera→camera links into zone→zone links ("leaves
   Driveway on the right, enters Porch top-right 2 s later"), which is far more
   specific. **Deferred** because tier-2 cameras give no location: ONVIF class
   events are a bare `State=true/false`, and the detection pass that produces
   YOLO boxes only runs on tier 3. Getting boxes for tier-2 cameras would mean
   adding back the decoding that tier 2 exists to avoid. If it's ever wanted:
   learn zones for a week then stop, sample a fraction of events, and use
   keyframe differencing rather than YOLO for "where".

## From pairs to visits

Pairwise links are the building blocks, but what people care about is the
**visit**: something moving through the property. Examples on this install:

| Visit | Cameras | Shape |
|---|---|---|
| Street crossing | Driveway + Porch | ~2 s apart, brief, either direction |
| Package drop-off | Driveway + Porch | up the driveway, short stop at the door, back down: a round trip; person |
| Car into garage | Driveway + Porch | one smooth pass, then nothing; vehicle |
| Arrive and go in | Driveway → Porch → desk | parking, getting out, Porch, then desk inside |

Motion-start timing alone can't separate the last three: they all begin
"Driveway, then Porch shortly after". What separates them is the whole shape of
the visit, which is where clustering fits:

1. **Segment** the timeline into visits: merge motion on linked cameras when
   gaps are below a threshold, using the learned graph to decide which cameras
   are related.
2. **Describe** each visit: cameras touched and in what order, active time per
   camera, overlap, whether activity returned to the first camera (round
   trip), total duration, classes seen, time of day.
3. **Cluster** the descriptions into recurring visit types. The most useful
   output is often the outlier: a visit that fits nothing known (someone
   lingering at the door for 5 minutes; desk activity with no outdoor approach).

Labelling a few dozen visits (by watching them) is the cheapest way to check
which features separate the types and whether class is needed.

## Real-time matching and confidence

### Matching

Each learned route is a small chain of steps, each with a camera, optional
class, and the lag distribution from the previous step:

```
"arrive and go in":  Driveway(vehicle) ──2–60 s──▶ Porch(person) ──10–30 s──▶ desk(person)
```

The matcher keeps a short list of open, partly matched routes. On each event:

1. **Extend** open routes whose next step is this camera, scored by how well
   the gap fits that step's lag distribution.
2. **Start** new candidates where the event can be a route's first step.
3. **Expire** candidates that have waited past their next step's maximum lag.

On a few cameras that is a handful of candidates and a few lookups per event:
microseconds.

### Confidence boost

Bayesian: the event's own confidence is the prior, and the timing is evidence
as a likelihood ratio (how likely is this gap for a real crossing vs chance?),
which the correlograms give directly.

```
posterior odds = prior odds × LR(timing)
```

| | Probability | Odds |
|---|---|---|
| Porch reports "person" (camera's own track record) | 0.60 | 1.5 |
| …18 s after a Driveway vehicle, at the arrival peak (LR ≈ 6) | **0.90** | 9 |
| …and desk sees a person 20 s later (LR ≈ 4) | **0.97** | 36 |

Design points:

- **Tier-2 classes have no confidence number.** The camera just says yes or
  no. The prior has to be a per-camera, per-class hit rate; the tier controller
  already measures something close when verifying a camera's person reports
  against YOLO.
- **Boosts can arrive late.** A later event is boosted immediately (its
  predecessor is known); an earlier event only once its follower arrives.
  Store the raw score at once and revise it when the route completes or
  expires. Alerts choose: fire on the raw score, or wait a few seconds for
  confirmation.
- **Missing follow-ups count little against.** Cameras miss things; people
  stop or turn round. An incomplete route mostly just doesn't boost.
- **Store the explanation**: raw and boosted score, matched route and step,
  linked earlier event, and the LR used, so the UI can say "Person 0.60 → 0.90:
  followed the usual Driveway → Porch arrival 18 s later."
- **Learn from raw events only**, never from boosted scores, or a route keeps
  confirming itself.
- **Unfamiliar sequences** fall out of the same scoring as "best route match is
  very poor", and may be the more valuable alert.

## Spreading detections across a visit

A single good detection is strong evidence about the object, and the timing
match is what says it is the same object. So one detection anywhere in a
matched chain can label the whole chain. Even if a person is only detected on
desk, the Driveway and Porch motion that matched the route gets that class too,
with **no decoding and no YOLO**. This is how the system gives classes to plain
motion on tier-2 cameras.

Refinements:

1. **Label the visit, not each event.** In "arrive and go in", the Driveway
   motion is usually the car; the person only appears once they get out. Each
   event becomes "part of a visit that contained a person", rather than "a
   person". With per-step classes in the route, step 1 can also be labelled
   "likely vehicle".

   ```
   Visit #812  (matched "arrive and go in")
     ├─ Driveway motion  14:02:10   step 1
     ├─ Porch motion     14:02:31   step 2
     └─ desk person      14:02:52   step 3  ◀ the detection
     classes in visit: person (from desk)
   ```

2. **The label is only as strong as the link.**
   `P(event belongs to this person's visit) = P(link real) × P(detection right)`.
   Tight, established links pass the label at nearly full strength; loose
   windows on a busy afternoon pass it weakly; long chains multiply the
   uncertainty, as they should.
3. **Concurrent activity.** A car passing on the street while someone walks to
   the door gives two overlapping chains. A detection labels only the chain it
   best matches; step classes help keep a street crossing from absorbing a door
   label.
4. **Labels arrive after the fact.** Fine for history and search; live alerts
   on the first camera still use what is known at that moment.

Boosting and spreading are the same operation once visits are stored: evidence
from any event flows through the visit's links.

Offline check available now: tier-2 class events are already stored as
`camera_events` metadata, so past data can be replayed to count how many
motion-only events would gain a class by propagation.

## A system-oriented architecture

The reframe: stop treating Revere as independent cameras sharing a process.
Cameras are sensors looking at one place; the system reasons about **what is
happening at the property**.

| Camera-oriented (today) | System-oriented |
|---|---|
| Unit: camera event | Unit: **visit** |
| Each camera decides alone | Cameras report observations; the system decides |
| One alert per camera per detection | One alert per visit, updated as it unfolds |
| Confidence from one view | Confidence from all views plus learned patterns |
| Tiers, YOLO, decoding planned per camera | Detection effort spent where most informative |
| Camera health = stream status | Health also = still fits its usual patterns |

### Layers

1. **Observations**: raw, cheap, uniform. Every source (ONVIF events, MOG2,
   detection pass, YOLO motion plugin) emits the same small record: camera,
   source, what (motion/person/vehicle/...), start/end in milliseconds,
   confidence where it exists. One append-only log per site. No
   interpretation.
2. **Site model**: learned, slowly changing. The camera graph (links, lags,
   per class, by time of day), visit types, per-camera noise profiles
   (lighting, wind, night IR), and per-source reliability. Relearned nightly
   with decay; reset for a camera when its view changes.
3. **Visit engine**: live inference. Consumes observations, keeps open route
   candidates, groups observations into visits, spreads classes and
   confidence. The matcher and propagation above, as the core rather than a
   bolt-on.
4. **Outputs**, all visit-based: alerts, timeline, search, and the record the
   MCP tools / LLM read ("who came by today?"). "Person arrived by car, went
   inside, 14:02" instead of three motion alerts. The per-camera view stays
   available underneath.

### What the system view adds

- **Fewer, better alerts.** One per visit. Motion that no visit explains and no
  class supports is likely environmental and can be suppressed.
- **Site-wide detection budget.** If a visit is already a confirmed person from
  desk, don't spend YOLO on Porch. If a Driveway event is ambiguous and usually
  leads to Porch in 18 s, spend the look on the Porch keyframe where the person
  will be close and large. Actively choosing where to look suits the Raspberry
  Pi target. Tiers could become partly site-level: a camera with weak classes
  can be covered by a neighbour.
- **Camera health.** A camera that stops taking part in its usual routes has
  been moved, blocked or broken. desk's links changed character exactly when
  it moved.
- **Blind spots.** A consistent unexplained gap within a route (40 s nobody
  sees between Driveway and desk) shows where coverage is missing: a concrete
  "add a camera here" suggestion.
- **Prediction.** "A visit is heading for the door" can prioritise that
  camera's stream, pre-roll it in vision, or pick which camera the app shows.
- **Clock alignment.** Each source's typical detection delay (ONVIF pull
  latency vs MOG2) can be learned from the links and corrected, putting all
  cameras on one timeline.

## Cost

Light. The work runs on event start times, not video.

| Piece | Measured / estimated cost |
|---|---|
| One pair's correlogram, 84 days (~16k onsets) | **13 ms** in Python (measured) |
| Full relearn, 16 cameras (120 pairs) | ~1–2 s CPU, nightly or incremental |
| Hawkes fit, a week of events, 16 cameras | < 1 s (each event interacts with a handful of others) |
| Live scoring / matching per event | microseconds |
| Model storage | ~1.4 MB dense for 120 pairs × 121 lags × 24 h; far less sparse |
| Millisecond observation log | a few KB per camera per day |

The only expensive variant is zone learning on tier-2 cameras (needs decoding),
which is deferred. Everything else fits on a Pi without breaking tier 2's
"no decoding in steady state" promise.

## Constraints and lessons

- **Coupling isn't causation.** Shared lighting (clouds, sun) links cameras
  convincingly in daylight; the ceiling-facing desk showed this. Mitigations:
  learn routes from **class-backed** events (person/vehicle) rather than raw
  motion; flag a camera whose motion correlates broadly with many others at
  short lags in daylight as weather, not a path.
- **Cameras move.** Links learned before a move are meaningless afterwards.
  Decay old evidence, and detect moves (a camera's link pattern or scene
  changing abruptly) to reset that camera's part of the model.
- **Cameras weren't placed for this.** Real installs have tiny overlaps,
  targets in frame corners, and neighbours' activity in view. The method has
  to cope with whatever it gets; placement advice (blind spots, a door in a
  corner) is a side benefit.
- **Detection source changes shift lags.** ONVIF events and MOG2 have different
  delays; re-tiering a camera changes its lags and needs relearning (or learned
  per-source offsets).
- **Timestamp precision.** The motion rings are 1-second flags, which blurs
  peaks and makes order within a visit unreliable ("same second"). A
  millisecond observation log fixes it.
- **Data takes time.** The street link was clear in 9 days because cars are
  frequent; door links need weeks. Cold start must fall back to today's
  per-camera behaviour, with visit features only adding.
- **Busy places.** On a busy street, chance pairs inside a lag window grow; the
  null model and class gating matter more as activity rises.

## Incremental path

1. **Observation log.** Write every motion start/end, camera class and
   detection, from all cameras, with millisecond timestamps and source, to one
   site-level append-only log. Small, cheap, useful immediately (sharper
   timing, cross-camera timeline) and the foundation for everything else.
2. **Offline site model.** Learn the graph (correlograms with an analytic
   null, then Hawkes) and visit types nightly from the log. Replay history to
   measure the gain: how many motion-only events gain a class, how many alerts
   collapse into visits.
3. **Visit engine in shadow mode.** Run live and record visits without
   affecting alerts, the same pattern the detection pass used. Compare against
   per-camera alerting.
4. **User-facing visits.** Visit-based alerts, timeline and search, with
   explanations and user feedback ("not one visit", "mail carrier") feeding
   back into learning.

## Open questions

- Where does the site model live for cloud-connected installs: on-box only,
  or synced? Visits are the natural unit to sync and notify on in the cloud;
  they also make tenant isolation in belfry even more important.
- What does a user see when a visit merge is wrong, and how do they correct it?
- How should alerts trade speed against confirmation: fire on the first
  camera, or wait a few seconds for the route to confirm?
- Per-camera reliability priors for tier-2 classes: derive from the tier
  controller's verification stats, or learn continuously from cross-camera
  agreement?
- Multi-site installs (several properties on one Revere): one site model per
  property; how are cameras assigned to sites?

## Appendix: method and reproduction

**Motion ring format** (`r_storage::r_ring`, `RING_MOTION_FLAG_SIZE = 1`): a
4-byte little-endian `created_at` (epoch seconds), then one byte per second
(0 = no motion, 1 = motion). The element for second `t` is at index
`(t - created_at) % n_elements`; only the last `n_elements` seconds are valid.
Tier-2/3 rings are written by `r_recording_context` from camera events, tier 1
by `r_motion_engine` from MOG2; in both cases a motion run is written when it
ends.

**Onsets**: 0→1 transitions in the per-second flags.

**Correlogram**: for each A onset, all B onsets within ±W s; histogram of
`b − a` in 1 s bins.

**Jitter null**: the same with each B onset shifted by a uniform random
integer in ±600 s, 300 repetitions; report the mean and 99.5th percentile per
bin. A bin is flagged when observed > 99.5th percentile and ≥ 5 pairs. With
~121 bins a few isolated flags are expected by chance.

**Episodes**: OR the cameras' flags, merge runs separated by gaps ≤ 15 s.

Script used for the analysis (reads the rings read-only; needs numpy):

```python
"""usage: python xcorr.py <video_dir> <camA> <camB> [window_s] [out.json]"""
import json, struct, sys, time
import numpy as np

def load_ring(path):
    with open(path, 'rb') as f:
        b = f.read()
    return struct.unpack('<I', b[:4])[0], np.frombuffer(b, dtype=np.uint8, offset=4)

def extract(ring, t0, t1):
    created, data = ring
    idx = (np.arange(t0, t1, dtype=np.int64) - created) % len(data)
    return (data[idx] != 0).astype(np.int8)

def onsets(flags, t0):
    return np.nonzero(np.diff(flags, prepend=0) == 1)[0].astype(np.int64) + t0

def correlogram(a, b, w):
    b = np.sort(b)
    lo, hi = np.searchsorted(b, a - w, 'left'), np.searchsorted(b, a + w, 'right')
    lags = np.concatenate([b[l:h] - x for x, l, h in zip(a, lo, hi)]) if len(a) else np.array([], np.int64)
    return np.bincount((lags + w).astype(np.int64), minlength=2 * w + 1)

def main():
    vdir, ca, cb = sys.argv[1:4]
    w = int(sys.argv[4]) if len(sys.argv) > 4 else 60
    out = sys.argv[5] if len(sys.argv) > 5 else None
    ra, rb = load_ring(f'{vdir}/{ca}.mdb'), load_ring(f'{vdir}/{cb}.mdb')
    now = int(time.time())
    # both rings live, and within both rings' retention
    t0 = max(max(ra[0], rb[0]) + 120, now - min(len(ra[1]), len(rb[1])) + 10)
    oa, ob = onsets(extract(ra, t0, now), t0), onsets(extract(rb, t0, now), t0)
    obs = correlogram(oa, ob, w)
    rng = np.random.default_rng(1)
    null = np.array([correlogram(oa, ob + rng.integers(-600, 601, len(ob)), w) for _ in range(300)])
    mu, hi = null.mean(0), np.percentile(null, 99.5, axis=0)
    for lag, o, m, h in zip(range(-w, w + 1), obs, mu, hi):
        print(f'{lag:+5d}s {o:6d} {m:7.1f} {h:7.1f} {o / max(m, 0.1):6.2f}{"  <==" if o > h and o >= 5 else ""}')
    if out:
        json.dump({'lags': list(range(-w, w + 1)), 'obs': obs.tolist(), 'null_mean': mu.tolist(),
                   'null_hi': hi.tolist(), 'onsets_a': oa.tolist(), 'onsets_b': ob.tolist(),
                   't0': t0, 't1': now}, open(out, 'w'))

if __name__ == '__main__':
    main()
```

On Windows the rings are under `%LOCALAPPDATA%\revere\revere\video\`.
