// JACoop: dynamic portal-gun surfaces.
//
// coop_portal_win is the see-through pane. The `portal` keyword puts it at
// SS_PORTAL sort — the renderer's R_MirrorViewBySurface picks it up and
// renders the exit portal's view first; the stage's depthWrite punches a
// hole in the wall behind so that remote view actually survives when the
// main scene draws over it.  Vertex alpha is a faint colored sheen.

coop_portal_win
{
	portal
	surfaceparm nolightmap
	surfaceparm nomarks
	cull none
	{
		map $whiteimage
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
		depthWrite
	}
}

// soft colored fill shown while the pair is unlinked (no remote view yet)
coop_portal_fill
{
	surfaceparm nolightmap
	surfaceparm nomarks
	cull none
	{
		map $whiteimage
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
		alphaGen vertex
	}
}

// the bold saturated oval rim
coop_portal_rim
{
	surfaceparm nolightmap
	surfaceparm nomarks
	cull none
	{
		map $whiteimage
		rgbGen vertex
	}
}

// soft additive halo just outside the rim
coop_portal_glow
{
	surfaceparm nolightmap
	surfaceparm nomarks
	cull none
	{
		map gfx/effects/whiteglow
		blendFunc GL_ONE GL_ONE
		rgbGen vertex
	}
}
