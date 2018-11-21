# UPC Contract

This is a description of all the different kinds of UPCs things accept and their
eventual outcome.

Keys:
- **\[C\]** reate
- **\[D\]** elete
- **\[M\]** ove
- **\[G\]** ather

## SquadMicro

**Note:** Ignores tuples if they are any of `C > 0`, `G > 0`, or `sharp U`.

- `P = location`
  - If not near location, move to location
  - `D = 1`
    - If near location, hit enemies near location
  - `D != 1`
    - Also hits enemies along the way
- `P = location that tracks units`
  - `D = 1`
    - Only attacking specified units
  - `D != 1`
    - Attacks specified units and units near us
