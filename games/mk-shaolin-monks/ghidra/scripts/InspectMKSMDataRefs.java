// @category PS2Recomp
import ghidra.app.script.GhidraScript;
import java.io.*;
public class InspectMKSMDataRefs extends GhidraScript {
 public void run() throws Exception {
  String[] args=getScriptArgs();
  try(PrintWriter out=new PrintWriter(args[0])) {
   out.println("Program SHA256: "+currentProgram.getExecutableSHA256());
   for(int i=1;i<args.length;i++) {
    var address=toAddr(Long.decode(args[i]));out.println("DATA "+address);
    int count=0;
    for(var ref:currentProgram.getReferenceManager().getReferencesTo(address)) {
     if(count++==256){out.println("Reference output capped at 256");break;}
     out.println(ref+" FUNCTION "+getFunctionContaining(ref.getFromAddress()));
    }
   }
  }
 }
}
