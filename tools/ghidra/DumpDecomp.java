// Vuelca el pseudo-C decompilado de una o varias funciones a un archivo.
// La salida es codigo derivado del juego: debe ir a data/ (no se versiona).
//   analyzeHeadless data/ghidra ac3 -process ace_eu.exe -noanalysis \
//       -scriptPath tools/ghidra -postScript DumpDecomp.java <salida.c> <func|0xaddr> [...]
//@category AC3

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import java.io.PrintWriter;

public class DumpDecomp extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        try (PrintWriter out = new PrintWriter(args[0])) {
            for (int i = 1; i < args.length; i++) {
                Function f = null;
                if (args[i].startsWith("0x")) {
                    f = getFunctionAt(toAddr(Long.parseLong(args[i].substring(2), 16)));
                } else {
                    for (Function g : currentProgram.getFunctionManager().getFunctions(true)) {
                        if (g.getName().equals(args[i])) { f = g; break; }
                    }
                }
                if (f == null) { out.println("// no encontrada: " + args[i]); continue; }
                DecompileResults r = di.decompileFunction(f, 60, monitor);
                out.println("// ==== " + f.getName() + " @ " + f.getEntryPoint());
                out.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "// fallo: " + r.getErrorMessage());
            }
        }
    }
}
