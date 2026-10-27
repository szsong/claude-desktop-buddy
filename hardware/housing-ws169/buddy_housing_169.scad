// Claude buddy housing for the Waveshare ESP32-S3-Touch-LCD-1.69, with a
// 302030 LiPo (3 x 20 x 30 mm) behind the board.
//
// Two printed parts:
//   tub  - back case: floor with a battery bay, all four side walls, the
//          USB-C opening, three flex buttons (RST, BOOT, PWR) and a ledge
//          the cover glass rests on.
//   lid  - front bezel with the screen window. Its skirt drops into a
//          rebate in the tub walls around the glass and clicks in, clamping
//          the glass onto the ledge.
//
// The board has no mounting holes; its cover glass (33.1 x 41.1, bigger
// than the PCB) is what the case holds. Assembly: battery into the bay,
// plug it in, drop the board in screen up, press the lid on.
//
// Coordinates are Waveshare's STEP model (ESP32-S3-Touch-LCD-1_69.stp), mm:
//   origin at the board centre, x across (USB-C at -x, buttons at +x),
//   y along (LCD flex / RTC connector at -y), z toward the screen,
//   z = 0 on the back of the LCD panel.
//
// Render one part with:  openscad -D 'part="tub"' -o tub.stl buddy_housing_169.scad

include <glass_outline.scad>   // glass_pts: cover glass outline from the STEP

part = "assembly";   // "tub" | "lid" | "assembly" | "exploded" | "section" | "xray" | "board"
print_layout = true; // tub floor down, lid face down

/* [Board, from the STEP] */
glass_z0     = 2.00;   // back of the cover glass
glass_z1     = 3.80;   // front of the cover glass
// the glass front edge is rounded: inset of its outline at each height
glass_round  = [[3.0, 0], [3.3, 0.20], [3.6, 0.66], [3.75, 1.20], [3.8, 1.60]];
panel        = [-15.04, -19.93, 15.04, 17.48];  // LCD panel, z 0 .. 2.0
fpc_y0       = -20.52;                          // flex wrapped round the -y end
pcb          = [-14.92, -19.76, 14.93, 17.38];
pcb_z        = [-2.50, -1.50];
// active area (non-touch drawing: 27.97 x 32.63, 1.17 from the +y end,
// 1.05 from the sides of the 30.07 x 37.42 panel), R5 corners
active       = [-13.985, -16.32, 13.985, 16.31];
active_r     = 5.0;
// side-push buttons on the +x edge
btn_y        = [-9.76, -0.85, 8.06];
btn_name     = ["RST", "BOOT", "PWR"];
btn_tip      = 15.55;   // actuator tip
btn_z        = -3.37;   // actuator centre
// USB-C, mid-mount, on the -x edge
usb_face     = -16.24;
usb_cy       = -0.93;
usb_cz       = -4.25;
usb_w        = 8.95;
usb_h        = 3.10;
usb_back     = -8.70;
// battery connector (1.25 mm, 2 pin): mating face points +x, toward the
// middle of the board, so the plug and its wires sit beside the battery
jst          = [-13.92, 4.97, -8.72, 12.62];
jst_z0       = -6.00;
plug_out     = 3.0;     // plug body past the receptacle mouth (estimate)
rtc          = [-11.41, -16.63, -8.41, -12.33];   // RTC battery connector
rtc_z0       = -6.90;
deepest      = -4.60;   // lowest part under the battery (LCD flex connector)

/* [Battery] */
batt_t       = 3.0;
batt_w       = 20.0;    // along x
batt_l       = 30.0;    // along y, wires leave the +y end
batt_x0      = -4.40;   // clear of the plug (jst x1 + plug_out = -5.72)
batt_y0      = -17.20;  // Sean's photo: about 2.5 from the -y end of the PCB
batt_gap     = 0.20;    // between the deepest part and the battery
batt_swell   = 0.30;    // spare depth behind the battery (foam tape)

/* [Case] */
glass_clr    = 0.15;    // glass to walls / skirt
skirt_t      = 0.75;    // lid skirt
skirt_clr    = 0.15;
lip          = 0.90;    // tub wall outside the skirt
wall         = skirt_t + skirt_clr + lip;   // glass pocket to outside
rebate_z     = 2.30;    // tub step the skirt sits above
lid_gap      = 0.05;    // glass front to lid underside
bezel        = 1.20;
floor_t      = 1.40;
edge_ch      = 0.80;    // front edge chamfer
back_ch      = 0.60;    // back edge chamfer
win_margin   = 0.40;    // window past the active area, per side
win_ch       = 0.90;    // window chamfer toward the viewer
// cavity under the glass ledge: clears the panel, PCB, flex and buttons
cav_clr      = 0.30;
cav          = [panel[0] - cav_clr, fpc_y0 - cav_clr,
                btn_tip + cav_clr, panel[3] + cav_clr];
cav_r        = 5.0;
// snap beads on the lid skirt, middle of each side
snap         = 0.20;
snap_clr     = 0.05;

// flex buttons cut into the +x wall: vertical tabs hinged at the floor,
// free end toward the screen
tab_w        = 5.0;
tab_t        = 0.8;
tab_slot     = 0.6;
nub_gap      = 0.15;    // nub tip to the button at rest
nub_w        = 2.0;
nub_h        = 1.6;

// USB-C opening: plug-overmold relief in the outside, cut in until it is
// usb_reach from the receptacle face, plus a slot in the ledge so the
// receptacle drops in from the front
usb_reach    = 0.35;
usb_rel_w    = 12.0;
usb_rel_h    = 6.4;
usb_slot_clr = 0.40;

labels       = true;
$fn = 48;

// derived
floor_in = deepest - batt_gap - batt_t - batt_swell;   // inside of the floor
bot_z    = floor_in - floor_t;
lid_in   = glass_z1 + lid_gap;                         // top of the tub walls
top_z    = lid_in + bezel;
batt_z1  = deepest - batt_gap;                         // battery front face
batt_x1  = batt_x0 + batt_w;
batt_y1  = batt_y0 + batt_l;
out_x    = max([for (p = glass_pts) p[0]]) + glass_clr + wall;
out_y0   = min([for (p = glass_pts) p[1]]) - glass_clr - wall;
out_y1   = max([for (p = glass_pts) p[1]]) + glass_clr + wall;
tab_face = out_x - tab_t;                              // inside of a thinned tab
nub_tip  = btn_tip + nub_gap;
nub_d    = tab_face - nub_tip;
tab_z1   = btn_z + nub_h / 2 + nub_d + 0.2;            // free end of the tab

assert(batt_x0 > jst[2] + plug_out, "battery sits on the plug");
assert(batt_x1 < cav[2] - 0.1, "battery wider than the cavity");
assert(tab_z1 + tab_slot < glass_z0 - 0.8, "tab runs into the glass ledge");
assert(usb_face - usb_reach > -out_x + 0.5, "USB relief cuts through the wall");

echo(str("outer size ", 2 * out_x, " x ", out_y1 - out_y0, " x ", top_z - bot_z, " mm"));
echo(str("battery bay z ", floor_in, " .. ", batt_z1, ", floor_in ", floor_in));

// ---------------- shapes ----------------

module glass2d(d = 0) { offset(delta = d) polygon(glass_pts); }

module rrect(r, x0, y0, x1, y1) {
    translate([x0 + r, y0 + r]) offset(r) square([x1 - x0 - 2 * r, y1 - y0 - 2 * r]);
}

module slab(z0, z1) { translate([0, 0, z0]) linear_extrude(z1 - z0) children(); }

// cover glass, grown by d all round (the outline is convex, so hull works)
module glass_solid(d = 0) {
    hull() {
        slab(glass_z0 - d, glass_z0 - d + 0.01) glass2d(d);
        for (g = glass_round)
            slab(g[0] + d - 0.01, g[0] + d) glass2d(d - g[1]);
    }
}

module outer_body() {
    hull() {
        slab(bot_z, bot_z + back_ch) glass2d(glass_clr + wall - back_ch);
        slab(bot_z + back_ch, top_z - edge_ch) glass2d(glass_clr + wall);
        slab(top_z - edge_ch, top_z) glass2d(glass_clr + wall - edge_ch);
    }
}

// a ring band on the skirt line, limited to the middle of each side
module snap_band(r) {
    intersection() {
        difference() {
            hull() {
                slab(3.1 - r, 3.1 - r + 0.01) glass2d(glass_clr + skirt_t);
                slab(3.1 - 0.01, 3.1 + 0.01) glass2d(glass_clr + skirt_t + r);
                slab(3.1 + r - 0.01, 3.1 + r) glass2d(glass_clr + skirt_t);
            }
            slab(0, 5) glass2d(glass_clr + skirt_t - 0.3);
        }
        union() {
            slab(0, 5) translate([-50, -9]) square([100, 16]);
            slab(0, 5) translate([-6, -50]) square([12, 100]);
        }
    }
}

// ---------------- tub ----------------

// U slot (two vertical slits joined above the free end) plus the inside
// thinning, for the tab at y = yc
module tab_cut(yc) {
    y0 = yc - tab_w / 2;
    translate([cav[2] - 0.5, y0 - tab_slot, floor_in])
        difference() {
            cube([out_x - cav[2] + 1, tab_w + 2 * tab_slot, tab_z1 + tab_slot - floor_in]);
            translate([-0.1, tab_slot, -0.1]) cube([out_x - cav[2] + 1.2, tab_w, tab_z1 - floor_in + 0.1]);
        }
    translate([cav[2] - 0.5, y0, floor_in]) cube([tab_face - cav[2] + 0.5, tab_w, tab_z1 - floor_in]);
}

module tab_add(yc) {
    // nub, with a 45 degree ramp on the screen side as a lead-in
    hull() {
        translate([nub_tip, yc - nub_w / 2, btn_z - nub_h / 2]) cube([nub_d + 0.01, nub_w, nub_h]);
        translate([tab_face - 0.01, yc - nub_w / 2, btn_z - nub_h / 2])
            cube([0.01 + 0.01, nub_w, nub_h + nub_d]);
    }
    // raised dot outside so each button is easy to find by feel
    translate([out_x, yc, btn_z]) rotate([0, 90, 0]) cylinder(d1 = 2.6, d2 = 2.0, h = 0.4);
}

module usb_cut() {
    // plug relief from the outside, rounded
    translate([-out_x - 1, usb_cy - usb_rel_w / 2, usb_cz - usb_rel_h / 2])
        hull() for (y = [2, usb_rel_w - 2], z = [2, usb_rel_h - 2])
            translate([0, y, z]) rotate([0, 90, 0])
                cylinder(r = 2, h = 1 + out_x + usb_face - usb_reach);
    // slot through the ledge for the receptacle, open to the glass pocket
    translate([usb_face - usb_reach - 0.01, usb_cy - usb_w / 2 - usb_slot_clr,
               usb_cz - usb_h / 2 - usb_slot_clr])
        cube([cav[0] - usb_face + usb_reach + 0.5, usb_w + 2 * usb_slot_clr,
              glass_z0 - usb_cz + usb_h / 2 + usb_slot_clr + 0.01]);
}

module battery_ribs() {
    h = 1.2;
    c = 0.3;   // battery clearance in the bay
    // -x side (stops short of the wire end) and the -y end
    translate([batt_x0 - c - 0.8, batt_y0 - c - 0.8, floor_in - 0.01])
        cube([0.8, batt_l - 4, h + 0.01]);
    translate([batt_x0 - c - 0.8, batt_y0 - c - 0.8, floor_in - 0.01])
        cube([batt_w + 2 * c + 0.8, 0.8, h + 0.01]);
}

module back_labels() {
    // engraved in the back, readable with the case face down
    for (i = [0 : len(btn_y) - 1])
        translate([out_x - 6.0, btn_y[i], bot_z - 0.01]) mirror([1, 0, 0])
            linear_extrude(0.41)
                text(btn_name[i], size = 2.0, font = "Liberation Sans:style=Bold",
                     halign = "center", valign = "center");
}

module tub() {
    difference() {
        union() {
            difference() {
                intersection() { outer_body(); slab(bot_z - 1, lid_in) square(100, center = true); }
                // glass pocket, then the rebate for the lid skirt
                slab(glass_z0, rebate_z + 0.01) glass2d(glass_clr);
                slab(rebate_z, lid_in + 1) glass2d(glass_clr + skirt_t + skirt_clr);
                // cavity under the ledge
                slab(floor_in, glass_z0 + 0.01) rrect(cav_r, cav[0], cav[1], cav[2], cav[3]);
                usb_cut();
                for (y = btn_y) tab_cut(y);
                snap_band(snap + snap_clr);
                // pry notch at the -y end of the seam
                translate([-3, out_y0 - 1, lid_in - 0.7]) cube([6, 1 + wall - 0.3, 1]);
            }
            for (y = btn_y) tab_add(y);
            battery_ribs();
        }
        if (labels) back_labels();
    }
}

// ---------------- lid ----------------

module window() {
    m = win_margin;
    hull() {
        slab(glass_z1 - 0.5, top_z - win_ch)
            rrect(active_r + m, active[0] - m, active[1] - m, active[2] + m, active[3] + m);
        slab(top_z - 0.01, top_z + 0.01)
            rrect(active_r + m + win_ch, active[0] - m - win_ch, active[1] - m - win_ch,
                  active[2] + m + win_ch, active[3] + m + win_ch);
    }
}

module lid() {
    difference() {
        union() {
            intersection() { outer_body(); slab(lid_in, top_z + 1) square(100, center = true); }
            // skirt, filled down over the rounded glass edge
            difference() {
                slab(rebate_z + 0.05, lid_in + 0.01) glass2d(glass_clr + skirt_t);
                slab(rebate_z, lid_in + 1) glass2d(glass_clr);
            }
            difference() {
                slab(glass_round[0][0], lid_in + 0.01) glass2d(glass_clr);
                glass_solid(lid_gap);
            }
            snap_band(snap);
        }
        window();
    }
}

// ---------------- board stand-in (for previews; the fit check uses the STEP) ----------------

module board() {
    color([0.05, 0.05, 0.07]) glass_solid();
    color([0.25, 0.30, 0.45]) intersection() {
        glass_solid(0.01);
        slab(glass_z1 - 0.01, glass_z1 + 0.05)
            rrect(active_r, active[0], active[1], active[2], active[3]);
    }
    color("dimgray") slab(0, glass_z0) rrect(4.8, panel[0], panel[1], panel[2], panel[3]);
    color("black") slab(-1.5, 0) square([28, 18], center = true);
    color([0.1, 0.3, 0.6]) slab(pcb_z[0], pcb_z[1]) rrect(4.5, pcb[0], pcb[1], pcb[2], pcb[3]);
    color("silver") translate([usb_face, usb_cy - usb_w / 2, usb_cz - usb_h / 2])
        cube([usb_back - usb_face, usb_w, usb_h]);
    color("white") for (y = btn_y) {
        translate([12.1, y - 2.25, -4.3]) cube([2.25, 4.5, 1.8]);
        translate([14.3, y - 1.0, btn_z - 0.47]) cube([btn_tip - 14.3, 2.0, 0.95]);
    }
    color("ivory") translate([jst[0], jst[1], jst_z0]) cube([jst[2] - jst[0], jst[3] - jst[1], pcb_z[0] - jst_z0]);
    color("ivory") translate([rtc[0], rtc[1], rtc_z0]) cube([rtc[2] - rtc[0], rtc[3] - rtc[1], pcb_z[0] - rtc_z0]);
    color("dimgray") translate([-6.12, -16.26, deepest]) cube([12.3, 6, pcb_z[0] - deepest]);
}

module battery() {
    translate([batt_x0, batt_y0, batt_z1 - batt_t]) {
        color("silver") cube([batt_w, batt_l - 4, batt_t]);
        color("orange") translate([0, batt_l - 4, 0]) cube([batt_w, 4, batt_t]);
    }
    color("red") translate([batt_x0 + 4, batt_y1, batt_z1 - 1.5]) rotate([-90, 0, 0]) cylinder(d = 0.9, h = 2);
    color("black") translate([batt_x0 + 2.5, batt_y1, batt_z1 - 1.5]) rotate([-90, 0, 0]) cylinder(d = 0.9, h = 2);
}

// ---------------- output ----------------

if (part == "tub") {
    if (print_layout) translate([0, 0, -bot_z]) tub(); else tub();
} else if (part == "lid") {
    if (print_layout) translate([0, 0, top_z]) mirror([0, 0, 1]) lid(); else lid();
} else if (part == "board") {
    board(); battery();
} else if (part == "exploded") {
    color("lightgray") tub();
    translate([0, 0, 6]) battery();
    translate([0, 0, 14]) board();
    color("lightgray") translate([0, 0, 26]) lid();
} else if (part == "section") {
    // cut across the board through the BOOT button and the USB-C
    intersection() {
        union() {
            color("lightgray") tub();
            battery();
            board();
            color("silver") lid();
        }
        translate([-50, usb_cy, -50]) cube([100, 100, 100]);
    }
} else if (part == "xray") {
    color("lightgray", 0.5) tub();
    battery();
    board();
    color("lightgray", 0.5) lid();
} else {
    color("gainsboro") tub();
    board();
    color("whitesmoke") lid();
}
