{ pkgs, legacyPolygott }: {
	deps = [
		pkgs.bashInteractive
	] ++ legacyPolygott;
}