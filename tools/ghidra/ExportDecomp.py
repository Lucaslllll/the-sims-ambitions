# Ghidra headless script: decompile every function into one C file.
# @category Export
from ghidra.app.decompiler import DecompInterface, DecompileOptions
from ghidra.util.task import ConsoleTaskMonitor
import os

out_path = getScriptArgs()[0]
ifc = DecompInterface()
opts = DecompileOptions()
ifc.setOptions(opts)
ifc.openProgram(currentProgram)
mon = ConsoleTaskMonitor()
fm = currentProgram.getFunctionManager()
with open(out_path, 'w') as f:
    for fn in fm.getFunctions(True):
        if fn.isExternal() or fn.isThunk():
            continue
        res = ifc.decompileFunction(fn, 60, mon)
        f.write('// ==== %s @ %s\n' % (fn.getName(), fn.getEntryPoint()))
        if res and res.decompileCompleted():
            f.write(res.getDecompiledFunction().getC())
        else:
            f.write('// decompile failed\n')
        f.write('\n')
