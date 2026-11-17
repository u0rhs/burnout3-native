// RenderWare 3.6 core: pipelines, common (RW 3.1 file name: src/bapipcom.c; reading aid only).
// Written from the retail asm (decomp assembly/asm/rw/renderware.s). Tags: [C] confirmed from the asm, [L], [G].
// boot1 staging (rw_wip).

// [C] returns 1 (jr ra; addiu v0, zero, 1).
// 0x0020F098
extern "C" int _rwPipeAttach(void)
{
    return 1;
}
