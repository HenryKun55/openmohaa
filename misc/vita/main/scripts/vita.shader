// Vita settings menu font (misc/vita/make_font.py): alpha-blended like the game's fonts.
gfx/fonts/vita-14
{
	nopicmip
	nomipmaps
	{
		map gfx/fonts/vita-14.tga
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen identity
		nodepthtest
	}
}
