// JACoop: dynamic portal-gun surfaces.
// coop_portal_win is the see-through pane — sort portal makes the renderer
// run R_MirrorViewBySurface on it, matching against the ET_PORTAL entity the
// game syncs (origin2 = exit portal, eventParm = exit normal).  The stage is
// an additive, near-black vertex-tinted glow so the remote view shows through.
coop_portal_win
{
	surfaceparm nomarks
	surfaceparm nolightmap
	cull none
	sort portal
	{
		map gfx/effects/whiteglow
		blendFunc add
		rgbGen vertex
	}
}

// the bright oval rim around the pane
coop_portal_rim
{
	surfaceparm nomarks
	surfaceparm nolightmap
	cull none
	{
		map gfx/effects/whiteglow
		blendFunc add
		rgbGen vertex
	}
}
