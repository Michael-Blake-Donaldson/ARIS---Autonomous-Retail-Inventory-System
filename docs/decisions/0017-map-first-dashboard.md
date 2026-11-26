# 0017 Share fleet selection across operator views

**Status** Accepted for Phase 18

## Context

Fleet displayed a large selected-robot scene alongside a small fleet schematic. Location review put product details in a narrow sidebar. Operators had to switch views to see the status and saved results for a shelf.

## Decision

Use one top-down map on Fleet. The map projects two observed robot estimates and the selected robot's route onto the authored shelf layout. It uses aisle labels at the overview scale and shelf IDs at closer scales. Selectable map labels and the robot list share the same selection. The selected shelf's summary has a direct action for opening its most recent audit.

Give the audit product table the main content area and keep search and shelf status in a side panel. Show photos, names, DPCIs and quantity columns up front. Expand EPC detail and audit history on demand. Paginate at 20 products so a large report has bounded per-frame row rendering.

Keep the native scene in Robot Maintenance and the existing single-robot workspace. Maintenance uses the original LiDAR and trail overlays and preserves the current selection. Fleet shows an observed navigation hold reason or a reported operator hold beside each robot.

## Consequences

Fleet uses curated shelf geometry for the two stockroom locations. The top-down map and product table render through the existing ImGui backend. Shelf evidence uses the Phase 17 schema and limits. The wider audit table and separate details panel fit a 1280 × 720 native window. The native reopening probe interacts with the same controls as a user.
