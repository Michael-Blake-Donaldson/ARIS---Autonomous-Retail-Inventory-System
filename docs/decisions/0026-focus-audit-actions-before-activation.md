# Focus audit actions before activation

**Status** Accepted for Phase 28

Location Audits uses F9 to focus Mark reviewed and F10 to focus Re-audit location. The focused button requires a separate activation. The keyboard and pointer share the same buttons and Fleet commands, including existing checks for the latest report, pending review, storage errors and robot availability.

The UI ignores these focus requests while text input is active. Each request applies to one dashboard draw and is dropped on focus loss. The report's action buttons require an active window. A request for an unavailable action leaves the current focus in place, so the operator should check the visible focus before activating a button.
