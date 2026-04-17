* Two-segment source forcing an INTERSEG reference.  `jsl helper`
* generates a 4-byte instruction whose 3-byte address operand lives
* in a different load segment, so the assembler emits an EXPR that
* the linker resolves as an inter-segment patch.  Exercises the SUPER
* INTERSEG1 packing path (3-byte patch, shift=0, file=1) in addition
* to the SUPER RELOC3 path the reloc case already covers.

	keep interseg
main	start
	jsl helper
	rts
	end

helper	start
	rts
	end
