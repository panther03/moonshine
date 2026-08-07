import argparse
import os
import re
from pathlib import Path

from dol_c_kit import Project
import patches


def check_arena_reserve(linker_script, base):
    """Verify the debug-stack gap this build's arena reserve assumes.

    OSInit lowers __OSArenaLo to ALIGN32(_stack_addr) when no debug monitor is
    present, so the reserve has to span that gap plus the mod region. If a
    region's map disagrees, the heap would silently overlap the blob.
    """
    text = Path(linker_script).read_text()
    syms = {}
    for name in ("_stack_addr", "__ArenaLo"):
        m = re.search(r"^{} = (0x[0-9a-fA-F]+);".format(re.escape(name)), text, re.M)
        if not m:
            raise RuntimeError("{} not found in {}".format(name, linker_script))
        syms[name] = int(m.group(1), 16)

    if syms["__ArenaLo"] != base:
        raise RuntimeError("link base {:#x} is not __ArenaLo {:#x}".format(base, syms["__ArenaLo"]))

    gap = syms["__ArenaLo"] - ((syms["_stack_addr"] + 0x1F) & ~0x1F)
    if gap != patches.debug_stack_size:
        raise RuntimeError(
            "debug stack gap is {:#x} (__ArenaLo {:#x}, _stack_addr {:#x}) but "
            "patches.debug_stack_size is {:#x}".format(
                gap, syms["__ArenaLo"], syms["_stack_addr"], patches.debug_stack_size))


def main():
    ap = argparse.ArgumentParser(description="Link the mod object into a patched main.dol, a Gecko code list, or a launcher manifest.")
    ap.add_argument("--obj", required=True, help="Relocatable mod object (susamune_pre.o)")
    ap.add_argument("--linker-script", required=True, help="Linker script (.ld) defining SMS symbols")
    ap.add_argument("--kuribo-home", required=True, help="Kuribo toolchain directory (provides powerpc-eabi-ld)")
    ap.add_argument("--vers", default="jp", choices=["jp", "us", "pal"])
    ap.add_argument("--out", required=True, help="Output file")
    ap.add_argument("--in-dol", help="Input main.dol (required for patch_dol)")
    ap.add_argument("--print-commands", action="store_true", help="Echo the raw compiler/linker argv")
    ap.add_argument("mode", choices=["patch_dol", "gecko", "launcher"])
    args = ap.parse_args()

    obj = Path(args.obj).resolve()
    linker_script = Path(args.linker_script).resolve()
    out = Path(args.out).resolve()
    in_dol = Path(args.in_dol).resolve() if args.in_dol else None

    # dol_c_kit builds intermediate paths as `obj_dir + name` and prepends
    # "./", so it only works with a relative obj_dir. Run from the object's
    # directory and address everything else by absolute path.
    os.chdir(obj.parent)

    base = patches.base_addr[args.vers]
    if base is None:
        raise ValueError(f"Base address unset for version {args.vers}!")

    check_arena_reserve(linker_script, base)

    p = Project()
    # The intermediates (<name>.o / <name>.bin) live in obj_dir, which is the
    # build dir shared by every version, and all three versions link in
    # parallel -- so the name has to carry the version or they clobber
    # each other mid-link.
    p.project_name = f"susamune_{args.vers}"
    p.verbose = True
    p.print_commands = args.print_commands
    p.obj_dir = ""
    p.kuribo_compiler_home = args.kuribo_home
    p.base_addr = base
    # `.guicfg` is pinned at base_addr and the code starts after it, so the
    # Gecko codes the web configurator emits address a spot that never moves.
    # See patches.gui_block_size / SUSAMUNE_ADDR_GUI_BLOCK.
    p.code_base_addr = base + patches.gui_block_size
    p.linker_flags.append("--section-start=.guicfg={:#x}".format(base))
    p.blob_max_size = patches.mod_blob_max_size
    p.add_linker_script_file(str(linker_script))
    p.add_obj_file(obj.name)

    for i, patch in enumerate(patches.patches):
        addr = patch[args.vers]
        if addr is None:
            raise ValueError(f"Patch {i} address unset for version {args.vers}!")
        if patch["type"] == patches.PatchType.B:
            p.hook_branch(patch[args.vers], patch["sym"], nop_count=patch.get("nop_count", 0))
        elif patch["type"] == patches.PatchType.BL:
            p.hook_branchlink(patch[args.vers], patch["sym"], nop_count=patch.get("nop_count", 0))
        elif patch["type"] == patches.PatchType.W32:
            p.hook_word(patch[args.vers], patch["val"])

    if args.mode == "patch_dol":
        if in_dol is None:
            ap.error("--in-dol is required to patch a dol")
        p.build_dol(str(in_dol), str(out))
    elif args.mode == "gecko":
        p.build_gecko(str(out))
    elif args.mode == "launcher":
        meta = {
            "game_id": patches.game_id[args.vers],
            "region": patches.region[args.vers],
            "disc_name": patches.disc_name[args.vers],
        }
        p.build_launcher_manifest(str(out), meta=meta, region_reserve=patches.arena_reserve)


if __name__ == "__main__":
    main()
