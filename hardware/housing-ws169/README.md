# Buddy housing for the Waveshare ESP32-S3-Touch-LCD-1.69

Parametric OpenSCAD source: `buddy_housing_169.scad` (glass outline in
`glass_outline.scad`). Ready-made STLs in `stl/`. Holds the board plus a
302030 LiPo (3 x 20 x 30) behind it.

![overview](preview/overview.png)

## Part

One piece, `stl/buddy169_case.stl`. Print floor down, as exported, no
supports. PLA or PETG, 0.2 mm layers, 3+ perimeters. PETG is kinder to the
press-fit lips and the flex buttons.

## Assembly

1. Stick the battery between its three floor tabs with thin foam tape (up to
   0.3 mm), wires at the PWR-button end.
2. Plug the battery into the board's BAT connector and tuck the spare lead
   beside the battery, toward the USB-C side.
3. Press the board in screen up. The cover glass clicks past eight short
   lips at the top of the walls and sits on a ledge.
4. To take it out, put a spudger in the notch at the RTC end (opposite the
   battery wires) and lever the glass up past the lips.

## Key dimensions (mm)

- Outside: 36.4 x 44.4 x 13.3. Front and back edge chamfers 0.6. The walls
  are flush with the flat front of the glass.
- Walls 1.6 outside the glass pocket, floor 1.4.
- Glass pocket: the glass outline sliced from the STEP plus 0.05. The glass
  rests on a ledge (0.7 to 1.5 wide) at its back face.
- Press-fit lips: eight 5 mm long lips, two per side, reaching 0.25 past
  the glass outline. The flat underside (z 3.35) hooks over the glass's rounded
  front edge, 0.1 clear of it at rest; the top is a 45 degree lead-in.
- Battery: placed as in Sean's photo, x -8.1..12.6, y -17.2..16.3
  (20.7 x 33.5 measured off the photo, cell plus taped protection board).
  Its USB-C-side edge rests against the USB-C and RTC connectors. Three floor
  tabs, 1.5 thick and 1.6 tall, 0.3 off the battery, hold the other three
  edges: one on the button side (11.2 long, a third of the battery's
  length) and one on each end (6.9 long, a third of its width). The
  battery's front face clears the parts under it by 0.2; 0.3 spare depth
  behind it for foam tape.
- USB-C: 12.0 x 6.4 plug relief cut in until it is 0.35 from the receptacle
  face, and a slot through the glass ledge so the receptacle drops in.
- Buttons: three 5.0 wide flex tabs in the `+x` wall, hinged at the floor,
  free toward the screen with R1.5 rounded corners, thinned to 0.8. Domed
  finger bumps 4.0 across, 0.8 proud. Nub tips sit 0.15 off each button,
  with a 45 degree lead-in on the screen side. Labels engraved on the back.

## Board reference (Waveshare STEP `ESP32-S3-Touch-LCD-1_69.stp`)

Origin at the board centre, z = 0 on the back of the LCD panel, `+z` toward
the viewer, USB-C on `-x`, buttons on `+x`, LCD flex at `-y`.

- Cover glass 33.14 x 41.10, z 2.0 to 3.8, rounded front edge.
- LCD panel 30.08 x 37.41, z 0 to 2.0. PCB 29.85 x 37.14 x 1.0, z -2.5 to -1.5.
- Buttons RST / BOOT / PWR at y -9.76 / -0.85 / 8.06, actuator tip x 15.55, z -3.37.
- USB-C face at x -16.24, centred y -0.93, z -4.25, shell 8.95 x 3.1.
- BAT connector (1.25 mm, 2 pin) x -13.92..-8.72, y 4.97..12.62, mouth facing `+x`.
- Deepest parts behind the PCB: RTC connector -6.9, BAT connector -6.0,
  everything under the battery -4.6 or less.

## Fit check

`check/` holds the scripts used to check the design against the STEP
(trimesh, OCP): `step_export.py` meshes the STEP, `fitcheck.py` tests the
board at rest and swept up to 14 mm along the press-in path, the battery in
its tabs, the glass on its ledge and under the lips, and the nub gaps.

## Rebuilding

```sh
openscad -D 'part="case"' -o stl/buddy169_case.stl buddy_housing_169.scad
openscad -D 'part="exploded"' buddy_housing_169.scad   # preview with a board stand-in
```
