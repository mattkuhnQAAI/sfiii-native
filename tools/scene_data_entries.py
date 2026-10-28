"""Scene-module (X1) entries the game reaches only through the scene's own data tables.

A scene keeps addresses of some of its own functions in its script tables: callbacks the
engine copies into an object and later calls through a register (X005 does jsr @r1 on an
object's +0x70 slot). No instruction loads such an address, so discovery does not see it
and the call stops with "not an entry". This rule looks for those addresses:

- a candidate is a 4-byte-aligned word of the image, not an instruction of known code and not
  a literal that known code loads, whose value is a 2-byte-aligned address inside the image;
- it is accepted only if it is not known code, not a literal-pool slot, and a straight walk
  from it decodes as plausible SH-2 code: no illegal word, literals and branch targets inside
  the image, no walk into a known literal pool, legal delay slots, ending at an unconditional
  rts/rte/jmp/bra/braf plus its delay slot within MAX_INSNS instructions.

Every candidate is returned with its verdict so a build can log accepted and rejected ones.
Nothing here changes timing: it only adds recompiled entry points."""
import re

SCENE = re.compile(r"X1[A-Z0-9_]+$")
ENDS = ("rts", "rte", "jmp", "bra", "braf")
MAX_INSNS = 256

# Regression guard: the soak crash this rule fixes (X005 calls 0605E06C through an object slot).
# Kept as explicit seeds so the entry stays even if the table rule changes; still validated.
REGRESSION_SEEDS = {"X1JASN01": (0x0605E06C,), "X1FUTTIN": (0x0605E06C,)}


def is_scene(name):
    return bool(SCENE.match(name))


def plausible_code(img, start, pools):
    """None if a straight walk from start reads as code, else why not."""
    pc = start
    for _ in range(MAX_INSNS):
        if not img.contains(pc, 2):
            return "runs off the image at %08X" % pc
        if pc in pools:
            return "runs into a literal pool at %08X" % pc
        ins = img.insn(pc)
        if ins.op == ".word":
            return "illegal %04X at %08X" % (ins.word, pc)
        if ins.target is not None and not img.contains(ins.target, ins.size or 2):
            return "%s target %08X outside the image at %08X" % (ins.op, ins.target, pc)
        if ins.delay:
            slot_pc = pc + 2
            if not img.contains(slot_pc, 2) or slot_pc in pools:
                return "no delay slot at %08X" % slot_pc
            slot = img.insn(slot_pc)
            if slot.op == ".word" or slot.delay or slot.op in ("bt", "bf"):
                return "illegal delay slot %04X at %08X" % (slot.word, slot_pc)
            if ins.op in ENDS:
                return None
            pc += 4
            continue
        pc += 2
    return "no unconditional branch within %d instructions" % MAX_INSNS


def data_table_entries(img, program, name=None):
    """(accepted entry addresses, [(target, verdict, reason, table words)]) for one image,
    given discovery (`program`) over its other seeds. `name` adds that module's
    REGRESSION_SEEDS (inside the image only), validated the same way."""
    code = set(program.code)
    entries = set(program.funcs)
    pools = set()
    for a in code:
        ins = img.insn(a)
        if ins.op in ("mov.w", "mov.l") and ins.size and img.contains(ins.target, ins.size):
            pools.update(range(ins.target, ins.target + ins.size, 2))
    refs = {}
    for at in range(img.base + (-img.base % 4), img.end - 3, 4):
        if at in code or at + 2 in code or at in pools:
            continue
        value = img.u32(at)
        if img.base <= value < img.end - 1 and not value & 1:
            refs.setdefault(value, []).append(at)
    accepted, rows = set(), []
    for target in sorted(refs):
        if target in entries:
            verdict, reason = "known", "already an entry"
        elif target in code:
            verdict, reason = "rejected", "inside known code"
        elif target in pools:
            verdict, reason = "rejected", "a literal-pool slot"
        else:
            reason = plausible_code(img, target, pools)
            verdict = "rejected" if reason else "accepted"
            if not reason:
                accepted.add(target)
                reason = "plausible code"
        rows.append((target, verdict, reason, refs[target]))
    for target in REGRESSION_SEEDS.get(name, ()):
        if target in refs or not img.contains(target, 2):
            continue
        if target in entries:
            rows.append((target, "known", "already an entry (regression seed)", []))
            continue
        reason = "inside known code" if target in code else plausible_code(img, target, pools)
        if reason:
            rows.append((target, "rejected", reason + " (regression seed)", []))
        else:
            accepted.add(target)
            rows.append((target, "accepted", "regression seed, plausible code", []))
    return accepted, rows
