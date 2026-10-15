// Aplica los nombres de tools/ghidra/names.tsv al programa abierto.
//   analyzeHeadless data/ghidra ac3 -process ace_eu.exe -noanalysis \
//       -scriptPath tools/ghidra -postScript ApplyNames.java tools/ghidra/names.tsv
// Crea la funcion si no existe y pone el nombre como USER_DEFINED.
//@category AC3

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.Files;
import java.nio.file.Paths;

public class ApplyNames extends GhidraScript {
    @Override
    public void run() throws Exception {
        String path = getScriptArgs().length > 0 ? getScriptArgs()[0] : "tools/ghidra/names.tsv";
        int funcs = 0, data = 0, bad = 0;
        for (String line : Files.readAllLines(Paths.get(path))) {
            if (line.isBlank() || line.startsWith("#")) continue;
            String[] f = line.split("\t");
            Address a = toAddr(Long.parseLong(f[0].substring(2), 16));
            if (f[1].equals("f")) {
                Function fn = getFunctionAt(a);
                if (fn == null) fn = createFunction(a, null);
                if (fn == null) { bad++; printerr("sin funcion en " + f[0]); continue; }
                fn.setName(f[2], SourceType.USER_DEFINED);
                if (f.length > 3) fn.setComment(f[3]);
                funcs++;
            } else {
                createLabel(a, f[2], true, SourceType.USER_DEFINED);
                if (f.length > 3) setPlateComment(a, f[3]);
                data++;
            }
        }
        println("nombres aplicados: " + funcs + " funciones, " + data + " datos, " + bad + " fallos");
    }
}
