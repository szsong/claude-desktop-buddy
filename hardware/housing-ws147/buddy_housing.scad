// Claude buddy housing for the Waveshare ESP32-C6-LCD-1.47 (no pin header).
//
// Two printed parts:
//   shell  - front bezel with screen window, all four side walls, USB-C
//            opening and two flex buttons (BOOT left, RST right).
//   plate  - back plate. The board screws onto it with 4x M2x4 pan-head
//            screws into the board's own threaded posts, then the plate
//            snaps into the shell (screen first).
//
// Coordinates follow the board, all in mm:
//   x across the board (0..pcb_w), y along it (0 = USB-C edge),
//   z = 0 on the PCB top face, +z toward the screen.
// Board numbers come from Waveshare's STEP model and drawing
// (ESP32-C6-LCD-1.47-3D_Drawing.rar), checked against each other.
//
// Render one part with:  openscad -D 'part="shell"' -o shell.stl buddy_housing.scad

part = "assembly";   // "shell" | "plate" | "assembly" | "exploded" | "board"
print_layout = true; // flip the shell bezel-down for printing

/* [Board] */
pcb_w        = 20.32;
pcb_l        = 36.37;
pcb_t        = 0.80;
screen_z     = 4.30;   // screen glass face above PCB top
post_z       = -3.30;  // bottom of the M2 mounting posts
usb_z        = -4.05;  // lowest point (USB-C shell)
// active area of the LCD, from the PCB's lower-left corner
active_x0    = 1.47;   active_w = 17.39;
active_y0    = 3.02;   active_l = 32.35;
// M2 posts (3.5 dia), [x, y]
posts = [[2.00, 2.40], [pcb_w - 2.00, 2.40],
         [3.52, pcb_l - 1.97], [pcb_w - 3.52, pcb_l - 1.97]];
post_d       = 3.5;
// USB-C receptacle, centred on the y = 0 edge, under the PCB
usb_cx       = pcb_w / 2 - 0.02;
usb_w        = 9.58;
usb_h        = 3.25;               // shell, from usb_z up to the PCB back
usb_cz       = usb_z + usb_h / 2;
usb_out      = 1.21;               // overhang past the PCB edge
usb_len      = 7.71;
// side-push BOOT (x = 0 side) and RST (x = pcb_w side)
btn_y        = 7.04;
btn_z        = -1.50;
btn_len      = 4.55;
btn_h        = 2.20;
btn_out      = 1.19;
flex_out     = 0.64;               // screen flex past the USB edge
// Sean measured the USB-C and button overhang at roughly 1.4 by eye, so the
// push-in path clears this larger value. The STEP numbers above stay the
// nominal rest position.
fit_out      = 1.40;
board_out    = 0;     // board() stand-in only: overhang to model (0 = STEP)
sweep        = 0;     // board() stand-in only: sweep this far back (push-in check)

/* [Case] */
wall         = 1.6;
bezel        = 1.2;   // front plate thickness
screen_gap   = 0.10;  // glass to bezel
side_clr     = 0.30;  // button tip (at fit_out) to inner wall
end_clr      = 0.30;  // board end / flex to inner wall
r_out        = 2.5;   // outer corner radius
edge_ch      = 0.8;   // chamfer on the front edge
win_margin   = 0.30;  // window grows past the active area on each side
win_ch       = 0.9;   // window chamfer toward the viewer

plate_t      = 1.6;
boss_h       = 1.2;   // posts sit on these, keeps USB-C off the plate
boss_d       = 4.2;
screw_d      = 2.3;
head_d       = 4.4;   // counterbore for M2 pan head
head_cb      = 0.8;
plate_clr    = 0.15;  // plate edge to wall, per side
snap         = 0.30;  // bead height on the plate edge
snap_clr     = 0.10;

// flex buttons cut into the long walls: vertical tabs, hinged at the bezel
// end so both long slits print vertically with the shell bezel-down
tab_w        = 5.0;   // along y
tab_t        = 0.8;   // thinned from the inside so it bends
tab_slot     = 0.6;
nub_gap      = -0.05; // nub tip at fit_out - 0.05: 0.16 off the STEP's 1.19,
                      // still clear of it while the board slides in
nub_w        = 2.0;
nub_h        = 1.6;
labels       = true;

// USB-C opening: tight hole through the inner skin, open to the back so
// the connector slides in, and plug-overmold relief through the outer skin.
// A tongue on the back plate closes the slot under the connector.
usb_hole_w   = usb_w + 0.8;
usb_hole_h   = usb_h + 0.8;
usb_rel_w    = 12.0;
usb_rel_h    = 6.4;
usb_rel_d    = 1.0;

$fn = 48;

// derived
in_x0  = -(max(btn_out, fit_out) + side_clr);
in_x1  = pcb_w + max(btn_out, fit_out) + side_clr;
in_y0  = -(max(flex_out, 0) + end_clr + 0.0);
in_y1  = pcb_l + end_clr;
top_in = screen_z + screen_gap;            // underside of the bezel
top_z  = top_in + bezel;                   // front face
plate_top = post_z - boss_h;
bot_z  = plate_top - plate_t;              // back face
out_x0 = in_x0 - wall;  out_x1 = in_x1 + wall;
out_y0 = in_y0 - wall;  out_y1 = in_y1 + wall;
snap_z = bot_z + plate_t / 2;
skin   = wall - usb_rel_d;                 // inner skin of the USB wall

assert(max(usb_out, fit_out) < -in_y0 + skin - 0.05,
       "USB-C would reach the plug relief");
assert(usb_cz - usb_hole_h / 2 > plate_top, "USB slot bottom below the plate");

echo(str("outer size ", out_x1 - out_x0, " x ", out_y1 - out_y0,
         " x ", top_z - bot_z, " mm"));
echo(str("screw: M2 pan head, thread into post = 4 - ",
         plate_t - head_cb + boss_h, " = ",
         4 - (plate_t - head_cb + boss_h), " mm of 2.5 available"));

module rrect(x0, y0, x1, y1, r) {
    translate([x0 + r, y0 + r]) offset(r) square([x1 - x0 - 2 * r, y1 - y0 - 2 * r]);
}

module rbox(x0, y0, z0, x1, y1, z1, r) {   // box rounded around its y axis
    hull() for (x = [x0 + r, x1 - r], z = [z0 + r, z1 - r])
        translate([x, y0, z]) rotate([-90, 0, 0]) cylinder(r = r, h = y1 - y0);
}

// ---------------- shell ----------------

module shell_outer() {
    hull() {
        translate([0, 0, bot_z]) linear_extrude(top_z - edge_ch - bot_z)
            rrect(out_x0, out_y0, out_x1, out_y1, r_out);
        translate([0, 0, top_z - edge_ch]) linear_extrude(edge_ch)
            rrect(out_x0 + edge_ch, out_y0 + edge_ch,
                  out_x1 - edge_ch, out_y1 - edge_ch, r_out - edge_ch);
    }
}

module window() {
    x0 = active_x0 - win_margin;  x1 = active_x0 + active_w + win_margin;
    y0 = active_y0 - win_margin;  y1 = active_y0 + active_l + win_margin;
    hull() {
        translate([0, 0, top_in - 0.01]) linear_extrude(0.01 + bezel - win_ch)
            rrect(x0, y0, x1, y1, 0.6);
        translate([0, 0, top_z - 0.01]) linear_extrude(0.02)
            rrect(x0 - win_ch, y0 - win_ch, x1 + win_ch, y1 + win_ch, 0.6 + win_ch);
    }
}

// U-shaped slot plus the inside thinning, for one wall.
// side = -1 for the BOOT wall (x < 0), +1 for the RST wall.
function nub_tip(side) = side < 0 ? -max(btn_out, fit_out) - nub_gap
                                   : pcb_w + max(btn_out, fit_out) + nub_gap;
function tab_face(side) = (side < 0 ? in_x0 : in_x1) + side * (wall - tab_t);
function nub_d(side) = abs(nub_tip(side) - tab_face(side));
// free end of the tab, just past the bottom of the nub's lead-in ramp
function tab_z0(side) = btn_z - nub_h / 2 - nub_d(side) - 0.2;

// U-shaped slot (two vertical slits joined below the free end) plus the
// inside thinning, for one wall. side = -1 for BOOT (x < 0), +1 for RST.
module tab_cut(side) {
    y0 = btn_y - tab_w / 2;
    z0 = tab_z0(side);
    xo = side < 0 ? out_x0 : out_x1;
    xi = side < 0 ? in_x0  : in_x1;
    translate([min(xo, xi) - 0.1, y0 - tab_slot, z0 - tab_slot])
        difference() {
            cube([wall + 0.2, tab_w + 2 * tab_slot, top_in - z0 + tab_slot]);
            translate([-0.1, tab_slot, tab_slot]) cube([wall + 0.4, tab_w, top_in - z0 + 1]);
        }
    // thin the tab from the inside
    translate([side < 0 ? xi - (wall - tab_t) : xi - 0.01, y0, z0])
        cube([wall - tab_t + 0.01, tab_w, top_in - z0 + 0.01]);
}

module tab_add(side) {
    xf = tab_face(side);
    tip = nub_tip(side);
    d = nub_d(side);
    // nub, with a 45 degree ramp on the back-facing side as a lead-in
    hull() {
        translate([min(xf, tip) - 0.01, btn_y - nub_w / 2, btn_z - nub_h / 2])
            cube([d + 0.02, nub_w, nub_h]);
        translate([xf - (side < 0 ? 0.01 : 0), btn_y - nub_w / 2, btn_z - nub_h / 2 - d])
            cube([0.01, nub_w, nub_h + d]);
    }
    // small raised pad outside so the button is easy to find by feel
    xo = side < 0 ? out_x0 : out_x1;
    translate([xo, btn_y, btn_z]) rotate([0, 90 * side, 0])
        cylinder(d1 = 2.6, d2 = 2.0, h = 0.4);
}

module label(side, txt) {
    // engraved 0.4 deep, readable from outside with the screen facing up
    xo = side < 0 ? out_x0 : out_x1;
    translate([xo + 0.2 * side, btn_y + tab_w / 2 + tab_slot + 4.6, btn_z])
        multmatrix([[0, 0, -side, 0], [side, 0, 0, 0], [0, 1, 0, 0], [0, 0, 0, 1]])
            linear_extrude(0.6)
                text(txt, size = 2.2, font = "Liberation Sans:style=Bold",
                     halign = "center", valign = "center");
}

module usb_cut() {
    // inner skin: the receptacle
    translate([usb_cx - usb_hole_w / 2, in_y0 - wall - 0.1, usb_cz - usb_hole_h / 2])
        rbox(0, 0, 0, usb_hole_w, wall + 0.2, usb_hole_h, 1.0);
    translate([usb_cx - usb_hole_w / 2, in_y0 - skin - 0.01, bot_z - 1])
        cube([usb_hole_w, skin + 0.02, usb_cz - bot_z + 1]);
    // outer skin: room for the plug's overmold
    translate([usb_cx - usb_rel_w / 2, out_y0 - 0.1, usb_cz - usb_rel_h / 2])
        rbox(0, 0, 0, usb_rel_w, usb_rel_d + 0.1, usb_rel_h, 2.0);
}

module snap_groove() {
    for (x = [in_x0, in_x1])
        translate([x, in_y0 + 3, snap_z]) rotate([-90, 0, 0])
            cylinder(r = snap + snap_clr + plate_clr, h = in_y1 - in_y0 - 6, $fn = 24);
}

module shell() {
    difference() {
        union() {
            difference() {
                shell_outer();
                // cavity
                translate([0, 0, bot_z - 1]) linear_extrude(top_in - bot_z + 1)
                    rrect(in_x0, in_y0, in_x1, in_y1, 0.6);
                window();
                usb_cut();
                snap_groove();
                // lead-in chamfer at the open back
                hull() {
                    translate([0, 0, bot_z - 0.01]) linear_extrude(0.01)
                        rrect(in_x0 - 0.5, in_y0 - 0.5, in_x1 + 0.5, in_y1 + 0.5, 1.1);
                    translate([0, 0, bot_z + 0.5]) linear_extrude(0.01)
                        rrect(in_x0, in_y0, in_x1, in_y1, 0.6);
                }
                tab_cut(-1);
                tab_cut(1);
                // pry notch at the top end to pop the plate out
                translate([pcb_w / 2 - 3, in_y1 - 0.1, bot_z - 0.1]) cube([6, 0.9, 1.0]);
            }
            tab_add(-1);
            tab_add(1);
        }
        if (labels) {
            label(-1, "BOOT");
            label(1, "RST");
        }
    }
}

// ---------------- plate ----------------

module plate() {
    difference() {
        union() {
            translate([0, 0, bot_z]) linear_extrude(plate_t)
                rrect(in_x0 + plate_clr, in_y0 + plate_clr,
                      in_x1 - plate_clr, in_y1 - plate_clr, 0.5);
            for (p = posts) translate([p[0], p[1], plate_top - 0.01])
                cylinder(d = boss_d, h = boss_h + 0.01);
            // tongue that fills the USB slot under the connector
            translate([usb_cx - usb_hole_w / 2 + plate_clr, in_y0 - skin + plate_clr, bot_z])
                cube([usb_hole_w - 2 * plate_clr, skin + 0.5, plate_t]);
            // snap beads on the long edges
            intersection() {
                for (x = [in_x0 + plate_clr, in_x1 - plate_clr])
                    translate([x, in_y0 + 4, snap_z]) rotate([-90, 0, 0])
                        cylinder(r = snap, h = in_y1 - in_y0 - 8, $fn = 24);
                translate([in_x0 - 1, in_y0, bot_z]) cube([in_x1 - in_x0 + 2, in_y1 - in_y0, plate_t]);
            }
        }
        for (p = posts) translate([p[0], p[1], bot_z - 0.1]) {
            cylinder(d = screw_d, h = plate_t + boss_h + 0.2);
            cylinder(d = head_d, h = head_cb + 0.1);
        }
    }
}

// ---------------- board (for checking only) ----------------

module sw() {   // each board piece, swept back along -z for the push-in check
    for (i = [0 : $children - 1]) hull() { children(i); translate([0, 0, -sweep]) children(i); }
}

module board() {
    color("darkgreen") sw() translate([0, 0, -pcb_t]) cube([pcb_w, pcb_l, pcb_t]);
    color("black") sw() translate([(pcb_w - 19.39) / 2, pcb_l - 36.28, 0])
        cube([19.39, 36.28, screen_z]);
    color("dimgray") sw() translate([active_x0, active_y0, screen_z - 0.01])
        cube([active_w, active_l, 0.02]);
    color("orange") sw() translate([3, -flex_out, -pcb_t]) cube([pcb_w - 6, flex_out, pcb_t + 1]);
    color("gold") for (p = posts) sw() translate([p[0], p[1], post_z]) cylinder(d = post_d, h = -post_z - pcb_t);
    o = board_out;
    color("silver") sw() translate([usb_cx - usb_w / 2, -max(usb_out, o), usb_z])
        cube([usb_w, usb_len, usb_h]);
    color("white") for (x = [-max(btn_out, o), pcb_w - 2.0])
        sw() translate([x, btn_y - btn_len / 2, btn_z - btn_h / 2])
            cube([max(btn_out, o) + 2.0, btn_len, btn_h]);
    color("silver") sw() translate([pcb_w / 2 - 0.34 - 5.7, 7.84, -3.40]) cube([11.4, 5.51, 3.40 - pcb_t]);
}

// ---------------- output ----------------

if (part == "shell") {
    if (print_layout) translate([0, 0, top_z]) mirror([0, 0, 1]) shell(); else shell();
} else if (part == "plate") {
    translate([0, 0, -bot_z]) plate();
} else if (part == "board") {
    board();
} else if (part == "exploded") {
    color("lightgray", 0.9) translate([0, 0, 14]) shell();
    board();
    color("tan") translate([0, 0, -10]) plate();
} else {
    color("lightgray", 0.6) shell();
    board();
    color("tan") plate();
}
