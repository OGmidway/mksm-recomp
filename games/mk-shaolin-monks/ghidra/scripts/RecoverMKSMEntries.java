// @category PS2Recomp
import ghidra.app.script.GhidraScript;
import java.io.*;
public class RecoverMKSMEntries extends GhidraScript {
 public void run() throws Exception {
  if(!"b1ed99d81b07969553ed2f9afaad4caed1bbeb637d53a369f4dafca8eedd2ab2".equals(currentProgram.getExecutableSHA256()))throw new IllegalStateException("Wrong retail ELF");
  String[] args=getScriptArgs();
  StringWriter verified=new StringWriter();
  try(PrintWriter out=new PrintWriter(verified)){
   out.println("name,start,end,size");
   for(int i=1;i<args.length;i++){
    var a=toAddr(Long.decode(args[i]));var f=getFunctionAt(a);
    if(f==null){if(getFunctionContaining(a)!=null)throw new IllegalStateException("Interior entry requires manual review: "+a);disassemble(a);f=createFunction(a,null);}
    if(f==null)throw new IllegalStateException("No decoded function: "+a);
    long end=f.getBody().getMaxAddress().getOffset()+1;
    if(end<=a.getOffset()||end-a.getOffset()>0x10000)throw new IllegalStateException("Unexpected function span: "+a);
    out.printf("mk_recovered_%08x,0x%x,0x%x,%d%n",a.getOffset(),a.getOffset(),end,end-a.getOffset());
    println("VERIFIED_ENTRY "+a+" end="+Long.toHexString(end));
   }
  }
  java.nio.file.Files.writeString(java.nio.file.Path.of(args[0]),verified.toString());
 }
}
