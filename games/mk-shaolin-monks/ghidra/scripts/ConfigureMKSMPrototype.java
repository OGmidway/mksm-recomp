// Configure analysis of the verified NTSC retail ELF.
// @category PS2Recomp
import ghidra.app.script.GhidraScript;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.HexFormat;
public class ConfigureMKSMPrototype extends GhidraScript {
 public void run() throws Exception {
  String expected="f0f0234bec88bd0d68912b92d86d75d7e6dc14a877bbb7b0c69843517a653f64";
  String actual=HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(Files.readAllBytes(Path.of(getScriptArgs()[0]))));
  if(!expected.equals(actual)) throw new IllegalStateException("Unexpected MKSM ELF hash");
  if(!currentProgram.getLanguageID().toString().equals("r5900:LE:32:default")) throw new IllegalStateException("Incorrect PS2 processor");
  for(String key:getCurrentAnalysisOptionsAndValues(currentProgram).keySet()) {
   if(key.equals("Non-Returning Functions - Discovered") || key.equals("Non-Returning Functions - Known") || key.equals("Decompiler Parameter ID")) setAnalysisOption(currentProgram,key,"false");
   if(key.contains("Use Deprecated Demangler")) setAnalysisOption(currentProgram,key,"true");
   if(key.equals("STABS")) setAnalysisOption(currentProgram,key,"true");
  }
  println("MKSM_PROTOTYPE_VERIFIED language="+currentProgram.getLanguageID()+" sha256="+actual);
 }
}
