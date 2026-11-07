# Virtual memory cards

Cubeboot can give the GameCube two memory cards that live as files on the FlippyDrive's SD card, in
`/cubeboot/saves` (key `vmc_dir`): one pair per layer, `MemoryCard{A,B}.USA.raw`, `.EUR.raw` and
`.JAP.raw`, the names Swiss uses. They work in
**every game Cubeboot starts itself** (any disc image from the loader grid) and on the **IPL's own
Memory Card page** (the cube menu), where they look and behave like real cards: the page lists,
copies and deletes saves on them, and its Move / Copy / Erase popup gains Undo (one action back per
card), Import (every `.gci` file and every card image `.raw` / `.gcp` from `/cubeboot/saves/import`,
moved to `imported` afterwards), Swap (the two virtual cards change place) and Format with a
US/PAL | JPN choice.

## Behaviour

- A slot gets a virtual card only while it is **empty**. A physical memory card (or any other EXI
  device) in a slot always wins, also when it is inserted or removed later: the virtual card is parked
  while the real one is there and comes back when it is pulled. Virtual cards are never written over by
  anything but the game's or the menu's own card traffic.
- Regions: by default (`vmc_regions = stock`) the cards behave as a real memory card does on the console: US and
  PAL games and consoles share one pair of cards (the `USA` files), Japanese games and consoles use the `JAP` pair
  (Shift-JIS text). Swiss does it differently, with separate cards for US, European and Japanese games;
  `vmc_regions = swiss` follows that rule (`USA`, `EUR`, `JAP` pairs, by the game's code: J, E, everything else),
  which is what you want when the folder is shared with Swiss. The IPL's Memory Card page shows the console's own
  region unless `<vmc_dir>/vmc_region.txt` names another; the popup's **Region** item switches (US/PAL and JPN, or
  USA, EUR and JAP) and remembers the choice in that file.
- A card file that does not exist is **generated** (default 2043 blocks; USA and EUR cards in
  Windows-1252, JAP cards in Shift-JIS), on the first start of a game or of the menu and on a layer
  switch, in whatever folder `vmc_dir` names. Existing files are never reformatted or resized.
- Writes go through a journalled write-back window: a power cut leaves the card file consistent (the
  journal `<card>.wal` is replayed at the next start).
- Games: the card code is found by a scanner in whatever executable is loaded (no per-game tables);
  games that start a second executable (a menu, a 50/60 Hz stub) keep the card. A game whose code the
  scanner cannot place safely starts exactly as stock, without the virtual card.
- IPL menu: the page code is found by signatures at boot (USA, JAP, PAL and PAL 1.2 IPLs checked); the
  cards and the popup extension install only when every check passes, otherwise the menu stays stock.
- Swiss launches and DOL launches are not touched: a game started through Swiss sees Swiss's own emulated card, not
  ours. Swiss's in-game patches use the same resources (the DSI vector, low memory 0x80000C00, the data breakpoint
  register, the top of MEM1), so the two cannot both be live in one game. To have ONE card everywhere, set
  `vmc_dir = /swiss/saves`: the three layers are exactly Swiss's six files, so Swiss's emulation, Cubeboot launches and
  the IPL's Memory Card page share them (proven: the page lists Swiss's cards).

## Using the Memory Card page

The page is the IPL's own. Both virtual cards are listed like real ones (a physical card in a slot replaces the virtual one for that slot).
Press A on a save or on an empty block to open the popup. The items, in order, and when each is offered (an item that cannot run now is drawn
dim and A on it only plays the refusal sound):

| Item | Shown | Available when | What it does |
| --- | --- | --- | --- |
| Move, Copy, Erase | on a save | as in the stock IPL | the IPL's own operations, unchanged |
| Undo | always | an Undo snapshot exists for this card (virtual cards only) | puts the card's directory and block map back to the state before the last Erase, Format, Import, Move or Copy that touched it; one step |
| Region | always | at least one slot is a virtual card | opens a choice of card pairs on the Yes/No column (US/PAL and JPN in stock mode, with the current one preselected; the two other pairs in swiss mode, the title naming the current one); A switches both cards to that pair, a missing pair is generated behind the IPL's own message box reading "Creating Memory Card" (two card files of `vmc_blocks` size are written to the SD: about a minute on the FlippyDrive; the message closes by itself and the page lists the new pair); a pair whose files exist switches at once; remembered in `<vmc_dir>/vmc_region.txt` |
| Swap | always | both slots are virtual cards and no physical card is in either slot (checked live, hot-swap included) | the two card files change place on the SD (A becomes B, B becomes A, their Undo snapshots move along), both cards are re-plugged and the page lists them anew; persistent, the next boot serves the swapped files |
| Import | always | this slot is a virtual card | takes every `.gci` in `<vmc_dir>/import/` and every save of every card image (`.raw`, `.gcp`) there and writes them to this card; a save that is already on the card or does not fit is skipped; each imported file then moves to `<vmc_dir>/imported/` (an older copy of the same name there gives way) |
| Format | always | always, a physical card too | the IPL's own Format of this slot's card behind the stock Yes/No; the text encoding follows the card's region (Shift-JIS for a JPN card, Windows-1252 otherwise); the message afterwards closes by itself after three seconds |

Move, Copy and Erase keep the stock Yes/No; Format keeps the stock Yes/No too; Region is a choice on that column; Undo, Import and Swap run at once. Undo is offered after the actions above; Move and Copy
take a snapshot of both cards (Move) or of the destination (Copy), so either card's Undo reverts its side. The cursor opens on the first available
item. Up from the top item wraps to the bottom and down from the bottom to the top. The popup box grows with the items and is kept between the
page's header and its info panel (on the lower grid rows the IPL places it above the icon, as stock).

A card file of a standard size whose header, directory or block map is bad (a Swiss folder's stale file, a card gone bad) is served as it
is, like a real card gone bad: the IPL says "The Memory Card in Slot B must be formatted. Format it now?" and its own Format rewrites the file
(a game does the same with its own prompt). Nothing is changed until that Yes. A file of a wrong size, or one that cannot be read, is not
served and the IPL says "Nothing is inserted" for that slot; the other slot is served alone. A region switch tries every slot the console may
serve, so a slot that was off at boot comes back with a pair whose files are good; a switch that would lose a card that is there is refused and
the current pair stays. The per-boot status lines (`/cubeboot/cbvmc_ipl_status.txt`) are only written with `vmc_diag` set.

What is persistent: everything. Card contents live in the card files; Swap renames files; Region writes `vmc_region.txt`; Import moves
files from `import/` to `imported/`. Nothing is written to config.ini.

## config.ini keys (section `[cubeboot]`)

```
[cubeboot]
# Virtual memory cards on the SD card (needs a FlippyDrive).  1 = on, 0 = off (nothing of this runs)
vmc = 1

# The folder that holds the card files.  Each slot has one card file per region
# (MemoryCardA.USA.raw, MemoryCardA.JAP.raw, ... and the same for slot B); a missing
# file is created on first use.  Point it at /swiss/saves to share the cards with
# Swiss's own memory card emulation (same file names).
vmc_dir = /cubeboot/saves
# vmc_dir = /swiss/saves

# How games and the IPL's Memory Card menu pick a card:
#   stock = as a real memory card: one pair of cards for US and PAL (the USA files), another for Japan (JAP)
#   swiss = as Swiss does: separate pairs for US (USA), European (EUR) and Japanese (JAP) games
# Use swiss when sharing a folder with Swiss, so that both see the same card for a PAL game.
vmc_regions = stock

# Size of a card file created when it is missing: 59, 123, 251, 507, 1019 or 2043 blocks.
# An existing card is never changed.
vmc_blocks = 2043

# Diagnostics.  0 = write nothing to the SD card.  1 = write four small status files under
# /cubeboot (what to attach to a support question).  Higher bits are developer switches.
vmc_diag = 0
```

`vmc_blocks` only describes a card file to create; an existing card keeps its own size and encoding.

## Diagnostics (on the SD, `/cubeboot/`)

Nothing is written to the SD unless asked. `vmc_diag = 1` writes four small status files, rewritten each
boot: `vmc_scan.txt` (what the scanner decided for the last game), `cbvmc_status.txt` (the game-side
install), `cbvmc_ipl_decision.txt` and `cbvmc_ipl_status.txt` (the IPL-side decision and install); that is
what a support question needs. The higher bits of `vmc_diag` are developer switches (the popup's action
note, counters, stage lines, heartbeats, memory dumps; see `vmc/cbvmc_ipl_plan.h`).

## Building

The runtime images in `patches/data/cbvmc_*.bin` are built from `vmc/` with devkitPPC:
`vmc/build_images.sh blob && vmc/build_images.sh loader && vmc/build_images.sh iplblob && vmc/build_images.sh iplres`
(outputs in `vmc/build/`; they must equal the committed files). `vmc/run_tests.sh` runs the host tests
(card model, journal, generator, popup actions, probe, watch) with a host C compiler and nothing else;
`CBVMC_DOL=<a retail DOL>` adds the scanner tests on a real executable. Nothing of any game or of the
SDK is in this tree: the scanner and the IPL part find their code by shape at run time.
