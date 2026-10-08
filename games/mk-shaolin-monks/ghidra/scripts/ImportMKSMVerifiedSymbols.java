// @category PS2Recomp
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.data.*;
import java.nio.file.*;
public class ImportMKSMVerifiedSymbols extends GhidraScript {
 public void run() throws Exception {
  String[] args=getScriptArgs();boolean retail=args[1].equals("retail");
  String expected=retail?"b1ed99d81b07969553ed2f9afaad4caed1bbeb637d53a369f4dafca8eedd2ab2":"f0f0234bec88bd0d68912b92d86d75d7e6dc14a877bbb7b0c69843517a653f64";
  if(!expected.equals(currentProgram.getExecutableSHA256()))throw new IllegalStateException("Wrong ELF");
  var symbols=currentProgram.getSymbolTable();var ns=symbols.getNamespace("MKSM_Recovered",currentProgram.getGlobalNamespace());
  if(ns==null)ns=symbols.createNameSpace(currentProgram.getGlobalNamespace(),"MKSM_Recovered",SourceType.IMPORTED);
  int count=0;
  var lines=Files.readAllLines(Path.of(args[0]));
  for(String line:lines.subList(1,lines.size())) {
   String[] v=line.split("\t");var addr=toAddr(Long.decode(v[retail?3:2]));
   if(v[1].equals("function")) {
    var f=getFunctionAt(addr);if(f==null){if(getFunctionContaining(addr)!=null)throw new IllegalStateException("Boundary conflict: "+addr);disassemble(addr);f=createFunction(addr,null);}
    if(f==null)throw new IllegalStateException("No function: "+addr);
    if(f.getSymbol().getSource()==SourceType.USER_DEFINED){println("Preserved user function: "+f);continue;}
    f.setParentNamespace(ns);f.setName(v[0],SourceType.IMPORTED);
    f.setComment("Recovered from reviewed prototype linker-map symbols. Full paired disassembly and map cross-references reviewed; exact ELF hashes checked. Retail implementation retained. See retail-symbols.json. Prototype address "+v[2]+", retail "+v[3]+".");
   }else {
    var label=symbols.getSymbol(v[0],addr,ns);if(label==null)label=symbols.createLabel(addr,v[0],ns,SourceType.IMPORTED);
    var primary=symbols.getPrimarySymbol(addr);if(primary==null||primary.getSource()!=SourceType.USER_DEFINED)label.setPrimary();
   }
   count++;
  }
  // Member names below are reconstructed from instructions, not original debug types.
  var category=new CategoryPath("/MKSM_Recovered");
  var alloc=new StructureDataType(category,"ALLOC_DEF_recovered",0);
  String[] fields={"base","length","low","high","alignment","allow_system_shrink","reset_flags","malloc_mode","free_list","used_list","node_pool","malloc_start","unknown_30","unknown_34","unknown_38","lock_flags","saved_position","available_at_setup"};
  for(String field:fields)alloc.add(DWordDataType.dataType,4,field,"Inferred from Memory_Initialise/ResetBlock/AllocBlock/SetBlock; exact member spelling unknown.");
  alloc.add(new ArrayDataType(CharDataType.dataType,32,1),32,"name",null);
  alloc.setDescription("Reconstructed ALLOC_DEF layout: 0x68 bytes. Member names are inferred, not imported debug type metadata.");
  var resolved=currentProgram.getDataTypeManager().resolve(alloc,DataTypeConflictHandler.REPLACE_HANDLER);
  var address=toAddr(retail?0x64e998:0x6283d8);
  var existing=currentProgram.getListing().getDataAt(address);
  // Preserve a user-assigned type from another category.
  if(existing==null||existing.getDataType().getCategoryPath().toString().equals("/")||existing.getDataType().getCategoryPath().toString().equals(category.toString()))
   DataUtilities.createData(currentProgram,address,new ArrayDataType(resolved,128,104),-1,false,DataUtilities.ClearDataMode.CLEAR_ALL_CONFLICT_DATA);
  println("MKSM_SYMBOLS_IMPORTED="+count+" build="+args[1]);
 }
}
