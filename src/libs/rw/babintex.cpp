// RenderWare 3.6 core: texture streaming (RW 3.1 file name: src/babintex.c; reading aid only).
// Written from the retail asm (decomp assembly/asm/rw/renderware.s). Tags: [C] confirmed from the asm, [L], [G].
// boot1 staging (rw_wip).

extern unsigned char textureTKList[];   // [C] the texture plugin registry (0x18 bytes, symbol table)

extern "C" int _rwPluginRegistryAddPluginStream(void *registry, unsigned int pluginID, void *readCB, void *writeCB,
                                                void *getSizeCB);

// [C] a call of _rwPluginRegistryAddPluginStream(&textureTKList, pluginID, readCB, writeCB, getSizeCB).
// 0x001E0730
extern "C" int RwTextureRegisterPluginStream(unsigned int pluginID, void *readCB, void *writeCB, void *getSizeCB)
{
    return _rwPluginRegistryAddPluginStream(textureTKList, pluginID, readCB, writeCB, getSizeCB);
}
