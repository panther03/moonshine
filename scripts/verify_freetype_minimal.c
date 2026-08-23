/*
 * Host-side equivalence harness for the launcher's FreeType 2.4.12 module
 * allowlist.  Build this against that exact source/configuration, not the
 * host's system FreeType.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H

extern const FT_Module_Class tt_driver_class;
extern const FT_Module_Class t1_driver_class;
extern const FT_Module_Class cff_driver_class;
extern const FT_Module_Class t1cid_driver_class;
extern const FT_Module_Class pfr_driver_class;
extern const FT_Module_Class t42_driver_class;
extern const FT_Module_Class winfnt_driver_class;
extern const FT_Module_Class pcf_driver_class;
extern const FT_Module_Class bdf_driver_class;
extern const FT_Module_Class sfnt_module_class;
extern const FT_Module_Class autofit_module_class;
extern const FT_Module_Class pshinter_module_class;
extern const FT_Module_Class ft_raster1_renderer_class;
extern const FT_Module_Class ft_smooth_renderer_class;
extern const FT_Module_Class ft_smooth_lcd_renderer_class;
extern const FT_Module_Class ft_smooth_lcdv_renderer_class;
extern const FT_Module_Class psaux_module_class;
extern const FT_Module_Class psnames_module_class;

extern FT_Memory FT_New_Memory(void);
extern void FT_Done_Memory(FT_Memory memory);

typedef struct Library {
	FT_Library handle;
	FT_Memory memory;
} Library;

static const FT_Module_Class *const kFullModules[] = {
	&tt_driver_class,
	&t1_driver_class,
	&cff_driver_class,
	&t1cid_driver_class,
	&pfr_driver_class,
	&t42_driver_class,
	&winfnt_driver_class,
	&pcf_driver_class,
	&bdf_driver_class,
	&sfnt_module_class,
	&autofit_module_class,
	&pshinter_module_class,
	&ft_raster1_renderer_class,
	&ft_smooth_renderer_class,
	&ft_smooth_lcd_renderer_class,
	&ft_smooth_lcdv_renderer_class,
	&psaux_module_class,
	&psnames_module_class,
};

static void fail(const char *what, FT_ULong codepoint, unsigned int size)
{
	fprintf(stderr, "%s at U+%04lX, %u px\n", what, codepoint, size);
	exit(1);
}

static Library make_library(const FT_Module_Class *const *modules,
			    size_t count)
{
	Library result;
	size_t i;

	result.memory = FT_New_Memory();
	if (!result.memory || FT_New_Library(result.memory, &result.handle)) {
		fprintf(stderr, "cannot create FreeType library\n");
		exit(1);
	}

	for (i = 0; i < count; ++i) {
		FT_Error error = FT_Add_Module(result.handle, modules[i]);
		if (error) {
			fprintf(stderr, "cannot add module %lu: 0x%X\n",
				(unsigned long)i, error);
			exit(1);
		}
	}

	return result;
}

static void done_library(Library *library)
{
	FT_Done_Library(library->handle);
	FT_Done_Memory(library->memory);
}

static void compare_slot(FT_GlyphSlot a, FT_GlyphSlot b, FT_ULong cp,
			 unsigned int size)
{
	size_t bytes;

#define SAME(field) \
	do { if (a->field != b->field) fail(#field, cp, size); } while (0)
	SAME(metrics.width);
	SAME(metrics.height);
	SAME(metrics.horiBearingX);
	SAME(metrics.horiBearingY);
	SAME(metrics.horiAdvance);
	SAME(metrics.vertBearingX);
	SAME(metrics.vertBearingY);
	SAME(metrics.vertAdvance);
	SAME(linearHoriAdvance);
	SAME(linearVertAdvance);
	SAME(advance.x);
	SAME(advance.y);
	SAME(format);
	SAME(bitmap.rows);
	SAME(bitmap.width);
	SAME(bitmap.pitch);
	SAME(bitmap.pixel_mode);
	SAME(bitmap.num_grays);
	SAME(bitmap_left);
	SAME(bitmap_top);
#undef SAME

	bytes = (size_t)a->bitmap.rows *
		 (size_t)(a->bitmap.pitch < 0 ? -a->bitmap.pitch : a->bitmap.pitch);
	if (bytes && memcmp(a->bitmap.buffer, b->bitmap.buffer, bytes) != 0)
		fail("bitmap", cp, size);
}

static void compare_glyph(FT_Face full, FT_Face minimal, FT_ULong cp,
			  unsigned int size, FT_UInt *previous)
{
	FT_UInt full_index = FT_Get_Char_Index(full, cp);
	FT_UInt minimal_index = FT_Get_Char_Index(minimal, cp);
	FT_Vector a;
	FT_Vector b;

	if (full_index != minimal_index)
		fail("glyph index", cp, size);
	if (FT_Load_Glyph(full, full_index, FT_LOAD_RENDER))
		fail("full glyph load", cp, size);
	if (FT_Load_Glyph(minimal, minimal_index, FT_LOAD_RENDER))
		fail("minimal glyph load", cp, size);
	compare_slot(full->glyph, minimal->glyph, cp, size);

	if (*previous) {
		if (FT_Get_Kerning(full, *previous, full_index,
				   FT_KERNING_DEFAULT, &a) ||
		    FT_Get_Kerning(minimal, *previous, minimal_index,
				   FT_KERNING_DEFAULT, &b))
			fail("kerning", cp, size);
		if (a.x != b.x || a.y != b.y)
			fail("kerning delta", cp, size);
	}
	*previous = full_index;
}

int main(int argc, char **argv)
{
	static const unsigned int sizes[] = {
		8, 10, 12, 14, 16, 18, 20, 24, 32, 48, 64
	};
	Library full;
	FT_Library minimal;
	FT_Face full_face;
	FT_Face minimal_face;
	unsigned char *font;
	long font_size;
	FILE *file;
	size_t size_index;
	unsigned long checks = 0;

	if (argc != 2) {
		fprintf(stderr, "usage: %s font.ttf\n", argv[0]);
		return 2;
	}
	file = fopen(argv[1], "rb");
	if (!file || fseek(file, 0, SEEK_END) || (font_size = ftell(file)) <= 0 ||
	    fseek(file, 0, SEEK_SET)) {
		fprintf(stderr, "cannot read %s\n", argv[1]);
		return 2;
	}
	font = (unsigned char *)malloc((size_t)font_size);
	if (!font || fread(font, 1, (size_t)font_size, file) != (size_t)font_size) {
		fprintf(stderr, "cannot read %s\n", argv[1]);
		return 2;
	}
	fclose(file);

	full = make_library(kFullModules,
			    sizeof(kFullModules) / sizeof(kFullModules[0]));
	if (FT_Init_FreeType(&minimal)) {
		fprintf(stderr, "cannot create minimal FreeType library\n");
		return 1;
	}
	if (FT_New_Memory_Face(full.handle, font, font_size, 0, &full_face) ||
	    FT_New_Memory_Face(minimal, font, font_size, 0, &minimal_face)) {
		fprintf(stderr, "cannot open font\n");
		return 1;
	}
	if (full_face->num_glyphs != minimal_face->num_glyphs ||
	    full_face->face_flags != minimal_face->face_flags ||
	    full_face->style_flags != minimal_face->style_flags)
		fail("face metadata", 0, 0);

	for (size_index = 0; size_index < sizeof(sizes) / sizeof(sizes[0]);
	     ++size_index) {
		FT_ULong cp;
		FT_UInt full_index;
		FT_UInt previous = 0;
		unsigned int size = sizes[size_index];

		if (FT_Set_Pixel_Sizes(full_face, 0, size) ||
		    FT_Set_Pixel_Sizes(minimal_face, 0, size))
			fail("pixel size", 0, size);

		compare_glyph(full_face, minimal_face, 0, size, &previous);
		cp = FT_Get_First_Char(full_face, &full_index);
		while (full_index) {
			compare_glyph(full_face, minimal_face, cp, size, &previous);
			++checks;
			cp = FT_Get_Next_Char(full_face, cp, &full_index);
		}
	}

	FT_Done_Face(full_face);
	FT_Done_Face(minimal_face);
	done_library(&full);
	FT_Done_FreeType(minimal);
	free(font);
	printf("FreeType full/minimal match: %lu rendered glyph-size cases\n",
	       checks);
	return 0;
}
