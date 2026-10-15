// Fuerza la version de PsyQ que usa el analizador de firmas de ghidra_psx_ldr.
// Se usa como -preScript al importar, antes del analisis automatico:
//   analyzeHeadless ... -import x.exe -preScript SetPsyqVersion.java 4.6.0
//@category AC3

import ghidra.app.script.GhidraScript;
import ghidra.framework.options.Options;
import ghidra.program.model.listing.Program;

public class SetPsyqVersion extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            printerr("uso: SetPsyqVersion.java <version, p. ej. 4.6.0>");
            return;
        }
        Options info = currentProgram.getOptions(Program.PROGRAM_INFO);
        println("PsyQ Version antes: " + info.getValueAsString("PsyQ Version"));
        info.setString("PsyQ Version", args[0]);
        println("PsyQ Version ahora: " + info.getValueAsString("PsyQ Version"));
    }
}
