// @category PS2Recomp
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
public class InspectMKSMFunctions extends GhidraScript {
 public void run() throws Exception {
  String[] args=getScriptArgs(); DecompInterface decomp=new DecompInterface();decomp.openProgram(currentProgram);
  try(PrintWriter out=new PrintWriter(args[0])) {
   out.println("Program SHA256: "+currentProgram.getExecutableSHA256());
   for(int i=1;i<args.length;i++) {
    var addr=toAddr(Long.decode(args[i]));var f=getFunctionAt(addr);
    out.println("\nADDRESS "+addr+" FUNCTION "+f);
    for(var ref:currentProgram.getReferenceManager().getReferencesTo(addr))out.println("IN "+ref);
    if(f==null){f=getFunctionContaining(addr);out.println("CONTAINING "+f);} if(f==null){disassemble(addr);f=createFunction(addr,null);} if(f==null)continue;
    var result=decomp.decompileFunction(f,30,monitor);
    if(result.decompileCompleted())out.println(result.getDecompiledFunction().getC());else out.println(result.getErrorMessage());
    var instructions=currentProgram.getListing().getInstructions(f.getBody(),true);
    while(instructions.hasNext()){var ins=instructions.next();out.print(ins.getAddress()+" "+ins);for(var ref:ins.getReferencesFrom())out.print(" | "+ref);out.println();}
   }
  }finally{decomp.dispose();}
 }
}

