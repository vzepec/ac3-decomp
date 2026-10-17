// Estadisticas de un programa PSX ya analizado en Ghidra (etapa 3).
//
// Uso (headless, sobre un proyecto ya importado y analizado):
//   analyzeHeadless data/ghidra ac3 -process ace_eu.exe -noanalysis \
//       -scriptPath tools/ghidra -postScript AceStats.java data/ghidra/stats_eu
//
// Escribe <prefijo>.txt (resumen) y <prefijo>_funcs.tsv (una fila por funcion).
// Una funcion cuenta como "reconocida" si su nombre no es el generico FUN_xxx,
// es decir, si la nombro el analizador de firmas PsyQ (o el loader).
//@category AC3

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.framework.options.Options;
import java.io.PrintWriter;
import java.util.*;

public class AceStats extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String prefix = args.length > 0 ? args[0] : "ace_stats";
        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();

        // Solo cuenta funciones del bloque de codigo cargado del ejecutable:
        // el loader de PSX crea ademas un segmento artificial GTEMAC con
        // manejadores de macros GTE que NO son codigo del juego.
        // Se mide el bloque que contiene el punto de entrada del ejecutable.
        MemoryBlock code = null;
        ghidra.program.model.address.AddressIterator ep =
            currentProgram.getSymbolTable().getExternalEntryPointIterator();
        while (ep.hasNext() && code == null) {
            MemoryBlock b = currentProgram.getMemory().getBlock(ep.next());
            if (b != null && b.isInitialized() && !b.getName().equals("GTEMAC")) code = b;
        }
        if (code == null) code = currentProgram.getMemory().getBlock(currentProgram.getImageBase());
        int total = 0, named = 0, thunks = 0, skipped = 0;
        long namedBytes = 0, unnamedBytes = 0;
        Map<String, Integer> bySource = new TreeMap<>();
        try (PrintWriter tsv = new PrintWriter(prefix + "_funcs.tsv")) {
            tsv.println("entry\tname\tsize\tsource\tcop2");
            for (Function f : fm.getFunctions(true)) {
                if (!code.contains(f.getEntryPoint())) { skipped++; continue; }
                total++;
                if (f.isThunk()) thunks++;
                long size = f.getBody().getNumAddresses();
                boolean isNamed = !f.getName().startsWith("FUN_");
                SourceType src = f.getSymbol().getSource();
                bySource.merge(src.toString(), 1, Integer::sum);
                int cop2 = 0;
                InstructionIterator it = listing.getInstructions(f.getBody(), true);
                while (it.hasNext()) {
                    if (isCop2(it.next())) cop2++;
                }
                if (isNamed) { named++; namedBytes += size; } else { unnamedBytes += size; }
                tsv.printf("%s\t%s\t%d\t%s\t%d%n", f.getEntryPoint(), f.getName(), size, src, cop2);
            }
        }

        int cop2Total = 0, instrTotal = 0;
        InstructionIterator all = listing.getInstructions(new ghidra.program.model.address.AddressSet(code.getStart(), code.getEnd()), true);
        while (all.hasNext()) {
            instrTotal++;
            if (isCop2(all.next())) cop2Total++;
        }

        try (PrintWriter out = new PrintWriter(prefix + ".txt")) {
            out.println("programa: " + currentProgram.getName());
            out.println("lenguaje: " + currentProgram.getLanguageID());
            Options info = currentProgram.getOptions(Program.PROGRAM_INFO);
            for (String k : info.getOptionNames()) {
                if (k.toLowerCase().contains("psyq")) out.println("opcion " + k + " = " + info.getValueAsString(k));
            }
            for (MemoryBlock b : currentProgram.getMemory().getBlocks()) {
                out.printf("bloque %-12s %s-%s %d bytes%n", b.getName(), b.getStart(), b.getEnd(), b.getSize());
            }
            out.println("bloque medido: " + code.getName() + " " + code.getStart() + "-" + code.getEnd());
            out.println("funciones fuera del bloque (ignoradas, p. ej. GTEMAC): " + skipped);
            out.println("funciones: " + total);
            out.println("  reconocidas (nombre no FUN_): " + named + " (" + namedBytes + " bytes)");
            out.println("  sin reconocer (FUN_): " + (total - named) + " (" + unnamedBytes + " bytes)");
            out.println("  thunks: " + thunks);
            out.println("  por origen del simbolo: " + bySource);
            out.println("instrucciones: " + instrTotal);
            out.println("instrucciones COP2 (GTE): " + cop2Total);
        }
        println("estadisticas escritas en " + prefix + ".txt");
    }

    // COP2 = opcode 0x12 (operaciones GTE, mfc2/mtc2/cfc2/ctc2),
    // lwc2 = 0x32, swc2 = 0x3A.
    private boolean isCop2(Instruction ins) throws Exception {
        byte[] b = ins.getBytes();
        if (b.length != 4) return false;
        int w = (b[0] & 0xff) | (b[1] & 0xff) << 8 | (b[2] & 0xff) << 16 | (b[3] & 0xff) << 24;
        int op = w >>> 26;
        return op == 0x12 || op == 0x32 || op == 0x3A;
    }
}
