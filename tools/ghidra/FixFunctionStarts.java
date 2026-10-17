// Corrige el inicio de funciones que se invocan por puntero (tablas de
// callbacks) y que el analisis automatico empezo unos bytes mas tarde, o no
// creo. Para cada direccion: quita la funcion que empiece hasta 16 bytes
// despues, desensambla y crea la funcion justo ahi; si cae dentro de otra
// funcion, recorta el cuerpo de esa.
//   analyzeHeadless data/ghidra ac3 -process ace_eu.exe -noanalysis \
//       -scriptPath tools/ghidra -postScript FixFunctionStarts.java 0x8008BCBC ...
//@category AC3

import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;

public class FixFunctionStarts extends GhidraScript {
    @Override
    public void run() throws Exception {
        for (String arg : getScriptArgs()) {
            Address a = toAddr(Long.decode(arg));
            Function at = getFunctionAt(a);
            if (at != null) {
                println(arg + ": ya es inicio de " + at.getName());
                continue;
            }
            for (int d = 4; d <= 16; d += 4) {
                Function late = getFunctionAt(a.add(d));
                if (late != null) {
                    println(arg + ": quito " + late.getName() + " (empezaba " + d + " B despues)");
                    removeFunction(late);
                    break;
                }
            }
            Function outer = getFunctionContaining(a);
            new DisassembleCommand(a, new AddressSet(a, a.add(3)), true).applyTo(currentProgram);
            boolean ok = new CreateFunctionCmd(a).applyTo(currentProgram);
            if (outer != null) {
                CreateFunctionCmd.fixupFunctionBody(currentProgram, outer, monitor);
            }
            Function f = getFunctionAt(a);
            println(arg + ": " + (ok && f != null ? "creada " + f.getName() : "FALLO"));
        }
    }
}
