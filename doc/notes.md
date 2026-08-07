## 2026-08-03

GUI config idea:

Don't use "gui.bin", but rather distribute the gui config as a gecko code.
These gecko codes will target fixed addresses that by default, hold the default config for each code.
So the behavior of the code itself is in the C++, but its config data will be overwritten by gecko. 
let's say each config thing is some struct that has color, gradient, bla bla. All this is in the fixed location in .data.
The C++ code reads from this. The gecko issues some 04 commands to overwrite this with the config blob.

The only exception to this is configurable GUI. This I think should be implemented completely with code (Just like the current one, basically).
However, I do think it should not hook in directly to the game loop, but hook in in a place where it can be called from the C++. This way, it can be a no op by default and also be toggled on and off in-game.

One big problem remains: whats the way we distribute this gecko code? because people are already going to have their gct files most likely. so we'll want to allow loading it from elsewhere. Maybe the gecko codes themselves can be gui.bin, and we just make nintendont load gecko codes from alternate sources?

## 2026-07-28

> move all the initialization to appinit?

Not possible. They cannot safely initialize directly in onAppInit() because, at that point:
* The current heap is a temporary boot heap that is later destroyed.
* gpSystemFont is still null.

## 2026-07-18

Miscellaneous notes from zeldocto conversation that have not been sorted into gecko_codes.md and todo.md:

Settings Presets:
Fast Any% mode 
IL mode

QFT AND shine get. some people like both.

make the gecko code limit higher
or just put gci loader (low priority)

https://github.com/AlexanderHarrison/HOW-TO-WRITE-GECKO-CODES

## 2026-06-27

TODO: 
- fix the crashing warps
- figure out how much heap memory we have
- the ui thing below

tabs:
- Lists
- MRU
- Stage select
- Teleporter

Note: All list views can be scrolled.
Note: All displays should share the same text buffer (to minimize memory allocations.)

Lists:

By default, shows the _Selected_ list of warps.  A list works exactly like the hardcoded 20 shines list. Select a warp to teleport to it. You can also delete a warp from a list.

You can press a button and open the list selection menu. There will be some default lists like the 20 shines list that's currently hardcoded.

In this menu, you can also create a new list. Idk how the text entry menu will work. Maybe it will just name the lists by number.

You can also press a button to delete a list.

You can also press a button to save the lists to the memory card (or I was also thinking an SD card because that might be easier to do in Nintendont; it would work even with the memcard unplugged).

MRU:

A deduplicated in-order list of every warp you've done since booting the game (stores up to a configurable limit).

Stage select:

Shows a screen similar to the one that already exists which warps you to each episode of every stage. You can press one button to warp immediately, and press another to add to the current list. 

Teleporter:

Lets you dial in a custom warp with every possible setting. You can set the stage id (should hopefully have names for the recognized ones), the episode, etc.
And then also ALL the flags that you want the warp to set to. All of the flags will default to unset except for the ones that we currently set by default.
TBD an efficient data structure to handle this because it will of course be very sparse. Probably a linked list or something. But we know exactly what indices things should be at. 

The flags should have names of what they do according to the google sheets.

A warp's info (stage, episode, flags) should be encapsulated in a single "warp" data structure that is written to the memory card, forms the lists, etc.

## 2026-04-29

https://github.com/project-slippi/Nintendont/blob/slippi/SLIPPI.md

Has better networking and native support for replays etc.

## 2026-04-27

Do integrity check of written to area in nintendont

-> Added a checksum. It does not stay the same in  the region  we are writing to. So presumably something is using it still.
-> However, if we are just looking at the region 0x10000000 - 0x20000000, nothing appears to change (although the checksum does not start at 0), and we are also able to write to it without problems. We may be able to use this 16MB to handle savestates via copying just the arena.

### autosplitter with memory card 

The idea is to pick up memory card activity from the game as split indicators. 
However, the first problem is that we want the card to be removed for the duration of the run. As far as I can tell, the system does not even try to put anything over the EXI
if the card (sense pin) is not detected. So there is probably nothing we can observe.
One potential solution is to have the card _error_ instead of be marked as not detected. This is possible by having the card report an unsupported sector size in the EXI ID. It is checked here in the SMS code:

```cpp
s32 TCardManager::probe_()
{
	s32 sectorSize;
	s32 result = CARDProbeEx(mChannel, nullptr, &sectorSize);

	// unsupported sector size
	if (result == CARD_RESULT_READY && sectorSize != 0x2000)
		result = -256;

	return result;
}
```
in the CardSave menu this -256 result prompts the game to display the error message, which can also be skipped through like the no card detected menu.  
In the FlipperMCE we can make the card report a different sector size by modifying GC_MC_SECTOR_SIZE at this line: https://github.com/FlipperMCE/firmware/blob/3e5b948a82862b36844ca2fca08548ebf0bb422c/src/gc/card_emu/gc_memory_card.c#L201
On Dolphin, we can report a different sector size by changing the code in [EXI_DeviceMemoryCard.cpp](https://github.com/dolphin-emu/dolphin/blob/ab6b30afe2cdde8ba6eea9a33ea64ce700d933a1/Source/Core/Core/HW/EXI/EXI_DeviceMemoryCard.cpp#L399):

```cpp
case Command::NintendoID:
      //
      // Nintendo card:
      // 00 | 80 00 00 00 10 00 00 00
      // "bigben" card:
      // 00 | ff 00 00 05 10 00 00 00 00 00 00 00 00 00 00
      // we do it the Nintendo way.
      if (m_position == 1)
        byte = 0x80;  // dummy cycle
      else if (m_position == 5)
      {
        byte = 0x80;
      }
      else if (m_position == 4)
      {
        byte = gStuff ? 0x08 : 0x00;
      }
      else
      {
        byte = 0x00;
        //byte = static_cast<u8>(m_memory_card->GetCardId() >> (24 - (((m_position - 2) & 3) * 8)));
      }
```

The problem with this solution is, at least on emulator, doing this instead of removing the card loses a couple frames. Maybe on console it comes out to the same speed, TBD.
So for now it seems like the autosplitter thing wont work. It's anyway not clear how you would detect the console being reset through the memory card either. 

## 2026-04-15

```cpp
static void drawHeapUsage(JKRHeap *heap, f32 &maxUsage, JUtility::TColor color, u16 y) {
    const auto heapSize = getHeapSize(heap);

    const f32 currentUsage = static_cast<f32>((heapSize - heap->getTotalFreeSize())) / heapSize;
    if (currentUsage > maxUsage)
        maxUsage = currentUsage;

    {
        s16 adjust = getScreenRatioAdjustX();
        drawMonitorBar(currentUsage, maxUsage, color, (gBaseMonitorX - adjust) + 2, y,
                       (gMonitorWidth + adjust) - 6, 4);
    }
}
```

## 2026-04-14

seems like the dolphin memory at 0x70000000 is a lie? Or at least it doesn't support byte level instructions? 
Only when we using uncached memory, dolphin crashes inside our memcpy routine, BEFORE we've even touched the stack.
It starts writing the wrong data at 0x80000000 (0xc0000000). I don't know where the data comes from, but it only writes a single byte into each word.
It doesn't look like each byte from a word is being split into words, there doesn't seem to be a rhyme or reason.
I wonder if Dolphin doesn't fully emulate the uncached memory, and only supports writing through the cache flush instructions instead. we can try that or see if full word instructions work.


Other state to be worried about:
- L1 scratchpad state (if enabled)
- DMA scratchpad 

Apparently L2 is _not_ a problem, even though its address space is mapped. It is not configurable as a scratchpad.
Coherence with the graphics processor? Maybe just use uncached for now.

## 2026-04-13

if we can just save space in the game's ram, that will be enough to combine the practice and ranked rom. practice mode can set aside nintendonts ram for savestate, ranked can set it aside for tcpip/anything else it needs (traces?).

are we ever going to have problems using the same boot.bin and etc. while replacing main.dol? aren't there certain offsets in boot.bin that are tied to main.dol that we have to worry about?

delfino plaza crashes when the heap is raised to 0x80644020. 0x80544020 works. Have not tested what the exact limit is, but this seems like a bad path to go on, because I won't know which level in the game is going to crash...

Note: Once our code gets big enough, we might bump into the stack and heap. The stack grows downward from 0x80424008, while the heap
is configured by default to start from 0x18 off that (0x80424020). Currently, our code ends at 0x80414D80 so there is a ways to go.

void save_state() {
asm("
load fixed address for savestate location into temp. register
save context into location
setup args for memcpy
call memcpy
restore link register from savestate
return
");
}

void load_state() {
asm("
load fixed address for savestate location into temp. register
setup args for memcpy
call memcpy
restore context (incl. link register)
return
")
}

## 2026-04-10

it seems that the rapid input can be enabled to actually repeat inputs using the other field in the struct:
		/* 0x18 */ u32 mRepeat;
		/* 0x1C */ u32 mRepeatCount;
		/* 0x20 */ u32 mRepeatStart;
		/* 0x24 */ u32 mRepeatMask;
		/* 0x28 */ u32 mRepeatDelay;
		/* 0x2C */ u32 mRepeatRate;


// lines to make get card status always return NO_CARD, unfortunately this doesn't work and just hangs the game
        80107dc8 42 80 00 20     b          LAB_80107de8
                             LAB_80107de8                                    XREF[2]:     80107dc8(j), 80107dd0(j)  
        80107de8 38 60 ff fc     li         this,-0x4


number of shines address: 80575278

https://github.com/dolphin-emu/dolphin/blob/c0e0b685f372e49f628cbab43e6bbd687fcbd636/Source/Core/Core/State.cpp#L136

savestate handling

## 2026-04-08

big brain binary search
```
int searchLevel = 3;
    int searchPath = 0b011;
u32 startShine = 0;
      u32 numShines = 128;
      for (int l = searchLevel; l > 0; l--) {
        numShines >>= 1;
        if (searchPath & 1) {
          startShine += numShines;
        }
        searchPath >>= 1;
      }
//u32 shineCap = Min(startShine + numShines, 120);
      //for (u32 shine = 64; shine < 95; shine++) {
      //  TFlagManager::smInstance->setShineFlag(shine);
      //}
```


hardware device that unplugs memory card automatically?
memcard gc pro - already supports wireless, so just have to see if its possible to tell it to disconnect.

## 2026-04-07

Code snippet for manual timer reset:

```
if (gChangingStateSettingsMenu) { 
        volatile u32* timer_addr = ((volatile u32*)(0x80907A7C));
        *timer_addr = 0;
        timer_addr = ((volatile u32*)(0x80907A78));
        *timer_addr = 0;
        gChangingStateSettingsMenu = false;
        
    }
```
Turns out the qf timer has a reset flag internally, setting this to 1 will make it reset in between stage warps.

## 2026-04-06

800f9f54

lwz	r8, -0x6818 (r13)
lwz	r8, 0x005C (r8)

timer (raw) address: 80907A7C

THIS NUMBER AINT IT!! ^^^ there is some other offset or something...


timer update routine seems to start at 800024b0
timer (string) address: 80435374

ask slippi people about anti cheat mechanisms for ranked?

libKuriboClang.a? What does it do? If we have errors that smell like it belongs to this, check it for what symbols it defines?

## 2026-04-05

Changing heaps? What is the right way to initialize stuff and keep state between level changes?
Probably there is a heap that does not get cleared? We can just store stuff there. Have to find the right place to hook it in. Most likely in the same place as the initial heap initialization where the guy inserted the kuribo hook.
will resetting run that code again? probably not

moveStage() - what does it do?

use clangd from kuribo clang, in case the system one doesnt integrate well? tbd

Remangling works, but it does not address vtable layout. for this, you really need the compiler to use the same abi at codewarrior. this is what the kuribo clang does.
all the other solutions like reimplementing the behavior of virtual functions by hand sound like way too much work, so I am going to use the kuribo LLVM instead.
We can just put the binaries in util/ inside the repository itself and give credit.

## 2026-04-03

- Put everything all in one class in menu.cpp, since that seems to be the standard in the rest of the codebase
- Allocate retained state like all the J2DTextboxes and panes and etc. in the init() function
     - can use a mix - for example dont need to allocate (# num of stages) * (# num of episodes) number of textboxes for the episodes. Can change them and hide them on the fly.
     - Descriptors for this data can be stored outside the class in global, const memory
- external structs should only contain info (e.g. j2dtextbox) and not state

## 2026-04-01

do we really need to make our own gui stuff?
can we use the TExPane stuff without blo menu?
decomp has code for the save menu & etc. :

https://github.com/doldecomp/sms/blob/main/src/GC2D/CardSave.cpp#L121

nintendont can emulate memory cards
tell people to use slot B and use sd card?

## 2026-03-31

ideally, shouldn't bother supporting multiple regions. just base everything on jp 1.0. it will run on all console regions when using nintendont/swiss.
can also bypass video incompatibility so they work ootb.

## 2026-03-29


https://github.com/lark-parser/lark

Use to parse the grammar in the ABI spec

            if ((mController->mButtons.mRapidInput &
                 (TMarioGamePad::DPAD_DOWN | TMarioGamePad::MAINSTICK_DOWN))) {
                for (int i = mSettingID + 1; i < mCurrentGroupInfo->mSettingInfos.size(); ++i) {
                    auto *settingInfo = getSettingInfo(i);
                    if (settingInfo->mSettingData->isUserEditable()) {
                        mCurrentSettingInfo = settingInfo;
                        mSettingID          = i;
                        break;
                    }
                }
            }
            if ((mController->mButtons.mRapidInput &
                 (TMarioGamePad::DPAD_UP | TMarioGamePad::MAINSTICK_UP))) {
                for (int i = mSettingID - 1; i >= 0; --i) {
                    auto *settingInfo = getSettingInfo(i);
                    if (settingInfo->mSettingData->isUserEditable()) {
                        mCurrentSettingInfo = settingInfo;
                        mSettingID          = i;
                        break;
                    }
                }
            }


## 2026-03-26

to handle the iso stuff, can put build/iso - extracted ISO that does not change
then build/overlay - all the stuff we output, mirroring the directory structure of the ISO
the target that builds the ISO either creates another directory or applies these patches on the fly
actually everything should probably just udpate a build/out_iso directory that way we're not constantly copying the entire iso


pacman -S \
    mingw-w64-x86_64-gcc \
    mingw-w64-x86_64-python \
    mingw-w64-x86_64-python-pip \
    mingw-w64-x86_64-python-setuptools

pacman -S \
    mingw-w64-ucrt-x86_64-libjpeg-turbo \
    mingw-w64-ucrt-x86_64-zlib \
    mingw-w64-ucrt-x86_64-libtiff \
    mingw-w64-ucrt-x86_64-freetype \
    mingw-w64-ucrt-x86_64-lcms2 \
    mingw-w64-ucrt-x86_64-libwebp \
    mingw-w64-ucrt-x86_64-openjpeg2 \
    mingw-w64-ucrt-x86_64-libimagequant \
    mingw-w64-ucrt-x86_64-libraqm \
    mingw-w64-ucrt-x86_64-libavif


nevermind, i just told pip not to use any of these dependencies (e.g. "-C zlib=disable")

## 2026-03-25
 
dependencies would be just:
- meson (or meson can just be installed in the virtualenv?? for ides, set venv/bin/meson in vscode settings as meson path)
- ninja 
- python: virtualenv 
- devkitppc

Pros of kuribo:
- Patching goes in the code - no external build scripts with a lot of the logic 
    - This may be possible to accomplish with build scripts as well? I.e. a header file with the addresses and the mappings to the patch functions.
- Theoretically compatible with other mods.. but would this project really go with other mods?
    - If each mod is presumably going to try and modify the game loop, how could they hope to be compatible?
    - Does the engine call each patch in order? But each patched function is going to try and call the original, so how does that work?
    - In theory it would be nice, if very niche, to have all of this work on SMS hacks. Integrating with kuribo would make that easier, but anyway it's extremely far fetched
- GeckoJIT - can just copy the practice gecko codes and turn them on and off dynamically.
    - On the other hand, they seem simple enough to recreate. The quarterframe timer I am probably going to want to implement myself at some point,.
    - Patching them statically is also easy thanks to dol_c_kit.

Cons of kuribo:
- Forced to use CMake
- Have to depend on a bunch of code that I have to make modifications to to support JP and SMS
- I either have to work on upstreaming it, or keep it to myself and then when he updates it will be hell for me to integrate if there's anything useful
- Not a very active project so not clear if he's really working on this still

Pros of not using kuribo:
- (all of the cons above)
- can use meson build system
- can still use the sunshine c headers, i will just integrate them into my own project and give credit 
- When patching the binary instead of loading in via kuribo, probably easier to integrate devkitPPC's libraries? 

0x80426b64
0x804240e0 start of kernel in heap
... not reading the full thing?

## old

https://github.com/JoshuaMKW/dolreader.git


0x7F75BCA8


Note: HxD can start the comparison again after an offset if you just move your cursor
for when things are inserted

OSProtectRange 0x803463DC
Call to replace at 0x802a73f0 [0x800fadf4], 0x802a7404 [0x800fae08] (US) [JP]
hook_string with 0x60000000 (nop) as a string i guess?

JKRExpHeap::createRoot call to replace at 0x802a744c [0x800fae50] (US)

> In that hook, I accomplish a few things.
> 
> I call the original method JKRExpHeap::createRoot(int, bool)
> I call DVDOpen with the path to the Kuribo kernel binary
> I call DVDReadPrio on the file handle to read it into memory
> I call the Kuribo kernel entry point to load modules in








OSFatal: error handling

why not use bettersunshineengine? cause he doesnt support jp and to even compile it i'd have to fill in all those addresses
also because i dont want to inherit changes i dont know about 


TMenuDirector::direct
800f67a8

 -c OnInitApp:0x8000561c:0

 useful for injecting stuff that _actually_ initializes when the app is

# random 

- distance between original sms code (even at the start) and injected text2 is less than 24-bits... is there really a problem with relocations? why are we getting that error?

# how the injection works 

- Find a function call in the function you want to inject in 
- Add an argument to DOLInject saying the name of the function to call instead, the PC of the callsite, and the number of nops to replace before the instruction (?)
- For example: `-c OnUpdate:0x800f9b64:3` Insert 3 nops starting at 0x800f9b64, then 0x800f9b70 (3 instructions later), call `OnUpdate`.
- In this case in particular, 0x800f9b70 is where `gameLoop` calls `mDirector->direct()`. So it is the main call to the game update loop. 
- The call inserted by DOLInsert is just a direct branch and link, because it turns out the text2 segment fits in the 24-bit range just fine. No trampoline required.
- It also does not have to worry about volatile registers or saving context (i think), because it is replacing a function call. The compiler has already worried about saving volatile registers that may get overridden.
- A varying number of nops are required because some calls, like those required for dynamic dispatch, require loading the link register from other registers. For example, in the same OnUpdate example, this is the code before and after:

before:
```
lwz r12, 0(r3)
lwz r12, 0x64(r12)
mtlr r12
blrl 
```

after:
```
nop 
nop 
nop 
bl 0x80417800
```

- I do not know what "-c" and "-r" do just yet.
- The DOLInsert just replaces a function call and does not worry about putting anything back, so that is why the user is responsible for calling the function in their own thing.
- would also be stupid to do it a different way because you need the pointers to the local variables or the current class etc. in the functions you inject. not everything is going to be accessible via global vars.


=> how does kuribo work??
=> how does it link against symbols?
    => takes a map file in "KuriboConverter.exe"  (we should be able to generate one for jp version...)


we take sunshine_header_interface, which in turn uses kuribo
we depend on both 
use converter in kuribo 

how do you patch other files with kuribo? non-executable 

switch to cmake sadge ...