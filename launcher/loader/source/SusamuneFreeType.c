/*
 * The launcher only opens its embedded TrueType font.  FreeType's stock
 * initializer registers every driver in the vendored 2.4.12 archive, which
 * makes the static linker pull all of them into boot.dol.
 */

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H

#include <string.h>

#if SUSAMUNE_MINIMAL_FREETYPE

extern const FT_Module_Class tt_driver_class;
extern const FT_Module_Class sfnt_module_class;
extern const FT_Module_Class autofit_module_class;
extern const FT_Module_Class ft_raster1_renderer_class;
extern const FT_Module_Class ft_smooth_renderer_class;

/* These are part of FreeType 2.4.12's base/system objects, but not public. */
extern FT_Memory FT_New_Memory(void);
extern void FT_Done_Memory(FT_Memory memory);

FT_EXPORT_DEF(void)
FT_Add_Default_Modules(FT_Library library)
{
	static const FT_Module_Class *const modules[] = {
		&tt_driver_class,
		&sfnt_module_class,
		&autofit_module_class,
		&ft_raster1_renderer_class,
		&ft_smooth_renderer_class,
	};
	unsigned int i;

	for (i = 0; i < sizeof(modules) / sizeof(modules[0]); ++i)
		FT_Add_Module(library, modules[i]);
}

FT_EXPORT_DEF(FT_Error)
FT_Init_FreeType(FT_Library *library)
{
	FT_Memory memory = FT_New_Memory();
	FT_Error error;

	if (!memory)
		return FT_Err_Unimplemented_Feature;

	error = FT_New_Library(memory, library);
	if (error) {
		FT_Done_Memory(memory);
		return error;
	}

	FT_Add_Default_Modules(*library);
	return FT_Err_Ok;
}

FT_EXPORT_DEF(FT_Error)
FT_Done_FreeType(FT_Library library)
{
	if (library) {
		FT_Memory memory;

		/* FT_LibraryRec_ starts with `memory' in the pinned 2.4.12 ABI. */
		memcpy(&memory, library, sizeof(memory));
		FT_Done_Library(library);
		FT_Done_Memory(memory);
	}

	return FT_Err_Ok;
}

#endif
