// Crea funciones en los prologos "addiu sp, sp, -N" que el analisis
// automatico dejo fuera de cualquier funcion. Son funciones que solo se
// invocan por puntero (callbacks, tablas), que Ghidra no descubre solo.
// Limitacion: las funciones hoja sin prologo siguen sin detectarse.
//   analyzeHeadless data/ghidra ac3 -process ace_eu.exe -scriptPath tools/ghidra \
//       -postScript CreateMissingFunctions.java
//@category AC3

import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;

public class CreateMissingFunctions extends GhidraScript {
    @Override
    public void run() throws Exception {
        MemoryBlock code = null;
        for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
            if (b.getName().equals("CODE")) code = b;
        }
        if (code == null) {
            printerr("no hay bloque CODE");
            return;
        }
        int created = 0, failed = 0;
        Address a = code.getStart();
        while (a.compareTo(code.getEnd()) < 0) {
            int w = getInt(a);
            // addiu sp, sp, imm negativo: 0x27BDxxxx con bit 15 a 1
            if ((w & 0xFFFF8000) == 0x27BD8000 && getFunctionContaining(a) == null) {
                if (getInstructionAt(a) == null) {
                    new DisassembleCommand(a, null, true).applyTo(currentProgram, monitor);
                }
                Function f = createFunction(a, null);
                if (f != null) created++; else failed++;
            }
            a = a.add(4);
        }
        println("funciones creadas en prologos huerfanos: " + created + " (fallidas: " + failed + ")");
    }
}
