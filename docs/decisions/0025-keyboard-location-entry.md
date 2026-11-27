# 0025 Add keyboard entry to fleet location reports

**Status** Accepted for Phase 27

Fleet view shortcuts are available when a fleet is loaded and a text edit has released keyboard input. Ctrl+F opens and focuses the location search. Enter selects a location only when its full ID matches after trimming surrounding whitespace and folding case. Partial or mistyped queries keep the current selection available. View changes preserve the selected robot and report. Audit and motion actions keep their existing controls.

The native probe sends SDL keyboard and text events through the same event loop as the operator. It checks a mistype and a corrected Waco ID before verifying the Maintenance round trip. The idle performance probe holds Fleet on screen without dispatching jobs and excludes the first 400 ms from its frame samples.
