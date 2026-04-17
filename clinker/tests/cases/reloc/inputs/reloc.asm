* A single segment with a self-reference that forces the linker to
* emit a relocation record. The `jml lab1` pulls in a 3-byte intra-
* segment address that the linker must relocate when the segment is
* loaded.

	keep reloc
main	start
	jml lab1
lab1	rts
	end
