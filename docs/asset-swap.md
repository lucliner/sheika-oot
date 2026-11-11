# Asset Swap

Replaces the geometry of a character model (Link, an NPC) with a model from another game, as a **mod**: nothing in the
game or its executable changes. `tools/assetswap` reads your own romfs and writes only the folder you name.

```
python -I tools/assetswap swap <romfs> Model/Player_Adult.Player_Adult.bfres.zs <model.obj> --mod <game folder>\mods\-mymod
```

Then rename the mod folder (drop the leading `-`) to turn it on, and start the game. Do the same for
`Player_Child.Player_Child.bfres.zs`: the game starts as the child.

| Option | |
|---|---|
| `--scale s` | the model is scaled to the target's height by default |
| `--rotate-y deg` | the model must face +z with +y up; turn it when it does not |
| `--offset x,y,z` | move it (metres) after the fit |
| `--slots 3,0` | the target's shapes to put the parts in (default: chosen by `pick_slots`) |
| `--preview out.png [--pose]` | render the result; `--pose` bends arms, a knee and the head to check the skin weights |

Other commands: `inspect` (bones, shapes, vertex layouts), `preview` / `export-obj` (a game model), `preview-import` (a model
to be imported).

## What it does

1. **Reads** the model (OBJ so far; glTF next) in metres.
2. **Fits** it to the target: scaled to the target's height, standing on the ground, centred.
3. **Skins** it. Models from other games usually have no rig that matches, so the weights come from the distance to the
   target's body bones (spine, neck, head, clavicles, arms, wrists, waist, legs, ankles, toes), smoothed along the mesh.
   Cloth, hair, finger and face bones are not used: the game drives them by physics or facial animation. The model then
   moves with the game's own animations.
4. **Patches** the target's BFRES: the chosen shapes get the new geometry in their own vertex layout, the other shapes are
   emptied, and the fields tied to geometry (counts, offsets, skin bone lists, bounding spheres, the relocation table) are
   rewritten. The skeleton, materials and layouts are kept.

## Limits (so far)

- **Textures and materials are not swapped yet.** The new geometry uses the host shape's material, so it is drawn with the
  game's textures for that shape until the texture step lands.
- A mesh whose UVs leave 0..1 on a shape that stores them as unorm16 gets that shape's UV attribute redeclared as half-float
  (same size). Not yet run in the game.
- Eyes, face animation, and weapons held at hand bones follow the game's rig, not the imported model.
- Static (unrigged) OBJ only. 65,535 vertices per part.
- **Not verified in the game yet**: the written files read back correctly with the tool's own reader, and are what the
  format's layout says the game expects.

The layout notes the writer relies on are in the header of `tools/assetswap/bfres_write.py`.
