
StaticLibrary {
	Name = "AmigaHunkParser",

	Sources = { 
		"amiga_hunk_parser.c",
	},
}

Program {
	Name = "test",

	Depends = { "AmigaHunkParser" },
	Sources = { "test.c" }, 
}

Default "test"
