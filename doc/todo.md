## Known savestate bugs 

- ~~Crash when loading on blue save screen after collecting shine~~
- ~~Timer in piantissimo levels etc. does not save and restore~~
- ~~Goop on the ground of the level is not saved and restored.~~ Not a real bug, just a misconfigured Dolphin emulator setting.
- ~~Crash during shine spawn?~~ Inconsistent. Pianta 3 still breaks on console only.
- ~~Crash when loading during death~~ 
- Delfino Piranha Plant #1 (bianco hills) crash: invalid read from 0x61f9f244, pc = 0x800bc614. Only on emulator for some reason?
- FLUDD sounds break when loading after shine spawn. Might not be fixable reasonably. Sort of inevitable since we don't completely save/restore audio state. Can also just reload the level.

## Scratch

- [x] [Bindings menu](#bindings)
- [x] [SD card settings persistence](#sd-card-persistence)
- [x] Dont require different channels for different regions. load a bin from SD card
- [x] Restore the Nintendont menu and give a way to select the version, and configure the directory.
- [ ] [Customizable GUI support](#customizable-gui)
- [ ] Gecko code porting (gecko_codes.md)
- [ ] Emulator autodetect.
- [ ] Settings menu pauses the game
- [ ] probably have a separate way to control default settings rather than autosaving? and rather than pressing a button to just save the current
    - A bit problematic to deal with because we _sorta_ need two different settings structs, and I dont want that, so we have to be more clever
- [ ] Input tracing feature. Needs planning.
- [ ] Switch everything to a single Clang compiler (based on the kuribo tree) that does PowerPC and ARM to simplify dependencies.
- [ ] QFT doesnt pause on shine get grab when no shine get animation is on (maybe just my qft settings? probably not)
- [ ] Chatgpt critical review that codes have been implemented accurately
- [ ] update readme its not just savestates anymore. update credits to people who made the codes
- [ ] sound fix (warspyking)
- [ ] intermittent crash?? only happened once, with the 64KiB thing
- [ ] move bss out of MEM1
    - On console: Find a place in MEM2 to put it, link the mod against it. At load, copy the code into MEM1, and the data into this spot in MEM2.
    - On emulator, it is a bit more tricky. Pack everything into the DOL, except before the heap is initialized, copy the data into the Dolphin-exclusive memory (at 0x70000000). Then change OSGetArenaLo so that it is able to use the extra heap space taken up.
    - claude brings up the point that the dolphin memory at 0x70000000 is kind of buggy and doesnt properly handle things like byte or half word level accesses, which is true from my recollection, so this could be a problem. 
    - And he says it would only save 5KiB.
- [ ] Fast text broken on US
    - Can't reproduce?
- [ ] restore help text about configurable gui elements in site  

### Bindings

TODO describe

Settings menu itself needs a bind. Default Y-Start. Savestate has a bind, default is d-pad left save d-pad right load (same as currently).

Either do 2 (3) gui buttons or have it record inputs. 


### SD card settings persistence

TODO describe

SD card save persistence / USB as backup.
- Saved popup
- OR failed save.

popup when loading saves.

### Customizable GUI

TODO describe
