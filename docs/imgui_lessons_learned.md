# Dear ImGui: Lessons Learned

Vision's UI is built on [Dear ImGui](https://github.com/ocornut/imgui), an immediate-mode GUI library. This document records what that choice got right, where it has hurt, and the direction we want to take as the UI grows. It is meant for anyone adding to or reorganizing Vision's UI code.

## Contents

- [What worked](#what-worked)
- [Where it hurts](#where-it-hurts)
- [Immediate vs. retained mode: what the choice really is](#immediate-vs-retained-mode-what-the-choice-really-is)
- [Direction: keep ImGui, add structure](#direction-keep-imgui-add-structure)
- [Alternatives considered](#alternatives-considered)
- [First steps](#first-steps)

## What worked

- **We own the main loop.** Vision decides exactly when frames are decoded, uploaded and presented. For a video application that matters: the presentation pacing in `pipeline_host::load_video_textures()` would be much harder to control inside a framework that owns the event loop.
- **One source of truth.** The UI is redrawn from application state every frame, so it cannot drift out of sync with the data. The classic retained-mode bug, where the model changes but a widget isn't updated (or the reverse), mostly can't happen.
- **Custom controls are easy.** The timeline in the control bar, with its motion and detection icons, drag-to-scrub and live scrolling, is drawn directly with ImGui's draw list. The same control in a traditional widget toolkit would have been considerably more work.
- **It's small.** ImGui is a handful of source files with no runtime dependencies beyond a renderer backend. Simple UI really is simple to write.

## Where it hurts

Immediate mode doesn't create messy code by itself. What it does is make it easy to mix three different concerns in the per-frame function: **application state**, **side effects** (network and disk I/O), and **layout**. Nothing forces them apart, so as the UI grows they tangle. Each of the following is a real example from Vision.

### State hides in function-local statics

`control_bar()` in `apps/vision/include/imgui_ui.h` keeps state in `static` variables: `last_timeline_update`, `last_live_scroll_update`, and a `static control_bar_renderer`. Its tooltip code does the same (`s_tooltip_hover_idx`, `s_tooltip_hover_start`). This state is invisible to callers, can't be inspected or reset, and silently carries over when the control bar switches to a different stream. (Because `control_bar()` is a template, each call site also gets its own separate copy of these statics, which is one more thing a reader has to know to reason about it.) It is the most common way immediate-mode code turns into spaghetti.

### I/O ends up inside drawing

Because everything happens in the frame function, it is tempting to do work inline. The control bar's "update data" callback used to run three blocking HTTP queries (segments, motion events, analytics) directly from the render loop. When Revere was slow, or exited in the middle of a request, the whole window froze until the socket timed out, and overlay windows could not even be closed. The fix was architectural: a background worker in `pipeline_host` (`_timeline_entry_point`) now does the fetching, and the frame function only applies whatever result is ready. The general rule is in [I/O never runs on the render thread](#3-io-never-runs-on-the-render-thread).

### There is no real layout engine

ImGui positions things with pixel arithmetic. The control bar places its volume slider at `right_edge - 560.0f`. That worked in the full-width main window, but in the narrow overlay window it collided with the time-range text, and the overlay had to suppress the built-in slider and draw its own. Absolute positioning is where ImGui UIs become brittle, because controls cannot express "I need this much room" or adapt when they don't get it.

### Forms and multi-step flows are awkward

Dialogs and wizards, such as the camera setup wizard in `apps/revere/source/main.cpp`, become hand-written state machines driven by string keys and popups. It works, but this is the kind of UI where retained-mode toolkits are genuinely better.

## Immediate vs. retained mode: what the choice really is

It is often said that a retained-mode GUI must own the main loop. That isn't inherent. It's an assumption made by the big desktop toolkits. Many retained-mode libraries are designed to be driven from your own loop: you feed them input events, call an update (or `pump()`) method, and tell them when to render. Game UI libraries such as RmlUi and CEGUI work this way, and even Qt can be driven by calling `processEvents()` from your own loop, although it isn't designed around that.

So keeping control of the loop doesn't rule out retained mode. What we would actually lose by moving to a large retained-mode framework is:

- **Easy custom controls.** Drawing the timeline with a draw list is far simpler than writing a custom widget with its own paint, layout and event handling in a toolkit like Qt.
- **Smallness.** Qt is a fine product, but bringing it in for Vision would add far more weight than the problem justifies.

What we actually want from retained mode is narrower: **state that lives with the component that owns it**, and **a real layout system**. Both can be added on top of ImGui.

## Direction: keep ImGui, add structure

The plan is to keep ImGui as the drawing and input layer, and to add a small amount of structure as code is touched, rather than adopting a framework.

### 1. Small, state-owning view classes

Each non-trivial piece of UI becomes a small class that owns its state, instead of a free function with `static` locals. `control_bar_state` is already halfway there.

```cpp
class control_bar_view
{
    control_bar_state _s;          // all of this control's state; no function-local statics
public:
    std::vector<intent> draw(const ImRect& box);
    void apply(const intent& i);
};
```

The top-level loop holds these views and calls them each frame. That per-frame call is our `pump()`.

### 2. Drawing emits intents; one place changes state

Draw code reads state and reports what the user asked for ("seek to 3:42", "go live", "set volume to 0.5"), rather than calling the pipeline or the network directly. A single update step applies those intents.

```cpp
auto intents = control_bar.draw(box);   // reads state, emits intents
for(auto& i : intents)
    control_bar.apply(i);               // the only place state changes
```

This is essentially the Elm/Redux architecture run once per frame. It keeps the advantages of immediate mode (the main loop, a single source of truth, easy custom drawing) while making it obvious where state changes.

### 3. I/O never runs on the render thread

Any network or disk work goes to a worker, which posts its result back. The frame function applies the latest available result and never waits. `pipeline_host`'s timeline fetcher is the reference example: requests are coalesced per stream (only the latest wanted range is kept), and results are applied by the UI thread when ready.

### 4. A layout engine, with full control inside each control

The layout engine decides **where each control's box is and how big it is**. Everything inside the box belongs to the control. Because the layout only hands out a rectangle, a control can draw however it wants inside it; overriding layout is not a special case. Flutter's `CustomPaint`, the web's `<canvas>` and Qt's `paintEvent` all follow this same contract.

To make that work, controls also need to report their size before they are placed:

```cpp
class view
{
public:
    virtual ~view() = default;
    virtual ImVec2 measure(ImVec2 available) = 0; // "how big would I like to be?"
    virtual void   draw(const ImRect& box) = 0;   // "here's your box; anything goes inside it"
};
```

Container views (rows, columns, stacks) implement `measure` and place their children. Leaf controls such as the timeline simply draw inside `draw()`.

Two consequences:

- **Controls adapt instead of colliding.** The control bar can report a minimum comfortable width. When it's given less, it hides the volume slider itself, rather than drawing over the time-range text as it did in the overlay.
- **Overriding layout is per region, not per control.** A single control can use row layout for its buttons (Play, Go Live, Export) and switch to raw draw-list code for the timeline track.

Note that `control_bar(left, top, width, height, ...)` already receives its rectangle from outside, so half of this boundary exists today. What's missing is the other half: those rectangles currently come from hand-written arithmetic in `main.cpp`, and controls have no way to say what size they need.

## Alternatives considered

| Option | Why not (for now) |
|--------|-------------------|
| **Qt** (Widgets or Qt Quick) | Very capable, but large, LGPL-licensed, and custom drawn controls are much more work than with ImGui. Far more than Vision needs. |
| **Slint** | Declarative and reactive, closest to "more state, cleaner code", but its licensing needs a careful read for an open-source project, and custom drawing is less direct. |
| **RmlUi** | Retained-mode, HTML/CSS-like documents in C++, MIT-licensed, and it lets us keep our own loop. Solves layout properly, but custom drawn controls like the timeline become noticeably harder. |
| **Clay** | A single-header C layout library with flexbox-style rows, columns, padding and alignment. It only computes rectangles and has no renderer, so in principle it could provide the layout half of section 4 while ImGui keeps drawing. Not yet evaluated with ImGui; worth a spike before writing our own. |

The comparison point we already have is Vision's sibling, the Vue web UI: reactive, declarative, with a single source of truth and real layout. That's the best of both worlds, but it runs on the framework's loop, not ours.

## First steps

These can be done incrementally, as the code is touched:

1. **Move `control_bar()`'s hidden statics into `control_bar_state`** (`last_timeline_update`, `last_live_scroll_update`, the tooltip hover state, and the `control_bar_renderer` instance). This is the highest-value, lowest-risk change.
2. **Keep all I/O off the render thread.** Treat any blocking call reachable from a draw function as a bug.
3. **Wrap new or heavily modified UI in small view classes** that own their state and emit intents, rather than adding more free functions with callbacks.
4. **Introduce `measure`/`draw` when we next fight a layout problem.** Start with the control bar, since it already takes its rectangle as parameters and already has to work at two very different widths (main window and overlay).
