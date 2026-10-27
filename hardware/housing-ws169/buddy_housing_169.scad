// Claude buddy housing for the Waveshare ESP32-S3-Touch-LCD-1.69, with a
// 302030 LiPo (3 x 20 x 30 mm) behind the board.
//
// One printed part: floor with a battery bay, four walls, the USB-C
// opening, three flex buttons (RST, BOOT, PWR), a ledge the cover glass
// rests on, and short lips at the top of the walls that the glass presses
// in past. The lips hook over the glass's rounded front edge.
//
// The board has no mounting holes; its cover glass (33.1 x 41.1, bigger
// than the PCB) is what the case holds. Assembly: battery into the bay,
// plug it in, press the board in screen up until the glass clicks past the
// lips. A notch at the RTC end takes a spudger under the glass to pop it out.
//
// Coordinates are Waveshare's STEP model (ESP32-S3-Touch-LCD-1_69.stp), mm:
//   origin at the board centre, x across (USB-C at -x, buttons at +x),
//   y along (LCD flex / RTC connector at -y), z toward the screen,
//   z = 0 on the back of the LCD panel.
//
// Render the case with:  openscad -D 'part="case"' -o case.stl buddy_housing_169.scad

include <glass_outline.scad>   // glass_pts: cover glass outline from the STEP

part = "assembly";   // "case" | "assembly" | "exploded" | "section" | "xray" | "layout" | "board"
print_layout = true; // move the floor onto z = 0

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
rtc          = [-11.41, -16.63, -8.41, -12.33];   // RTC battery connector
rtc_z0       = -6.90;
deepest      = -4.60;   // lowest part under the battery (LCD flex connector)

/* [Battery] */
batt_t       = 3.0;
// size and place measured off Sean's photo of the battery on the board
// (cell plus its taped protection board): the -x edge rests against the
// USB-C and RTC connectors, which hold that side
batt_w       = 20.7;    // along x
batt_l       = 33.5;    // along y, wires leave the +y end
batt_x0      = -8.10;
batt_y0      = -17.20;
batt_gap     = 0.20;    // between the deepest part and the battery
batt_swell   = 0.30;    // spare depth behind the battery (foam tape)
// three tabs on the floor hold the +x side and both ends, each a third of
// its edge long, batt_clr off the battery
btab_t       = 1.5;
btab_h       = 1.6;
batt_clr     = 0.30;

/* [Case] */
glass_clr    = 0.05;    // glass to walls: a snug fit
wall         = 1.60;    // glass pocket to outside
floor_t      = 1.40;
edge_ch      = 0.60;    // front edge chamfer
back_ch      = 0.60;    // back edge chamfer
// press-fit lips at the top of the walls, two per side. The flat
// underside holds the glass's rounded edge; the top is a 45 degree lead-in.
lip_in       = 0.25;    // past the glass outline
lip_z0       = 3.35;    // underside; the glass edge is inset 0.28 here
lip_flat     = 0.20;    // full-depth band above the underside
// pry notch at the -y (RTC) end: cuts the wall top and dips into the ledge
// so a spudger gets under the glass
pry_w        = 6.0;
pry_d        = 0.8;     // into the wall
pry_z0       = 1.20;
// cavity under the glass ledge: clears the panel, PCB, flex and buttons
cav_clr      = 0.30;
cav          = [panel[0] - cav_clr, fpc_y0 - cav_clr,
                btn_tip + cav_clr, panel[3] + cav_clr];
cav_r        = 5.0;

// flex buttons cut into the +x wall: vertical tabs hinged at the floor,
// free end toward the screen
tab_w        = 5.0;
tab_t        = 0.8;
tab_slot     = 0.6;
nub_gap      = 0.15;    // nub tip to the button at rest
nub_w        = 2.0;
nub_h        = 1.6;
tab_r        = 1.5;     // free-end corner radius
bump_d       = 4.0;     // finger bump on the outside
bump_h       = 0.8;

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
top_z    = glass_z1;                                   // walls flush with the glass
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

assert(batt_x1 + batt_clr + btab_t < cav[2], "+x battery tab runs into the button wall");
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

// press-fit lips, two per side (clear of the corners,
// the USB slot and the pry notch)
module lips() {
    intersection() {
        difference() {
            slab(lip_z0, top_z + 0.01) glass2d(glass_clr + 0.01);
            hull() {
                slab(lip_z0 - 0.01, lip_z0 + lip_flat) glass2d(glass_clr - lip_in);
                slab(top_z, top_z + 0.02) glass2d(glass_clr + lip_in - (top_z - lip_z0 - lip_flat));
            }
        }
        union() {
            for (y = [[-12, -7], [6, 11]]) slab(0, 5) translate([-50, y[0]]) square([100, y[1] - y[0]]);
            for (x = [[-10, -5], [5, 10]]) slab(0, 5) translate([x[0], -50]) square([x[1] - x[0], 100]);
        }
    }
}

// ---------------- case ----------------

// tab outline in the wall plane (y, z), rounded at the free end
module tab2d(yc, grow = 0) {
    r = tab_r + grow;
    translate([yc - tab_w / 2 - grow, floor_in])
        hull() {
            square([tab_w + 2 * grow, 0.01]);
            for (y = [r, tab_w + 2 * grow - r])
                translate([y, tab_z1 + grow - floor_in - r]) circle(r);
        }
}

module in_wall(x0, x1) {   // extrude a (y, z) shape across the wall, x0..x1
    translate([x0, 0, 0]) rotate([90, 0, 90]) linear_extrude(x1 - x0) children();
}

// U slot round the tab plus the inside thinning, for the tab at y = yc
module tab_cut(yc) {
    in_wall(cav[2] - 0.5, out_x + 1) difference() { tab2d(yc, tab_slot); tab2d(yc); }
    in_wall(cav[2] - 0.5, tab_face) tab2d(yc);
}

module tab_add(yc) {
    // nub, with a 45 degree ramp on the screen side as a lead-in
    hull() {
        translate([nub_tip, yc - nub_w / 2, btn_z - nub_h / 2]) cube([nub_d + 0.01, nub_w, nub_h]);
        translate([tab_face - 0.01, yc - nub_w / 2, btn_z - nub_h / 2])
            cube([0.01 + 0.01, nub_w, nub_h + nub_d]);
    }
    // domed bump outside so each button is easy to find and press
    intersection() {
        translate([out_x - 0.01, yc, btn_z]) scale([bump_h / (bump_d / 2), 1, 1]) sphere(d = bump_d);
        translate([out_x - 0.01, yc - bump_d, btn_z - bump_d]) cube([bump_h + 0.1, 2 * bump_d, 2 * bump_d]);
    }
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

// battery tabs: +x side and both ends, centred on each edge
module battery_tabs() {
    c = batt_clr;
    xm = (batt_x0 + batt_x1) / 2;
    ym = (batt_y0 + batt_y1) / 2;
    translate([batt_x1 + c, ym - batt_l / 6, floor_in - 0.01]) cube([btab_t, batt_l / 3, btab_h + 0.01]);
    for (y = [batt_y0 - c - btab_t, batt_y1 + c])
        translate([xm - batt_w / 6, y, floor_in - 0.01]) cube([batt_w / 3, btab_t, btab_h + 0.01]);
}

module back_labels() {
    // engraved in the back, readable with the case face down
    for (i = [0 : len(btn_y) - 1])
        translate([out_x - 6.0, btn_y[i], bot_z - 0.01]) mirror([1, 0, 0])
            linear_extrude(0.41)
                text(btn_name[i], size = 2.0, font = "Liberation Sans:style=Bold",
                     halign = "center", valign = "center");
}

module housing() {
    difference() {
        union() {
            difference() {
                outer_body();
                // glass pocket
                slab(glass_z0, top_z + 1) glass2d(glass_clr);
                // cavity under the ledge
                slab(floor_in, glass_z0 + 0.01) rrect(cav_r, cav[0], cav[1], cav[2], cav[3]);
                usb_cut();
                for (y = btn_y) tab_cut(y);
                // pry notch at the -y end
                translate([-pry_w / 2, out_y0 + wall - pry_d, pry_z0])
                    cube([pry_w, cav[1] - (out_y0 + wall - pry_d) + 0.5, top_z - pry_z0 + 1]);
            }
            lips();
            for (y = btn_y) tab_add(y);
            battery_tabs();
        }
        if (labels) back_labels();
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
        color("silver") cube([batt_w, batt_l - 9, batt_t]);
        color("orange") translate([0, batt_l - 9, 0]) cube([batt_w, 9, batt_t]);
    }
    color("red") translate([batt_x0 + 1.5, batt_y1, batt_z1 - 1.5]) rotate([-90, 0, 0]) cylinder(d = 0.9, h = 2);
    color("black") translate([batt_x0 + 0.5, batt_y1, batt_z1 - 1.5]) rotate([-90, 0, 0]) cylinder(d = 0.9, h = 2);
}

// ---------------- output ----------------

if (part == "case") {
    if (print_layout) translate([0, 0, -bot_z]) housing(); else housing();
} else if (part == "board") {
    board(); battery();
} else if (part == "layout") {
    // back view laid out like Sean's photo: buttons up, USB-C down, RST
    // left, floor left off so the battery and its tabs show
    multmatrix([[0, 1, 0, 0], [1, 0, 0, 0], [0, 0, -1, 0], [0, 0, 0, 1]]) {
        intersection() {
            color("gainsboro") housing();
            slab(floor_in + 0.01, top_z + 1) square(100, center = true);
        }
        color("steelblue") battery_tabs();
        battery();
        board();
    }
} else if (part == "exploded") {
    color("lightgray") housing();
    translate([0, 0, 6]) battery();
    translate([0, 0, 16]) board();
} else if (part == "section") {
    // cut across the board through the BOOT button and the USB-C
    intersection() {
        union() { color("lightgray") housing(); battery(); board(); }
        translate([-50, usb_cy, -50]) cube([100, 100, 100]);
    }
} else if (part == "xray") {
    color("lightgray", 0.5) housing();
    battery();
    board();
} else {
    color("gainsboro") housing();
    board();
}
