// Decompila TODAS las funciones del programa a un archivo, para buscar por
// patron en el pseudo-C. La salida es codigo derivado del juego: data/ only.
//   analyzeHeadless data/ghidra ac3 -process ace_eu.exe -noanalysis \
//       -scriptPath tools/ghidra -postScript DumpAll.java data/ghidra/all_eu.c
//@category AC3

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import java.io.PrintWriter;

public class DumpAll extends GhidraScript {
    @Override
    public void run() throws Exception {
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        int ok = 0, bad = 0;
        try (PrintWriter out = new PrintWriter(getScriptArgs()[0])) {
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                if (f.getEntryPoint().getOffset() >= 0x20000000L && f.getEntryPoint().getOffset() < 0x30000000L) continue;
                DecompileResults r = di.decompileFunction(f, 30, monitor);
                out.println("// ==== " + f.getName() + " @ " + f.getEntryPoint());
                if (r.decompileCompleted()) { out.println(r.getDecompiledFunction().getC()); ok++; }
                else { out.println("// fallo: " + r.getErrorMessage()); bad++; }
            }
        }
        println("decompiladas " + ok + ", fallidas " + bad);
    }
}
