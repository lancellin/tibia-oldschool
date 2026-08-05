# SPRITES_CHANGELOG

Chronological record of the HD sprite workflow in `D:\tibia-oldschool`.

## Executive Summary

The HD sprite work evolved from isolated mosaic tests into a family-based production flow with three main tracks:

- isolated sprites without continuity, handled with direct upscale;
- ground mosaics with simple continuity or border continuity;
- continuity atlases for walls, facades, pyramids, and structures spanning multiple floors.

Over time it became clear that upscale quality alone was not the deciding factor. The key variable was the type of visual continuity in each sprite:

- a single tile without borders or transitions could be treated as a simple candidate;
- tiles with borders, corners, or repeated-join continuity required mosaics;
- walls and structures with `Z` needed atlases with vertical projection;
- sprites with multiple contexts sharing the same sprite IDs required family-aware cropping.

The consolidated flow became:

1. identify the `Client ID`;
2. study the sprite continuity;
3. build the appropriate mosaic or atlas;
4. upscale the input;
5. crop and restore the original alpha;
6. load the result into `Tibia.cwm` for testing;
7. validate in the client;
8. approve, roll back, or adjust;
9. copy the approved result into `Sprites Permanentes`.

## Phase 1. Understanding The Base And The Tools

### Documentation review and workspace inspection

- Documentation in `D:\tibia-oldschool\docs` was reviewed.
- `D:\tibia-oldschool\tools\assets` was inspected.
- The mosaic logic was interpreted as derived from RME behavior.
- It became clear that the main problem was not simple mosaicking, but border continuity and tile-to-tile alignment.

### Tools used

- `Upscayl` for 2x and, in some cases, 4x upscales.
- Local scripts under `D:\tibia-oldschool\tools\assets\`.
- Reading sprites from `Tibia.dat`, `Tibia.spr`, and `Tibia.cwm`.
- PNG crop/recomposition workflows.
- Partial CWM overlays merged into the active `Tibia.cwm`.
- Visual inspection in RME.
- Final validation with in-game screenshots.

### Operational rules consolidated

- Work was always done using `Client ID`, not `Server ID`.
- Approved assets were copied into `D:\tibia-oldschool\backup-extras\Sprites Permanentes`.
- For most changes, the client did not need to be restarted when only `CWM` changed.
- If `dat` changed, more caution was needed.
- For wall-like structures, `Z` became a mandatory part of the analysis.

## Phase 2. Early Mosaic And Ground Tests

### Ground sprites and simple mosaics

- Tests were executed on ground sprites without borders.
- The first validation proved that the input -> upscale -> crop -> test pipeline worked.
- Several ground-related CIDs were processed and approved.

### Decisions from this phase

- Tiles without continuity could be sent through direct upscale.
- Tiles with continuity needed a mosaic or atlas, depending on the kind of join.
- Once the mosaic was correct, the asset could go directly to permanents.

### Problems observed

- Some borders were inverted.
- Some corners appeared in the wrong orientation.
- In a few cases the border outside the center was the harder part.
- Visual confirmation in the client remained necessary.

## Phase 3. Classic Server Sprite Corrections

### Replacing old IDs with 7.4 sprites

- Some `7.72` sprites were judged too modern for the classic set.
- Instead of creating new IDs, the content was swapped inside the existing server IDs.
- The goal was to preserve the numeric IDs and only replace the visual content.

### Recorded replacements

- `351` was replaced by sprite `461`.
- `352` was replaced by sprite `462`.
- `353` was replaced by sprite `463`.
- `354` was replaced by sprite `464`.
- `355` was replaced by sprite `465`.
- `386` was replaced by sprite `530`.

### Technical notes

- These changes were applied inside the server sprites, not as part of the HD upscale flow.
- The original numeric IDs were preserved.
- The purpose was to align the server visuals with the classic style.

## Phase 4. Specific Mosaics And Borders

### Cave ground and related borders

- A mosaic logic was validated for cave ground.
- The center was simple; the hard part was the borders and complements.
- The workflow included border sync, center-ground matching, and corner correction.

### Results and conclusions

- Some mosaics were initially mirrored and had to be corrected.
- In some cases, the result was approved even with minor imperfections inherited from the original sprite.
- It became clear that a sprite can look correct in a mosaic and still fail if the border continuity is wrong.

## Phase 5. Walls And Continuity Atlases

### Shift in approach

- Several walls were not good candidates for a generic upscale.
- The better model was a connected wall made of linked segments, sometimes across multiple floors.
- This led to continuity atlases instead of a simple repeated tile.

### Families and auxiliary families

- Work included stone walls, themed walls, wall windows, posts, and facade structures.
- Families often had:
  - horizontal parts;
  - vertical parts;
  - corners;
  - poles;
  - floor transitions;
  - components shared across more than one context.

### Project decision

- For walls, isolated upscale was not enough.
- The atlas became an extended context for the wall.
- When the same sprite ID appeared in more than one context, crop selection depended on the wall type.

## Phase 6. Multi-Floor Walls And Z

### Z-awareness

- `Z` was added to the analysis for multi-floor constructions.
- This was essential for structures that repeat the same wall across different floors.
- The projection started to include the target floor, the floor above, and the floor below.

### Practical impact

- It removed much of the visual confusion in walls with weak vertical separation.
- It explained some horizontal division marks that looked like defects but were actually part of the floor transition.
- It made it possible to treat floor transitions without relying on a purely 2D interpretation.

### Result in the workflow

- After this change, atlases that had been previously problematic were approved.
- Z-based continuity became a required rule for any wall that spans more than one floor.

## Phase 7. Structures That Were Rolled Back Or Reprocessed

### Visually poor results

The following classes were rolled back or marked for reprocessing:

- tables;
- counters;
- earlier pyramid attempts;
- boat railings;
- themed walls with strong separation marks;
- doors;
- large rocks;
- ramps;
- walls with visible seams between blocks;
- facades that became distorted after upscale.

### Technical lesson

- Not every sprite should use the same upscale strategy.
- The model or workflow needed to vary with the texture.
- Some smooth sprites worked better with `high fidelity`.
- Others needed stronger contextual handling.

## Phase 8. Pyramids

### Initial attempts

- Early pyramid attempts did not work well.
- The work was later resumed using continuity atlases instead of treating the pyramid as a single sprite.

### Front face

- An atlas was built for the front part of the pyramid.
- The first upscale quality was poor, but the logic itself was correct.
- Horizontal lines in the result indicated continuity issues in the atlas, not a geometry error.

### Other faces

- The north, south, east, and west faces were treated in separate families.
- Direction had to be read carefully because the upper floor of a pyramid does not follow the same intuition as a normal wall.
- This resolved the remaining pyramid flow without relying on a single visual assumption.

### Result

- The pyramid family was approved in parts.
- Approved results were copied to permanents.

## Phase 9. Gray Stone Wall Family / 1305-1315

### Family identification

- A gray rubble / stone wall family was identified for the following CIDs:
  - `1305, 1306, 1307, 1308, 1310, 1311, 1312, 1313, 1314, 1315`
- The associated sprite IDs were between `4525` and `4540`.

### Structure analysis

- The family was not a single image.
- It had variants for:
  - horizontal;
  - vertical;
  - forward diagonal;
  - reverse diagonal;
  - repeated internal variants for the main rectangular contexts.

### Adopted logic

- An atlas with `Z` projection was used.
- Context above, below, and on the target floor was included.
- Separate panels were built for the main and secondary variants.
- Final recut prioritized the best context for each sprite ID.

### Result

- A partial test was submitted and validated.
- The complete atlas was then generated.
- The full family was tested.
- The result was moved to permanents.

## Phase 10. Other Approved Families And Sprites

### Approved grounds and mosaics

- `CID 923, 924, 925, 926, 927, 928, 929, 931, 934, 935`
- `CID 231` and sand variants
- `CID 417`
- `CID 1128`
- `CID 422`
- `CID 415`

### Approved walls and structures from earlier iterations

- `CID 351` and connected ground/border complements
- `CID 373` and `374`
- `CID 408, 439, 440, 441, 442, 447, 448, 450`
- `CID 436`
- `CID 4405, 4409, 4396`
- `CID 1771`
- `CID 452`
- `CID 870`
- `CID 429`
- `CID 106, 109`

### Stone walls and internal families

- `CID 1295`
- `CID 1294`
- `CID 1298`
- `CID 1299`
- `CID 2162, 2164, 2166, 2168`
- `CID 1281, 1282, 1283, 1289, 1290, 1735`
- `CID 1345, 1346, 1347, 1349, 2203`

### Note

- These items were not all handled through the exact same workflow.
- Some used continuity atlases.
- Some used direct upscale.
- Some were rolled back and left for later refinement.

## Phase 11. Pending Items And Rollbacks

### Known pending items

- Outfits and creatures were intentionally left for later.
- Some walls could still benefit from further refinement if new families are identified.
- Sprites that looked fine in the overall set still may contain local exceptions.

### Rollbacks

- Attempts that visually failed were reverted:
  - certain tables;
  - certain railings;
  - doors with distortion;
  - large rocks;
  - some walls with visible seams;
  - a wall set that showed an unnatural line between floors.

### Consolidated rule

- If the sprite did not hold up in the client, it returned to the previous state.
- Having a successful upscale was not enough by itself.

## Problems Encountered

- Borders and corners were the hardest part of the work.
- Some sprites changed quality a lot depending on the upscale model.
- Certain objects looked fine in 2D but failed once placed in the client.
- Floor separation could look like a mistake even when it was the correct transition.
- Some families shared sprite IDs across different contexts, requiring careful crop selection.
- In several cases, what looked like a single sprite turned out to be a full family.

## Decisions Taken

- Simple upscale was kept for sprites without continuity.
- Mosaic or atlas was used for any case with borders, joins, or continuity.
- `Z` became mandatory for layered constructions.
- Anything visually poor was rolled back, even if the logic seemed correct.
- Anything approved was copied to `Sprites Permanentes`.
- When possible, the right goal was visual fidelity of the structure, not resolution increase alone.

## Standard Delivery Flow

1. Identify the CID or family.
2. Classify the case as simple, mosaic, or atlas.
3. Build the mosaic or atlas.
4. Run the upscale on the correct input.
5. Crop and restore the original alpha.
6. Generate the partial CWM overlay.
7. Load it into `Tibia.cwm` for testing.
8. Validate in the client and adjust if needed.
9. Approve or roll back.
10. If approved, copy to `Sprites Permanentes`.

## Conclusion

The project established a practical HD sprite methodology:

- simple ground without continuity -> direct upscale;
- ground with continuity -> mosaic;
- wall or multi-layer structure -> `Z`-aware atlas;
- visually bad result -> rollback;
- approved sprite -> permanent archive.

This changelog should be used as the technical reference for future sprite families, walls, borders, and layered structures.
