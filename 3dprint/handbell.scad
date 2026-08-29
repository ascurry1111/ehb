/* ============================================================================
   Wireless Handbell — Prototype Enclosure (Rod/Handle + Detachable Cone Bell)
   v4 — corrected orientation, enclosed battery pocket, positive-lock cone
   ============================================================================
   Two parts, meant to be rendered and exported to STL separately:
     - "rod"  : the 9.5" handle. z=0 is the BARE BOTTOM END you hold. z
                increases going UP toward the cone — the electronics live up
                near the top, inside the cone, as far from your hand as the
                design allows (for better swing-motion sensing). LIS3DH and
                Feather mount externally on opposite faces via standoff
                bosses; a silicone-lined battery cradle (design from your
                collaborator's Battery_Slot.scad, integrated as a
                floor-less sleeve — the rod's own surface is the pocket
                floor) sits below the LIS3DH.
     - "cone" : the detachable conical bell shell. Its narrow end has a
                square socket that slides over the top of the rod and locks
                in place with two retention screws against a stop ridge
                (see "CONE ATTACHMENT" below) — no more friction-fit alone.

   ORIENTATION FIX (this was wrong in earlier versions)
   Your original spec measures "5 inch handle" from the BOTTOM of the rod,
   with the cone starting at the 5" mark and continuing UP and OUT past the
   rod's far end. Electronics are "0.5in down from the TOP" — the top being
   that same far end, which sits INSIDE the cone once assembled. Earlier
   versions of this file had the coordinate system backwards, which put the
   electronics near the free grip end (close to your hand) instead of
   inside the cone (far from it). This version fixes that: z=0 is the plain
   grip end, z=rod_length is the end inside the cone.

   BATTERY CRADLE (from your collaborator's Battery_Slot.scad)
   Integrated as a floor-less sleeve unioned onto the rod, oriented so its
   length axis (the "long sides," 44.4mm outer — the battery-length
   direction where the wire notch sits) runs vertically, notch at the top,
   wire pointing up toward the Feather/LIS3DH. Its depth axis (originally
   the pocket's open/up direction) now points radially outward from the
   rod's surface, so the pocket opens away from the rod like a normal
   external mount, and the rod's own surface serves as the pocket's floor
   — no separate printed floor. See "BATTERY CRADLE" section below for the
   exact axis mapping and the gap adjustment this required.

   CONE ATTACHMENT — stop ridge + retention screws (replaces plain friction)
   A friction-only fit is genuinely hard to get right (too tight risks
   cracking the rod on removal; too loose and it rattles or slides), and
   gives no clear "correct position." This version adds:
     - A raised STOP RIDGE on the rod, right where the collar should end —
       the cone physically can't slide past it, so there's exactly one
       insertion depth, no guesswork.
     - Two small SELF-TAPPING SCREWS, driven through small clearance holes
       in the cone's collar into blind pilot holes in the rod. The rod's
       OUTER surface stays perfectly flat at these holes — the extra
       material needed for good thread depth is added on the INSIDE
       instead (into the hollow interior, invisible from outside), so the
       cone never has to know it's there and only needs a small clearance
       hole for the screw shank. An external boss was tried first, but it
       needed a big clearance slot in the collar to let it slide past
       during insertion, which cut into the collar's structural
       integrity — the inward version gives the same thread depth with
       none of that cost. A snap-detent click was also considered, but a
       rigid bump would have to squeeze past the entire solid collar wall
       on the way in (not just click at the end), which risks the same
       "too tight" problem this is meant to solve — screws avoid that
       entirely.
     - The collar's own clearance is loosened, since it no longer has to
       do the retention work by itself — it just has to guide the cone
       into alignment as you slide it up to the stop.
   Removal: back out two screws, then pull straight down. No force-fit,
   no risk of gouging the rod.

   HOW TO USE
     1. Open this file in OpenSCAD (openscad.org, free).
     2. Set the `part` variable below to one of: "rod", "cone", or one of
        the five test prints (see "TEST PRINTS — how to use" near the
        bottom of this file for the full list).
     3. F6 to render, then File > Export > Export as STL.
     4. Repeat with other part values for the other STLs.
     Leave `part` as "both" for a combined preview of the rod+cone only —
     don't export STL in that mode.

   ASSUMPTIONS TO VERIFY BEFORE PRINTING
     - LIS3DH mounting hole spacing is an estimate (Adafruit doesn't
       publish it) — measure your actual board and adjust lis3dh_* below.
     - Lock screw fit (clearance hole / pilot hole sizing) and the stop
       ridge's clearance are FDM-printer-dependent — see the
       "test_rod_collar" / "test_cone_collar" test prints near the bottom
       of this file for a small, focused way to check that fit.
     - The battery pocket / collar-clearance spacing has a WARNING echo
       that fires if a future change to board offsets, the battery gap, or
       collar length pushes them too close together again — currently
       there's a comfortable margin, but this has been tight before after
       parameter changes, so it's worth a glance if you adjust any of
       those.
   ============================================================================ */

part = "both";  // "rod", "cone", "both" (preview only), or one of the five test
                 // prints: "test_feather_plate", "test_lis3dh_plate",
                 // "test_battery_cradle", "test_rod_collar", "test_cone_collar"
                 // — see "TEST PRINTS — how to use" near the bottom of this file.

$fn = 64;

// ---------------------------------------------------------------------------
// UNITS
// ---------------------------------------------------------------------------
IN = 25.4;

// ---------------------------------------------------------------------------
// ROD / HANDLE
// z=0 is the bare bottom end (your hand grips here). z increases going up
// toward the cone.
// ---------------------------------------------------------------------------
rod_length    = 10.0 * IN;   // 254.0 mm, total rod length
rod_side      = 1.0 * IN;    // 25.4 mm, square cross-section — exact 1"
rod_corner_r  = 2.5;         // mm, rounded corner radius — kept modest so there's
                              // enough flat area near the edges for standoffs, since
                              // both boards' mounting holes sit close to their own
                              // edges (Feather is 22.8mm wide, LIS3DH narrower still,
                              // both comfortably inside the rod's 25.4mm width now —
                              // this value has margin to spare, not a tight fit)
rod_wall      = 3;           // mm, wall thickness of the hollow rod (weight/filament savings)
handle_length = 5.0 * IN;    // 127 mm, bare grip length from the bottom
top_cap_thick = rod_wall;    // mm — actually now at the BOTTOM (the free end); see rod_hollow()

cone_attach_z = handle_length;  // 127 mm — the cone's collar starts here and covers the rest of the rod

// ---------------------------------------------------------------------------
// ELECTRONICS MOUNTING — external standoffs, both boards' TOP edge this far
// below the rod's TOP end (which is inside the cone). Component sides face
// outward for full access to USB-C / battery connector / STEMMA QT ports.
// ---------------------------------------------------------------------------
board_offset_from_top = 0.25 * IN;  // 6.35 mm
board_top_z = rod_length - board_offset_from_top;  // 228.6 mm, shared by both boards

// --- Adafruit ESP32 Feather V2 ---
feather_length     = 2.0 * IN;   // 50.8 mm
feather_width      = 22.8;       // mm — measured on the actual board (not 1", as first assumed)
feather_hole_inset = 0.1 * IN;   // 2.54 mm — standard Feather corner hole inset
feather_hole_d     = 2.5;        // mm, clearance hole diameter — Adafruit's own mounting holes
                                  // are 0.1" (2.54mm), sized for M2/M2.5 — confirmed too small
                                  // for M3 (~3mm shaft physically won't pass through), so M2.5
                                  // stays the right choice here, not just a default.
feather_standoff_h = 4;          // mm, standoff height — real airflow behind the board
feather_standoff_d = 6;          // mm
feather_pilot_d    = 2.2;        // mm, blind pilot hole diameter — proper pilot size for M2.5
feather_bottom_z   = board_top_z - feather_length;  // 177.8 mm

// --- Adafruit LIS3DH breakout, STEMMA QT form factor ---
// length confirmed exactly 1" from your schematic; width/hole spacing
// still estimated (Adafruit doesn't publish exact hole coordinates)
lis3dh_length      = 1.0 * IN;   // 25.4 mm — confirmed via schematic
lis3dh_width       = 0.7 * IN;   // 17.78 mm — confirmed correct
lis3dh_hole_inset  = 0.1 * IN;   // 2.54 mm — estimate, matches Adafruit's standard 0.1" hole pattern
lis3dh_hole_d      = 2.5;        // mm, clearance hole diameter — standardized with feather_hole_d
lis3dh_standoff_h  = 4;          // mm — standardized with feather_standoff_h
lis3dh_standoff_d  = 6;          // mm — standardized with feather_standoff_d
lis3dh_pilot_d     = 2.2;        // mm, blind pilot hole diameter — proper pilot size for M2.5,
                                  // standardized with feather_pilot_d
lis3dh_bottom_z    = board_top_z - lis3dh_length;  // 222.25 mm

// Extra depth the standoff's base cylinder is driven INTO the rod, past the
// nominal surface — guarantees a solid, gap-free union even where a
// standoff falls near the rounded corner (where the true surface curves
// away from the flat-plane assumption). Stays well inside rod_wall so it
// never breaks into the hollow interior.
standoff_overlap = 1.5;

// ---------------------------------------------------------------------------
// CONE ATTACHMENT — stop ridge + retention screws
// A snap-detent (rigid bump clicking into a dimple) was considered, but a
// rigid bump has to squeeze past the ENTIRE solid collar wall on the way
// in (not just click at the end) since it protrudes further than the
// collar's clearance gap — that reintroduces the same "too tight, risks
// damage" problem this is supposed to solve, and relies on plastic flex I
// can't verify without a physical test. A small screw is far more
// predictable: no interference during insertion, and it's just as easy to
// remove (loosen two screws vs. forcing a snap apart).
// ---------------------------------------------------------------------------
collar_len         = 8;    // mm — SHORT on purpose: with the stop ridge and
                            // lock screws doing the positioning/retention work,
                            // the collar just needs to guide alignment, and
                            // shortening it was necessary to leave room for
                            // the battery pocket above it (see the WARNING
                            // echo below for the exact numbers).
cone_fit_clearance = 0.5;  // mm total — loosened vs. a pure friction design,
                            // since retention now comes from the lock screws,
                            // not the collar's own tightness
ridge_extra        = 1.2;  // mm the stop ridge protrudes beyond rod_side on each side
ridge_height       = 2;    // mm, height of the stop ridge

lock_pilot_d      = 2.2;    // mm, blind pilot hole into the rod — proper pilot size for M2.5,
                             // standardized with feather_pilot_d / lis3dh_pilot_d
lock_boss_d       = 6;      // mm, diameter of a small boss added to the INSIDE of the
                             // rod wall (into the hollow interior) at each pilot hole —
                             // gives the same thread depth a raised external boss would,
                             // without changing the rod's outer surface at all. The cone
                             // never has to know it's there, so it only needs a small
                             // clearance hole for the screw shank (see
                             // cone_lock_clearance_holes() below), not the big slot an
                             // external boss required — much better for the collar's
                             // structural integrity, at no cost to thread engagement.
lock_boss_inward_reach = 2; // mm the inward boss extends past the wall's own thickness —
                             // matches what the old external boss gave: total engagement
                             // = rod_wall + lock_boss_inward_reach - 1mm floor
lock_clearance_d  = 2.6;    // mm, clearance hole through the cone's collar wall

stop_ridge_z = cone_attach_z + collar_len;      // 135 mm — hard stop; cone can't slide past this
lock_z       = cone_attach_z + collar_len / 2;  // 131 mm — roughly mid-collar, on the rod's ±X faces (clear of both boards)

// ---------------------------------------------------------------------------
// BATTERY — cradle design from your collaborator's Battery_Slot.scad
// (silicone-lined, PKCELL LP503035). Dimensions below are copied verbatim
// from that file. Integrated as a mostly-floor-less sleeve unioned onto
// the rod: the rod's own outer surface serves as the pocket's floor where
// the rod reaches — but the pocket's horizontal footprint (44.4mm) is
// wider than the rod (25.4mm), so a printed floor plate fills in the two
// side strips where there's no rod underneath, flush with the rod's
// surface, so every wall has solid floor beneath it.
//
// Orientation (corrected per your reference images): the cradle's LONG
// sides — its length axis, 44.4mm outer, where the wire notch sits near
// one end — run HORIZONTALLY, perpendicular to the rod. Its WIDTH axis
// (36.9mm outer) runs vertically. The notch is positioned at the top of
// that vertical extent, so the wire still points up toward the
// Feather/LIS3DH. Its DEPTH axis still points radially outward from the
// rod's +Y face, same as before — the walls extend from the rod's
// surface, pocket opening away from the rod.
// ---------------------------------------------------------------------------
cradle_battery_w  = 30.5;
cradle_battery_l  = 38.0;
cradle_wall       = 2.0;
cradle_clearance  = 1.2;
cradle_pocket_depth = 8.0;

cradle_pocket_w = cradle_battery_w + 2*cradle_clearance;   // 32.9
cradle_pocket_l = cradle_battery_l + 2*cradle_clearance;   // 40.4
cradle_outer_w  = cradle_pocket_w + 2*cradle_wall;         // 36.9 -- vertical extent once mounted
cradle_outer_l  = cradle_pocket_l + 2*cradle_wall;         // 44.4 -- horizontal extent once mounted
cradle_notch_depth  = 5.0;
cradle_notch_height = cradle_pocket_depth;
cradle_floor_overlap = 1;  // mm the floor plate reaches INTO the rod's own
                            // footprint, past rod_side/2, so the union has
                            // guaranteed overlap rather than a knife-edge seam

batt_gap_below_lis3dh = 0.25 * IN;  // 6.35 mm — reduced from a full inch since
                                      // the longer rod (10") and reduced board
                                      // offset (0.25") shifted things around;
                                      // there's ample margin now (see the
                                      // WARNING echo below), room to loosen
                                      // this back up if you'd prefer more gap.

/* The cradle's "top" (notch end) is positioned this far below the LIS3DH's
   bottom edge. */
cradle_top_z    = lis3dh_bottom_z - batt_gap_below_lis3dh;       // 218.44 mm
cradle_bottom_z = cradle_top_z - cradle_outer_w;                 // 181.54 mm -- uses cradle_outer_w now (the vertical extent), not cradle_outer_l

if (cradle_bottom_z < stop_ridge_z + 5) {
    echo("WARNING: battery cradle is too close to the cone's stop ridge/collar — shrink batt_gap_below_lis3dh further, shorten collar_len, or increase handle_length.");
}

// ---------------------------------------------------------------------------
// CONE BELL
// ---------------------------------------------------------------------------
transition_len  = 15;   // mm, square-to-round transition length
transition_d    = 48;   // mm, circular diameter at the end of the transition
cone_wall       = 2.5;  // mm, shell thickness through the tapering section
cone_bottom_od  = 4.0 * IN;   // 101.6 mm, outside diameter at the open (top/mouth) end
cone_overhang   = 0.125 * IN; // 3.175 mm, how far the cone extends past the rod's top end
cone_total_len  = (rod_length - handle_length) + cone_overhang; // 117.475 mm
main_cone_len   = cone_total_len - collar_len - transition_len;


// ---------------------------------------------------------------------------
// SHARED HELPERS
// ---------------------------------------------------------------------------
module rounded_rect_2d(w, h, r) {
    rr = min(r, min(w, h) / 2 - 0.01);
    hull() {
        translate([-w/2 + rr, -h/2 + rr]) circle(r = rr);
        translate([ w/2 - rr, -h/2 + rr]) circle(r = rr);
        translate([ w/2 - rr,  h/2 - rr]) circle(r = rr);
        translate([-w/2 + rr,  h/2 - rr]) circle(r = rr);
    }
}

module tapered_rect_to_circle(z0, z1, w0, h0, r0, d1) {
    hull() {
        translate([0, 0, z0]) linear_extrude(height = 0.01) rounded_rect_2d(w0, h0, r0);
        translate([0, 0, z1 - 0.01]) linear_extrude(height = 0.01) circle(d = d1);
    }
}

// ---------------------------------------------------------------------------
// ROD — plain hollow square tube, rounded corners, solid cap at the BOTTOM
// (the free grip end), open at the TOP (where the cone's collar covers it).
// ---------------------------------------------------------------------------
module rod_outer() {
    linear_extrude(height = rod_length) rounded_rect_2d(rod_side, rod_side, rod_corner_r);
}

module rod_hollow() {
    inner_side = rod_side - 2 * rod_wall;
    inner_r    = max(1, rod_corner_r - rod_wall / 2);
    difference() {
        rod_outer();
        intersection() {
            translate([0, 0, -1])
                linear_extrude(height = rod_length + 2)
                    rounded_rect_2d(inner_side, inner_side, inner_r);
            // caps the BOTTOM (solid grip end), open at the top
            translate([-100, -100, top_cap_thick])
                cube([200, 200, rod_length - top_cap_thick + 1]);
        }
    }
}

// ---------------------------------------------------------------------------
// STOP RIDGE + LOCK SCREW PILOT HOLES/BOSSES (rod side)
// The rod's OUTER surface stays perfectly flat at the pilot hole locations
// — the extra material for thread depth is added on the INSIDE instead
// (into the hollow interior, invisible from outside). That's what lets the
// cone's side get away with just a small clearance hole for the screw
// shank, rather than a big slot to let an external boss slide past.
// ---------------------------------------------------------------------------
module rod_stop_ridge() {
    // Was previously a SOLID filled disc across the whole cross-section —
    // that plugged the rod's hollow interior right at this height, which
    // is exactly the "solid section blocking the interior" causing the
    // slicer to add internal supports. Fixed: this is now a ring, adding
    // material only in the thin band outside the rod's normal wall
    // (matching rod_hollow()'s own inner boundary exactly), so the hollow
    // interior stays open straight through, same as everywhere else on
    // the rod.
    inner_side = rod_side - 2 * rod_wall;
    inner_r = max(1, rod_corner_r - rod_wall / 2);
    translate([0, 0, stop_ridge_z])
        linear_extrude(height = ridge_height)
            difference() {
                rounded_rect_2d(rod_side + 2*ridge_extra, rod_side + 2*ridge_extra, rod_corner_r + ridge_extra);
                rounded_rect_2d(inner_side, inner_side, inner_r);
            }
}

module rod_lock_pilot_bosses() {
    // Extra material on the INSIDE of the wall, at the pilot hole
    // locations, spanning (and overlapping) the wall's own thickness plus
    // reaching lock_boss_inward_reach further into the hollow interior.
    total_reach = rod_wall + lock_boss_inward_reach;
    for (side = [-1, 1]) {
        translate([side * (rod_side / 2), 0, lock_z])
        rotate([0, side > 0 ? 90 : -90, 0])
            translate([0, 0, -total_reach])
                cylinder(h = total_reach, d = lock_boss_d);
    }
}

module rod_lock_pilot_holes() {
    // Same depth as the boss's own reach, minus a 1mm floor so it doesn't
    // break through into the open hollow interior beyond the boss.
    pilot_depth = rod_wall + lock_boss_inward_reach - 1;
    for (side = [-1, 1]) {
        translate([side * (rod_side / 2), 0, lock_z])
        rotate([0, side > 0 ? 90 : -90, 0])
            translate([0, 0, -pilot_depth])
                cylinder(h = pilot_depth, d = lock_pilot_d);
    }
}

// ---------------------------------------------------------------------------
// EXTERNAL STANDOFFS — a boss growing OUTWARD from the rod's solid wall,
// driven `standoff_overlap` extra into the wall for a guaranteed gap-free
// fill even near rounded corners, with a blind pilot hole (open at the
// tip, stops 1mm shy of breaking into the hollow interior).
// face_y: +1 mounts on the +Y outer face (LIS3DH), -1 on the -Y outer face (Feather).
// ---------------------------------------------------------------------------
module external_standoffs(board_len, board_wid, hole_inset, standoff_h, standoff_d, pilot_d, top_z, face_y) {
    outer_y = face_y * (rod_side / 2);
    xs = [-(board_wid/2 - hole_inset), (board_wid/2 - hole_inset)];
    zs = [top_z - hole_inset, top_z - board_len + hole_inset];

    for (x = xs) for (z = zs) {
        translate([x, outer_y, z])
        rotate([face_y > 0 ? -90 : 90, 0, 0])
        difference() {
            translate([0, 0, -standoff_overlap])
                cylinder(h = standoff_h + standoff_overlap, d = standoff_d);
            translate([0, 0, standoff_h - (standoff_h + rod_wall - 1)])
                cylinder(h = standoff_h + rod_wall - 1, d = pilot_d);
        }
    }
}

// ---------------------------------------------------------------------------
// BATTERY CRADLE — reproduces Battery_Slot.scad's wall/notch/push-slot
// logic (dimensions copied verbatim), placed directly in the rod's
// coordinate frame:
//   cradle local X (width, 36.9)    -> rod global Z (vertical) — the
//                                      notch end (high local X) lands at
//                                      HIGH global Z (top, toward the
//                                      other hardware)
//   cradle local Y (length, 44.4)   -> rod global X, centered (horizontal,
//                                      perpendicular to the rod)
//   cradle local Z (depth, minus
//     the floor's `wall` layer)     -> rod global Y (radially outward
//                                      from the rod's +Y face) — local
//                                      Z=wall (the floor boundary) maps to
//                                      global Y=rod_side/2 (the rod's own
//                                      surface) where the rod reaches.
// Each translate()/cube() below is the original file's call with its
// coordinates substituted through that mapping — done as direct per-axis
// substitution (not a 3D rotate()) to keep every number traceable back to
// the source file rather than trusting a mental rotation.
// ---------------------------------------------------------------------------
module battery_cradle_sleeve() {
    w  = cradle_wall;
    pw = cradle_pocket_w;
    pl = cradle_pocket_l;
    ow = cradle_outer_w;
    ol = cradle_outer_l;
    pd = cradle_pocket_depth;
    nd = cradle_notch_depth;
    nh = cradle_notch_height;

    y0 = rod_side / 2;   // global Y where the rod's outer surface sits

    difference() {
        // main sleeve (no floor) — old: translate([0,0,w]) cube([ow,ol,pd])
        translate([-ol/2, y0, cradle_bottom_z])
            cube([ol, pd, ow]);

        // pocket cavity — old: translate([w,w,w]) cube([pw,pl,pd+0.01])
        translate([w - ol/2, y0, cradle_bottom_z + w])
            cube([pl, pd + 0.01, pw]);

        // wire notch, near the top — old: translate([w+pw-0.01, w+pl-nd, w]) cube([w+0.02, nd, nh+0.01])
        translate([(w + pl - nd) - ol/2, y0, cradle_bottom_z + (w + pw - 0.01)])
            cube([nd, nh + 0.01, w + 0.02]);

        // bottom push-out slot — old: translate([(ow-pw*0.5)/2, -0.01, w+2]) cube([pw*0.5, w+0.02, pd])
        translate([-0.01 - ol/2, y0 + (w + 2), cradle_bottom_z + (ow - pw*0.5)/2])
            cube([w + 0.02, pd, pw * 0.5]);
    }
}

// Fills in floor material where the pocket's horizontal footprint (44.4mm)
// extends past the rod's own width (25.4mm) — two symmetric strips, flush
// with the rod's surface, so the overhanging walls (and the part of the
// cavity floor out there) actually have something solid under them.
module battery_cradle_floor_plate() {
    ol = cradle_outer_l;
    ow = cradle_outer_w;
    y0 = rod_side / 2;
    overhang = ol/2 - rod_side/2;
    if (overhang > 0) {
        for (side = [-1, 1]) {
            x_start = side > 0 ? (rod_side/2 - cradle_floor_overlap) : (-ol/2);
            x_len   = overhang + cradle_floor_overlap;
            translate([x_start, y0 - cradle_wall, cradle_bottom_z])
                cube([x_len, cradle_wall, ow]);
        }
    }
}

// The cradle's two bottom-outer corners physically overlap the cone at
// this height — verified by direct calculation, not visible from a plain
// render: the corner sits ~30.4mm from the rod's central axis (combining
// both its sideways offset and how far it sticks out radially), while the
// cone's inner surface at that height is only ~28.8mm out. About a 1.6mm
// overlap right at the corner, clearing entirely by ~6.4mm up from the
// cradle's bottom edge. Fixed with a corner chamfer (leg length below is
// rounded up from a computed minimum of ~3.25mm) rather than repositioning
// the cradle, since repositioning would fight the exact battery gap
// requested elsewhere.
cradle_chamfer_leg    = 4;  // mm
cradle_chamfer_height = 9;  // mm up from the cradle's bottom edge (computed minimum ~6.4mm)

module battery_cradle_corner_chamfers() {
    y_corner = rod_side / 2 + cradle_pocket_depth;
    for (side = [-1, 1]) {
        x_corner = side * (cradle_outer_l / 2);
        pts = side > 0 ?
            [[x_corner - cradle_chamfer_leg, y_corner], [x_corner + 2, y_corner],
             [x_corner + 2, y_corner - cradle_chamfer_leg - 2], [x_corner, y_corner - cradle_chamfer_leg]] :
            [[x_corner + cradle_chamfer_leg, y_corner], [x_corner - 2, y_corner],
             [x_corner - 2, y_corner - cradle_chamfer_leg - 2], [x_corner, y_corner - cradle_chamfer_leg]];
        translate([0, 0, cradle_bottom_z - 0.5])
            linear_extrude(height = cradle_chamfer_height)
                polygon(points = pts);
    }
}

module battery_cradle() {
    difference() {
        union() {
            battery_cradle_sleeve();
            battery_cradle_floor_plate();
        }
        battery_cradle_corner_chamfers();
    }
}

module rod_assembly() {
    difference() {
        union() {
            rod_hollow();
            rod_stop_ridge();
            battery_cradle();
            rod_lock_pilot_bosses();
        }
        rod_lock_pilot_holes();
    }
    external_standoffs(feather_length, feather_width, feather_hole_inset,
                        feather_standoff_h, feather_standoff_d, feather_pilot_d,
                        board_top_z, -1);
    external_standoffs(lis3dh_length, lis3dh_width, lis3dh_hole_inset,
                        lis3dh_standoff_h, lis3dh_standoff_d, lis3dh_pilot_d,
                        board_top_z, +1);
}

// ---------------------------------------------------------------------------
// CONE BELL — collar (with lock-screw clearance holes) -> round transition -> taper to
// the open bell mouth.
// ---------------------------------------------------------------------------
module cone_family_solid(wall_offset) {
    sq_side       = rod_side + cone_fit_clearance + 2 * (cone_wall - wall_offset);
    circ_d_trans  = transition_d - 2 * wall_offset;
    circ_r_bottom = cone_bottom_od / 2 - wall_offset;
    r_corner      = rod_corner_r;

    union() {
        linear_extrude(height = collar_len) rounded_rect_2d(sq_side, sq_side, r_corner);
        translate([0, 0, collar_len])
            tapered_rect_to_circle(0, transition_len, sq_side, sq_side, r_corner, circ_d_trans);
        translate([0, 0, collar_len + transition_len])
            cylinder(h = main_cone_len, r1 = circ_d_trans / 2, r2 = circ_r_bottom, $fn = 96);
    }
}

module cone_lock_clearance_holes() {
    // Just a small hole for the screw shank — the rod's boss is on the
    // INSIDE now (in the hollow interior), so nothing sticks out on the
    // outer surface for the cone to clear. No need for the big slot an
    // external boss required — keeps the collar's wall material almost
    // entirely intact.
    // NOTE: this runs in the cone's own LOCAL coordinates (local z=0 at
    // the collar) — cone_placed() shifts the whole piece afterward.
    local_lock_z = collar_len / 2;
    outer_x = (rod_side + cone_fit_clearance) / 2 + cone_wall + 1;  // starts just past the collar's outer surface
    for (side = [-1, 1]) {
        translate([side * outer_x, 0, local_lock_z])
        rotate([0, side > 0 ? -90 : 90, 0])
            cylinder(h = cone_wall + 2, d = lock_clearance_d);
    }
}

module cone_bell() {
    difference() {
        cone_family_solid(0);
        cone_family_solid(cone_wall);
        cone_lock_clearance_holes();
    }
}

module cone_placed() {
    // local z=0 (collar) maps directly to the rod's cone_attach_z, and
    // increasing local z (toward the bell mouth) now matches increasing
    // global z (toward the top) — no mirroring needed with the corrected
    // rod orientation.
    translate([0, 0, cone_attach_z])
        cone_bell();
}

// ---------------------------------------------------------------------------
// TEST PRINTS — four small, focused pieces to validate fits before
// committing filament to the full parts. Each reuses the real production
// geometry/parameters (not separately-tuned copies), so a good fit here
// means a good fit on the real thing.
// ---------------------------------------------------------------------------
test_plate_margin = 8;  // mm of flat plate around each board's footprint

// --- Test 1 & 2: standoff mounting plates (Feather, LIS3DH) ---
// A flat plate, standoffs growing straight up — same standoff/pilot-hole
// dimensions as the real design, just without the rod's curvature, so you
// can test-fit each board's screws before printing the whole rod.
module test_standoff_plate(board_len, board_wid, hole_inset, standoff_h, standoff_d, pilot_d) {
    plate_w = board_wid + 2 * test_plate_margin;
    plate_l = board_len + 2 * test_plate_margin;
    plate_thick = rod_wall;  // matches the real wall thickness, so pilot hole depth is representative

    xs = [-(board_wid/2 - hole_inset), (board_wid/2 - hole_inset)];
    ys = [-(board_len/2 - hole_inset), (board_len/2 - hole_inset)];

    difference() {
        union() {
            translate([-plate_w/2, -plate_l/2, 0])
                cube([plate_w, plate_l, plate_thick]);
            for (x = xs) for (y = ys)
                translate([x, y, plate_thick])
                    cylinder(h = standoff_h, d = standoff_d);
        }
        for (x = xs) for (y = ys)
            translate([x, y, plate_thick + standoff_h - (standoff_h + rod_wall - 1)])
                cylinder(h = standoff_h + rod_wall - 1, d = pilot_d);
    }
}

module test_feather_plate() {
    test_standoff_plate(feather_length, feather_width, feather_hole_inset,
                         feather_standoff_h, feather_standoff_d, feather_pilot_d);
}

module test_lis3dh_plate() {
    test_standoff_plate(lis3dh_length, lis3dh_width, lis3dh_hole_inset,
                         lis3dh_standoff_h, lis3dh_standoff_d, lis3dh_pilot_d);
}

// --- Test 3: standalone battery cradle ---
// The integrated cradle relies on the rod for most of its floor — with no
// rod here, this version gets a FULL floor across the whole footprint.
// Built directly in the cradle's own natural (w, l, depth) axes — floor
// down, walls up, open top facing up — which is both the simplest
// standalone geometry and, conveniently, the ideal print orientation
// (flat, no overhangs at all).
module test_battery_cradle() {
    w  = cradle_wall;
    pw = cradle_pocket_w;
    pl = cradle_pocket_l;
    ow = cradle_outer_w;
    ol = cradle_outer_l;
    pd = cradle_pocket_depth;
    nd = cradle_notch_depth;
    nh = cradle_notch_height;

    difference() {
        cube([ow, ol, pd + w]);  // full box, floor included (w thick) plus the pocket depth
        translate([w, w, w])
            cube([pw, pl, pd + 0.01]);
        translate([w + pw - 0.01, w + pl - nd, w])
            cube([w + 0.02, nd, nh + 0.01]);
        translate([(ow - pw*0.5)/2, -0.01, w + 2])
            cube([pw * 0.5, w + 0.02, pd]);
    }
}

// --- Test 4: rod/cone collar fit ---
// Cropped straight out of the real rod and cone geometry via intersection
// — not a hand-rebuilt approximation — so a good fit here transfers
// directly to the full parts.
test_rod_below_margin = 40;  // mm of plain handle included below the collar's start
test_rod_above_margin = 20;  // mm included above the stop ridge

module test_rod_collar() {
    z0 = cone_attach_z - test_rod_below_margin;
    z1 = stop_ridge_z + test_rod_above_margin;
    difference() {
        intersection() {
            union() {
                rod_hollow();
                rod_stop_ridge();
                rod_lock_pilot_bosses();
            }
            translate([-100, -100, z0]) cube([200, 200, z1 - z0]);
        }
        rod_lock_pilot_holes();
    }
}

test_cone_extra = 10;  // mm of main taper included past the transition, for a clean edge to hold

module test_cone_collar() {
    z1 = collar_len + transition_len + test_cone_extra;
    difference() {
        intersection() {
            cone_family_solid(0);
            translate([-100, -100, -1]) cube([200, 200, z1 + 1]);
        }
        cone_family_solid(cone_wall);
        cone_lock_clearance_holes();
    }
}

// ---------------------------------------------------------------------------
// OPTIONAL CUTAWAY VIEW
// ---------------------------------------------------------------------------
show_cutaway = false;

module maybe_cutaway() {
    if (show_cutaway) {
        intersection() {
            children();
            translate([-1000, -1000, -1000]) cube([2000, 1000, 2000]);
        }
    } else {
        children();
    }
}

// ---------------------------------------------------------------------------
// TOP-LEVEL OUTPUT
// ---------------------------------------------------------------------------
if (part == "rod") {
    maybe_cutaway() rod_assembly();
} else if (part == "cone") {
    maybe_cutaway() cone_bell();
} else if (part == "test_feather_plate") {
    test_feather_plate();
} else if (part == "test_lis3dh_plate") {
    test_lis3dh_plate();
} else if (part == "test_battery_cradle") {
    test_battery_cradle();
} else if (part == "test_rod_collar") {
    test_rod_collar();
} else if (part == "test_cone_collar") {
    test_cone_collar();
} else {
    maybe_cutaway() rod_assembly();
    color("lightblue", 0.6)
        maybe_cutaway() cone_placed();
}

/* ============================================================================
   TEST PRINTS — how to use
   Set `part` (top of file) to one of:
     "test_feather_plate"  — Feather standoff fit
     "test_lis3dh_plate"   — LIS3DH standoff fit
     "test_battery_cradle" — battery/silicone-liner fit (full floor, prints flat)
     "test_rod_collar"     — short rod stub: handle section + stop ridge + lock holes
     "test_cone_collar"    — cone collar + transition only (no main taper)
   Print test_rod_collar and test_cone_collar together to check the slide-
   to-stop feel and the screw alignment before committing to the full rod
   and cone (whatever rod_length is currently set to). Each is a normal
   F6-render-then-export-STL like the main parts — no other setup needed.
   ============================================================================ */
